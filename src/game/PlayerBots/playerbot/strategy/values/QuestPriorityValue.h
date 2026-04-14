#pragma once

#include "QuestValues.h"
#include <vector>

namespace ai
{
    struct ScoredQuest
    {
        uint32 questId = 0;
        float score = 0.0f;
    };

    class QuestPriorityValue : public FloatCalculatedValue, public Qualified
    {
    public:
        QuestPriorityValue(PlayerbotAI* ai) : FloatCalculatedValue(ai, "quest priority", 5), Qualified() {}

        float Calculate() override;

    private:
        float ScoreProgress(uint32 questId) const;
        float ScoreCompletion(uint32 questId) const;
        float ScoreChain(uint32 questId) const;
        float ScoreZone(uint32 questId) const;
        float ScoreLevel(uint32 questId) const;
        float ScoreAge(uint32 questId) const;
        float ScoreGroup(uint32 questId) const;
    };

    class ActiveQuestPriorityListValue : public CalculatedValue<std::vector<ScoredQuest>>
    {
    public:
        ActiveQuestPriorityListValue(PlayerbotAI* ai) : CalculatedValue<std::vector<ScoredQuest>>(ai, "active quest priority list", 10) {}

        std::vector<ScoredQuest> Calculate() override;
    };
}
