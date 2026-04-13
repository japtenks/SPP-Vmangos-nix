#include "playerbot/BotArchetype.h"
#include "playerbot/PlayerbotAI.h"
#include "ObjectMgr.h"

using namespace ai;

namespace
{
    template <typename T>
    std::string ToUnderlyingString(T value)
    {
        return std::to_string(static_cast<uint32>(value));
    }
}

void BotSession::Reset(SessionState newState)
{
    state = newState;
    plannedDuration = 0;
    startedAt = time(nullptr);
    pausedAt = 0;
    isPaused = false;
}

InterruptTier CommittedTask::GetInterruptTier() const
{
    switch (purpose)
    {
        case TravelDestinationPurpose::QuestObjective1:
        case TravelDestinationPurpose::QuestObjective2:
        case TravelDestinationPurpose::QuestObjective3:
        case TravelDestinationPurpose::QuestObjective4:
        case TravelDestinationPurpose::Boss:
            return InterruptTier::INTERRUPT_HIGH;
        case TravelDestinationPurpose::QuestGiver:
        case TravelDestinationPurpose::QuestTaker:
        case TravelDestinationPurpose::Vendor:
        case TravelDestinationPurpose::AH:
        case TravelDestinationPurpose::Repair:
        case TravelDestinationPurpose::Mail:
        case TravelDestinationPurpose::Trainer:
            return InterruptTier::INTERRUPT_MEDIUM;
        case TravelDestinationPurpose::GenericRpg:
        case TravelDestinationPurpose::Explore:
        case TravelDestinationPurpose::Grind:
        case TravelDestinationPurpose::GatherFishing:
        case TravelDestinationPurpose::GatherHerbalism:
        case TravelDestinationPurpose::GatherMining:
        case TravelDestinationPurpose::GatherSkinning:
            return InterruptTier::INTERRUPT_LOW;
        case TravelDestinationPurpose::None:
        case TravelDestinationPurpose::QuestAllObjective:
        case TravelDestinationPurpose::MaxFlag:
        default:
            return InterruptTier::INTERRUPT_NONE;
    }
}

bool CommittedTask::CanBePreemptedBy(InterruptTier tier) const
{
    return static_cast<uint8>(tier) <= static_cast<uint8>(GetInterruptTier());
}

bool CommittedTask::ValidateTarget(PlayerbotAI* ai, time_t now, uint32 throttleSeconds)
{
    if (!now)
        now = time(nullptr);

    if (lastValidityCheck && now < (lastValidityCheck + throttleSeconds))
        return isValid;

    lastValidityCheck = now;

    if (failCooldownUntil && now < failCooldownUntil)
    {
        isValid = false;
        return false;
    }

    if (!ai || !ai->GetBot())
    {
        isValid = false;
        return false;
    }

    if (!targetGuid)
    {
        isValid = (purpose != TravelDestinationPurpose::None || questId != 0);
        return isValid;
    }

    if (targetGuid.IsPlayer())
    {
        isValid = sObjectMgr.GetPlayer(targetGuid) != nullptr;
        return isValid;
    }

    isValid = true;
    return true;
}

void CommittedTask::Clear()
{
    purpose = TravelDestinationPurpose::None;
    targetGuid = ObjectGuid();
    questId = 0;
    failCooldownUntil = 0;
    retryCount = 0;
    lastValidityCheck = 0;
    isValid = false;
}

std::string ai::BotArchetypeToString(BotArchetype archetype)
{
    switch (archetype)
    {
        case BotArchetype::CASUAL: return "casual";
        case BotArchetype::REGULAR: return "regular";
        case BotArchetype::RPG_QUEST: return "rpg_quest";
        case BotArchetype::GRINDER: return "grinder";
        case BotArchetype::FARMER: return "farmer";
        case BotArchetype::HARDCORE: return "hardcore";
        default: return ToUnderlyingString(archetype);
    }
}

std::string ai::SessionStateToString(SessionState state)
{
    switch (state)
    {
        case SessionState::IDLE: return "idle";
        case SessionState::QUESTING: return "questing";
        case SessionState::REP_FARMING: return "rep_farming";
        case SessionState::DUNGEON_RUN: return "dungeon_run";
        case SessionState::TOURNAMENT: return "tournament";
        case SessionState::RARE_HUNTING: return "rare_hunting";
        case SessionState::CRAFTING_COOLDOWN: return "crafting_cooldown";
        case SessionState::WORLD_PVP: return "world_pvp";
        case SessionState::TRAVELLING: return "travelling";
        case SessionState::MAINTENANCE: return "maintenance";
        default: return ToUnderlyingString(state);
    }
}

std::string ai::InterruptTierToString(InterruptTier tier)
{
    switch (tier)
    {
        case InterruptTier::INTERRUPT_CRITICAL: return "critical";
        case InterruptTier::INTERRUPT_HIGH: return "high";
        case InterruptTier::INTERRUPT_MEDIUM: return "medium";
        case InterruptTier::INTERRUPT_LOW: return "low";
        case InterruptTier::INTERRUPT_NONE: return "none";
        default: return ToUnderlyingString(tier);
    }
}

BotArchetype ai::BotArchetypeFromString(const std::string& value)
{
    if (value == "casual") return BotArchetype::CASUAL;
    if (value == "regular") return BotArchetype::REGULAR;
    if (value == "rpg_quest") return BotArchetype::RPG_QUEST;
    if (value == "grinder") return BotArchetype::GRINDER;
    if (value == "farmer") return BotArchetype::FARMER;
    if (value == "hardcore") return BotArchetype::HARDCORE;

    return static_cast<BotArchetype>(std::stoi(value));
}

SessionState ai::SessionStateFromString(const std::string& value)
{
    if (value == "idle") return SessionState::IDLE;
    if (value == "questing") return SessionState::QUESTING;
    if (value == "rep_farming") return SessionState::REP_FARMING;
    if (value == "dungeon_run") return SessionState::DUNGEON_RUN;
    if (value == "tournament") return SessionState::TOURNAMENT;
    if (value == "rare_hunting") return SessionState::RARE_HUNTING;
    if (value == "crafting_cooldown") return SessionState::CRAFTING_COOLDOWN;
    if (value == "world_pvp") return SessionState::WORLD_PVP;
    if (value == "travelling") return SessionState::TRAVELLING;
    if (value == "maintenance") return SessionState::MAINTENANCE;

    return static_cast<SessionState>(std::stoi(value));
}
