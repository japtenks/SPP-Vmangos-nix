
#include "playerbot/playerbot.h"
#include "FishValues.h"
#include "playerbot/strategy/actions/TellLosAction.h"

using namespace ai;

static bool HasOwnedFishingBobber(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    std::list<ObjectGuid> nearbyObjects = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("nearest game objects no los")->Get();
    std::list<GameObject*> objects = TellLosAction::GoGuidListToObjList(ai, nearbyObjects);

    for (auto& obj : objects)
    {
        if (obj->GetEntry() != 35591)
            continue;

        if (obj->GetOwnerGuid() != bot->GetObjectGuid())
            continue;

        return true;
    }

    return false;
}

bool CanFishValue::Calculate()
{
    if (!bot->GetSkill(SKILL_FISHING, false, false)) //Unable to fish.
        return false;

    std::list<Item*> poles = AI_VALUE2(std::list<Item*>, "inventory items", "fishing pole");

    if (poles.empty()) //No fishing pole.
        return false;

    return true;
}

bool CanOpenFishingDobberValue::Calculate()
{
    return HasOwnedFishingBobber(ai);
}

bool DoneFishingValue::Calculate()
{
    if (!bot->GetSkill(SKILL_FISHING, false, false)) //Unable to fish.
        return false;

    Item* mhItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);

    //Does not have fishing pole equiped.
    if (!mhItem || mhItem->GetProto()->Class != ITEM_CLASS_WEAPON || mhItem->GetProto()->SubClass != ITEM_SUBCLASS_WEAPON_FISHING_POLE)
        return false;

    if (HasOwnedFishingBobber(ai))
        return false;

    if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
    {
        std::string spellName = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->m_spellInfo->SpellName[0];
        if (spellName.find("Fishing") == 0)
            return false;
    }

    return true;
}
