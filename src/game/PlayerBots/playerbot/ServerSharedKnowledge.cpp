#include "ServerSharedKnowledge.h"

#include "Policies/SingletonImp.h"

#include <algorithm>

INSTANTIATE_SINGLETON_1(ServerSharedKnowledge);

void ServerSharedKnowledge::RecordTrainerTeaching(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId, float delta)
{
    if (!trainerEntry || !trainerRequirement || !teachId)
        return;

    TrainerTeachingKnowledgeKey key;
    key.trainerEntry = trainerEntry;
    key.trainerRequirement = trainerRequirement;
    key.teachId = teachId;
    key.mapId = mapId;

    std::unique_lock<std::shared_mutex> lock(m_trainerTeachingMutex);
    TrainerTeachingKnowledgeEntry& entry = m_trainerTeachingKnowledge[key];
    entry.confidence = std::min(1.0f, entry.confidence + delta);
    ++entry.observations;
    entry.lastConfirmed = time(nullptr);
}

float ServerSharedKnowledge::GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId) const
{
    if (!trainerEntry || !trainerRequirement || !teachId)
        return 0.0f;

    TrainerTeachingKnowledgeKey exactKey{ trainerEntry, trainerRequirement, teachId, mapId };
    TrainerTeachingKnowledgeKey anyMapKey{ trainerEntry, trainerRequirement, teachId, 0 };

    std::shared_lock<std::shared_mutex> lock(m_trainerTeachingMutex);

    auto exactItr = m_trainerTeachingKnowledge.find(exactKey);
    if (exactItr != m_trainerTeachingKnowledge.end())
        return exactItr->second.confidence;

    auto anyMapItr = m_trainerTeachingKnowledge.find(anyMapKey);
    if (anyMapItr != m_trainerTeachingKnowledge.end())
        return anyMapItr->second.confidence;

    return 0.0f;
}

float ServerSharedKnowledge::GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, std::vector<uint32> const& teachIds, uint32 mapId) const
{
    float confidence = 0.0f;
    for (uint32 teachId : teachIds)
        confidence = std::max(confidence, GetTrainerTeachingConfidence(trainerEntry, trainerRequirement, teachId, mapId));

    return confidence;
}

void ServerSharedKnowledge::RecordTrainerSkill(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, float delta)
{
    RecordTrainerTeaching(trainerEntry, trainerClass, skillId, mapId, delta);
}

float ServerSharedKnowledge::GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId) const
{
    return GetTrainerTeachingConfidence(trainerEntry, trainerClass, skillId, mapId);
}

float ServerSharedKnowledge::GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, std::vector<uint32> const& skillIds, uint32 mapId) const
{
    return GetTrainerTeachingConfidence(trainerEntry, trainerClass, skillIds, mapId);
}
