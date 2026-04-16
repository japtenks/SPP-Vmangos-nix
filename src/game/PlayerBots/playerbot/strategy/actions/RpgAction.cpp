#include <random>

#include "playerbot/playerbot.h"
#include "RpgAction.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/strategy/values/Formations.h"
#include "EmoteAction.h"
#include "RpgSubActions.h"
#include "playerbot/TravelMgr.h"

using namespace ai;

namespace
{
    bool IsQuestTravelWorkTarget(TravelTarget* travelTarget, GuidPosition const& rpgTarget)
    {
        if (!travelTarget || travelTarget->GetStatus() != TravelStatus::TRAVEL_STATUS_WORK || !travelTarget->GetDestination() || !rpgTarget)
            return false;

        QuestRelationTravelDestination* questDestination = dynamic_cast<QuestRelationTravelDestination*>(travelTarget->GetDestination());
        if (!questDestination)
            return false;

        const int32 rpgEntry = rpgTarget.IsCreature() ? static_cast<int32>(rpgTarget.GetEntry()) : -1 * static_cast<int32>(rpgTarget.GetEntry());
        return rpgEntry == travelTarget->GetEntry();
    }

    uint8 GetPreferredRpgServicePriority(const std::string& actionName)
    {
        if (actionName == "rpg end quest") return 1;
        if (actionName == "rpg start quest") return 2;
        if (actionName == "rpg train") return 3;
        if (actionName == "rpg repair") return 4;
        if (actionName == "rpg sell") return 5;
        if (actionName == "rpg buy") return 6;
        if (actionName == "rpg get mail") return 7;
        if (actionName == "rpg ah sell") return 8;
        if (actionName == "rpg ah buy") return 9;
        if (actionName == "rpg buy petition") return 10;
        return 0;
    }
}

bool RpgAction::Execute(Event& event)
{    
    GuidPosition guidP = AI_VALUE(GuidPosition, "rpg target");

    if (!guidP && ai->GetMaster())
    {
        guidP = GuidPosition(ai->GetMaster()->GetSelectionGuid(), ai->GetMaster());
        if (guidP)
        {
            RemIgnore(guidP);
            SET_AI_VALUE(GuidPosition,"rpg target",guidP);
        }
    }

    if (bot->GetShapeshiftForm() > 0)
        bot->SetShapeshiftForm(FORM_NONE);

    if(!SetNextRpgAction())
        RESET_AI_VALUE(GuidPosition, "rpg target");

    return true;
}

bool RpgAction::isUseful()
{
    return (AI_VALUE(std::string, "next rpg action").empty() || AI_VALUE(std::string, "next rpg action") == "rpg") && AI_VALUE(GuidPosition, "rpg target");
}

