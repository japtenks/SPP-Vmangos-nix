#pragma once

#include "Common.h"
#include "Policies/Singleton.h"

#include <shared_mutex>
#include <unordered_map>
#include <vector>

struct TrainerTeachingKnowledgeKey
{
    uint32 trainerEntry = 0;
    uint32 trainerRequirement = 0;
    uint32 teachId = 0;
    uint32 mapId = 0;

    bool operator==(TrainerTeachingKnowledgeKey const& other) const
    {
        return trainerEntry == other.trainerEntry &&
            trainerRequirement == other.trainerRequirement &&
            teachId == other.teachId &&
            mapId == other.mapId;
    }
};

struct TrainerTeachingKnowledgeEntry
{
    float confidence = 0.0f;
    uint32 observations = 0;
    time_t lastConfirmed = 0;
};

class ServerSharedKnowledge
{
public:
    void RecordTrainerTeaching(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId, float delta = 0.1f);
    float GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, uint32 teachId, uint32 mapId) const;
    float GetTrainerTeachingConfidence(uint32 trainerEntry, uint32 trainerRequirement, std::vector<uint32> const& teachIds, uint32 mapId) const;

    void RecordTrainerSkill(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, float delta = 0.1f);
    float GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId) const;
    float GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, std::vector<uint32> const& skillIds, uint32 mapId) const;

private:
    struct TrainerTeachingKnowledgeKeyHash
    {
        std::size_t operator()(TrainerTeachingKnowledgeKey const& key) const
        {
            std::size_t seed = key.trainerEntry;
            seed = (seed * 1315423911u) ^ key.trainerRequirement;
            seed = (seed * 1315423911u) ^ key.teachId;
            seed = (seed * 1315423911u) ^ key.mapId;
            return seed;
        }
    };

    mutable std::shared_mutex m_trainerTeachingMutex;
    std::unordered_map<TrainerTeachingKnowledgeKey, TrainerTeachingKnowledgeEntry, TrainerTeachingKnowledgeKeyHash> m_trainerTeachingKnowledge;
};

#define sServerSharedKnowledge MaNGOS::Singleton<ServerSharedKnowledge>::Instance()
