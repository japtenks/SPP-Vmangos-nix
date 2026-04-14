#include "ServerSocialMgr.h"

#include "Guild/Guild.h"
#include "Guild/GuildMgr.h"
#include "ObjectMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Policies/SingletonImp.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include <algorithm>
#include <cmath>
#include <sstream>

INSTANTIATE_SINGLETON_1(ServerSocialMgr);

namespace
{
    constexpr float affinityDecayPerDay = 0.04f;
    constexpr float hostilityDecayPerDay = 0.02f;
    constexpr time_t socialObservationCooldown = 5 * MINUTE;
    constexpr size_t maxGuildHubContenders = 6;

    float ClampUnitRange(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }

    std::string SerializeGuildContenders(std::vector<GuildAreaCandidate> const& contenders)
    {
        std::ostringstream out;
        bool first = true;
        for (const GuildAreaCandidate& candidate : contenders)
        {
            if (!candidate.areaId || candidate.weight <= 0.0f)
                continue;

            if (!first)
                out << ",";

            first = false;
            out << candidate.areaId << ":" << candidate.weight;
        }

        return out.str();
    }

    std::vector<GuildAreaCandidate> DeserializeGuildContenders(std::string const& value)
    {
        std::vector<GuildAreaCandidate> contenders;
        std::istringstream stream(value);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            const size_t separator = token.find(':');
            if (separator == std::string::npos)
                continue;

            GuildAreaCandidate candidate;
            candidate.areaId = static_cast<uint32>(std::stoul(token.substr(0, separator)));
            candidate.weight = std::stof(token.substr(separator + 1));
            if (candidate.areaId && candidate.weight > 0.0f)
                contenders.push_back(candidate);
        }

        return contenders;
    }

    void LogRelationshipChange(char const* label, uint64 ownerGuid, uint64 targetGuid, float delta,
        SocialRelationshipEntry const& entry, std::string const& reason)
    {
        if (reason.empty())
            return;

        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "BOTSOCIAL %s owner=%llu target=%llu delta=%.3f affinity=%.3f hostility=%.3f flags=%u reason=%s",
            label, ownerGuid, targetGuid, delta, entry.affinity, entry.hostility, entry.flags, reason.c_str());
    }
}

SocialRelationshipEntry ServerSocialMgr::ApplyDecay(SocialRelationshipEntry const& entry) const
{
    SocialRelationshipEntry decayed = entry;
    if (!entry.lastInteraction)
        return decayed;

    const time_t now = time(nullptr);
    if (entry.lastInteraction >= now)
        return decayed;

    const float elapsedDays = float(now - entry.lastInteraction) / float(DAY);
    decayed.affinity = ClampUnitRange(entry.affinity - (elapsedDays * affinityDecayPerDay));
    decayed.hostility = ClampUnitRange(entry.hostility - (elapsedDays * hostilityDecayPerDay));

    if (decayed.hostility < 0.18f)
        decayed.flags &= ~SOCIAL_RELATIONSHIP_PVP_HIT_LIST;
    if (decayed.hostility < 0.12f)
        decayed.flags &= ~SOCIAL_RELATIONSHIP_RIVAL;

    if (decayed.affinity <= 0.01f && decayed.hostility <= 0.01f)
        decayed.flags &= ~(SOCIAL_RELATIONSHIP_KNOWN | SOCIAL_RELATIONSHIP_GUILD_FRIENDLY);

    return decayed;
}

SocialRelationshipEntry ServerSocialMgr::LoadRelationship(uint64 ownerGuid, uint64 targetGuid)
{
    SocialRelationshipEntry entry;
    if (!ownerGuid || !targetGuid)
        return entry;

    auto result = CharacterDatabase.PQuery(
        "SELECT `affinity`,`hostility`,`last_interaction`,`flags` FROM `ai_playerbot_social_memory` WHERE `owner_guid` = '%llu' AND `target_guid` = '%llu'",
        ownerGuid, targetGuid);
    if (!result)
        return entry;

    Field* fields = result->Fetch();
    entry.affinity = fields[0].GetFloat();
    entry.hostility = fields[1].GetFloat();
    entry.lastInteraction = static_cast<time_t>(fields[2].GetUInt32());
    entry.flags = fields[3].GetUInt32();

    return entry;
}

