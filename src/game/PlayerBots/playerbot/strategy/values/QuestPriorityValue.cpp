#include "QuestPriorityValue.h"
#include "playerbot/playerbot.h"
#include "QuestDef.h"
#include <algorithm>

using namespace ai;

namespace
{
    float ClampQuestScore(float value)
    {
        return std::max(0.0f, std::min(100.0f, value));
    }

    const Quest* GetQuestTemplate(uint32 questId)
    {
        return sObjectMgr.GetQuestTemplate(questId);
    }
}

float QuestPriorityValue::Calculate()
{
    if (!Qualified::isValidNumberString(getQualifier()))
        return 0.0f;

    const uint32 questId = std::stoul(getQualifier());
    const Quest* quest = GetQuestTemplate(questId);
    if (!quest || !quest->IsActive())
        return 0.0f;

    const QuestStatusMap& questStatusMap = bot->GetQuestStatusMap();
    QuestStatusMap::const_iterator statusItr = questStatusMap.find(questId);
    if (statusItr == questStatusMap.end())
        return 0.0f;

    const QuestStatus status = statusItr->second.m_status;
    if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
        return 0.0f;

    const float score =
        ScoreProgress(questId) +
        ScoreCompletion(questId) +
        ScoreChain(questId) +
        ScoreZone(questId) +
        ScoreLevel(questId) +
        ScoreAge(questId) +
        ScoreGroup(questId);

    return ClampQuestScore(score);
}

float QuestPriorityValue::ScoreProgress(uint32 questId) const
{
    const Quest* quest = GetQuestTemplate(questId);
    if (!quest)
        return 0.0f;

    const QuestStatusMap& questStatusMap = bot->GetQuestStatusMap();
    QuestStatusMap::const_iterator statusItr = questStatusMap.find(questId);
    if (statusItr == questStatusMap.end())
        return 0.0f;

    const QuestStatusData& status = statusItr->second;

    float progressSum = 0.0f;
    uint32 progressObjectives = 0;

    for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        if (quest->ReqItemCount[i] > 0)
        {
            const float objectiveProgress = std::min(1.0f, float(status.m_itemcount[i]) / float(quest->ReqItemCount[i]));
            progressSum += objectiveProgress;
            ++progressObjectives;
        }

        if (quest->ReqCreatureOrGOCount[i] > 0)
        {
            const float objectiveProgress = std::min(1.0f, float(status.m_creatureOrGOcount[i]) / float(quest->ReqCreatureOrGOCount[i]));
            progressSum += objectiveProgress;
            ++progressObjectives;
        }
    }

    if (!progressObjectives)
    {
        if (status.m_status == QUEST_STATUS_COMPLETE)
            return 30.0f;

        return quest->GetSrcItemId() || quest->ReqSpell[0] ? 10.0f : 5.0f;
    }

    return (progressSum / float(progressObjectives)) * 30.0f;
}

float QuestPriorityValue::ScoreCompletion(uint32 questId) const
{
    if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE || bot->GetQuestRewardStatus(questId))
        return 0.0f;

    float score = 80.0f;
    if (AI_VALUE(bool, "has nearby quest taker"))
        score += 20.0f;

    return score;
}

float QuestPriorityValue::ScoreChain(uint32 questId) const
{
    const Quest* quest = GetQuestTemplate(questId);
    if (!quest)
        return 0.0f;

    float score = 0.0f;

    if (quest->GetRequiredClasses())
        score += 20.0f;

    if (quest->GetNextQuestInChain())
        score += 20.0f;

    if (quest->GetNextQuestId())
        score += 10.0f;

    return score;
}

float QuestPriorityValue::ScoreZone(uint32 questId) const
{
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    if (!travelTarget || !travelTarget->GetDestination())
        return 0.0f;

    if (QuestTravelDestination* questDestination = dynamic_cast<QuestTravelDestination*>(travelTarget->GetDestination()))
    {
        if (questDestination->GetQuestId() == questId)
            return 20.0f;
    }

    return 0.0f;
}

float QuestPriorityValue::ScoreLevel(uint32 questId) const
{
    const Quest* quest = GetQuestTemplate(questId);
    if (!quest)
        return 0.0f;

    const int32 levelDelta = int32(bot->GetLevel()) - int32(bot->GetQuestLevelForPlayer(quest));
    if (levelDelta <= 2 && levelDelta >= -2)
        return 15.0f;
    if (levelDelta <= 5 && levelDelta >= -4)
        return 8.0f;
    if (levelDelta >= 10)
        return -20.0f;
    if (levelDelta <= -8)
        return -10.0f;

    return 0.0f;
}

float QuestPriorityValue::ScoreAge(uint32 /*questId*/) const
{
    // We do not persist quest accept timestamps yet.
    return 0.0f;
}

float QuestPriorityValue::ScoreGroup(uint32 questId) const
{
    const Quest* quest = GetQuestTemplate(questId);
    if (!quest)
        return 0.0f;

    if (bot->GetGroup() && (quest->GetType() == QUEST_TYPE_DUNGEON || quest->IsAllowedInRaid()))
        return 12.0f;

    return 0.0f;
}

std::vector<ScoredQuest> ActiveQuestPriorityListValue::Calculate()
{
    std::vector<ScoredQuest> quests;

    const QuestStatusMap& questStatusMap = bot->GetQuestStatusMap();
    for (const auto& [questId, status] : questStatusMap)
    {
        if (status.m_status != QUEST_STATUS_INCOMPLETE && status.m_status != QUEST_STATUS_COMPLETE)
            continue;

        ScoredQuest entry;
        entry.questId = questId;
        entry.score = AI_VALUE2(float, "quest priority", std::to_string(questId));
        quests.push_back(entry);
    }

    std::sort(quests.begin(), quests.end(), [](const ScoredQuest& left, const ScoredQuest& right)
    {
        if (left.score == right.score)
            return left.questId < right.questId;
        return left.score > right.score;
    });

    return quests;
}
