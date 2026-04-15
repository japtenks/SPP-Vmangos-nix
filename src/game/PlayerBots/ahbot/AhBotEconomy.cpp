#include "AhBotEconomy.h"

#include "AhBot.h"
#include "AhBotConfig.h"
#include "AuctionHouse/AuctionHouseMgr.h"
#include "Category.h"
#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "ServerFacade.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <limits>

using namespace ahbot;

void AhBotEconomy::Initialize()
{
    EnsureTables();
}

void AhBotEconomy::EnsureTables()
{
    CharacterDatabase.Execute(
        "CREATE TABLE IF NOT EXISTS `ahbot_market_state` ("
        "`market_id` bigint(20) unsigned NOT NULL,"
        "`treasury` bigint(20) NOT NULL DEFAULT '0',"
        "`updated_at` bigint(20) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`market_id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");

    CharacterDatabase.Execute(
        "CREATE TABLE IF NOT EXISTS `ahbot_auction` ("
        "`auction_id` bigint(20) unsigned NOT NULL,"
        "`market_id` bigint(20) unsigned NOT NULL DEFAULT '0',"
        "`seller_guid` bigint(20) unsigned NOT NULL DEFAULT '0',"
        "`retention_pct` bigint(20) unsigned NOT NULL DEFAULT '30',"
        "`flags` bigint(20) unsigned NOT NULL DEFAULT '0',"
        "`created_at` bigint(20) unsigned NOT NULL DEFAULT '0',"
        "PRIMARY KEY (`auction_id`),"
        "KEY `idx_ahbot_auction_market` (`market_id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
}

MarketId AhBotEconomy::ResolveMarketId(uint32 auctionHouseId) const
{
    switch (auctionHouseId)
    {
        case 1: return MarketId::Alliance;
        case 6: return MarketId::Horde;
        default: return MarketId::Neutral;
    }
}

bool AhBotEconomy::IsMarketEnabled(uint32 auctionHouseId) const
{
    switch (ResolveMarketId(auctionHouseId))
    {
        case MarketId::Alliance: return sAhBotConfig.allianceMarketEnabled;
        case MarketId::Horde: return sAhBotConfig.hordeMarketEnabled;
        case MarketId::Neutral: return sAhBotConfig.neutralMarketEnabled;
    }

    return true;
}

std::string AhBotEconomy::GetMarketName(uint32 auctionHouseId) const
{
    switch (ResolveMarketId(auctionHouseId))
    {
        case MarketId::Alliance: return "alliance";
        case MarketId::Horde: return "horde";
        case MarketId::Neutral: return "neutral";
    }

    return "unknown";
}

uint32 AhBotEconomy::GetTreasury(uint32 auctionHouseId)
{
    uint32 marketId = static_cast<uint32>(ResolveMarketId(auctionHouseId));
    auto result = CharacterDatabase.PQuery("SELECT `treasury` FROM `ahbot_market_state` WHERE `market_id` = '%u'", marketId);
    if (!result)
    {
        CharacterDatabase.PExecute("INSERT INTO `ahbot_market_state` (`market_id`, `treasury`, `updated_at`) VALUES ('%u', '0', '%u')", marketId, uint32(time(nullptr)));
        return 0;
    }

    return result->Fetch()[0].GetUInt32();
}

void AhBotEconomy::AdjustTreasury(uint32 auctionHouseId, int64 delta)
{
    uint32 marketId = static_cast<uint32>(ResolveMarketId(auctionHouseId));
    int64 next = int64(GetTreasury(auctionHouseId)) + delta;
    if (next < 0)
        next = 0;

    CharacterDatabase.PExecute(
        "REPLACE INTO `ahbot_market_state` (`market_id`, `treasury`, `updated_at`) VALUES ('%u', '" UI64FMTD "', '%u')",
        marketId, uint64(next), uint32(time(nullptr)));
}

bool AhBotEconomy::IsAhBotCharacter(uint32 guid) const
{
    uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(HIGHGUID_PLAYER, guid));
    return account && sPlayerbotAIConfig.IsInRandomAccountList(account);
}