void ServerSocialMgr::SaveRelationship(uint64 ownerGuid, uint64 targetGuid, SocialRelationshipEntry const& entry)
{
    CharacterDatabase.PExecute(
        "REPLACE INTO `ai_playerbot_social_memory` (`owner_guid`,`target_guid`,`affinity`,`hostility`,`last_interaction`,`flags`) VALUES ('%llu','%llu','%f','%f','%u','%u')",
        ownerGuid, targetGuid, entry.affinity, entry.hostility, static_cast<uint32>(entry.lastInteraction), entry.flags);
}

SocialRelationshipEntry ServerSocialMgr::GetRelationship(uint64 ownerGuid, uint64 targetGuid)
{
    if (!ownerGuid || !targetGuid || ownerGuid == targetGuid)
        return {};

    const RelationshipKey key{ ownerGuid, targetGuid };

    {
        std::shared_lock<std::shared_mutex> lock(m_relationshipMutex);
        auto itr = m_relationships.find(key);
        if (itr != m_relationships.end())
            return ApplyDecay(itr->second);
    }

    SocialRelationshipEntry loaded = LoadRelationship(ownerGuid, targetGuid);
    {
        std::unique_lock<std::shared_mutex> lock(m_relationshipMutex);
        m_relationships[key] = loaded;
    }

    return ApplyDecay(loaded);
}

float ServerSocialMgr::GetAffinity(uint64 ownerGuid, uint64 targetGuid)
{
    return GetRelationship(ownerGuid, targetGuid).affinity;
}

float ServerSocialMgr::GetHostility(uint64 ownerGuid, uint64 targetGuid)
{
    return GetRelationship(ownerGuid, targetGuid).hostility;
}

uint32 ServerSocialMgr::GetRelationshipFlags(uint64 ownerGuid, uint64 targetGuid)
{
    return GetRelationship(ownerGuid, targetGuid).flags;
}

bool ServerSocialMgr::HasHitListFlag(uint64 ownerGuid, uint64 targetGuid)
{
    return (GetRelationshipFlags(ownerGuid, targetGuid) & SOCIAL_RELATIONSHIP_PVP_HIT_LIST) != 0;
}

uint32 ServerSocialMgr::GetKnownContactCount(uint64 ownerGuid)
{
    if (!ownerGuid)
        return 0;

    uint32 count = 0;
    std::shared_lock<std::shared_mutex> lock(m_relationshipMutex);
    for (const auto& [key, entry] : m_relationships)
    {
        if (key.ownerGuid != ownerGuid)
            continue;

        SocialRelationshipEntry decayed = ApplyDecay(entry);
        if ((decayed.flags & SOCIAL_RELATIONSHIP_KNOWN) || decayed.affinity > 0.05f || decayed.hostility > 0.08f)
            ++count;
    }

    return count;
}

void ServerSocialMgr::AddAffinity(uint64 ownerGuid, uint64 targetGuid, float delta, uint32 flags, std::string const& reason)
{
    if (!ownerGuid || !targetGuid || ownerGuid == targetGuid || delta <= 0.0f)
        return;

    const RelationshipKey key{ ownerGuid, targetGuid };
    std::unique_lock<std::shared_mutex> lock(m_relationshipMutex);
    SocialRelationshipEntry& entry = m_relationships[key];
    if (!entry.lastInteraction)
        entry = LoadRelationship(ownerGuid, targetGuid);

    entry = ApplyDecay(entry);
    entry.affinity = ClampUnitRange(entry.affinity + delta);
    entry.hostility = ClampUnitRange(std::max(0.0f, entry.hostility - (delta * 0.25f)));
    entry.flags |= flags | SOCIAL_RELATIONSHIP_KNOWN;
    entry.lastInteraction = time(nullptr);
    SaveRelationship(ownerGuid, targetGuid, entry);
    LogRelationshipChange("AFFINITY", ownerGuid, targetGuid, delta, entry, reason);
}

