
#include "playerbot/playerbot.h"
#include "MoveToTravelTargetAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/LootObjectStack.h"
#include "PathFinder.h"
#include "playerbot/TravelMgr.h"
#include <iomanip>
#include <vector>

using namespace ai;

namespace
{
    std::string BlockedRemoteTurnInSuffix(QuestRelationTravelDestination* destination)
    {
        if (!destination)
            return {};

        return std::to_string(destination->GetQuestId()) + ":" + std::to_string(destination->GetEntry());
    }

    bool IsRemoteQuestTurnInTarget(Player* bot, TravelTarget* target)
    {
        if (!bot || !target || !target->GetDestination() || !target->GetPosition())
            return false;

        QuestRelationTravelDestination* questDestination = dynamic_cast<QuestRelationTravelDestination*>(target->GetDestination());
        if (!questDestination || questDestination->GetPurpose() != TravelDestinationPurpose::QuestTaker)
            return false;

        WorldPosition const* position = target->GetPosition();
        if (position->getMapId() != bot->GetMapId())
            return true;

        return position->distance(bot) > 300.0f;
    }

    void RecordBlockedRemoteTurnIn(PlayerbotAI* ai, TravelTarget* target)
    {
        if (!ai || !target || !IsRemoteQuestTurnInTarget(ai->GetBot(), target))
            return;

        QuestRelationTravelDestination* questDestination = dynamic_cast<QuestRelationTravelDestination*>(target->GetDestination());
        if (!questDestination)
            return;

        AiObjectContext* context = ai->GetAiObjectContext();
        if (!context)
            return;

        const std::string suffix = BlockedRemoteTurnInSuffix(questDestination);
        const int retryCount = static_cast<int>(std::max<uint32>(1u, target->GetRetryCount(true) / 2));
        const time_t blockedUntil = time(nullptr) + 300;

        context->GetValue<int>("manual int", "blocked remote turnin count::" + suffix)->Set(retryCount);
        context->GetValue<time_t>("manual time", "blocked remote turnin until::" + suffix)->Set(blockedUntil);

        ai->TellDebug(ai->GetMaster(),
            "[PBTRACE] move_to_travel blocked_remote_turnin quest=" + std::to_string(questDestination->GetQuestId()) +
            " entry=" + std::to_string(questDestination->GetEntry()) +
            " retries=" + std::to_string(retryCount) +
            " until=" + std::to_string(static_cast<uint32>(blockedUntil)),
            "debug travel");
    }
}

