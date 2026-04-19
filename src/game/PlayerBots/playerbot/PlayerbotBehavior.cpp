#include "playerbot/PlayerbotBehavior.h"

#include <algorithm>

using namespace ai;

namespace
{
    std::string NormalizeBehaviorToken(std::string value)
    {
        std::replace(value.begin(), value.end(), ' ', '-');
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
        return value;
    }
}

bool BehaviorFrame::IsAssigned() const
{
    return task != BehaviorTask::NONE || !stage.empty() || !focus.empty();
}

void BehaviorFrame::Clear(BotState fallbackState)
{
    state = fallbackState;
    coordination = BehaviorCoordination::SOLO;
    task = BehaviorTask::NONE;
    stage.clear();
    focus.clear();
}

std::string ai::BehaviorCoordinationToString(BehaviorCoordination coordination)
{
    switch (coordination)
    {
        case BehaviorCoordination::GROUP:
            return "group";
        case BehaviorCoordination::SOLO:
        default:
            return "solo";
    }
}

BehaviorCoordination ai::BehaviorCoordinationFromString(std::string const& value)
{
    return NormalizeBehaviorToken(value) == "group" ? BehaviorCoordination::GROUP : BehaviorCoordination::SOLO;
}

std::string ai::BehaviorStateToString(BotState state)
{
    switch (state)
    {
        case BotState::BOT_STATE_COMBAT:
            return "CO";
        case BotState::BOT_STATE_NON_COMBAT:
            return "NC";
        case BotState::BOT_STATE_DEAD:
            return "DEAD";
        case BotState::BOT_STATE_REACTION:
            return "REACT";
        case BotState::BOT_STATE_ALL:
        default:
            return "ALL";
    }
}

BotState ai::BehaviorStateFromString(std::string const& value)
{
    const std::string normalized = NormalizeBehaviorToken(value);

    if (normalized == "co" || normalized == "combat")
        return BotState::BOT_STATE_COMBAT;
    if (normalized == "nc" || normalized == "non-combat" || normalized == "non_combat" || normalized == "noncombat")
        return BotState::BOT_STATE_NON_COMBAT;
    if (normalized == "dead")
        return BotState::BOT_STATE_DEAD;
    if (normalized == "react" || normalized == "reaction")
        return BotState::BOT_STATE_REACTION;

    return BotState::BOT_STATE_ALL;
}

std::string ai::BehaviorTaskToString(BehaviorTask task)
{
    switch (task)
    {
        case BehaviorTask::QUEST:
            return "quest";
        case BehaviorTask::MAINTENANCE:
            return "maintenance";
        case BehaviorTask::SKILLS:
            return "skills";
        case BehaviorTask::SOCIAL:
            return "social";
        case BehaviorTask::ECONOMY:
            return "economy";
        case BehaviorTask::GATHER:
            return "gather";
        case BehaviorTask::GROUP:
            return "group";
        case BehaviorTask::IDLE:
            return "idle";
        case BehaviorTask::DEFEND:
            return "defend";
        case BehaviorTask::ASSIST:
            return "assist";
        case BehaviorTask::QUEST_COMBAT:
            return "quest-combat";
        case BehaviorTask::CONTROL:
            return "control";
        case BehaviorTask::SURVIVAL:
            return "survival";
        case BehaviorTask::ROLE:
            return "role";
        case BehaviorTask::RECOVERY:
            return "recovery";
        case BehaviorTask::REACTION:
            return "reaction";
        case BehaviorTask::NONE:
        default:
            return "none";
    }
}

BehaviorTask ai::BehaviorTaskFromString(std::string const& value)
{
    const std::string normalized = NormalizeBehaviorToken(value);

    if (normalized == "quest")
        return BehaviorTask::QUEST;
    if (normalized == "maintenance")
        return BehaviorTask::MAINTENANCE;
    if (normalized == "skills")
        return BehaviorTask::SKILLS;
    if (normalized == "social")
        return BehaviorTask::SOCIAL;
    if (normalized == "economy")
        return BehaviorTask::ECONOMY;
    if (normalized == "gather")
        return BehaviorTask::GATHER;
    if (normalized == "group")
        return BehaviorTask::GROUP;
    if (normalized == "idle")
        return BehaviorTask::IDLE;
    if (normalized == "defend")
        return BehaviorTask::DEFEND;
    if (normalized == "assist")
        return BehaviorTask::ASSIST;
    if (normalized == "quest-combat")
        return BehaviorTask::QUEST_COMBAT;
    if (normalized == "control")
        return BehaviorTask::CONTROL;
    if (normalized == "survival")
        return BehaviorTask::SURVIVAL;
    if (normalized == "role")
        return BehaviorTask::ROLE;
    if (normalized == "recovery")
        return BehaviorTask::RECOVERY;
    if (normalized == "reaction")
        return BehaviorTask::REACTION;

    return BehaviorTask::NONE;
}

std::string ai::FormatBehaviorFrame(BehaviorFrame const& frame)
{
    const std::string state = BehaviorStateToString(frame.state);
    const std::string coordination = BehaviorCoordinationToString(frame.coordination);
    const std::string task = BehaviorTaskToString(frame.task);
    const std::string stage = frame.stage.empty() ? "none" : frame.stage;
    const std::string focus = frame.focus.empty() ? "none" : frame.focus;

    return state + " / " + coordination + " / " + task + " / " + stage + " / " + focus;
}