uint32 AhBotEconomy::GetCharacterMoney(uint32 guid) const
{
    if (!guid)
        return 0;

    if (Player* player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, guid)))
        return player->GetMoney();

    auto result = CharacterDatabase.PQuery("SELECT `money` FROM `characters` WHERE `guid` = '%u'", guid);
    return result ? result->Fetch()[0].GetUInt32() : 0;
}

bool AhBotEconomy::HasEnoughMoney(uint32 guid, uint32 amount) const
{
    return GetCharacterMoney(guid) >= amount;
}

bool AhBotEconomy::ChangeCharacterMoney(uint32 guid, int64 delta) const
{
    if (!guid)
        return false;

    if (Player* player = sObjectMgr.GetPlayer(ObjectGuid(HIGHGUID_PLAYER, guid)))
    {
        int64 next = int64(player->GetMoney()) + delta;
        if (next < 0)
            return false;

        player->SetMoney(uint32(next));
        return true;
    }

    uint32 current = GetCharacterMoney(guid);
    if (delta < 0 && current < uint32(-delta))
        return false;

    int64 next = int64(current) + delta;
    if (next < 0)
        return false;

    CharacterDatabase.PExecute("UPDATE `characters` SET `money` = '%u' WHERE `guid` = '%u'", uint32(next), guid);
    return true;
}

bool AhBotEconomy::IsGrowthMode() const
{
    return sAhBotConfig.economyType == AhBotEconomyType::Growth;
}

uint32 AhBotEconomy::GetCycleStamp() const
{
    const uint32 interval = std::max<uint32>(1, sAhBotConfig.updateInterval);
    return uint32(time(nullptr) / interval);
}

uint32 AhBotEconomy::GetDayStamp() const
{
    return uint32(time(nullptr) / (24 * 60 * 60));
}

AhBotEconomy::SubsidyBudget& AhBotEconomy::GetSubsidyBudget(uint32 auctionHouseId)
{
    SubsidyBudget& budget = subsidyBudgets[auctionHouseId];
    const uint32 cycleStamp = GetCycleStamp();
    const uint32 dayStamp = GetDayStamp();
    if (budget.cycleStamp != cycleStamp)
    {
        budget.cycleStamp = cycleStamp;
        budget.cycleInjected = 0;
    }
    if (budget.dayStamp != dayStamp)
    {
        budget.dayStamp = dayStamp;
        budget.dayInjected = 0;
    }
    return budget;
}

bool AhBotEconomy::TrySeedBuyerShortfall(AuctionEntry* auction, uint32 shortfall, uint32 currentMoney)
{
    if (!auction || !shortfall)
        return true;

    if (!IsGrowthMode())
        return false;

    SubsidyBudget& budget = GetSubsidyBudget(auction->GetHouseId());
    const uint32 cycleRemaining = sAhBotConfig.growthSubsidyCycleCap == 0
        ? std::numeric_limits<uint32>::max()
        : (budget.cycleInjected >= sAhBotConfig.growthSubsidyCycleCap ? 0 : sAhBotConfig.growthSubsidyCycleCap - budget.cycleInjected);
    const uint32 dayRemaining = sAhBotConfig.growthSubsidyDailyCap == 0
        ? std::numeric_limits<uint32>::max()
        : (budget.dayInjected >= sAhBotConfig.growthSubsidyDailyCap ? 0 : sAhBotConfig.growthSubsidyDailyCap - budget.dayInjected);
    const uint32 allowed = std::min(shortfall, std::min(cycleRemaining, dayRemaining));
    if (allowed < shortfall)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR,
            "AhBot growth subsidy cap blocked buyer %u shortfall %u/%u copper for auction %u (had %u)",
            auction->bidder, shortfall, auction->bid, auction->Id, currentMoney);
        return false;
    }

    if (!ChangeCharacterMoney(auction->bidder, int64(shortfall)))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR,
            "AhBot growth subsidy failed to seed %u copper for buyer %u on auction %u",
            shortfall, auction->bidder, auction->Id);
        return false;
    }

    budget.cycleInjected += shortfall;
    budget.dayInjected += shortfall;
    AdjustTreasury(auction->GetHouseId(), int64(shortfall));

    if (sAhBotConfig.growthLogSubsidy)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL,
            "AhBot growth subsidy injected %u copper for buyer %u on auction %u (cycle %u, day %u)",
            shortfall, auction->bidder, auction->Id, budget.cycleInjected, budget.dayInjected);
    }

    return true;
}

