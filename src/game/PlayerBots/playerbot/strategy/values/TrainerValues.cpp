
#include "playerbot/playerbot.h"
#include "playerbot/ServerSharedKnowledge.h"
#include "TrainerValues.h"
#include "SharedValueContext.h"
#include "ItemUsageValue.h"
#include "playerbot/PlayerbotHelpMgr.h"

#include <algorithm>
#include <unordered_map>

using namespace ai;

namespace
{
    std::vector<uint32> GetMissingWeaponSkills(Player* bot)
    {
        std::vector<uint32> missingSkills;
        if (!bot || !bot->GetPlayerbotAI())
            return missingSkills;

        AiObjectContext* context = bot->GetPlayerbotAI()->GetAiObjectContext();
        for (uint32 skillId : ItemUsageValue::TrackedWeaponSkills())
        {
            if (context->GetValue<bool>("needs weapon skill", std::to_string(skillId))->Get())
                missingSkills.push_back(skillId);
        }

        return missingSkills;
    }

    bool TeachesWeaponSkill(TrainerSpell const* trainerSpell, uint32 skillId)
    {
        if (!trainerSpell || !skillId)
            return false;

        SpellLearnSkillNode const* learnSkill = sSpellMgr.GetSpellLearnSkill(trainerSpell->spell[0]);
        if (learnSkill && learnSkill->skill == skillId)
            return true;

        SpellLearnSpellMapBounds bounds = sSpellMgr.GetSpellLearnSpellMapBounds(trainerSpell->spell[0]);
        for (SpellLearnSpellMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            SpellLearnSkillNode const* learnedSkill = sSpellMgr.GetSpellLearnSkill(itr->second.spell);
            if (learnedSkill && learnedSkill->skill == skillId)
                return true;
        }

        return false;
    }

    bool TeachesAnyMissingWeaponSkill(TrainerSpell const* trainerSpell, Player* bot)
    {
        for (uint32 skillId : GetMissingWeaponSkills(bot))
        {
            if (TeachesWeaponSkill(trainerSpell, skillId))
                return true;
        }

        return false;
    }

    float GetTrainerKnowledgeScore(uint32 trainerEntry, Player* bot)
    {
        if (!bot)
            return 0.0f;

        return sServerSharedKnowledge.GetTrainerSkillConfidence(trainerEntry, bot->GetClass(), GetMissingWeaponSkills(bot), bot->GetMapId());
    }
}


trainableSpellMap* TrainableSpellMapValue::Calculate()
{
    trainableSpellMap* spellMap = new trainableSpellMap;

    //           template, trainer
    std::unordered_map <uint32, std::vector<CreatureInfo const*>> trainerTemplateIds;

    //Select all trainer lists and their trainers.
    for (uint32 id = 0; id < sCreatureStorage.GetMaxEntry(); ++id)
    {
        CreatureInfo const* creatureInfo = sCreatureStorage.LookupEntry<CreatureInfo>(id);
        if (!creatureInfo)
            continue;

        if (!creatureInfo->trainer_type && !creatureInfo->trainer_class)
            continue;

        if(creatureInfo->entry)
            trainerTemplateIds[creatureInfo->entry].push_back(creatureInfo);
        else
            trainerTemplateIds[id].push_back(creatureInfo);
    }

    for (auto& [templateOrEntryId, trainers] : trainerTemplateIds)
    {
        TrainerSpellData const* trainer_spells = sObjectMgr.GetNpcTrainerTemplateSpells(templateOrEntryId);
        if (!trainer_spells)
            trainer_spells = sObjectMgr.GetNpcTrainerSpells(templateOrEntryId);

        if (!trainer_spells)
            continue;

        CreatureInfo const* firstTrainer = trainers.front();

        TrainerType trainerType = (TrainerType)firstTrainer->trainer_type;

        uint32 spellRequirement;
        if (trainerType == TRAINER_TYPE_CLASS || trainerType == TRAINER_TYPE_PETS)
            spellRequirement = firstTrainer->trainer_class;
        else if (trainerType == TRAINER_TYPE_MOUNTS)
            spellRequirement = 0 /* TrainerRace not in vmangos */;

        for (auto& [id, trainerSpell] : trainer_spells->spellList)
        {
            const TrainerSpell* sameTrainerSpell = &trainerSpell;
            for (auto& [otherTrainerSpell, trainers] : (*spellMap)[trainerType][spellRequirement])
            {
                if (false /* othertrainerSpell not available */)
                    continue;

                if (otherTrainerSpell->spellCost != trainerSpell.spellCost)
                    continue;

                if (otherTrainerSpell->reqSkill != trainerSpell.reqSkill)
                    continue;

                if (otherTrainerSpell->reqSkillValue != trainerSpell.reqSkillValue)
                    continue;

                if (otherTrainerSpell->reqLevel != trainerSpell.reqLevel)
                    continue;

#ifndef MANGOSBOT_TWO
                if (false /* othertrainerSpell not available */)
#else
                if (othertrainerSpell->spell[0] != trainerSpell.spell[0])
#endif
                    continue;

                if (0 /* conditionId not in vmangos TrainerSpell */ != 0 /* conditionId not in vmangos TrainerSpell */)
                    continue;

                sameTrainerSpell = otherTrainerSpell;
                break;
            }

            if (trainerType == TRAINER_TYPE_TRADESKILLS)
            {
                if (trainerSpell.reqSkill)
                    spellRequirement = trainerSpell.reqSkill;
                else
                {
                    // exist, already checked at loading
#ifdef MANGOSBOT_ZERO
                    SpellEntry const* spell = sSpellMgr.GetSpellEntry(trainerSpell.spell);
#else
                    SpellEntry const* spell = sSpellMgr.GetSpellEntry(trainerSpell.spell[0]);
#endif

                    spellRequirement = spell->EffectMiscValue[1];
                }
            }

            for (auto& trainer : trainers)
                (*spellMap)[trainerType][spellRequirement][sameTrainerSpell].push_back(trainer->entry);
        }
    }

    return spellMap;
}

