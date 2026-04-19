
#include "playerbot/playerbot.h"
#include "AddLootAction.h"

#include "playerbot/LootObjectStack.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"

#include "playerbot/strategy/actions/DestroyItemAction.h"

using namespace ai;
using namespace MaNGOS;

using namespace ai;

namespace
{
    static std::string SanitizeEngineLogField(std::string text)
    {
        std::replace(text.begin(), text.end(), ',', ';');
        return text;
    }

    void TraceLootReason(PlayerbotAI* ai, Player* requester, std::string const& action, std::string const& reason, WorldObject* wo = nullptr)
    {
        if (!ai || !ai->GetBot())
            return;

        // Keep loot failure instrumentation in the engine debug log so we can
        // analyze scheduler behavior without turning every trace into player chat.
        std::ostringstream out;
        out << "[PBTRACE] " << action << " " << reason;

        if (wo)
            out << " obj=" << ChatHelper::formatWorldobject(wo);

        if (ai->HasStrategy("debug loot", BotState::BOT_STATE_NON_COMBAT))
            ai->TellDebug(requester ? requester : ai->GetMaster(), out.str(), "debug loot");

        if (!sPlayerbotAIConfig.engineDebugLog || sPlayerbotAIConfig.engineDebugLogFile.empty())
            return;

        std::ostringstream engineOut;
        engineOut << sPlayerbotAIConfig.GetTimestampStr()
                  << ",bot=" << ai->GetBot()->GetName()
                  << ",action=" << action
                  << ",rel=-1.000"
                  << ",stage=TRACE"
                  << ",reason=" << SanitizeEngineLogField(reason);

        if (wo)
            engineOut << ",obj=" << SanitizeEngineLogField(ChatHelper::formatWorldobject(wo));

        sPlayerbotAIConfig.log(sPlayerbotAIConfig.engineDebugLogFile, "%s", engineOut.str().c_str());
    }
}

bool AddLootAction::Execute(Event& event)
{
    ObjectGuid guid = event.getObject();
    if (!guid)
        return false;

    return AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
}

bool AddAllLootAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    bool added = false;

    std::string text = event.getParam();

    if (!text.empty())
    {
        std::list<ObjectGuid> objects = ChatHelper::parseGameobjects(text);
        if (objects.empty())
            TraceLootReason(ai, requester, getName(), "no parsed objects");

        for (auto& guid : objects)
            added |= AddLoot(requester, guid);
    }
    else
    {
        std::list<ObjectGuid> gos = context->GetValue<std::list<ObjectGuid>>("nearest game objects no los")->Get();
        for (std::list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
            added |= AddLoot(requester, *i);

        std::list<ObjectGuid> corpses = context->GetValue<std::list<ObjectGuid>>("nearest corpses")->Get();
        for (std::list<ObjectGuid>::iterator i = corpses.begin(); i != corpses.end(); i++)
            added |= AddLoot(requester, *i);

        if (gos.empty() && corpses.empty())
            TraceLootReason(ai, requester, getName(), "no nearby loot candidates");
    }

    return added;
}

bool AddLootAction::isUseful()
{
    return true;
}

bool AddAllLootAction::isUseful()
{
    return true;
}