bool AhBotEconomy::FinalizeBuyerPayment(AuctionEntry* auction)
{
    if (!auction || !auction->bidder || !IsAhBotCharacter(auction->bidder))
        return true;

    const uint32 currentMoney = GetCharacterMoney(auction->bidder);
    if (currentMoney < auction->bid && !TrySeedBuyerShortfall(auction, auction->bid - currentMoney, currentMoney))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AhBot buyer %u only had %u of %u copper at auction finalize for auction %u",
            auction->bidder, currentMoney, auction->bid, auction->Id);
        return false;
    }

    return ChangeCharacterMoney(auction->bidder, -int64(auction->bid));
}

uint32 AhBotEconomy::ApplySellerPayout(AuctionEntry* auction, uint32 baseProfit)
{
    if (!auction)
        return baseProfit;

    AuctionMeta meta = LoadAuctionMeta(auction->Id);
    if (!meta.found && !IsAhBotCharacter(auction->owner))
        return baseProfit;

    uint32 retentionPct = meta.found ? meta.retentionPct : sAhBotConfig.botSaleRetentionPct;
    uint32 sellerShare = uint32((uint64(baseProfit) * std::min<uint32>(100, retentionPct)) / 100ULL);
    AdjustTreasury(auction->GetHouseId(), int64(baseProfit) - int64(sellerShare));
    if (meta.found)
        CharacterDatabase.PExecute("DELETE FROM `ahbot_auction` WHERE `auction_id` = '%u'", auction->Id);
    return sellerShare;
}

bool AhBotEconomy::HandleExpiredAuction(AuctionEntry* auction)
{
    if (!auction)
        return false;

    AuctionMeta meta = LoadAuctionMeta(auction->Id);
    if (!meta.found)
        return false;

    CharacterDatabase.PExecute("DELETE FROM `ahbot_auction` WHERE `auction_id` = '%u'", auction->Id);

    if (!(meta.flags & AuctionFlagSilentExpiry))
        return false;

    CharacterDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", auction->itemGuidLow);
    return true;
}

AhBotEconomy::AuctionMeta AhBotEconomy::LoadAuctionMeta(uint32 auctionId)
{
    AuctionMeta meta;
    auto result = CharacterDatabase.PQuery("SELECT `market_id`, `seller_guid`, `retention_pct`, `flags` FROM `ahbot_auction` WHERE `auction_id` = '%u'", auctionId);
    if (!result)
        return meta;

    Field* fields = result->Fetch();
    meta.found = true;
    meta.marketId = fields[0].GetUInt32();
    meta.sellerGuid = fields[1].GetUInt32();
    meta.retentionPct = fields[2].GetUInt32();
    meta.flags = fields[3].GetUInt32();
    return meta;
}

void AhBotEconomy::SaveAuctionMetadata(AuctionEntry* auction, PostingPlan const& plan)
{
    if (!auction)
        return;

    CharacterDatabase.PExecute(
        "REPLACE INTO `ahbot_auction` (`auction_id`, `market_id`, `seller_guid`, `retention_pct`, `flags`, `created_at`) "
        "VALUES ('%u', '%u', '%u', '%u', '%u', '%u')",
        auction->Id,
        uint32(ResolveMarketId(auction->GetHouseId())),
        plan.sellerGuid,
        sAhBotConfig.botSaleRetentionPct,
        plan.flags,
        uint32(time(nullptr)));
}