void ServerSocialMgr::AddHostility(uint64 ownerGuid, uint64 targetGuid, float delta, uint32 flags, std::string const& reason)
{
    if (!ownerGuid || !targetGuid || ownerGuid == targetGuid || delta <= 0.0f)
        return;

    const RelationshipKey key{ ownerGuid, targetGuid };
    std::unique_lock<std::shared_mutex> lock(m_relationshipMutex);
    SocialRelationshipEntry& entry = m_relationships[key];
    if (!entry.lastInteraction)
        entry = LoadRelationship(ownerGuid, targetGuid);

    entry = ApplyDecay(entry);
    entry.hostility = ClampUnitRange(entry.hostility + delta);
    entry.affinity = ClampUnitRange(std::max(0.0f, entry.affinity - (delta * 0.20f)));
    entry.flags |= flags | SOCIAL_RELATIONSHIP_KNOWN;
    if (entry.hostility >= 0.15f)
        entry.flags |= SOCIAL_RELATIONSHIP_RIVAL;
    if (entry.hostility >= 0.25f)
        entry.flags |= SOCIAL_RELATIONSHIP_PVP_HIT_LIST;

    entry.lastInteraction = time(nullptr);
    SaveRelationship(ownerGuid, targetGuid, entry);
    LogRelationshipChange("HOSTILITY", ownerGuid, targetGuid, delta, entry, reason);
}

void ServerSocialMgr::ObserveMutualSocialContact(Player* owner, Player* target, bool sameGuild)
{
    if (!owner || !target || owner == target)
        return;

    if (owner->GetMapId() != target->GetMapId())
        return;

    const uint64 ownerGuid = owner->GetObjectGuid().GetRawValue();
    const uint64 targetGuid = target->GetObjectGuid().GetRawValue();
    if (!ownerGuid || !targetGuid)
        return;

    SocialRelationshipEntry current = GetRelationship(ownerGuid, targetGuid);
    const time_t now = time(nullptr);
    if (current.lastInteraction && (now - current.lastInteraction) < socialObservationCooldown)
        return;

    const float distance = sServerFacade.GetDistance2d(owner, target);
    const float distanceBias = distance < 20.0f ? 1.0f : (distance < 40.0f ? 0.75f : 0.50f);
    const float delta = 0.015f * distanceBias * (sameGuild ? 1.5f : 1.0f);
    const uint32 flags = sameGuild ? (SOCIAL_RELATIONSHIP_KNOWN | SOCIAL_RELATIONSHIP_GUILD_FRIENDLY) : SOCIAL_RELATIONSHIP_KNOWN;

    AddAffinity(ownerGuid, targetGuid, delta, flags, sameGuild ? "social_contact_same_guild" : "social_contact");
    AddAffinity(targetGuid, ownerGuid, delta, flags, sameGuild ? "social_contact_same_guild" : "social_contact");
}

GuildHubState ServerSocialMgr::LoadGuildHubState(uint32 guildId)
{
    GuildHubState state;
    if (!guildId)
        return state;

    auto result = CharacterDatabase.PQuery(
        "SELECT `preferred_area_id`,`contenders`,`last_update` FROM `ai_playerbot_guild_hubs` WHERE `guild_id` = '%u'",
        guildId);
    if (!result)
        return state;

    Field* fields = result->Fetch();
    state.preferredAreaId = fields[0].GetUInt32();
    state.contenders = DeserializeGuildContenders(fields[1].GetString());
    state.lastUpdate = static_cast<time_t>(fields[2].GetUInt32());

    NormalizeGuildHubState(state);
    return state;
}