bool MoveToTravelTargetAction::Execute(Event& event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    if (target->GetStatus() == TravelStatus::TRAVEL_STATUS_READY)
    {
        ai->TellDebug(ai->GetMaster(), "The target is ready to travel start now.", "debug travel");
        target->SetStatus(TravelStatus::TRAVEL_STATUS_TRAVEL);
    }

    target->CheckStatus();

    if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_TRAVEL)
        return true;

    WorldPosition botLocation(bot);
    WorldPosition location = *target->GetPosition();

    if (QuestObjectiveTravelDestination* objectiveDestination = dynamic_cast<QuestObjectiveTravelDestination*>(target->GetDestination()))
    {
        const int32 objectiveEntry = objectiveDestination->GetEntry();
        const float localRoamRange = 35.0f;

        if (objectiveEntry > 0 && location.distance(bot) <= localRoamRange)
        {
            std::list<ObjectGuid> possibleTargets = AI_VALUE(std::list<ObjectGuid>, "possible targets");
            for (auto& possibleTarget : possibleTargets)
            {
                if (possibleTarget.GetEntry() != objectiveEntry || !possibleTarget.IsCreature())
                    continue;

                Creature* creature = ai->GetCreature(possibleTarget);
                if (!creature || !creature->IsAlive())
                    continue;

                if (sServerFacade.GetDistance2d(bot, creature) > localRoamRange)
                    continue;

                ai->TellDebug(ai->GetMaster(),
                    "[PBTRACE] move_to_travel local objective roam dest=\"" + target->GetDestination()->GetTitle() +
                    "\" local_target=" + std::to_string(objectiveEntry),
                    "debug travel");
                target->SetStatus(TravelStatus::TRAVEL_STATUS_WORK);
                return true;
            }
        }
        else if (objectiveEntry < 0 && location.distance(bot) <= INTERACTION_DISTANCE * 4.0f)
        {
            std::list<ObjectGuid> possibleObjects = bot->GetMap()->IsDungeon() ?
                AI_VALUE(std::list<ObjectGuid>, "nearest game objects") :
                AI_VALUE(std::list<ObjectGuid>, "nearest game objects no los");

            for (auto& possibleObject : possibleObjects)
            {
                if (possibleObject.GetEntry() != (-1 * objectiveEntry) || !possibleObject.IsGameObject())
                    continue;

                GameObject* gameObject = ai->GetGameObject(possibleObject);
                if (!gameObject || !gameObject->isSpawned())
                    continue;

                if (sServerFacade.GetDistance2d(bot, gameObject) > INTERACTION_DISTANCE * 4.0f)
                    continue;

                ai->TellDebug(ai->GetMaster(),
                    "[PBTRACE] move_to_travel local objective object dest=\"" + target->GetDestination()->GetTitle() +
                    "\" local_target=" + std::to_string(-objectiveEntry),
                    "debug travel");
                target->SetStatus(TravelStatus::TRAVEL_STATUS_WORK);
                return true;
            }
        }
    }
    
    Group* group = bot->GetGroup();
    if (ai->IsGroupLeader() && !urand(0, 1) && !bot->IsInCombat())
    {        
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->getSource();
            if (member == bot)
                continue;

            if (!member->IsAlive())
                continue;

            if (!member->IsMoving())
                continue;

            if (member->GetPlayerbotAI() &&
                !(member->GetPlayerbotAI()->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || member->GetPlayerbotAI()->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT)))
                continue;

            WorldPosition memberPos(member);
            WorldPosition targetPos = *target->GetPosition();

            float memberDistance = std::min(botLocation.distance(memberPos), location.distance(memberPos));

            if (memberDistance < 50.0f)
                continue;
            if (memberDistance > sPlayerbotAIConfig.reactDistance * 20)
                continue;

           // float memberAngle = botLocation.getAngleBetween(targetPos, memberPos);

           // if (botLocation.getMapId() == targetPos.getMapId() && botLocation.getMapId() == memberPos.getMapId() && memberAngle < M_PI_F / 2) //We are heading that direction anyway.
           //     continue;

            if (!urand(0, 5))
            {
                std::ostringstream out;
                if ((ai->GetMaster() && !bot->GetGroup()->IsMember(ai->GetMaster()->GetObjectGuid())) || !ai->HasActivePlayerMaster())
                    out << "Waiting a bit for ";
                else
                    out << "Please hurry up ";

                out << member->GetName();

                if (bot->GetPlayerbotAI() && !ai->HasActivePlayerMaster())
                {
                    out << " who is " << round(memberDistance) << "y away";
                    if (!memberPos.getAreaName().empty())
                        out << " in " << memberPos.getAreaName();
                }

                ai->TellPlayerNoFacing(GetMaster(), out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
            }

            // Introduce a random delay between 80% and 120% of maxWaitForMove to make waiting more natural
            uint32 randomDelay = sPlayerbotAIConfig.maxWaitForMove * (urand(80, 120) / 100.0f);
            target->SetExpireIn(target->GetTimeLeft() + randomDelay);

            SetDuration(randomDelay);

            // Occasionally face the member and perform an emote
            if (urand(0, 3) == 0) { // 25% chance to emote
                bot->SetFacingToObject(member);
                uint32 emoteChoice = urand(0, 2);
                switch (emoteChoice) {
                    case 0:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                        break;
                    case 1:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
                        break;
                    case 2:
                        bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
                        break;
                }
            }

            return true;
        }
    }

    float maxDistance = target->GetDestination()->GetRadiusMin();

    WorldPosition movePosition = location;
    if (maxDistance > 0.0f)
    {
        WorldPosition reachablePosition = location;
        if (reachablePosition.GetReachableRandomPointOnGround(bot, maxDistance, urand(0, 1)))
            movePosition = reachablePosition;
    }

    bool usedPathStep = false;
    if (location.getMapId() == bot->GetMapId())
    {
        const bool botInWater = botLocation.isInWater() || botLocation.isUnderWater();
        const bool targetInWater = location.isInWater() || location.isUnderWater();
        const bool waterTransition = (botInWater != targetInWater) || (botLocation.isUnderWater() != location.isUnderWater());

        std::vector<WorldPosition> path = location.getPathStepFrom(botLocation, bot, !waterTransition);
        if (!path.empty())
        {
            const float maxStepDistance = std::min(60.0f, sPlayerbotAIConfig.sightDistance);
            WorldPosition selectedPathPoint = path.back();

            for (const WorldPosition& pathPoint : path)
            {
                const float stepDistance = botLocation.distance(pathPoint);
                if (stepDistance < sPlayerbotAIConfig.targetPosRecalcDistance)
                    continue;

                selectedPathPoint = pathPoint;
                if (stepDistance >= maxStepDistance)
                    break;
            }

            movePosition = selectedPathPoint;
            usedPathStep = true;
        }
        else if (!waterTransition && location.distance(bot) > 80.0f)
        {
            ai->TellDebug(ai->GetMaster(),
                "[PBTRACE] move_to_travel no normal path dest=\"" + target->GetDestination()->GetTitle() +
                "\" dist=" + std::to_string(static_cast<uint32>(location.distance(bot))) +
                " pos_str=" + target->GetPosStr(),
                "debug travel");
            target->IncRetry(true);

            if (target->IsMaxRetry(true))
            {
                RecordBlockedRemoteTurnIn(ai, target);
                ai->TellDebug(ai->GetMaster(), "The target is cooling down because we failed to find a normal path to it a few times in a row.", "debug travel");
                target->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
                target->SetForced(false);
            }

            return false;
        }
    }

    if (movePosition.getMapId() == bot->GetMapId())
        movePosition.ClosestCorrectPoint(5.0f, 50.0f, bot->GetInstanceId());

    float x = movePosition.getX();
    float y = movePosition.getY();
    float z = movePosition.getZ();
    float mapId = movePosition.getMapId();

    bool canMove = false;

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        std::ostringstream out;

        out << "Moving to ";

        out << target->GetDestination()->GetTitle();

        if (!(*target->GetPosition() == WorldPosition()))
        {
            out << " at " << uint32(target->GetPosition()->distance(bot)) << "y";
        }

        if (target->GetStatus() != TravelStatus::TRAVEL_STATUS_EXPIRED)
            out << " for " << (target->GetTimeLeft() / 1000) << "s";

        if (target->GetRetryCount(true))
            out << " (move retry: " << target->GetRetryCount(true) << ")";
        else if (target->GetRetryCount(false))
            out << " (retry: " << target->GetRetryCount(false) << ")";

        ai->TellPlayerNoFacing(GetMaster(), out);
    }

    ai->TellDebug(ai->GetMaster(),
        "[PBTRACE] move_to_travel attempt dest=\"" + target->GetDestination()->GetTitle() +
        "\" map=" + std::to_string(static_cast<uint32>(mapId)) +
        " point={" + std::to_string(static_cast<int32>(x)) + "," + std::to_string(static_cast<int32>(y)) + "," + std::to_string(static_cast<int32>(z)) + "}" +
        " path_step=" + std::string(usedPathStep ? "yes" : "no") +
        " chosen_dist=" + std::to_string(static_cast<uint32>(movePosition.distance(location))) +
        " dist=" + std::to_string(static_cast<uint32>(location.distance(bot))) +
        " retries=" + std::to_string(static_cast<uint32>(target->GetRetryCount(true))),
        "debug travel");

    canMove = MoveTo(mapId, x, y, z, false, false);

    if (!canMove)
    {
        ai->TellDebug(ai->GetMaster(),
            "[PBTRACE] move_to_travel failed dest=\"" + target->GetDestination()->GetTitle() +
            "\" dist=" + std::to_string(static_cast<uint32>(location.distance(bot))) +
            " pos_str=" + target->GetPosStr(),
            "debug travel");
        target->IncRetry(true);

        if (target->IsMaxRetry(true))
        {
            ai->TellDebug(ai->GetMaster(), "The target is cooling down because we failed to move to it a few times in a row.", "debug travel");
            target->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);      
            target->SetForced(false);
        }
    }
    else
        target->DecRetry(true);

    if (ai->HasStrategy("debug move", BotState::BOT_STATE_NON_COMBAT))
    {
        WorldPosition* pos = target->GetPosition();
        GuidPosition* guidP = dynamic_cast<GuidPosition*>(pos);

        std::string name = (guidP && guidP->GetWorldObject(bot->GetInstanceId())) ? chat->formatWorldobject(guidP->GetWorldObject(bot->GetInstanceId())) : "travel target";

        if (mapId == bot->GetMapId())
        {
            ai->Poi(x, y, name);
        }
        else
        {
            LastMovement& lastMove = *context->GetValue<LastMovement&>("last movement");
            if (!lastMove.lastPath.empty() && lastMove.lastPath.getBack().distance(location) < 20.0f)
            {
                for (auto& p : lastMove.lastPath.getPointPath())
                {
                    if (p.getMapId() == bot->GetMapId())
                        ai->Poi(p.getX(), p.getY(), name);
                }
            }
        }
    }
     
    return canMove;
}