AhBotEconomy::MarketEvidence AhBotEconomy::LoadMarketEvidence(MarketId market) const
{
    MarketEvidence evidence;
    std::string accounts = GetRandomBotAccountsCsv();
    if (accounts.empty())
        return evidence;

    auto result = CharacterDatabase.PQuery(
        "SELECT `race`, MAX(`level`), MAX(`world_phase_mask`) FROM `characters` "
        "WHERE `account` IN (%s) GROUP BY `race`",
        accounts.c_str());

    if (!result)
        return evidence;

    do
    {
        Field* fields = result->Fetch();
        uint8 race = fields[0].GetUInt8();
        if (!IsCharacterInMarket(market, race))
            continue;

        evidence.maxLevel = std::max<uint32>(evidence.maxLevel, fields[1].GetUInt32());
        evidence.maxWorldPhaseMask = std::max<uint32>(evidence.maxWorldPhaseMask, fields[2].GetUInt32());
    }
    while (result->NextRow());

    return evidence;
}

uint8 AhBotEconomy::GetEvidencePhase(MarketEvidence const& evidence) const
{
    uint8 levelPhase = 0;
    if (evidence.maxLevel >= 60)
        levelPhase = 4;
    else if (evidence.maxLevel >= 50)
        levelPhase = 3;
    else if (evidence.maxLevel >= 40)
        levelPhase = 2;
    else if (evidence.maxLevel >= 25)
        levelPhase = 1;

    uint8 worldPhase = uint8(std::min<uint32>(5, evidence.maxWorldPhaseMask));
    return std::max(levelPhase, worldPhase);
}

uint8 AhBotEconomy::GetItemPhase(ItemPrototype const* proto) const
{
    if (!proto)
        return 0;

    uint32 ilvl = std::max<uint32>(proto->ItemLevel, proto->RequiredLevel * 2);
    if (ilvl >= 76) return 5;
    if (ilvl >= 66) return 4;
    if (ilvl >= 58) return 3;
    if (ilvl >= 45) return 2;
    if (ilvl >= 30) return 1;
    return 0;
}

SourceType AhBotEconomy::ClassifySource(ItemPrototype const* proto)
{
    if (!proto)
        return SourceType::WorldDrop;

    if (!GetCraftSpellsForItem(proto->ItemId).empty())
        return SourceType::Crafted;

    if (proto->Class == ITEM_CLASS_RECIPE ||
        ai::ItemUsageValue::IsItemSoldByAnyVendorButHasLimitedMaxCount(proto))
        return SourceType::VendorRecipeOrSpecial;

    static SkillType gatherSkills[] = { SKILL_HERBALISM, SKILL_MINING, SKILL_SKINNING, SKILL_FISHING };
    for (SkillType skill : gatherSkills)
    {
        if (ai::ItemUsageValue::IsItemUsedBySkill(proto, skill))
            return SourceType::Gathered;
    }

    uint8 phase = GetItemPhase(proto);
    if (phase >= 4 && (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON))
        return SourceType::RaidDrop;
    if (phase >= 3)
        return SourceType::DungeonDrop;
    if (proto->Quality >= ITEM_QUALITY_UNCOMMON)
        return SourceType::MobDrop;

    return SourceType::WorldDrop;
}

bool AhBotEconomy::HasEligibleCrafter(MarketId market, ItemPrototype const* proto)
{
    if (!proto)
        return false;

    std::vector<CraftSpellInfo> const& spells = GetCraftSpellsForItem(proto->ItemId);
    if (spells.empty())
        return false;

    std::string accounts = GetRandomBotAccountsCsv();
    if (accounts.empty())
        return false;

    for (CraftSpellInfo const& craft : spells)
    {
        std::ostringstream query;
        query << "SELECT c.`race` FROM `characters` c "
              << "INNER JOIN `character_spell` cs ON cs.`guid` = c.`guid` "
              << "LEFT JOIN `character_skills` sk ON sk.`guid` = c.`guid` AND sk.`skill` = '" << craft.skillId << "' "
              << "WHERE c.`account` IN (" << accounts << ") AND cs.`spell` = '" << craft.spellId << "'";
        if (craft.skillId)
            query << " AND IFNULL(sk.`value`, 0) >= '" << craft.requiredSkillRank << "'";

        auto result = CharacterDatabase.Query(query.str().c_str());
        if (!result)
            continue;

        do
        {
            uint8 race = result->Fetch()[0].GetUInt8();
            if (IsCharacterInMarket(market, race))
                return true;
        }
        while (result->NextRow());
    }

    return false;
}

