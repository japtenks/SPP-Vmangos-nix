#pragma once

#include "Common.h"
#include "Policies/Singleton.h"

#include <shared_mutex>
#include <unordered_map>
#include <vector>

class Guild;
class Player;

enum SocialRelationshipFlags : uint32
{
    SOCIAL_RELATIONSHIP_NONE = 0x00,
    SOCIAL_RELATIONSHIP_KNOWN = 0x01,
    SOCIAL_RELATIONSHIP_GUILD_FRIENDLY = 0x02,
    SOCIAL_RELATIONSHIP_RIVAL = 0x04,
    SOCIAL_RELATIONSHIP_PVP_HIT_LIST = 0x08,
};

struct SocialRelationshipEntry
{
    float affinity = 0.0f;
    float hostility = 0.0f;
    time_t lastInteraction = 0;
    uint32 flags = SOCIAL_RELATIONSHIP_NONE;
};

struct GuildAreaCandidate
{
    uint32 areaId = 0;
    float weight = 0.0f;
};

struct GuildHubState
{
    uint32 preferredAreaId = 0;
    std::vector<GuildAreaCandidate> contenders;
    time_t lastUpdate = 0;
};

class ServerSocialMgr
{
public:
    SocialRelationshipEntry GetRelationship(uint64 ownerGuid, uint64 targetGuid);
    float GetAffinity(uint64 ownerGuid, uint64 targetGuid);
    float GetHostility(uint64 ownerGuid, uint64 targetGuid);
    uint32 GetRelationshipFlags(uint64 ownerGuid, uint64 targetGuid);
    uint32 GetKnownContactCount(uint64 ownerGuid);
    bool HasHitListFlag(uint64 ownerGuid, uint64 targetGuid);

    void AddAffinity(uint64 ownerGuid, uint64 targetGuid, float delta, uint32 flags = SOCIAL_RELATIONSHIP_KNOWN);
    void AddHostility(uint64 ownerGuid, uint64 targetGuid, float delta, uint32 flags = SOCIAL_RELATIONSHIP_KNOWN);
    void ObserveMutualSocialContact(Player* owner, Player* target, bool sameGuild = false);

    GuildHubState GetGuildHubState(uint32 guildId);
    uint32 GetGuildPreferredArea(uint32 guildId);
    float GetGuildAreaAlignment(uint32 guildId, uint32 areaId);
    void ObserveGuildArea(Player* bot, uint32 areaId, float weightMultiplier = 1.0f);

    float GetGuildJoinBias(Player* bot, Guild* guild, uint64 inviterGuid = 0);
    bool ShouldLeaveGuild(Player* bot, Guild* guild);

    uint32 NormalizeAreaId(Player* bot) const;
    uint32 NormalizeAreaId(uint32 mapId, float x, float y, float z) const;

private:
    struct RelationshipKey
    {
        uint64 ownerGuid = 0;
        uint64 targetGuid = 0;

        bool operator==(RelationshipKey const& other) const
        {
            return ownerGuid == other.ownerGuid && targetGuid == other.targetGuid;
        }
    };

    struct RelationshipKeyHash
    {
        std::size_t operator()(RelationshipKey const& key) const
        {
            std::size_t seed = static_cast<std::size_t>(key.ownerGuid);
            seed = (seed * 1315423911u) ^ static_cast<std::size_t>(key.targetGuid);
            return seed;
        }
    };

    struct GuildStateCacheEntry
    {
        GuildHubState state;
        bool loaded = false;
    };

    SocialRelationshipEntry LoadRelationship(uint64 ownerGuid, uint64 targetGuid);
    void SaveRelationship(uint64 ownerGuid, uint64 targetGuid, SocialRelationshipEntry const& entry);
    SocialRelationshipEntry ApplyDecay(SocialRelationshipEntry const& entry) const;

    GuildHubState LoadGuildHubState(uint32 guildId);
    void SaveGuildHubState(uint32 guildId, GuildHubState const& state);
    static void NormalizeGuildHubState(GuildHubState& state);

    mutable std::shared_mutex m_relationshipMutex;
    std::unordered_map<RelationshipKey, SocialRelationshipEntry, RelationshipKeyHash> m_relationships;

    mutable std::shared_mutex m_guildHubMutex;
    std::unordered_map<uint32, GuildStateCacheEntry> m_guildHubs;
};

#define sServerSocialMgr MaNGOS::Singleton<ServerSocialMgr>::Instance()
