
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotFactory.h"
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
    uint32 GetKnowledgeCityId(Player* bot)
    {
        if (!bot)
            return 0;

        AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
        while (areaEntry && areaEntry->ZoneId)
        {
            AreaTableEntry const* parentArea = GetAreaEntryByAreaID(areaEntry->ZoneId);
            if (!parentArea || parentArea == areaEntry)
                break;

            areaEntry = parentArea;
        }

        return areaEntry ? areaEntry->Id : bot->GetZoneId();
    }

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

        SpellLearnSkillNode const* learnSkill = sSpellMgr.GetSpellLearnSkill(trainerSpell->spell);
        if (learnSkill && learnSkill->skill == skillId)
            return true;

        SpellLearnSpellMapBounds bounds = sSpellMgr.GetSpellLearnSpellMapBounds(trainerSpell->spell);
        for (SpellLearnSpellMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            SpellLearnSkillNode const* learnedSkill = sSpellMgr.GetSpellLearnSkill(itr->second.spell);
            if (learnedSkill && learnedSkill->skill == skillId)
                return true;
        }

        return false;
    }

    std::vector<uint32> GetTrainerSkillIds(TrainerSpell const* trainerSpell)
    {
        std::vector<uint32> teachIds;
        if (!trainerSpell)
            return teachIds;

        auto addTeach = [&](uint32 teachId)
        {
            if (!teachId)
                return;

            if (std::find(teachIds.begin(), teachIds.end(), teachId) == teachIds.end())
                teachIds.push_back(teachId);
        };

        addTeach(trainerSpell->spell);

        SpellLearnSkillNode const* learnSkill = sSpellMgr.GetSpellLearnSkill(trainerSpell->spell);
        if (learnSkill)
            addTeach(learnSkill->skill);

        SpellLearnSpellMapBounds bounds = sSpellMgr.GetSpellLearnSpellMapBounds(trainerSpell->spell);
        for (SpellLearnSpellMap::const_iterator itr = bounds.first; itr != bounds.second; ++itr)
        {
            addTeach(itr->second.spell);
            SpellLearnSkillNode const* learnedSkill = sSpellMgr.GetSpellLearnSkill(itr->second.spell);
            if (learnedSkill)
                addTeach(learnedSkill->skill);
        }

        if (trainerSpell->reqSkill)
            addTeach(trainerSpell->reqSkill);

        return teachIds;
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

    float GetTrainerKnowledgeScore(uint32 trainerEntry, uint32 trainerRequirement, std::vector<uint32> const& teachIds, Player* bot, bool useSkillKnowledge)
    {
        if (!bot || teachIds.empty())
            return 0.0f;

        const uint32 cityId = GetKnowledgeCityId(bot);
        if (useSkillKnowledge)
            return sServerSharedKnowledge.GetTrainerSkillConfidence(trainerEntry, trainerRequirement, teachIds, bot->GetMapId(), cityId);

        return sServerSharedKnowledge.GetTrainerTeachingConfidence(trainerEntry, trainerRequirement, teachIds, bot->GetMapId(), cityId);
    }

    float GetTrainerPreferenceScore(Player* bot, float knowledgeConfidence)
    {
        if (!bot || !bot->GetPlayerbotAI())
            return knowledgeConfidence;

        const ArchetypeWeights& weights = bot->GetPlayerbotAI()->GetArchetypeWeights();
        const float knowledgeMaturity = sServerSharedKnowledge.GetKnowledgeMaturity();
        const float routineBias = 0.75f + (0.25f * weights.routineTolerance);
        const float explorationBias = 0.60f + (0.40f * weights.explorationRadiusBias);
        const float knowledgeBonus = knowledgeConfidence * weights.knowledgeWeight * routineBias * (0.35f + (0.65f * knowledgeMaturity));

        if (knowledgeConfidence > 0.0f)
            return knowledgeBonus;

        const float curiosityBonus = 0.10f * weights.curiosityWeight * explorationBias * (1.0f - (0.70f * knowledgeMaturity));
        return curiosityBonus;
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
                if (othertrainerSpell->spell != trainerSpell.spell)
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
                    SpellEntry const* spell = sSpellMgr.GetSpellEntry(trainerSpell.spell);
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
                if (bot->GetLevel() < 10 && sSpellMgr.IsProfessionSpell(trainerSpell->spell) && sSpellMgr.GetSpellRank(trainerSpell->spell) == 1)
#endif
                {
                    // -------------------------------------------------------
                    // Opportunistic fishing exception:
                    //   • Always learn if the bot is level >= 40 and hasn't
                    //     picked up Fishing yet (long-term idle activity).
                    //   • 10 % random chance at any level — seeded by GUID
                    //     for stability within a session.
                    // Only the Fishing Apprentice spell (rank 1, skill 356)
                    // bypasses the skip.  All other rank-1 professions are
                    // still skipped normally.
                    // -------------------------------------------------------
                    SpellLearnSkillNode const* learnSkill =
                        sSpellMgr.GetSpellLearnSkill(trainerSpell->spell);
                    const bool isFishingSpell =
                        learnSkill && learnSkill->skill == SKILL_FISHING;

                    if (isFishingSpell && !bot->HasSkill(SKILL_FISHING))
                    {
                        const bool alwaysLearn   = (bot->GetLevel() >= 40);
                        // Stable 10 % roll: use bot GUID + current hour so it
                        // re-rolls occasionally but doesn't flip every tick.
                        const uint32 seed = bot->GetGUIDLow() ^
                            static_cast<uint32>(time(nullptr) / 3600);
                        const bool chanceLearn   = ((seed % 10) == 0);

                        if (!alwaysLearn && !chanceLearn)
                            continue;   // still skip — roll didn't fire
                        // else: fall through and add to trainableSpells
                    }
                    else
                    {
                        continue;   // normal skip for non-fishing professions
                    }
                }

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
    const uint32 cityId = GetKnowledgeCityId(bot);

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
                            sServerSharedKnowledge.RecordTrainerSkill(trainer, bot->GetClass(), skillId, bot->GetMapId(), cityId, 0.05f);
                    }

                    for (auto& trainer : trainers)
                    {
                        const float knowledgeScore = GetTrainerKnowledgeScore(trainer, bot->GetClass(), GetMissingWeaponSkills(bot), bot, true);
                        trainerKnowledgeScores[trainer] = std::max(trainerKnowledgeScores[trainer], GetTrainerPreferenceScore(bot, knowledgeScore));
                        if (std::find(retTrainers.begin(), retTrainers.end(), trainer) == retTrainers.end())
                            retTrainers.push_back(trainer);
                    }
                }
            }

            for (auto& [trainerSpell, trainers] : trainerSpellList)
            {
                if (std::find(trainableSpells.begin(), trainableSpells.end(), trainerSpell) == trainableSpells.end())
                    continue;

                std::vector<uint32> taughtSkills = GetTrainerSkillIds(trainerSpell);
                for (uint32 teachId : taughtSkills)
                {
                    for (int32 trainer : trainers)
                        sServerSharedKnowledge.RecordTrainerTeaching(trainer, requirement, teachId, bot->GetMapId(), cityId, 0.02f);
                }

                for (auto& trainer : trainers)
                {
                    if (!taughtSkills.empty())
                    {
                        const float knowledgeScore = GetTrainerKnowledgeScore(trainer, requirement, taughtSkills, bot, false);
                        trainerKnowledgeScores[trainer] = std::max(trainerKnowledgeScores[trainer], GetTrainerPreferenceScore(bot, knowledgeScore));
                    }
                    else
                        trainerKnowledgeScores[trainer] = std::max(trainerKnowledgeScores[trainer], GetTrainerPreferenceScore(bot, 0.0f));

                    if(std::find(retTrainers.begin(), retTrainers.end(), trainer) == retTrainers.end())
                        retTrainers.push_back(trainer);
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Profession rank-up scoring
    // When the bot has a profession skill capped at its current tier
    // (e.g. Mining at 75 ready for Journeyman), boost any tradeskill trainer
    // that teaches the next rank of that profession so it rises to the top.
    // -----------------------------------------------------------------------
    if (PlayerbotFactory::NeedsProfessionRankUp(bot))
    {
        trainableSpellMap* spellMap2 = GAI_VALUE(trainableSpellMap*, "trainable spell map");
        for (auto& [trainerType2, spellReqList2] : *spellMap2)
        {
            if (trainerType2 != TRAINER_TYPE_TRADESKILLS)
                continue;

            for (auto& [requirement2, trainerSpellList2] : spellReqList2)
            {
                for (auto& [trainerSpell2, trainers2] : trainerSpellList2)
                {
                    // Only rank-up spells (rank >= 2: Journeyman, Expert, Artisan)
                    if (sSpellMgr.GetSpellRank(trainerSpell2->spell) < 2)
                        continue;

                    TrainerSpellState state2 = bot->GetTrainerSpellState(trainerSpell2);
                    if (state2 != TRAINER_SPELL_GREEN)
                        continue;

                    for (int32 trainer2 : trainers2)
                    {
                        // Boost: rank-up trainers score 0.90 (very high —
                        // comparable to a known weapon-skill trainer).
                        trainerKnowledgeScores[trainer2] =
                            std::max(trainerKnowledgeScores[trainer2], 0.90f);

                        if (std::find(retTrainers.begin(), retTrainers.end(), trainer2)
                                == retTrainers.end())
                            retTrainers.push_back(trainer2);
                    }
                }
            }
        }
    }

    if (!trainerKnowledgeScores.empty() && !retTrainers.empty())
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