bool AhBotEconomy::HasEligibleGatherer(MarketId market, ItemPrototype const* proto) const
{
    if (!proto)
        return false;

    SkillType skill = SKILL_NONE;
    if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_HERBALISM)) skill = SKILL_HERBALISM;
    else if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_MINING)) skill = SKILL_MINING;
    else if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_SKINNING)) skill = SKILL_SKINNING;
    else if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_FISHING)) skill = SKILL_FISHING;

    if (skill == SKILL_NONE)
        return false;

    std::string accounts = GetRandomBotAccountsCsv();
    if (accounts.empty())
        return false;

    auto result = CharacterDatabase.PQuery(
        "SELECT c.`race` FROM `characters` c "
        "INNER JOIN `character_skills` cs ON cs.`guid` = c.`guid` "
        "WHERE c.`account` IN (%s) AND cs.`skill` = '%u' AND cs.`value` > 0",
        accounts.c_str(), uint32(skill));

    if (!result)
        return false;

    do
    {
        if (IsCharacterInMarket(market, result->Fetch()[0].GetUInt8()))
            return true;
    }
    while (result->NextRow());

    return false;
}

AhBotEconomy::SellerProfile AhBotEconomy::LoadSellerProfile(uint32 guid) const
{
    SellerProfile seller;
    if (!guid)
        return seller;

    auto result = CharacterDatabase.PQuery(
        "SELECT `guid`, `race`, `level`, `zone`, `map` FROM `characters` WHERE `guid` = '%u'",
        guid);
    if (!result)
        return seller;

    Field* fields = result->Fetch();
    seller.guid = fields[0].GetUInt32();
    seller.race = fields[1].GetUInt8();
    seller.level = std::max<uint8>(1, fields[2].GetUInt8());
    seller.zone = fields[3].GetUInt32();
    seller.map = fields[4].GetUInt32();
    return seller;
}

bool AhBotEconomy::HasCharacterSkill(uint32 guid, uint32 skillId, uint32 minValue) const
{
    if (!guid || !skillId)
        return false;

    auto result = CharacterDatabase.PQuery(
        "SELECT 1 FROM `character_skills` WHERE `guid` = '%u' AND `skill` = '%u' AND `value` >= '%u' LIMIT 1",
        guid, skillId, minValue);
    return result != nullptr;
}

uint32 AhBotEconomy::GetSellerItemLevelFloor(ItemPrototype const* proto) const
{
    if (!proto)
        return 1;

    uint32 floor = std::max<uint32>(1, proto->RequiredLevel);
    if (proto->ItemLevel > 1)
        floor = std::max<uint32>(floor, proto->ItemLevel / 2);
    return floor;
}

