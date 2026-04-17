#include "VendorValues.h"
#include "ItemUsageValue.h"
#include "BudgetValues.h"
#include "playerbot/PlayerbotAI.h"
#include "SharedValueContext.h"

using namespace ai;

VendorMap* VendorMapValue::Calculate()
{
    VendorMap* vendorpMap = new VendorMap;


    for (uint32 entry = 0; entry < sCreatureStorage.GetMaxEntry(); entry++)
    {
        CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);

        if (!cInfo)
            continue;

        VendorItemList vendorItems;
        VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(entry);

        if (vItems)
            for (auto vItem : vItems->m_items)
                if (!vItem->maxcount) //Ignore limited amount items.
                    vendorpMap->insert(std::make_pair(vItem->item, entry));

        uint32 vendorId = 0 /* VendorTemplateId not in vmangos */;
        if (vendorId)
        {
            vItems = sObjectMgr.GetNpcVendorTemplateItemList(vendorId);
            if (vItems)
                for (auto vItem : vItems->m_items)
                    if (!vItem->maxcount) //Ignore limited amount items.
                        vendorpMap->insert(std::make_pair(vItem->item, entry));
        }
    }

    return vendorpMap;
}

//What items does this entry have in its loot list?
std::list<int32> ItemVendorListValue::Calculate()
{
    uint32 itemId = stoi(getQualifier());

    VendorMap* vendorMap = GAI_VALUE(VendorMap*, "vendor map");

    std::list<int32> entries;

    auto range = vendorMap->equal_range(itemId);

    for (auto itr = range.first; itr != range.second; ++itr)
        entries.push_back(itr->second);

    return entries;
}

bool VendorHasUsefulItemValue::Calculate()
{
    uint32 entry = stoi(this->getQualifier());
    CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);

    if (!cInfo)
        return false;

    VendorItemList vendorItems;
    VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(entry);

    if (vItems)
        for (auto vItem : vItems->m_items)
                vendorItems.push_back(vItem);

    uint32 vendorId = 0 /* VendorTemplateId not in vmangos */;

    if (vendorId)
    {
        vItems = sObjectMgr.GetNpcVendorTemplateItemList(vendorId);

        if (vItems)
            for (auto vItem : vItems->m_items)
                vendorItems.push_back(vItem);
    }

    std::unordered_map <ItemUsage, uint32> freeMoney;

    freeMoney[ItemUsage::ITEM_USAGE_EQUIP] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::gear);
    freeMoney[ItemUsage::ITEM_USAGE_USE] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::consumables);
    freeMoney[ItemUsage::ITEM_USAGE_SKILL] = freeMoney[ItemUsage::ITEM_USAGE_DISENCHANT] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::tradeskill);
    freeMoney[ItemUsage::ITEM_USAGE_AMMO] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::ammo);
    freeMoney[ItemUsage::ITEM_USAGE_QUEST] = AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::anything);

    for (auto vendorItem : vendorItems)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(vendorItem->item);
#ifndef MANGOSBOT_ZERO
        if (vendorItem->ExtendedCost) //Needs to be replaced with check if bot has (free) currency for this item.
            continue;
#endif

        ItemUsage usage = AI_VALUE2_LAZY(ItemUsage, "item usage", vendorItem->item);

        if (freeMoney.find(usage) == freeMoney.end() || proto->BuyPrice > freeMoney[usage])
            continue;

        return true;
    }

    return false;
}

bool ShouldBuyVendorGearValue::Calculate()
{
    // Require gear budget before doing anything else.
    if (AI_VALUE2(uint32, "free money for", (uint32)NeedMoneyFor::gear) == 0)
        return false;

    // Check every equipment slot: if any is empty or holds grey/white-quality
    // gear the bot is a candidate for a vendor or AH shopping trip.
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            return true;    // Empty slot -- vendors or AH may fill it.

        ItemPrototype const* proto = item->GetProto();
        if (proto && proto->Quality <= ITEM_QUALITY_NORMAL)
            return true;    // Grey or white item -- almost certainly upgradeable.
    }

    return false;
}