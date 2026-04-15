#pragma once
#include "GenericActions.h"

namespace ai
{
    class ArchetypeAction : public ChatCommandAction
    {
    public:
        ArchetypeAction(PlayerbotAI* ai) : ChatCommandAction(ai, "archetype") {}
        bool Execute(Event& event) override;

        bool isUsefulWhenStunned() override { return true; }
    };
}