bool AhBotEconomy::IsSellerPlausibleForListing(SellerProfile const& seller, ItemPrototype const* proto, SourceType source, uint8 itemPhase) const
{
    if (!seller.guid || !proto)
        return false;

    const uint32 levelFloor = GetSellerItemLevelFloor(proto);
    const uint32 softFloor = levelFloor > 8 ? levelFloor - 8 : 1;
    const uint32 phaseFloor = itemPhase == 0 ? 1 : std::max<uint32>(softFloor, itemPhase * 10);

    switch (source)
    {
        case SourceType::Gathered:
        {
            SkillType gatherSkill = SKILL_NONE;
            if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_SKINNING))
                gatherSkill = SKILL_SKINNING;
            else if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_HERBALISM))
                gatherSkill = SKILL_HERBALISM;
            else if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_MINING))
                gatherSkill = SKILL_MINING;
            else if (ai::ItemUsageValue::IsItemUsedBySkill(proto, SKILL_FISHING))
                gatherSkill = SKILL_FISHING;

            if (gatherSkill == SKILL_NONE || !HasCharacterSkill(seller.guid, gatherSkill))
                return false;

            return seller.level >= std::max<uint32>(1, phaseFloor);
        }
        case SourceType::Crafted:
        {
            std::vector<CraftSpellInfo> const& spells = const_cast<AhBotEconomy*>(this)->GetCraftSpellsForItem(proto->ItemId);
            for (CraftSpellInfo const& craft : spells)
            {
                if (!craft.skillId)
                    continue;

                if (HasCharacterSkill(seller.guid, craft.skillId, std::max<uint32>(1, craft.requiredSkillRank)))
                    return seller.level >= std::max<uint32>(1, phaseFloor);
            }

            static const SkillType tradeSkills[] = {
                SKILL_TAILORING, SKILL_LEATHERWORKING, SKILL_ENGINEERING, SKILL_BLACKSMITHING,
                SKILL_ALCHEMY, SKILL_ENCHANTING, SKILL_COOKING, SKILL_FIRST_AID
            };
            for (SkillType skill : tradeSkills)
            {
                if (ai::ItemUsageValue::IsItemUsedBySkill(proto, skill) && HasCharacterSkill(seller.guid, skill))
                    return seller.level >= std::max<uint32>(1, phaseFloor);
            }

            return false;
        }
        case SourceType::VendorRecipeOrSpecial:
            return seller.level >= std::max<uint32>(10, phaseFloor);
        case SourceType::RaidDrop:
            return seller.level >= std::max<uint32>(55, levelFloor);
        case SourceType::DungeonDrop:
            return seller.level >= std::max<uint32>(18, phaseFloor);
        case SourceType::MobDrop:
        case SourceType::WorldDrop:
        default:
            return seller.level >= std::max<uint32>(1, phaseFloor);
    }
}

uint32 AhBotEconomy::PickProfileSeller(MarketId market, ItemPrototype const* proto, SourceType source, uint8 itemPhase, uint32 fallbackSellerGuid) const
{
    std::vector<uint32> candidates;
    SellerProfile fallback = LoadSellerProfile(fallbackSellerGuid);
    if (fallback.guid && IsCharacterInMarket(market, fallback.race) &&
        IsSellerPlausibleForListing(fallback, proto, source, itemPhase))
    {
        candidates.push_back(fallback.guid);
    }

    std::string accounts = GetRandomBotAccountsCsv();
    if (accounts.empty())
        return candidates.empty() ? 0 : candidates[0];

    auto result = CharacterDatabase.PQuery(
        "SELECT `guid`, `race`, `level`, `zone`, `map` FROM `characters` WHERE `account` IN (%s)",
        accounts.c_str());
    if (!result)
        return candidates.empty() ? 0 : candidates[0];

    do
    {
        Field* fields = result->Fetch();
        SellerProfile seller;
        seller.guid = fields[0].GetUInt32();
        seller.race = fields[1].GetUInt8();
        seller.level = std::max<uint8>(1, fields[2].GetUInt8());
        seller.zone = fields[3].GetUInt32();
        seller.map = fields[4].GetUInt32();

        if (!IsCharacterInMarket(market, seller.race))
            continue;

        if (std::find(candidates.begin(), candidates.end(), seller.guid) != candidates.end())
            continue;

        if (IsSellerPlausibleForListing(seller, proto, source, itemPhase))
            candidates.push_back(seller.guid);
    }
    while (result->NextRow());

    if (candidates.empty())
        return 0;

    return candidates[urand(0, candidates.size() - 1)];
}