bool AddAllLootAction::AddLoot(Player* requester, ObjectGuid guid)
{
    LootObject loot(bot, guid);

    if (ai->HasStrategy("debug loot", BotState::BOT_STATE_NON_COMBAT))
        loot.Refresh(bot, guid, true);

    WorldObject* wo = loot.GetWorldObject(bot);

    if (!wo)
    {
        wo = ai->GetWorldObject(guid);

        if (!wo)
            ai->TellDebug(requester, "Trying to add loot from " + std::to_string(guid) + " but it doesn't exists.", "debug loot");
        else
            ai->TellDebug(requester, "for trying to add loot from " + ChatHelper::formatWorldobject(wo), "debug loot");

        TraceLootReason(ai, requester, getName(), "missing world object", wo);
        return false;
    }
    else
    {
        ai->TellDebug(requester, "Add loot from " + ChatHelper::formatWorldobject(wo), "debug loot");
    }

    if (loot.IsEmpty())
    {
        ai->TellDebug(requester, "Loot object is empty.", "debug loot");
        TraceLootReason(ai, requester, getName(), "empty loot", wo);
        return false;
    }

    if (abs(wo->GetPositionZ() - bot->GetPositionZ()) > INTERACTION_DISTANCE)
    {
        ai->TellDebug(requester, "Object too high or low.", "debug loot");
        TraceLootReason(ai, requester, getName(), "z distance too large", wo);
        return false;
    }

    if (!loot.IsLootPossible(bot))
    {
        ai->TellDebug(requester, "Looting is not possible.", "debug loot");
        TraceLootReason(ai, requester, getName(), "loot not possible", wo);
        return false;
    }

    float lootDistanceToUse = sPlayerbotAIConfig.lootDistance;

    Group* group = bot->GetGroup();

    bool isInGroup = group ? true : false;
    bool isInDungeon = bot->GetMap()->IsDungeon();

    if (isInGroup)
    {
        //if is not master looter (and loot is set to MASTER_LOOT)
        //NOTE: They are !unable to loot quests items! too if so
        if (isInDungeon
            && group->GetLootMethod() == LootMethod::MASTER_LOOT
            && group->GetLooterGuid()
            && group->GetLooterGuid() != bot->GetObjectGuid())
        {
            ai->TellDebug(requester, "Not master looter.", "debug loot");
            TraceLootReason(ai, requester, getName(), "blocked by master looter", wo);
            return false;
        }

        if (ai->IsGroupLeader())
        {
            lootDistanceToUse = sPlayerbotAIConfig.lootDistance;
        }
        else
        {
            if (ai->HasActivePlayerMaster())
            {
                lootDistanceToUse = sPlayerbotAIConfig.groupMemberLootDistanceWithActiveMaster;
            }
            else
            {
                lootDistanceToUse = sPlayerbotAIConfig.groupMemberLootDistance;
            }
        }
    }
    else
    {
        lootDistanceToUse = sPlayerbotAIConfig.lootDistance;
    }

    if (sServerFacade.GetDistance2d(requester, wo) > lootDistanceToUse)
    {
        ai->TellDebug(requester, "Outside of loot range: " + std::to_string(lootDistanceToUse), "debug loot");
        TraceLootReason(ai, requester, getName(), "outside loot range", wo);
        return false;
    }

    //check hostile units after distance checks, to avoid unnecessary calculations

    if (isInGroup && !ai->IsGroupLeader())
    {
        float MOB_AGGRO_DISTANCE = 30.0f;
        std::list<Unit*> hostiles = ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, MOB_AGGRO_DISTANCE);

        if (hostiles.size() > 0)
        {
            std::ostringstream out;
            out << hostiles.front()->GetName() << " is blocking " << wo->GetName() << ", need to kill it or I will not loot";
            ai->TellError(requester, out.str());
            TraceLootReason(ai, requester, getName(), "blocked by nearby hostile", wo);
            return false;
        }
    }

    uint8 usedBagSpacePercent = AI_VALUE(uint8, "bag space");

    if (usedBagSpacePercent > 99 && !ai->CanLootSomethingFromWO(wo))
    {
        if (ai->HasQuestItemsInWOLootList(wo))
        {
            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("destroy all gray"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("smart destroy item"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99)
            {
                ai->TellPlayer(requester, "Can not loot quest item, my bags are full", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                TraceLootReason(ai, requester, getName(), "bags full with quest loot pending", wo);
                return false;
            }

        }

        if (usedBagSpacePercent > 99)
        {
            ai->TellError(requester, "There is some loot but I do not have free bag space, so not looting");
            TraceLootReason(ai, requester, getName(), "bags full", wo);
            return false;
        }
    }

    return AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
}

bool AddGatheringLootAction::AddLoot(Player* requester, ObjectGuid guid)
{
    LootObject loot(bot, guid);

    WorldObject *wo = loot.GetWorldObject(bot);
    if (!wo)
    {
        TraceLootReason(ai, requester, getName(), "missing world object");
        return false;
    }

    if (loot.IsEmpty())
    {
        TraceLootReason(ai, requester, getName(), "empty loot", wo);
        return false;
    }

    if (!sServerFacade.IsWithinLOSInMap(bot, wo))
    {
        TraceLootReason(ai, requester, getName(), "no line of sight", wo);
        return false;
    }

    if (loot.skillId == SKILL_NONE)
    {
        TraceLootReason(ai, requester, getName(), "not a gathering node", wo);
        return false;
    }

    if (!loot.IsLootPossible(bot))
    {
        TraceLootReason(ai, requester, getName(), "loot not possible", wo);
        return false;
    }

    float gatheringDistanceToUse = sPlayerbotAIConfig.gatheringDistance;

    Group* group = bot->GetGroup();

    bool isInGroup = group ? true : false;
    bool isInDungeon = bot->GetMap()->IsDungeon();

    if (isInGroup && !ai->IsGroupLeader())
    {
        if (ai->HasActivePlayerMaster())
        {
            gatheringDistanceToUse = sPlayerbotAIConfig.groupMemberGatheringDistanceWithActiveMaster;
        }
        else
        {
            gatheringDistanceToUse = sPlayerbotAIConfig.groupMemberGatheringDistance;
        }
    }
    else if (ai->IsGroupLeader())
    {
        gatheringDistanceToUse = sPlayerbotAIConfig.gatheringDistance;
    }
    else
    {
        gatheringDistanceToUse = sPlayerbotAIConfig.gatheringDistance;
    }

    if (sServerFacade.GetDistance2d(requester, wo) > gatheringDistanceToUse)
    {
        TraceLootReason(ai, requester, getName(), "outside gathering range", wo);
        return false;
    }

    //check hostile units after distance checks, to avoid unnecessary calculations

    float MOB_AGGRO_DISTANCE = 30.0f;
    std::list<Unit*> hostiles = ai->GetAllHostileNPCNonPetUnitsAroundWO(wo, MOB_AGGRO_DISTANCE);
    std::list<Unit*> strongHostiles;
    for (auto hostile : hostiles)
    {
        if (!(bot->GetLevel() > hostile->GetLevel() + 7))
        {
            strongHostiles.push_back(hostile);
        }
    }

    if (isInGroup && !ai->IsGroupLeader())
    {
        if (hostiles.size() > 0)
        {
            std::ostringstream out;
            out << hostiles.front()->GetName() << " is blocking " << wo->GetName() << ", need to kill it or I will not gather";
            ai->TellError(requester, out.str());
            TraceLootReason(ai, requester, getName(), "blocked by nearby hostile", wo);
            return false;
        }
    }
    else
    {
        if (strongHostiles.size() > 1)
        {
            std::ostringstream out;
            out << strongHostiles.front()->GetName() << " is blocking " << wo->GetName() << ", need to kill it or I will not gather";
            ai->TellError(requester, out.str());
            TraceLootReason(ai, requester, getName(), "blocked by strong hostiles", wo);
            return false;
        }
    }

    uint8 usedBagSpacePercent = AI_VALUE(uint8, "bag space");

    if (usedBagSpacePercent > 99 && !ai->CanLootSomethingFromWO(wo))
    {
        if (ai->HasQuestItemsInWOLootList(wo))
        {
            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("destroy all gray"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99 && ai->DoSpecificAction("smart destroy item"))
            {
                usedBagSpacePercent = AI_VALUE(uint8, "bag space");
            }

            if (usedBagSpacePercent > 99)
            {
                ai->TellPlayer(requester, "Can not loot quest item, my bags are full", PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
                TraceLootReason(ai, requester, getName(), "bags full with quest loot pending", wo);
                return false;
            }

        }

        if (usedBagSpacePercent > 99)
        {
            ai->TellError(requester, "There is some loot but I do not have free bag space, so not looting");
            TraceLootReason(ai, requester, getName(), "bags full", wo);
            return false;
        }
    }

    return AddAllLootAction::AddLoot(requester, guid);
}
