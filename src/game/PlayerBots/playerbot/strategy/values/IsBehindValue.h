#pragma once
#include <cmath>
#include "playerbot/strategy/Value.h"

namespace ai
{
    inline float NormalizeAngleToPi(float angle)
    {
        while (angle > M_PI)
            angle -= static_cast<float>(2.0 * M_PI);
        while (angle < -M_PI)
            angle += static_cast<float>(2.0 * M_PI);

        return angle;
    }

    class IsBehindValue : public BoolCalculatedValue, public Qualified
	{
	public:
        IsBehindValue(PlayerbotAI* ai) : BoolCalculatedValue(ai), Qualified() {}

        virtual bool Calculate() override
        {
            Unit* target = AI_VALUE(Unit*, qualifier);
            if (!target)
                return false;

            if (!bot->CanReachWithMeleeAutoAttack(target))
                return false;

            const float angleToBot = target->GetAngle(bot);
            const float relativeAngle = std::fabs(NormalizeAngleToPi(angleToBot - target->GetOrientation()));
            return relativeAngle > static_cast<float>(M_PI / 2.0);
        }
    };
}