void ServerSocialMgr::SaveGuildHubState(uint32 guildId, GuildHubState const& state)
{
    CharacterDatabase.PExecute(
        "REPLACE INTO `ai_playerbot_guild_hubs` (`guild_id`,`preferred_area_id`,`contenders`,`last_update`) VALUES ('%u','%u','%s','%u')",
        guildId, state.preferredAreaId, SerializeGuildContenders(state.contenders).c_str(), static_cast<uint32>(state.lastUpdate));
}

void ServerSocialMgr::NormalizeGuildHubState(GuildHubState& state)
{
    for (GuildAreaCandidate& candidate : state.contenders)
        candidate.weight = std::max(0.0f, candidate.weight);

    state.contenders.erase(std::remove_if(state.contenders.begin(), state.contenders.end(),
        [](GuildAreaCandidate const& candidate) { return !candidate.areaId || candidate.weight <= 0.01f; }), state.contenders.end());
    std::sort(state.contenders.begin(), state.contenders.end(),
        [](GuildAreaCandidate const& left, GuildAreaCandidate const& right) { return left.weight > right.weight; });
    if (state.contenders.size() > maxGuildHubContenders)
        state.contenders.resize(maxGuildHubContenders);

    if (!state.contenders.empty())
        state.preferredAreaId = state.contenders.front().areaId;
    else
        state.preferredAreaId = 0;
}

GuildHubState ServerSocialMgr::GetGuildHubState(uint32 guildId)
{
    if (!guildId)
        return {};

    {
        std::shared_lock<std::shared_mutex> lock(m_guildHubMutex);
        auto itr = m_guildHubs.find(guildId);
        if (itr != m_guildHubs.end() && itr->second.loaded)
            return itr->second.state;
    }

    GuildHubState loaded = LoadGuildHubState(guildId);
    {
        std::unique_lock<std::shared_mutex> lock(m_guildHubMutex);
        GuildStateCacheEntry& entry = m_guildHubs[guildId];
        entry.state = loaded;
        entry.loaded = true;
        return entry.state;
    }
}

uint32 ServerSocialMgr::GetGuildPreferredArea(uint32 guildId)
{
    return GetGuildHubState(guildId).preferredAreaId;
}

float ServerSocialMgr::GetGuildAreaAlignment(uint32 guildId, uint32 areaId)
{
    if (!guildId || !areaId)
        return 0.0f;

    GuildHubState state = GetGuildHubState(guildId);
    if (!state.preferredAreaId)
        return 0.0f;

    if (state.preferredAreaId == areaId)
        return 1.0f;

    for (GuildAreaCandidate const& candidate : state.contenders)
    {
        if (candidate.areaId == areaId)
            return ClampUnitRange(candidate.weight);
    }

    return 0.0f;
}

