#pragma once
#include "playerbot/strategy/Value.h"

namespace ai
{
    class MasterTargetValue : public UnitCalculatedValue
	{
	public:
        MasterTargetValue(PlayerbotAI* ai, std::string name = "master target") : UnitCalculatedValue(ai, name) {}

        virtual Unit* Calculate() override
        {
            Player* master = ai->GetGroupMaster();
            return master && ai->IsSafe(master) ? master : nullptr;
        }
    };
}
