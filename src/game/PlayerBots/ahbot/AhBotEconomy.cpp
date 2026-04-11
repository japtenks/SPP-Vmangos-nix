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

void AhBotEconomy::FinalizeBuyerPayment(AuctionEntry* auction)
{
    if (!auction || !auction->bidder || !IsAhBotCharacter(auction->bidder))
        return;

    uint32 currentMoney = GetCharacterMoney(auction->bidder);
    uint32 debit = std::min(currentMoney, auction->bid);
    if (debit)
        ChangeCharacterMoney(auction->bidder, -int64(debit));

    if (debit < auction->bid)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "AhBot buyer %u only had %u of %u copper at auction finalize for auction %u",
            auction->bidder, debit, auction->bid, auction->Id);
    }
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
        "SELECT c.`guid`, c.`race`, ii.`guid`, ii.`count` "
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