bool AhBotEconomy::HasNormalLaneUnlock(MarketId market, ItemPrototype const* proto, SourceType source, uint8 itemPhase)
{
    if (!proto || itemPhase > sAhBotConfig.phase)
        return false;

    MarketEvidence evidence = LoadMarketEvidence(market);
    uint8 evidencePhase = GetEvidencePhase(evidence);
    if (itemPhase > evidencePhase)
        return false;

    switch (source)
    {
        case SourceType::Crafted:
            return HasEligibleCrafter(market, proto);
        case SourceType::Gathered:
            return HasEligibleGatherer(market, proto);
        case SourceType::VendorRecipeOrSpecial:
            return ai::ItemUsageValue::IsItemSoldByAnyVendor(proto) || proto->Class == ITEM_CLASS_RECIPE;
        default:
            return true;
    }
}

InventoryCandidate AhBotEconomy::FindInventoryCandidate(MarketId market, uint32 itemId, uint32 desiredStackCount) const
{
    InventoryCandidate candidate;
    std::string accounts = GetRandomBotAccountsCsv();
    if (accounts.empty())
        return candidate;

    auto result = CharacterDatabase.PQuery(
        "SELECT c.`guid`, c.`race`, ii.`guid`, ii.`count`, ii.`flags` "
        "FROM `characters` c "
        "INNER JOIN `character_inventory` ci ON ci.`guid` = c.`guid` "
        "INNER JOIN `item_instance` ii ON ii.`guid` = ci.`item_guid` "
        "WHERE c.`account` IN (%s) AND ii.`item_id` = '%u' AND ii.`count` <= '%u' "
        "ORDER BY ii.`count` DESC",
        accounts.c_str(), itemId, desiredStackCount);

    if (!result)
        return candidate;

    do
    {
        Field* fields = result->Fetch();
        uint8 race = fields[1].GetUInt8();
        if (!IsCharacterInMarket(market, race))
            continue;

        uint32 itemFlags = fields[4].GetUInt32();
        if (itemFlags & ITEM_DYNFLAG_BOUND)
            continue;

        candidate.sellerGuid = fields[0].GetUInt32();
        candidate.itemGuid = fields[2].GetUInt32();
        candidate.itemCount = fields[3].GetUInt32();
        return candidate;
    }
    while (result->NextRow());

    return candidate;
}

uint32 AhBotEconomy::PickSyntheticSeller(MarketId market, uint32 fallbackSellerGuid) const
{
    uint32 auctionHouseId = 7;
    switch (market)
    {
        case MarketId::Alliance: auctionHouseId = 1; break;
        case MarketId::Horde: auctionHouseId = 6; break;
        case MarketId::Neutral: auctionHouseId = 7; break;
    }

    uint32 seller = auctionbot.SelectRandomBidder(auctionHouseId);
    return seller ? seller : fallbackSellerGuid;
}

