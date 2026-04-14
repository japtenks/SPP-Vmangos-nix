#pragma once

#include "Common.h"
#include "Policies/Singleton.h"

#include <shared_mutex>
#include <unordered_map>
#include <vector>

enum class NpcKnowledgePurpose : uint8
{
    TRAINER_TEACHING = 1,
    TRAINER_SKILL = 2,
};

struct NpcUsefulnessKnowledgeKey
{
    uint32 npcEntry = 0;
    uint32 purpose = 0;
    uint32 requirement = 0;
    uint32 offeredId = 0;
    uint32 mapId = 0;
    uint32 cityId = 0;

    bool operator==(NpcUsefulnessKnowledgeKey const& other) const
    {
        return npcEntry == other.npcEntry &&
            purpose == other.purpose &&
            requirement == other.requirement &&
            offeredId == other.offeredId &&
            mapId == other.mapId &&
            cityId == other.cityId;
    }
};

struct NpcUsefulnessKnowledgeEntry
{
    float confidence = 0.0f;
    uint32 observations = 0;
    time_t lastConfirmed = 0;
};

class ServerSharedKnowledge
{
public:
    void RecordNpcUsefulness(uint32 npcEntry, uint32 purpose, uint32 requirement, uint32 offeredId, uint32 mapId, uint32 cityId = 0, float delta = 0.1f);
    float GetNpcUsefulnessConfidence(uint32 npcEntry, uint32 purpose, uint32 requirement, uint32 offeredId, uint32 mapId, uint32 cityId = 0) const;
    float GetNpcUsefulnessConfidence(uint32 npcEntry, uint32 purpose, uint32 requirement, std::vector<uint32> const& offeredIds, uint32 mapId, uint32 cityId = 0) const;
    uint32 GetNpcUsefulnessObservations(uint32 purpose = 0) const;
    float GetKnowledgeMaturity(uint32 purpose = 0) const;

    void RecordTrainerTeaching(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId, uint32 cityId = 0, float delta = 0.1f);
    float GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId, uint32 cityId = 0) const;
    float GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, std::vector<uint32> const& teachIds, uint32 mapId, uint32 cityId = 0) const;

    void RecordTrainerSkill(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, uint32 cityId = 0, float delta = 0.1f);
    float GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, uint32 cityId = 0) const;
    float GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, std::vector<uint32> const& skillIds, uint32 mapId, uint32 cityId = 0) const;

private:
    struct NpcUsefulnessKnowledgeKeyHash
    {
        std::size_t operator()(NpcUsefulnessKnowledgeKey const& key) const
        {
            std::size_t seed = key.npcEntry;
            seed = (seed * 1315423911u) ^ key.purpose;
            seed = (seed * 1315423911u) ^ key.requirement;
            seed = (seed * 1315423911u) ^ key.offeredId;
            seed = (seed * 1315423911u) ^ key.mapId;
            seed = (seed * 1315423911u) ^ key.cityId;
            return seed;
        }
    };

    mutable std::shared_mutex m_npcUsefulnessMutex;
    std::unordered_map<NpcUsefulnessKnowledgeKey, NpcUsefulnessKnowledgeEntry, NpcUsefulnessKnowledgeKeyHash> m_npcUsefulnessKnowledge;
};

#define sServerSharedKnowledge MaNGOS::Singleton<ServerSharedKnowledge>::Instance()