std::vector<TrainerSpell const*> TrainableSpellsValue::Calculate()
{
    std::vector<TrainerSpell const*> trainableSpells;

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->GetClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->GetRace())
                continue;

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                uint32 reqLevel = 0;

                reqLevel = false ? trainerSpell->reqLevel : std::max(reqLevel, trainerSpell->reqLevel);
                TrainerSpellState state = bot->GetTrainerSpellState(trainerSpell);
                if (state != TRAINER_SPELL_GREEN)
                    continue;

                //Skip initial profession training.
#ifdef MANGOSBOT_ZERO
                if (bot->GetLevel() < 10 && sSpellMgr.IsProfessionSpell(trainerSpell->spell) && sSpellMgr.GetSpellRank(trainerSpell->spell) == 1)
#else
                if (bot->GetLevel() < 10 && sSpellMgr.IsProfessionSpell(trainerSpell->spell[0]) && sSpellMgr.GetSpellRank(trainerSpell->spell[0]) == 1)
#endif
                    continue;

                trainableSpells.push_back(trainerSpell);
            }
        }
    }   

    return trainableSpells;
}

std::string TrainableSpellsValue::Format()
{
    std::vector<std::string> vec;  
    for (auto t : value) {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(t->spell);
        if (!spell)
            continue;
        vec.push_back(chat->formatSpell(spell));
    } 
    
    return sPlayerbotHelpMgr.makeList(vec, "[<part>]");
}

std::vector<int32> AvailableTrainersValue::Calculate()
{
    std::vector<TrainerSpell const*> trainableSpells = AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier());;
    std::vector<int32> retTrainers;
    const bool needsWeaponSkillTraining = !GetMissingWeaponSkills(bot).empty();
    std::unordered_map<int32, float> trainerKnowledgeScores;

    int8 qualifierType = getQualifier().empty() ? -1 : stoi(getQualifier());

    trainableSpellMap* spellMap = GAI_VALUE(trainableSpellMap*, "trainable spell map");

    for (auto& [trainerType, spellReqList] : *spellMap)
    {
        if (trainerType >= 0 && trainerType != qualifierType)
            continue;

        for (auto& [requirement, trainerSpellList] : spellReqList)
        {
            if (trainerType == TRAINER_TYPE_CLASS && requirement != bot->GetClass())
                continue;
            if (trainerType == TRAINER_TYPE_MOUNTS && requirement != bot->GetRace())
                continue;

            if (trainerType == TRAINER_TYPE_CLASS && needsWeaponSkillTraining)
            {
                for (auto& [trainerSpell, trainers] : trainerSpellList)
                {
                    if (!TeachesAnyMissingWeaponSkill(trainerSpell, bot))
                        continue;

                    for (uint32 skillId : GetMissingWeaponSkills(bot))
                    {
                        if (!TeachesWeaponSkill(trainerSpell, skillId))
                            continue;

                        for (int32 trainer : trainers)
                            sServerSharedKnowledge.RecordTrainerSkill(trainer, bot->GetClass(), skillId, bot->GetMapId(), 0.05f);
                    }

                    for (auto& trainer : trainers)
                    {
                        trainerKnowledgeScores[trainer] = std::max(trainerKnowledgeScores[trainer], GetTrainerKnowledgeScore(trainer, bot));
                        if (std::find(retTrainers.begin(), retTrainers.end(), trainer) == retTrainers.end())
                            retTrainers.push_back(trainer);
                    }
                }
            }

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                if (std::find(trainableSpells.begin(), trainableSpells.end(), trainerSpell) == trainableSpells.end())
                    continue;

                for (auto& trainer : trainers)
                {
                    if(std::find(retTrainers.begin(), retTrainers.end(), trainer) == retTrainers.end())
                        retTrainers.push_back(trainer);
                }
            }
        }
    }

    if (needsWeaponSkillTraining && !retTrainers.empty())
    {
        std::stable_sort(retTrainers.begin(), retTrainers.end(), [&](int32 left, int32 right)
        {
            return trainerKnowledgeScores[left] > trainerKnowledgeScores[right];
        });
    }

    return retTrainers;
}

uint32 TrainCostValue::Calculate()
{
    uint32 TotalCost = 0;

    for (auto& spells : AI_VALUE2(std::vector<TrainerSpell const*>, "trainable spells", getQualifier()))
        TotalCost += spells->spellCost;
   
    return TotalCost;
}