PostingPlan AhBotEconomy::BuildPostingPlan(uint32 auctionHouseId, Category* category, ItemPrototype const* proto, uint32 desiredStackCount, uint32 fallbackSellerGuid)
{
    PostingPlan plan;
    if (!proto || !category || !IsMarketEnabled(auctionHouseId))
        return plan;

    plan.sourceType = ClassifySource(proto);
    plan.progressionPhase = GetItemPhase(proto);
    if (plan.progressionPhase > sAhBotConfig.phase)
        return plan;

    MarketId market = ResolveMarketId(auctionHouseId);
    bool normalUnlocked = HasNormalLaneUnlock(market, proto, plan.sourceType, plan.progressionPhase);
    bool allowOverride = !normalUnlocked && sAhBotConfig.phaseOverrideEnabled;

    if (!normalUnlocked && !allowOverride)
        return plan;

    plan.allowed = true;
    plan.lane = normalUnlocked ? SupplyLane::Normal : SupplyLane::Override;
    plan.sellerGuid = PickSyntheticSeller(market, fallbackSellerGuid);
    plan.stackCount = std::max<uint32>(1, desiredStackCount);

    if (plan.lane == SupplyLane::Override)
    {
        plan.flags |= AuctionFlagOverrideLane | AuctionFlagBackfill | AuctionFlagSynthetic | AuctionFlagSilentExpiry;
        plan.stackCount = std::max<uint32>(1, desiredStackCount / 4);

        MarketEvidence evidence = LoadMarketEvidence(market);
        uint8 evidencePhase = GetEvidencePhase(evidence);
        uint8 phaseDistance = plan.progressionPhase > evidencePhase ? plan.progressionPhase - evidencePhase : 0;
        plan.priceMultiplier = 1.75f + 0.5f * float(phaseDistance);
        return plan;
    }

    if (sAhBotConfig.mode == AhBotMode::Materials)
    {
        plan.sellerGuid = PickProfileSeller(market, proto, plan.sourceType, plan.progressionPhase, fallbackSellerGuid);
        if (!plan.sellerGuid)
        {
            plan.allowed = false;
            return plan;
        }

        plan.flags |= AuctionFlagBackfill | AuctionFlagSilentExpiry;
        plan.priceMultiplier = 1.0f;
        return plan;
    }

    if (sAhBotConfig.mode == AhBotMode::Inventory)
    {
        plan.inventory = FindInventoryCandidate(market, proto->ItemId, desiredStackCount);
        if (plan.inventory.itemGuid && plan.inventory.itemCount > 0)
        {
            plan.flags |= AuctionFlagRealInventory;
            plan.sellerGuid = plan.inventory.sellerGuid;
            plan.stackCount = plan.inventory.itemCount;
            return plan;
        }

        if (!sAhBotConfig.backfillEnabled)
        {
            plan.allowed = false;
            return plan;
        }
    }

    if (sAhBotConfig.mode == AhBotMode::Synthetic)
        plan.flags |= AuctionFlagSynthetic;
    else
        plan.flags |= AuctionFlagBackfill | AuctionFlagSilentExpiry;

    plan.priceMultiplier = 1.0f;
    return plan;
}

std::vector<AhBotEconomy::CraftSpellInfo> const& AhBotEconomy::GetCraftSpellsForItem(uint32 itemId)
{
    auto itr = craftSpellCache.find(itemId);
    if (itr != craftSpellCache.end())
        return itr->second;

    std::vector<CraftSpellInfo>& cache = craftSpellCache[itemId];
    for (uint32 spellId = 0; spellId < sServerFacade.GetSpellInfoRows(); ++spellId)
    {
        SpellEntry const* spell = sServerFacade.LookupSpellInfo(spellId);
        if (!spell)
            continue;

        bool createsItem = false;
        for (uint32 effect = EFFECT_INDEX_0; effect < MAX_EFFECT_INDEX; ++effect)
        {
            if (spell->Effect[effect] == SPELL_EFFECT_CREATE_ITEM && spell->EffectItemType[effect] == itemId)
            {
                createsItem = true;
                break;
            }
        }

        if (!createsItem)
            continue;

        CraftSpellInfo info;
        info.spellId = spellId;
        for (uint32 row = 0; row < sSkillLineAbilityStore.GetNumRows(); ++row)
        {
            SkillLineAbilityEntry const* ability = sSkillLineAbilityStore.LookupEntry(row);
            if (!ability || ability->spellId != spellId)
                continue;

            info.skillId = ability->skillId;
            info.requiredSkillRank = ability->min_value;
            break;
        }

        cache.push_back(info);
    }

    return cache;
}

std::string AhBotEconomy::GetRandomBotAccountsCsv() const
{
    std::ostringstream out;
    bool first = true;
    for (uint32 account : sPlayerbotAIConfig.randomBotAccounts)
    {
        if (!first)
            out << ",";
        out << account;
        first = false;
    }

    return out.str();
}

bool AhBotEconomy::IsCharacterInMarket(MarketId market, uint8 race) const
{
    Team team = Player::TeamForRace(race);
    switch (market)
    {
        case MarketId::Alliance: return team == ALLIANCE;
        case MarketId::Horde: return team == HORDE;
        case MarketId::Neutral: return true;
    }

    return true;
}
