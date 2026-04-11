#pragma once

#include "Common.h"
#include "SharedDefines.h"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

class AuctionEntry;
class ItemPrototype;

namespace ahbot
{
    class Category;

    enum class MarketId : uint8
    {
        Alliance = 0,
        Horde = 1,
        Neutral = 2
    };

    enum class SourceType : uint8
    {
        Gathered = 0,
        Crafted = 1,
        MobDrop = 2,
        DungeonDrop = 3,
        RaidDrop = 4,
        WorldDrop = 5,
        VendorRecipeOrSpecial = 6
    };

    enum class SupplyLane : uint8
    {
        None = 0,
        Normal = 1,
        Override = 2
    };

    enum AuctionFlags
    {
        AuctionFlagRealInventory = 0x01,
        AuctionFlagBackfill = 0x02,
        AuctionFlagOverrideLane = 0x04,
        AuctionFlagSynthetic = 0x08,
        AuctionFlagSilentExpiry = 0x10
    };

    struct InventoryCandidate
    {
        uint32 sellerGuid = 0;
        uint32 itemGuid = 0;
        uint32 itemCount = 0;
    };

    struct PostingPlan
    {
        bool allowed = false;
        uint32 sellerGuid = 0;
        uint32 stackCount = 0;
        uint8 progressionPhase = 0;
        uint32 flags = 0;
        float priceMultiplier = 1.0f;
        SourceType sourceType = SourceType::WorldDrop;
        SupplyLane lane = SupplyLane::None;
        InventoryCandidate inventory;
    };

    class AhBotEconomy
    {
    public:
        static AhBotEconomy& instance()
        {
            static AhBotEconomy instance;
            return instance;
        }

    public:
        void Initialize();
        MarketId ResolveMarketId(uint32 auctionHouseId) const;
        bool IsMarketEnabled(uint32 auctionHouseId) const;
        std::string GetMarketName(uint32 auctionHouseId) const;
        uint32 GetTreasury(uint32 auctionHouseId);
        void AdjustTreasury(uint32 auctionHouseId, int64 delta);
        bool IsAhBotCharacter(uint32 guid) const;
        uint32 GetCharacterMoney(uint32 guid) const;
        bool HasEnoughMoney(uint32 guid, uint32 amount) const;
        void FinalizeBuyerPayment(AuctionEntry* auction);
        uint32 ApplySellerPayout(AuctionEntry* auction, uint32 baseProfit);
        bool HandleExpiredAuction(AuctionEntry* auction);
        PostingPlan BuildPostingPlan(uint32 auctionHouseId, Category* category, ItemPrototype const* proto, uint32 desiredStackCount, uint32 fallbackSellerGuid);
        void SaveAuctionMetadata(AuctionEntry* auction, PostingPlan const& plan);

    private:
        struct MarketEvidence
        {
            uint32 maxLevel = 1;
            uint32 maxWorldPhaseMask = 0;
        };

        struct CraftSpellInfo
        {
            uint32 spellId = 0;
            uint32 skillId = 0;
            uint32 requiredSkillRank = 0;
        };

        struct AuctionMeta
        {
            bool found = false;
            uint32 marketId = 0;
            uint32 sellerGuid = 0;
            uint32 retentionPct = 0;
            uint32 flags = 0;
        };

    private:
        AhBotEconomy() = default;

        void EnsureTables();
        MarketEvidence LoadMarketEvidence(MarketId market) const;
        uint8 GetEvidencePhase(MarketEvidence const& evidence) const;
        uint8 GetItemPhase(ItemPrototype const* proto) const;
        SourceType ClassifySource(ItemPrototype const* proto);
        bool HasNormalLaneUnlock(MarketId market, ItemPrototype const* proto, SourceType source, uint8 itemPhase);
        bool HasEligibleCrafter(MarketId market, ItemPrototype const* proto);
        bool HasEligibleGatherer(MarketId market, ItemPrototype const* proto) const;
        InventoryCandidate FindInventoryCandidate(MarketId market, uint32 itemId, uint32 desiredStackCount) const;
        uint32 PickSyntheticSeller(MarketId market, uint32 fallbackSellerGuid) const;
        std::vector<CraftSpellInfo> const& GetCraftSpellsForItem(uint32 itemId);
        AuctionMeta LoadAuctionMeta(uint32 auctionId);
        bool ChangeCharacterMoney(uint32 guid, int64 delta) const;
        std::string GetRandomBotAccountsCsv() const;
        bool IsCharacterInMarket(MarketId market, uint8 race) const;

    private:
        std::unordered_map<uint32, std::vector<CraftSpellInfo>> craftSpellCache;
    };
}

#define sAhBotEconomy ahbot::AhBotEconomy::instance()