void ServerSocialMgr::ObserveGuildArea(Player* bot, uint32 areaId, float weightMultiplier)
{
    if (!bot || !bot->GetGuildId() || !areaId || weightMultiplier <= 0.0f)
        return;

    Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
    if (!guild)
        return;

    GuildHubState state = GetGuildHubState(guild->GetId());
    const uint32 oldPreferredAreaId = state.preferredAreaId;

    float roleWeight = 1.0f;
    MemberSlot* member = guild->GetMemberSlot(bot->GetObjectGuid());
    if (guild->GetLeaderGuid() == bot->GetObjectGuid())
        roleWeight = 2.75f;
    else if (member && member->RankId <= GR_OFFICER)
        roleWeight = 1.75f;

    if (PlayerbotAI* ai = bot->GetPlayerbotAI())
    {
        if (ai->GetSession().state == SessionState::IDLE || ai->GetSession().state == SessionState::TRAVELLING)
            roleWeight *= 1.10f;
    }

    for (GuildAreaCandidate& candidate : state.contenders)
        candidate.weight *= 0.995f;

    auto itr = std::find_if(state.contenders.begin(), state.contenders.end(),
        [areaId](GuildAreaCandidate const& candidate) { return candidate.areaId == areaId; });
    if (itr == state.contenders.end())
        state.contenders.push_back({ areaId, 0.0f });

    itr = std::find_if(state.contenders.begin(), state.contenders.end(),
        [areaId](GuildAreaCandidate const& candidate) { return candidate.areaId == areaId; });
    itr->weight += (0.03f * roleWeight * weightMultiplier);
    state.lastUpdate = time(nullptr);
    NormalizeGuildHubState(state);

    std::unique_lock<std::shared_mutex> lock(m_guildHubMutex);
    GuildStateCacheEntry& cacheEntry = m_guildHubs[guild->GetId()];
    cacheEntry.loaded = true;
    cacheEntry.state = state;
    SaveGuildHubState(guild->GetId(), state);

    if (state.preferredAreaId && state.preferredAreaId != oldPreferredAreaId)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_DEBUG,
            "BOTSOCIAL GUILD_HUB_SHIFT guild=%u old_area=%u new_area=%u actor=%s role_weight=%.2f weight_multiplier=%.2f",
            guild->GetId(), oldPreferredAreaId, state.preferredAreaId, bot->GetName(), roleWeight, weightMultiplier);
    }
}

float ServerSocialMgr::GetGuildJoinBias(Player* bot, Guild* guild, uint64 inviterGuid)
{
    if (!bot || !guild)
        return 0.0f;

    const uint64 botGuid = bot->GetObjectGuid().GetRawValue();
    float score = 0.0f;

    if (inviterGuid)
    {
        score += GetAffinity(botGuid, inviterGuid) * 1.4f;
        score -= GetHostility(botGuid, inviterGuid) * 1.8f;
    }

    const uint32 areaId = NormalizeAreaId(bot);
    score += GetGuildAreaAlignment(guild->GetId(), areaId) * 0.8f;

    const uint64 leaderGuid = guild->GetLeaderGuid().GetRawValue();
    if (leaderGuid)
    {
        score += GetAffinity(botGuid, leaderGuid) * 0.6f;
        score -= GetHostility(botGuid, leaderGuid) * 1.1f;
    }

    if (GetKnownContactCount(botGuid) < 3)
        score -= 0.35f;

    return score;
}

bool ServerSocialMgr::ShouldLeaveGuild(Player* bot, Guild* guild)
{
    if (!bot || !guild)
        return false;

    const uint64 botGuid = bot->GetObjectGuid().GetRawValue();
    const uint64 leaderGuid = guild->GetLeaderGuid().GetRawValue();
    const uint32 areaId = NormalizeAreaId(bot);

    float score = 0.0f;
    score += GetGuildAreaAlignment(guild->GetId(), areaId) * 0.8f;
    if (leaderGuid)
    {
        score += GetAffinity(botGuid, leaderGuid) * 0.5f;
        score -= GetHostility(botGuid, leaderGuid) * 1.4f;
    }

    if (score > -0.20f)
        return false;

    return urand(0, 999) == 0;
}

uint32 ServerSocialMgr::NormalizeAreaId(Player* bot) const
{
    if (!bot)
        return 0;

    return NormalizeAreaId(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
}

uint32 ServerSocialMgr::NormalizeAreaId(uint32 mapId, float x, float y, float z) const
{
    uint32 areaId = sTerrainMgr.GetAreaId(mapId, x, y, z);
    AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(areaId);
    while (areaEntry && areaEntry->ZoneId)
    {
        AreaTableEntry const* parentArea = GetAreaEntryByAreaID(areaEntry->ZoneId);
        if (!parentArea || parentArea == areaEntry)
            break;

        areaEntry = parentArea;
    }

    return areaEntry ? areaEntry->Id : areaId;
}
