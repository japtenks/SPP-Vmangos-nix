#include "ServerSharedKnowledge.h"

#include "Policies/SingletonImp.h"

#include <algorithm>

INSTANTIATE_SINGLETON_1(ServerSharedKnowledge);

void ServerSharedKnowledge::RecordTrainerSkill(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, float delta)
{
    if (!trainerEntry || !trainerClass || !skillId)
        return;

    TrainerSkillKnowledgeKey key;
    key.trainerEntry = trainerEntry;
    key.trainerClass = trainerClass;
    key.skillId = skillId;
    key.mapId = mapId;

    std::unique_lock<std::shared_mutex> lock(m_trainerSkillMutex);
    TrainerSkillKnowledgeEntry& entry = m_trainerSkillKnowledge[key];
    entry.confidence = std::min(1.0f, entry.confidence + delta);
    ++entry.observations;
    entry.lastConfirmed = time(nullptr);
}

float ServerSharedKnowledge::GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId) const
{
    if (!trainerEntry || !trainerClass || !skillId)
        return 0.0f;

    TrainerSkillKnowledgeKey exactKey{ trainerEntry, trainerClass, skillId, mapId };
    TrainerSkillKnowledgeKey anyMapKey{ trainerEntry, trainerClass, skillId, 0 };

    std::shared_lock<std::shared_mutex> lock(m_trainerSkillMutex);

    auto exactItr = m_trainerSkillKnowledge.find(exactKey);
    if (exactItr != m_trainerSkillKnowledge.end())
        return exactItr->second.confidence;

    auto anyMapItr = m_trainerSkillKnowledge.find(anyMapKey);
    if (anyMapItr != m_trainerSkillKnowledge.end())
        return anyMapItr->second.confidence;

    return 0.0f;
}

float ServerSharedKnowledge::GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, std::vector<uint32> const& skillIds, uint32 mapId) const
{
    float confidence = 0.0f;
    for (uint32 skillId : skillIds)
        confidence = std::max(confidence, GetTrainerSkillConfidence(trainerEntry, trainerClass, skillId, mapId));

    return confidence;
}
