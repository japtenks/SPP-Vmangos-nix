#pragma once

#include "Common.h"
#include "ObjectGuid.h"
#include "playerbot/strategy/values/TravelValues.h"
#include <ctime>
#include <string>

class PlayerbotAI;

namespace ai
{
    class TravelTarget;

    enum class BotArchetype : uint8
    {
        CASUAL = 0,
        REGULAR = 1,
        RPG_QUEST = 2,
        GRINDER = 3,
        FARMER = 4,
        HARDCORE = 5,
    };

    struct ArchetypeWeights
    {
        float progressWeight = 1.0f;
        float chainWeight = 1.0f;
        float zoneWeight = 1.0f;
        float levelWeight = 1.0f;
        float knowledgeWeight = 1.0f;
        float curiosityWeight = 1.0f;
        float routineTolerance = 1.0f;
        float explorationRadiusBias = 1.0f;

        float fleeHealthThreshold = 0.25f;
        float hazardSeverityThreshold = 0.50f;

        uint32 minSessionMinutes = 15;
        uint32 maxSessionMinutes = 60;
        float daysPerWeek = 3.0f;

        float groupJoinChance = 0.20f;
        float groupLeaveChance = 0.05f;
    };

    enum class InterruptTier : uint8
    {
        INTERRUPT_CRITICAL = 0,
        INTERRUPT_HIGH = 1,
        INTERRUPT_MEDIUM = 2,
        INTERRUPT_LOW = 3,
        INTERRUPT_NONE = 4,
    };

    enum class SessionState : uint8
    {
        IDLE = 0,
        QUESTING = 1,
        REP_FARMING = 2,
        DUNGEON_RUN = 3,
        TOURNAMENT = 4,
        RARE_HUNTING = 5,
        CRAFTING_COOLDOWN = 6,
        WORLD_PVP = 7,
        TRAVELLING = 8,
        MAINTENANCE = 9,
    };

    struct BotSession
    {
        SessionState state = SessionState::IDLE;
        uint32 plannedDuration = 0;
        time_t startedAt = 0;
        time_t pausedAt = 0;
        time_t maintenanceBreakpointUntil = 0;
        bool isPaused = false;

        void Reset(SessionState newState = SessionState::IDLE);
    };

    struct CommittedTask
    {
        TravelDestinationPurpose purpose = TravelDestinationPurpose::None;
        ObjectGuid targetGuid;
        int32 destinationEntry = 0;
        uint8 objectiveIndex = 0;
        uint32 questId = 0;
        time_t failCooldownUntil = 0;
        uint8 retryCount = 0;
        time_t lastValidityCheck = 0;
        bool isValid = false;

        InterruptTier GetInterruptTier() const;
        bool CanBePreemptedBy(InterruptTier tier) const;
        bool MatchesTarget(const TravelTarget* target) const;
        bool ValidateTarget(PlayerbotAI* ai, time_t now = 0, uint32 throttleSeconds = 5);
        void Clear();
    };

    std::string BotArchetypeToString(BotArchetype archetype);
    std::string SessionStateToString(SessionState state);
    std::string InterruptTierToString(InterruptTier tier);

    BotArchetype BotArchetypeFromString(const std::string& value);
    SessionState SessionStateFromString(const std::string& value);
}
