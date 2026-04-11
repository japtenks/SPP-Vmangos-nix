#include "AhBotRuntime.h"

#include "Database/DatabaseEnv.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Log.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include <unordered_set>

namespace
{
std::unordered_set<uint32> s_ahBotCreatedAuctionIds;

bool ApplyAhBotBidder(AuctionEntry* auction, Player* buyer, uint32 newBid)
{
    if (!auction || !buyer)
        return false;

    if (sRandomPlayerbotMgr.HasAhReservation(auction->Id))
        sRandomPlayerbotMgr.ReleaseAhBotCopperForAuction(auction->Id);

    if (!sRandomPlayerbotMgr.ReserveAhBotCopper(buyer->GetGUIDLow(), newBid, auction->Id))
        return false;

    auction->bidder = buyer->GetGUIDLow();
    auction->bid = newBid;
    CharacterDatabase.PExecute("UPDATE auction SET buyer_guid = '%u', last_bid = '%u' WHERE id = '%u'",
        auction->bidder, auction->bid, auction->Id);
    return true;
}
}

namespace AhBotRuntime
{
bool TryPlaceAhBotBid(AuctionEntry* auction, uint32 bidPrice)
{
    Player* buyer = sRandomPlayerbotMgr.GetRandomAhBuyer(auction, bidPrice);
    return buyer ? ApplyAhBotBidder(auction, buyer, bidPrice) : false;
}

bool TryPlaceAhBotBuyout(AuctionEntry* auction)
{
    if (!auction || !auction->buyout)
        return false;

    Player* buyer = sRandomPlayerbotMgr.GetRandomAhBuyer(auction, auction->buyout);
    return buyer ? ApplyAhBotBidder(auction, buyer, auction->buyout) : false;
}

Player* SelectAhBotSeller(ItemPrototype const* prototype, uint32 count, AuctionHouseEntry const* ahEntry)
{
    return sRandomPlayerbotMgr.GetRandomAhSeller(prototype, count, ahEntry);
}

bool CreateAhBotStockAuction(Player* seller, Item* item, uint32 bidPrice, uint32 buyoutPrice, uint32 auctionTime, AuctionHouseEntry const* ahEntry)
{
    if (!seller || !seller->GetSession() || !item || !ahEntry)
        return false;

    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);
    if (!auctionHouse)
        return false;

    AuctionEntry* auctionEntry = new AuctionEntry;
    auctionEntry->Id = sObjectMgr.GenerateAuctionID();
    auctionEntry->itemGuidLow = item->GetObjectGuid().GetCounter();
    auctionEntry->itemTemplate = item->GetEntry();
    auctionEntry->owner = seller->GetGUIDLow();
    auctionEntry->ownerAccount = seller->GetSession()->GetAccountId();
    auctionEntry->startbid = bidPrice;
    auctionEntry->bidder = 0;
    auctionEntry->bid = 0;
    auctionEntry->buyout = buyoutPrice;
    auctionEntry->depositTime = time(nullptr);
    auctionEntry->expireTime = time(nullptr) + auctionTime;
    auctionEntry->deposit = 0;
    auctionEntry->auctionHouseEntry = ahEntry;

    sAuctionMgr.AddAItem(item);
    auctionHouse->AddAuction(auctionEntry);
    item->SaveToDB();
    auctionEntry->SaveToDB();
    sAuctionMgr.OnAhBotAuctionCreated(auctionEntry);
    TrackAhBotCreatedAuction(auctionEntry->Id);
    return true;
}

void TrackAhBotCreatedAuction(uint32 auctionId)
{
    s_ahBotCreatedAuctionIds.insert(auctionId);
}

bool IsAhBotCreatedAuction(uint32 auctionId)
{
    return s_ahBotCreatedAuctionIds.find(auctionId) != s_ahBotCreatedAuctionIds.end();
}

void UntrackAhBotCreatedAuction(uint32 auctionId)
{
    s_ahBotCreatedAuctionIds.erase(auctionId);
}
}
