#pragma once

#include "playerbot/BotArchetype.h"
#include "playerbot/strategy/Value.h"

namespace ai
{
    class SessionStateValue : public Uint8CalculatedValue
    {
    public:
        SessionStateValue(PlayerbotAI* ai, std::string name = "session state", int checkInterval = 5)
            : Uint8CalculatedValue(ai, name, checkInterval) {}

    protected:
        uint8 Calculate() override
        {
            return static_cast<uint8>(ai->GetSession().state);
        }
    };

    class SessionPausedValue : public BoolCalculatedValue
    {
    public:
        SessionPausedValue(PlayerbotAI* ai, std::string name = "session paused", int checkInterval = 5)
            : BoolCalculatedValue(ai, name, checkInterval) {}

    protected:
        bool Calculate() override
        {
            return ai->GetSession().isPaused;
        }
    };

    class CommittedTaskValidValue : public BoolCalculatedValue
    {
    public:
        CommittedTaskValidValue(PlayerbotAI* ai, std::string name = "committed task valid", int checkInterval = 5)
            : BoolCalculatedValue(ai, name, checkInterval) {}

    protected:
        bool Calculate() override
        {
            return ai->GetCommittedTask().isValid;
        }
    };

    class CommittedTaskTierValue : public Uint8CalculatedValue
    {
    public:
        CommittedTaskTierValue(PlayerbotAI* ai, std::string name = "committed task tier", int checkInterval = 5)
            : Uint8CalculatedValue(ai, name, checkInterval) {}

    protected:
        uint8 Calculate() override
        {
            return static_cast<uint8>(ai->GetCommittedTask().GetInterruptTier());
        }
    };

    class CommittedTaskPurposeValue : public Uint32CalculatedValue
    {
    public:
        CommittedTaskPurposeValue(PlayerbotAI* ai, std::string name = "committed task purpose", int checkInterval = 5)
            : Uint32CalculatedValue(ai, name, checkInterval) {}

    protected:
        uint32 Calculate() override
        {
            return static_cast<uint32>(ai->GetCommittedTask().purpose);
        }
    };

    class CommittedTaskQuestValue : public Uint32CalculatedValue
    {
    public:
        CommittedTaskQuestValue(PlayerbotAI* ai, std::string name = "committed task quest", int checkInterval = 5)
            : Uint32CalculatedValue(ai, name, checkInterval) {}

    protected:
        uint32 Calculate() override
        {
            return ai->GetCommittedTask().questId;
        }
    };
}
