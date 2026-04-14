#pragma once

#include "Common.h"
#include "Policies/Singleton.h"

#include <shared_mutex>
#include <unordered_map>
#include <vector>

struct TrainerSkillKnowledgeKey
{
    uint32 trainerEntry = 0;
    uint32 trainerClass = 0;
    uint32 skillId = 0;
    uint32 mapId = 0;

    bool operator==(TrainerSkillKnowledgeKey const& other) const
    {
        return trainerEntry == other.trainerEntry &&
            trainerClass == other.trainerClass &&
            skillId == other.skillId &&
            mapId == other.mapId;
    }
};

struct TrainerSkillKnowledgeEntry
{
    float confidence = 0.0f;
    uint32 observations = 0;
    time_t lastConfirmed = 0;
};

class ServerSharedKnowledge
{
public:
    void RecordTrainerSkill(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId, float delta = 0.1f);
    float GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, uint32 skillId, uint32 mapId) const;
    float GetTrainerSkillConfidence(uint32 trainerEntry, uint32 trainerClass, std::vector<uint32> const& skillIds, uint32 mapId) const;

private:
    struct TrainerSkillKnowledgeKeyHash
    {
        std::size_t operator()(TrainerSkillKnowledgeKey const& key) const
        {
            std::size_t seed = key.trainerEntry;
            seed = (seed * 1315423911u) ^ key.trainerClass;
            seed = (seed * 1315423911u) ^ key.skillId;
            seed = (seed * 1315423911u) ^ key.mapId;
            return seed;
        }
    };

    mutable std::shared_mutex m_trainerSkillMutex;
    std::unordered_map<TrainerSkillKnowledgeKey, TrainerSkillKnowledgeEntry, TrainerSkillKnowledgeKeyHash> m_trainerSkillKnowledge;
};

#define sServerSharedKnowledge MaNGOS::Singleton<ServerSharedKnowledge>::Instance()