bool RpgAction::SetNextRpgAction()
{
    Strategy* rpgStrategy; 
    std::vector<Action*> actions;
    std::vector<uint32> relevances;
    std::list<TriggerNode*> triggerNodes;

    std::set<std::string> strategies;
    ai->GetAiObjectContext()->GetSupportedStrategies(strategies);

    for (const auto& strategy : strategies)
    {
        if (strategy.find("rpg") == std::string::npos)
            continue;

        rpgStrategy = ai->GetAiObjectContext()->GetStrategy(strategy);

        rpgStrategy->InitTriggers(triggerNodes, BotState::BOT_STATE_NON_COMBAT);

        for (auto& triggerNode : triggerNodes)
        {
            Trigger* trigger = context->GetTrigger(triggerNode->getName());

            if (trigger)
            {

                triggerNode->setTrigger(trigger);

                NextAction** nextActions = triggerNode->getHandlers();

                Trigger* trigger = triggerNode->getTrigger();

                bool isChecked = false;

                for (int32 i = 0; i < NextAction::size(nextActions); i++)
                {
                    NextAction* nextAction = nextActions[i];

                    if (nextAction->getRelevance() > 2.0f)
                        continue;

                    if (!isChecked && !trigger->IsActive())
                        break;

                    isChecked = true;

                    Action* action = ai->GetAiObjectContext()->GetAction(nextAction->getName());

                    if (!dynamic_cast<RpgEnabled*>(action) || !action->isPossible() || !action->isUseful())
                        continue;

                    actions.push_back(action);
                    relevances.push_back((nextAction->getRelevance() - 1) * 1000);
                }
                NextAction::destroy(nextActions);
            }
        }

        for (std::list<TriggerNode*>::iterator i = triggerNodes.begin(); i != triggerNodes.end(); i++)
        {
            TriggerNode* trigger = *i;
            delete trigger;
        }
        triggerNodes.clear();
    }

    if (actions.empty())
        return false;

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    GuidPosition rpgTarget = AI_VALUE(GuidPosition, "rpg target");
    const bool inRangeServiceTarget = rpgTarget && AI_VALUE2(float, "distance", "rpg target") <= INTERACTION_DISTANCE * 1.5f;
    const bool questWorkTarget = IsQuestTravelWorkTarget(travelTarget, rpgTarget);

    if (inRangeServiceTarget)
    {
        Action* preferredAction = nullptr;
        for (Action* action : actions)
        {
            const uint8 priority = GetPreferredRpgServicePriority(action->getName());
            if (!priority)
                continue;

            if (!preferredAction || priority < GetPreferredRpgServicePriority(preferredAction->getName()))
                preferredAction = action;
        }

        if (!preferredAction && questWorkTarget)
        {
            for (Action* action : actions)
            {
                if (action->getName() == "rpg end quest")
                {
                    preferredAction = action;
                    break;
                }

                if (!preferredAction && action->getName() == "rpg start quest")
                    preferredAction = action;
            }
        }

        if (preferredAction)
        {
            if ((ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT)) &&
                rpgTarget.GetWorldObject(bot->GetInstanceId()))
            {
                std::ostringstream out;
                out << "do: ";
                out << chat->formatWorldobject(rpgTarget.GetWorldObject(bot->GetInstanceId()));
                out << " " << preferredAction->getName() << " [service fast path]";
                ai->TellPlayerNoFacing(GetMaster(), out);
            }

            SET_AI_VALUE(std::string, "next rpg action", preferredAction->getName());
            return true;
        }
    }

    if (ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT))
    {
        std::vector<std::pair<Action*, uint32>> sortedActions;
        
        for (int i = 0; i < actions.size(); i++)
            sortedActions.push_back(std::make_pair(actions[i], relevances[i]));

        std::sort(sortedActions.begin(), sortedActions.end(), [](std::pair<Action*, uint32>i, std::pair<Action*, uint32> j) {return i.second > j.second; });

        ai->TellPlayerNoFacing(GetMaster(), "------" + chat->formatWorldobject(AI_VALUE(GuidPosition, "rpg target").GetWorldObject(bot->GetInstanceId())) + "------");

        for (auto action : sortedActions)
        {
            std::ostringstream out;

            out << " " << action.first->getName() << " " << action.second;

            ai->TellPlayerNoFacing(GetMaster(), out);
        }
    }

    std::mt19937 gen(time(0));

    WeightedShuffle(actions.begin(), actions.end(), relevances.begin(), relevances.end(), gen);

    Action* action = actions.front();

    if ((ai->HasStrategy("debug", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("debug rpg", BotState::BOT_STATE_NON_COMBAT)))
    {
        std::ostringstream out;
        out << "do: ";
        out << chat->formatWorldobject(AI_VALUE(GuidPosition, "rpg target").GetWorldObject(bot->GetInstanceId()));

        out << " " << action->getName();

        ai->TellPlayerNoFacing(GetMaster(), out);
    }

    SET_AI_VALUE(std::string, "next rpg action", action->getName());

    return true;
}

bool RpgAction::AddIgnore(ObjectGuid guid)
{
    if (HasIgnore(guid))
        return false;

    std::set<ObjectGuid>& ignoreList = context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Get();

    ignoreList.insert(guid);

    if (ignoreList.size() > 50)
        ignoreList.erase(ignoreList.begin());

    context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Set(ignoreList);

    return true;
}

bool RpgAction::RemIgnore(ObjectGuid guid)
{
    if (!HasIgnore(guid))
        return false;

    std::set<ObjectGuid>& ignoreList = context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Get();

    ignoreList.erase(ignoreList.find(guid));

    context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Set(ignoreList);

    return true;
}

bool RpgAction::HasIgnore(ObjectGuid guid)
{
    std::set<ObjectGuid>& ignoreList = context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Get();
    if (ignoreList.empty())
        return false;

    if (ignoreList.find(guid) == ignoreList.end())
        return false;

    return true;
}
