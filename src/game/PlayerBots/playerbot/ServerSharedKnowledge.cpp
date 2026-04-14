#include "ServerSharedKnowledge.h"

#include "Policies/SingletonImp.h"

#include <algorithm>

INSTANTIATE_SINGLETON_1(ServerSharedKnowledge);

namespace
{
    float ClampKnowledgeMaturity(uint32 observations)
    {
        constexpr float matureObservationTarget = 200.0f;
        return std::min(1.0f, static_cast<float>(observations) / matureObservationTarget);
    }
}

void ServerSharedKnowledge::RecordNpcUsefulness(uint32 npcEntry, uint32 purpose, uint32 requirement, uint32 offeredId, uint32 mapId, uint32 cityId, float delta)
{
    if (!npcEntry || !purpose || !requirement || !offeredId)
        return;

    NpcUsefulnessKnowledgeKey key{npcEntry, purpose, requirement, offeredId, mapId, cityId};

    std::unique_lock<std::shared_mutex> lock(m_npcUsefulnessMutex);
    NpcUsefulnessKnowledgeEntry& entry = m_npcUsefulnessKnowledge[key];
    entry.confidence = std::min(1.0f, entry.confidence + delta);
    ++entry.observations;
    entry.lastConfirmed = time(nullptr);
}

float ServerSharedKnowledge::GetNpcUsefulnessConfidence(uint32 npcEntry, uint32 purpose, uint32 requirement, uint32 offeredId, uint32 mapId, uint32 cityId) const
{
    if (!npcEntry || !purpose || !requirement || !offeredId)
        return 0.0f;

    const NpcUsefulnessKnowledgeKey lookupKeys[] =
    {
        { npcEntry, purpose, requirement, offeredId, mapId, cityId },
        { npcEntry, purpose, requirement, offeredId, mapId, 0 },
        { npcEntry, purpose, requirement, offeredId, 0, cityId },
        { npcEntry, purpose, requirement, offeredId, 0, 0 }
    };

    std::shared_lock<std::shared_mutex> lock(m_npcUsefulnessMutex);
    for (NpcUsefulnessKnowledgeKey const& key : lookupKeys)
    {
        auto itr = m_npcUsefulnessKnowledge.find(key);
        if (itr != m_npcUsefulnessKnowledge.end())
            return itr->second.confidence;
    }

    return 0.0f;
}

float ServerSharedKnowledge::GetNpcUsefulnessConfidence(uint32 npcEntry, uint32 purpose, uint32 requirement, std::vector<uint32> const& offeredIds, uint32 mapId, uint32 cityId) const
{
    float confidence = 0.0f;
    for (uint32 offeredId : offeredIds)
        confidence = std::max(confidence, GetNpcUsefulnessConfidence(npcEntry, purpose, requirement, offeredId, mapId, cityId));

    return confidence;
}

uint32 ServerSharedKnowledge::GetNpcUsefulnessObservations(uint32 purpose) const
{
    std::shared_lock<std::shared_mutex> lock(m_npcUsefulnessMutex);

    uint32 observations = 0;
    for (auto const& [key, entry] : m_npcUsefulnessKnowledge)
    {
        if (purpose && key.purpose != purpose)
            continue;

        observations += entry.observations;
    }

    return observations;
}

float ServerSharedKnowledge::GetKnowledgeMaturity(uint32 purpose) const
{
    return ClampKnowledgeMaturity(GetNpcUsefulnessObservations(purpose));
}

void ServerSharedKnowledge::RecordTrainerTeaching(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId, uint32 cityId, float delta)
{
    RecordNpcUsefulness(trainerEntry, static_cast<uint32>(NpcKnowledgePurpose::TRAINER_TEACHING), trainerRequirement, teachId, mapId, cityId, delta);
}

float ServerSharedKnowledge::GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId, uint32 cityId) const
{
    return GetNpcUsefulnessConfidence(trainerEntry, static_cast<uint32>(NpcKnowledgePurpose::TRAINER_TEACHING), trainerRequirement, teachId, mapId, cityId);
}

float ServerSharedKnowledge::GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, std::vector<uint32> const& teachIds, uint32 mapId, uint32 cityId) const
{
    return GetNpcUsefulnessConfidence(trainerEntry, static_cast<uint32>(NpcKnowledgePurpose::TRAINER_TEACHING), trainerRequirement, teachIds, mapId, cityId);
}

void ServerSharedKnowledge::RecordTrainerSkill(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, uint32 cityId, float delta)
{
    RecordNpcUsefulness(trainerEntry, static_cast<uint32>(NpcKnowledgePurpose::TRAINER_SKILL), trainerClass, skillId, mapId, cityId, delta);
}

float ServerSharedKnowledge::GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, uint32 cityId) const
{
    return GetNpcUsefulnessConfidence(trainerEntry, static_cast<uint32>(NpcKnowledgePurpose::TRAINER_SKILL), trainerClass, skillId, mapId, cityId);
}

float ServerSharedKnowledge::GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, std::vector<uint32> const& skillIds, uint32 mapId, uint32 cityId) const
{
    return GetNpcUsefulnessConfidence(trainerEntry, static_cast<uint32>(NpcKnowledgePurpose::TRAINER_SKILL), trainerClass, skillIds, mapId, cityId);
}