bool MoveToTravelTargetAction::isUseful()
{
    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (bot->IsTaxiFlying())
        return false;

    if (MEM_AI_VALUE(WorldPosition, "current position")->LastChangeDelay() < 10)
#ifndef MANGOSBOT_ZERO
        if (bot->IsMovingIgnoreFlying())
            return false;
#else
        if (bot->IsMoving())
            return false;
#endif

    if (!AI_VALUE(bool, "can move around"))
        return false;

    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");
    TravelStatus travelStatus = travelTarget->GetStatus();

    if (travelStatus != TravelStatus::TRAVEL_STATUS_READY && travelStatus != TravelStatus::TRAVEL_STATUS_TRAVEL)
        return false;

    if (!AI_VALUE(bool, "travel target traveling"))
        return false;

    if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT))
    {
        auto conditions = travelTarget->GetConditions();
        for (auto& cond : conditions)
        {
            if (cond == "should travel named::guild order")
                return false;
        }
    }

    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetObjectGuid()))
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT))
            if (!travelTarget->IsForced())
                return false;

    WorldPosition travelPos(*travelTarget->GetPosition());

    if (travelPos.isDungeon() && bot->GetGroup() && bot->GetGroup()->IsLeader(bot->GetObjectGuid()) && sTravelMgr.MapTransDistance(bot, travelPos, true) < sPlayerbotAIConfig.sightDistance && !AI_VALUE2(bool, "group and", "near leader"))
        return false;
     
    if (AI_VALUE(bool, "has available loot"))
    {
        LootObject lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);
        if (lootObject.IsLootPossible(bot))
            return false;
    }

    if (!travelTarget->IsForced())
        if (!AI_VALUE2(bool, "can free move to", travelTarget->GetPosStr()))
        {
            ai->TellDebug(ai->GetMaster(),
                "[PBTRACE] move_to_travel blocked by can free move to pos_str=" + travelTarget->GetPosStr(),
                "debug travel");
            return false;
        }

    return true;
}
