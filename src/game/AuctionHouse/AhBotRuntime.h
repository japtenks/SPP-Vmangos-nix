#ifndef AHBOT_RUNTIME_H
#define AHBOT_RUNTIME_H

#include "AuctionHouseMgr.h"

class Item;
class Player;
class ItemPrototype;

namespace AhBotRuntime
{
bool TryPlaceAhBotBid(AuctionEntry* auction, uint32 bidPrice);
bool TryPlaceAhBotBuyout(AuctionEntry* auction);
Player* SelectAhBotSeller(ItemPrototype const* prototype, uint32 count, AuctionHouseEntry const* ahEntry);
bool CreateAhBotStockAuction(Player* seller, Item* item, uint32 bidPrice, uint32 buyoutPrice, uint32 auctionTime, AuctionHouseEntry const* ahEntry);
void TrackAhBotCreatedAuction(uint32 auctionId);
bool IsAhBotCreatedAuction(uint32 auctionId);
void UntrackAhBotCreatedAuction(uint32 auctionId);
}

#endif
