#pragma once

#include "BotState.h"
#include <string>

namespace ai
{
    enum class BehaviorCoordination
    {
        SOLO = 0,
        GROUP = 1
    };

    enum class BehaviorTask
    {
        NONE = 0,
        QUEST,
        MAINTENANCE,
        SKILLS,
        SOCIAL,
        ECONOMY,
        GATHER,
        GROUP,
        IDLE,
        DEFEND,
        ASSIST,
        QUEST_COMBAT,
        CONTROL,
        SURVIVAL,
        ROLE,
        RECOVERY,
        REACTION
    };

    // This key is a framework scaffold for describing current intent without
    // forcing an immediate planner rewrite. Existing logic can populate it
    // gradually while keeping the runtime state engines unchanged.
    struct BehaviorFrame
    {
        BotState state = BotState::BOT_STATE_NON_COMBAT;
        BehaviorCoordination coordination = BehaviorCoordination::SOLO;
        BehaviorTask task = BehaviorTask::NONE;
        std::string stage;
        std::string focus;

        bool IsAssigned() const;
        void Clear(BotState fallbackState = BotState::BOT_STATE_NON_COMBAT);
    };

    std::string BehaviorCoordinationToString(BehaviorCoordination coordination);
    BehaviorCoordination BehaviorCoordinationFromString(std::string const& value);

    std::string BehaviorStateToString(BotState state);
    BotState BehaviorStateFromString(std::string const& value);

    std::string BehaviorTaskToString(BehaviorTask task);
    BehaviorTask BehaviorTaskFromString(std::string const& value);

    std::string FormatBehaviorFrame(BehaviorFrame const& frame);
} // namespace ai
