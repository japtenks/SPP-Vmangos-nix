
#include "playerbot/playerbot.h"
#include "playerbot/LootObjectStack.h"
#include "ChooseTravelTargetAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerSharedKnowledge.h"
#include "playerbot/ServerSocialMgr.h"
#include "playerbot/strategy/values/QuestValues.h"
#include "playerbot/strategy/values/TravelValues.h"
#include "playerbot/strategy/values/SharedValueContext.h"
#include "playerbot/strategy/values/GuildValues.h"
#include "GuildMgr.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <unordered_set>

using namespace ai;

namespace
{
    constexpr char kDecisionTracePrefix[] = "[PBTRACE]";

    uint8 GetStarterZoneThreshold(uint32 zoneId, uint32 areaId)
    {
        const uint32 id = zoneId ? zoneId : areaId;
        switch (id)
        {
            case 12:   // Elwynn Forest / Northshire
            case 40:
            case 1:    // Dun Morogh / Coldridge Valley
            case 38:
            case 141:  // Teldrassil / Shadowglen
            case 148:
            case 14:   // Durotar / Valley of Trials
            case 17:
            case 85:   // Tirisfal Glades / Deathknell
            case 130:
            case 215:  // Mulgore / Camp Narache
                return 6;
            default:
                return 0;
        }
    }

    enum class StarterCluster : uint8
    {
        None = 0,
        AllianceEastern,
        NightElf,
        OrcTroll,
        Tauren,
        Undead
    };

    StarterCluster GetStarterCluster(uint8 race, uint32 zoneId, uint32 areaId)
    {
        const uint32 id = zoneId ? zoneId : areaId;

        switch (race)
        {
            case RACE_HUMAN:
                return (id == 12 || id == 40) ? StarterCluster::AllianceEastern : StarterCluster::None;
            case RACE_DWARF:
                return (id == 1 || id == 38) ? StarterCluster::AllianceEastern : StarterCluster::None;
            case RACE_NIGHTELF:
                return (id == 141 || id == 148) ? StarterCluster::NightElf : StarterCluster::None;
            case RACE_ORC:
            case RACE_TROLL:
                return (id == 14 || id == 17) ? StarterCluster::OrcTroll : StarterCluster::None;
            case RACE_TAUREN:
                return id == 215 ? StarterCluster::Tauren : StarterCluster::None;
            case RACE_UNDEAD:
                return (id == 85 || id == 130) ? StarterCluster::Undead : StarterCluster::None;
            default:
                return StarterCluster::None;
        }
    }

    bool IsStarterRepTraveler(Player* bot)
    {
        if (!bot)
            return false;

        return (bot->GetGUIDLow() % 100) < 2;
    }

    bool IsStarterZoneForRace(uint8 race, uint32 zoneId, uint32 areaId)
    {
        switch (race)
        {
            case RACE_HUMAN:     return zoneId == 12 || areaId == 12 || zoneId == 40 || areaId == 40;
            case RACE_DWARF:     return zoneId == 1 || areaId == 1 || zoneId == 38 || areaId == 38;
            case RACE_NIGHTELF:  return zoneId == 141 || areaId == 141 || zoneId == 148 || areaId == 148;
            case RACE_ORC:
            case RACE_TROLL:     return zoneId == 14 || areaId == 14 || zoneId == 17 || areaId == 17;
            case RACE_UNDEAD:    return zoneId == 85 || areaId == 85 || zoneId == 130 || areaId == 130;
            case RACE_TAUREN:    return zoneId == 215 || areaId == 215;
            default:             return false;
        }
    }

    bool IsBotInStarterZone(Player* bot)
    {
        if (!bot)
            return false;

        const uint32 zoneId = bot->GetZoneId();
        const uint32 areaId = sServerFacade.GetAreaId(bot);
        const uint8 threshold = GetStarterZoneThreshold(zoneId, areaId);
        if (!threshold || bot->GetLevel() > threshold)
            return false;

        return IsStarterZoneForRace(bot->GetRace(), zoneId, areaId);
    }

    StarterCluster GetDestinationStarterCluster(uint32 zoneId, uint32 areaId)
    {
        const uint32 id = zoneId ? zoneId : areaId;
        switch (id)
        {
            case 12:
            case 40:
            case 1:
            case 38:
                return StarterCluster::AllianceEastern;
            case 141:
            case 148:
                return StarterCluster::NightElf;
            case 14:
            case 17:
                return StarterCluster::OrcTroll;
            case 215:
                return StarterCluster::Tauren;
            case 85:
            case 130:
                return StarterCluster::Undead;
            default:
                return StarterCluster::None;
        }
    }

    bool IsDestinationInAllowedStarterCluster(Player* bot, WorldPosition const* destination)
    {
        if (!bot || !destination)
            return false;

        uint32 zoneId = 0;
        uint32 areaId = 0;
        sTerrainMgr.GetZoneAndAreaId(zoneId, areaId, destination->getMapId(), destination->getX(), destination->getY(), destination->getZ());
        if (IsStarterZoneForRace(bot->GetRace(), zoneId, areaId))
            return true;

        if (!IsStarterRepTraveler(bot))
            return false;

        const StarterCluster botCluster = GetStarterCluster(bot->GetRace(), bot->GetZoneId(), sServerFacade.GetAreaId(bot));
        const StarterCluster destinationCluster = GetDestinationStarterCluster(zoneId, areaId);
        return botCluster != StarterCluster::None && botCluster == destinationCluster;
    }

    std::string FormatTravelPurpose(TravelDestinationPurpose purpose)
    {
        auto itr = TravelDestinationPurposeName.find(purpose);
        if (itr != TravelDestinationPurposeName.end())
            return itr->second;

        return std::to_string(static_cast<uint32>(purpose));
    }

    std::string FormatCommittedTaskSummary(PlayerbotAI* ai)
    {
        if (!ai)
            return "task=none";

        CommittedTask const& committedTask = ai->GetCommittedTask();
        std::ostringstream out;
        out << "task={purpose=" << FormatTravelPurpose(committedTask.purpose)
            << ",quest=" << committedTask.questId
            << ",entry=" << committedTask.destinationEntry
            << ",obj=" << static_cast<uint32>(committedTask.objectiveIndex)
            << ",tier=" << InterruptTierToString(committedTask.GetInterruptTier())
            << ",valid=" << (committedTask.isValid ? "yes" : "no")
            << ",retry=" << static_cast<uint32>(committedTask.retryCount)
            << "}";
        return out.str();
    }

    std::string FormatDecisionState(PlayerbotAI* ai)
    {
        if (!ai)
            return "archetype=unknown session=unknown";

        BotSession const& session = ai->GetSession();
        std::ostringstream out;
        out << "archetype=" << BotArchetypeToString(ai->GetArchetype())
            << " session=" << SessionStateToString(session.state)
            << (session.isPaused ? "(paused)" : "");
        return out.str();
    }

    void TellTravelTrace(PlayerbotAI* ai, Player* requester, const std::string& text)
    {
        if (!ai)
            return;

        ai->TellDebug(requester ? requester : ai->GetMaster(), std::string(kDecisionTracePrefix) + " " + text, "debug travel");
    }

    std::string FormatBool(bool value)
    {
        return value ? "yes" : "no";
    }

    std::string FormatQuestStatusLabel(QuestStatus status)
    {
        switch (status)
        {
            case QUEST_STATUS_NONE:
                return "none";
            case QUEST_STATUS_COMPLETE:
                return "complete";
            case QUEST_STATUS_INCOMPLETE:
                return "incomplete";
            case QUEST_STATUS_FAILED:
                return "failed";
            default:
                return std::to_string(static_cast<uint32>(status));
        }
    }

    std::string DescribeQuest(Player* bot, uint32 questId)
    {
        std::ostringstream out;
        Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
        out << questId;
        if (quest)
            out << ":" << quest->GetTitle();
        out << "{status=" << FormatQuestStatusLabel(bot->GetQuestStatus(questId))
            << ",rewarded=" << FormatBool(bot->GetQuestRewardStatus(questId)) << "}";
        return out.str();
    }

    void LogStarterQuestState(PlayerbotAI* ai, Player* requester, Player* bot, const std::string& label)
    {
        if (!ai || !bot || !IsBotInStarterZone(bot))
            return;

        std::vector<std::string> activeQuests;
        QuestStatusMap& questStatusMap = bot->GetQuestStatusMap();
        for (const auto& [questId, questStatus] : questStatusMap)
        {
            if (questStatus.m_rewarded)
                continue;

            activeQuests.push_back(DescribeQuest(bot, questId));
        }

        if (activeQuests.empty())
        {
            TellTravelTrace(ai, requester, "starter_quest_diag " + label + " active_quests=none");
            return;
        }

        TellTravelTrace(ai, requester, "starter_quest_diag " + label + " active_quests=" + Qualified::MultiQualify(activeQuests, " | "));
    }

    std::string DescribeQuestCandidate(Player* bot, QuestRelationTravelDestination* destination,
        WorldPosition const* position, const PlayerTravelInfo& travelInfo, const std::string& source)
    {
        std::ostringstream out;
        Quest const* quest = destination ? sObjectMgr.GetQuestTemplate(destination->GetQuestId()) : nullptr;
        WorldPosition const* closestPoint = destination ? destination->GetClosestPoint(bot) : nullptr;
        const bool possible = destination && destination->IsPossible(travelInfo);
        const bool active = destination && destination->IsActive(bot, travelInfo);
        const bool inStarterCluster = position && IsDestinationInAllowedStarterCluster(bot, position);
        std::string canTakeQuest = "n/a";

        if (destination && destination->GetRelation() == 0 && quest)
        {
            if (closestPoint && closestPoint->getMapId() == bot->GetMapId())
                canTakeQuest = FormatBool(bot->CanTakeQuest(quest, false));
            else
                canTakeQuest = "map-mismatch";
        }

        const int32 prevQuestId = quest ? quest->GetPrevQuestId() : 0;
        std::string prevQuestState = "-";
        if (prevQuestId)
        {
            const uint32 requiredQuestId = static_cast<uint32>(std::abs(prevQuestId));
            prevQuestState = DescribeQuest(bot, requiredQuestId);
        }

        out << "starter_quest_diag " << source
            << " entry=" << (destination ? destination->GetEntry() : 0)
            << " title=" << (destination ? destination->GetTitle() : "<null>")
            << " questId=" << (destination ? destination->GetQuestId() : 0)
            << " relation=" << (destination ? destination->GetRelation() : 0)
            << " active=" << FormatBool(active)
            << " possible=" << FormatBool(possible)
            << " canTakeQuest=" << canTakeQuest
            << " inStarterCluster=" << FormatBool(inStarterCluster)
            << " prevQuestId=" << prevQuestId
            << " prevQuestState=" << prevQuestState;

        if (position)
            out << " distance=" << std::fixed << std::setprecision(1) << position->distance(bot);

        return out.str();
    }

    void LogStarterQuestNoTargetDiagnostics(PlayerbotAI* ai, Player* requester, Player* bot,
        PartitionedTravelList const& partitionedList)
    {
        if (!ai || !bot || !IsBotInStarterZone(bot))
            return;

        const PlayerTravelInfo travelInfo(bot);
        LogStarterQuestState(ai, requester, bot, "no_target");

        std::unordered_set<std::string> seenFetchedCandidates;
        size_t fetchedCount = 0;
        size_t fetchedOverflow = 0;
        for (const auto& [partition, points] : partitionedList)
        {
            (void)partition;

            for (const auto& [destination, position, distance] : points)
            {
                (void)distance;

                QuestRelationTravelDestination* questDestination = dynamic_cast<QuestRelationTravelDestination*>(destination);
                if (!questDestination || questDestination->GetRelation() != 0 || !position || !IsDestinationInAllowedStarterCluster(bot, position))
                    continue;

                const std::string key = std::to_string(questDestination->GetEntry()) + ":" + std::to_string(questDestination->GetQuestId());
                if (!seenFetchedCandidates.insert(key).second)
                    continue;

                if (fetchedCount < 12)
                {
                    TellTravelTrace(ai, requester, DescribeQuestCandidate(bot, questDestination, position, travelInfo, "fetched_candidate"));
                    ++fetchedCount;
                }
                else
                {
                    ++fetchedOverflow;
                }
            }
        }

        if (!fetchedCount)
            TellTravelTrace(ai, requester, "starter_quest_diag fetched_candidate none");
        else if (fetchedOverflow)
            TellTravelTrace(ai, requester, "starter_quest_diag fetched_candidate overflow=" + std::to_string(fetchedOverflow));

        DestinationList localQuestGivers = sTravelMgr.GetDestinations(travelInfo, (uint32)TravelDestinationPurpose::QuestGiver, {}, false, 0.0f);
        std::vector<std::pair<float, QuestRelationTravelDestination*>> localCandidates;
        std::unordered_set<std::string> seenLocalCandidates;
        for (TravelDestination* destination : localQuestGivers)
        {
            QuestRelationTravelDestination* questDestination = dynamic_cast<QuestRelationTravelDestination*>(destination);
            if (!questDestination || questDestination->GetRelation() != 0)
                continue;

            WorldPosition const* position = questDestination->GetClosestPoint(bot);
            if (!position || !IsDestinationInAllowedStarterCluster(bot, position))
                continue;

            const std::string key = std::to_string(questDestination->GetEntry()) + ":" + std::to_string(questDestination->GetQuestId());
            if (!seenLocalCandidates.insert(key).second)
                continue;

            localCandidates.push_back({ position->distance(bot), questDestination });
        }

        std::sort(localCandidates.begin(), localCandidates.end(), [](const auto& left, const auto& right)
        {
            return left.first < right.first;
        });

        if (localCandidates.empty())
        {
            TellTravelTrace(ai, requester, "starter_quest_diag local_candidate none");
            return;
        }

        size_t loggedLocalCandidates = 0;
        for (const auto& [distance, destination] : localCandidates)
        {
            if (loggedLocalCandidates >= 12)
                break;

            (void)distance;
            TellTravelTrace(ai, requester,
                DescribeQuestCandidate(bot, destination, destination->GetClosestPoint(bot), travelInfo, "local_candidate"));
            ++loggedLocalCandidates;
        }

        if (localCandidates.size() > loggedLocalCandidates)
            TellTravelTrace(ai, requester, "starter_quest_diag local_candidate overflow=" + std::to_string(localCandidates.size() - loggedLocalCandidates));
    }

    enum class QuestIntentKind : uint8
    {
        None = 0,
        BroadQuestGiver,
        BlockedRemoteTurnIn,
        RemoteQuestTaker,
        RemoteObjective,
        ClosestQuestFallback,
        SameZoneQuestGiverContinuation,
        LocalObjective,
        StarterLocalPriority,
        NearbyQuestGiver,
        NearbyQuestTaker,
        LevelOneClosestQuest
    };

    struct QuestIntentFetch
    {
        QuestIntentKind kind = QuestIntentKind::None;
        uint32 purpose = 0;
        int32 questId = 0;
        float range = 0.0f;
    };

    struct QuestIntentContext
    {
        bool starterOrNewBot = false;
        bool levelOneNoQuest = false;
        bool hasUsableActiveQuestWork = false;
        bool hasUsableLocalQuestWork = false;
    };

    bool HasQuestIntent(Player* bot);
    float GetQuestHubBonus(Player* bot, TravelDestination* destination, EntryQuestRelationMap const& relationMap, std::vector<uint32> const& questIds);
    float GetQuestFollowOnBonus(Player* bot, TravelDestination* destination, EntryQuestRelationMap const& relationMap);
    float GetLocalQuestClusterScore(PlayerbotAI* ai, TravelDestination* destination, WorldPosition const* position);
    float GetQuestPriorityScore(PlayerbotAI* ai, TravelDestination* destination, std::unordered_map<uint32, float>& cache);

    bool IsStarterFirstChainQuest(Player* bot, uint32 questId)
    {
        if (!bot || !questId)
            return false;

        // Quests that require a special item or emote that bots cannot
        // autonomously satisfy — block regardless of race.
        static const std::unordered_set<uint32> kUnsatisfiableStarterQuests = {
            3861, // CLUCK! — requires Special Chicken Feed in bags
        };
        if (kUnsatisfiableStarterQuests.count(questId))
            return false;

        // Class/race seed note quests — valid for any race at level 1.
        static const std::unordered_set<uint32> kSeedNoteQuests = {
            3091, // Simple Note
            3093, // Rune-Inscribed Note
            3116, // Simple Sigil
            3117, // Etched Sigil
            3118, // Encrypted Sigil
            3119, // Hallowed Sigil
            3120, // Verdant Sigil
        };
        if (kSeedNoteQuests.count(questId))
            return true;

        switch (bot->GetRace())
        {
            // -----------------------------------------------------------------
            // HUMAN — Northshire Abbey + Elwynn Forest
            // -----------------------------------------------------------------
            case RACE_HUMAN:
                switch (questId)
                {
                    // Northshire
                    case 783:  // A Threat Within
                    case 7:    // Kobold Camp Cleanup
                    case 15:   // Investigate Echo Ridge
                    case 21:   // Skirmish at Echo Ridge
                    case 18:   // Brotherhood of Thieves
                    case 3903: // Milly Osworth
                    case 5261: // Eagan Peltskinner
                    case 6:    // Bounty on Garrick Padfoot
                    case 54:   // Report to Goldshire
                    // Elwynn Forest
                    case 16:   // Give Gerard a Drink
                    case 2158: // Rest and Relaxation
                    case 60:   // Kobold Candles
                    case 61:   // Shipment to Stormwind
                    case 47:   // Gold Dust Exchange
                    case 62:   // The Fargodeep Mine
                    case 76:   // The Jasperlode Mine
                    case 87:   // Goldtooth
                    case 83:   // Red Linen Goods
                    case 112:  // Collecting Kelp
                    case 114:  // The Escape
                    case 5545: // A Bundle of Trouble
                        return true;
                    default:
                        return false;
                }

            // -----------------------------------------------------------------
            // DWARF + GNOME — Coldridge Valley + Dun Morogh (shared zone)
            // -----------------------------------------------------------------
            case RACE_DWARF:
            case RACE_GNOME:
                switch (questId)
                {
                    // Coldridge Valley
                    case 179:  // Dwarven Outfitters
                    case 170:  // A New Threat
                    case 183:  // The Boar Hunter
                    case 233:  // Coldridge Valley Mail Delivery
                    case 234:  // Coldridge Valley Mail Delivery
                    case 182:  // The Troll Cave
                    case 218:  // The Stolen Journal
                    case 282:  // Senir's Observations
                    case 420:  // Senir's Observations
                    // Dun Morogh
                    case 403:  // Guarded Thunderbrew Barrel
                    case 2160: // Supplies to Tannok
                    case 400:  // Tools for Steelgrill
                    case 310:  // Bitter Rivals
                    case 317:  // Stocking Jetsteam
                    case 308:  // Distracting Jarven
                    case 311:  // Return to Marleth
                    case 318:  // Evershine
                    case 313:  // The Grizzled Den
                    case 319:  // A Favor for Evershine
                    case 320:  // Return to Bellowfiz
                    case 315:  // The Perfect Stout
                    case 432:  // Those Blasted Troggs!
                    case 291:  // The Reports
                        return true;
                    default:
                        return false;
                }

            // -----------------------------------------------------------------
            // NIGHT ELF — Shadowglen + Teldrassil
            // -----------------------------------------------------------------
            case RACE_NIGHTELF:
                switch (questId)
                {
                    // Shadowglen
                    case 458:  // The Woodland Protector
                    case 459:  // The Woodland Protector
                    case 456:  // The Balance of Nature
                    case 457:  // The Balance of Nature
                    // Teldrassil
                    case 929:  // Crown of the Earth
                    case 933:  // Crown of the Earth
                    case 2438: // The Emerald Dreamcatcher
                    case 475:  // A Troubling Breeze
                    case 476:  // Gnarlpine Corruption
                    case 488:  // Zenn's Bidding
                    case 997:  // Denalan's Earth
                    case 489:  // Seek Redemption!
                    case 918:  // Timberling Seeds
                    case 919:  // Timberling Sprouts
                    case 922:  // Rellian Greenspyre
                    case 932:  // Twisted Hatred
                    case 2459: // Ferocitas the Dream Eater
                    case 2541: // The Sleeping Druid
                    case 487:  // The Road to Darnassus
                        return true;
                    default:
                        return false;
                }

            // -----------------------------------------------------------------
            // UNDEAD — Deathknell + Tirisfal Glades
            // -----------------------------------------------------------------
            case RACE_UNDEAD:
                switch (questId)
                {
                    // Deathknell
                    case 363:  // Rude Awakening
                    case 364:  // The Mindless Ones
                    case 380:  // Night Web's Hollow
                    case 381:  // The Scarlet Crusade
                    // Tirisfal Glades
                    case 5481: // Gordo's Task
                    case 404:  // A Putrid Task
                    case 5482: // Doom Weed
                    case 361:  // A Letter Undelivered
                    case 365:  // Fields of Grief
                    case 407:  // Fields of Grief
                    case 374:  // Proof of Demise
                    case 358:  // Graverobbers
                    case 366:  // Return the Book
                    case 405:  // The Prodigal Lich
                    case 427:  // At War With The Scarlet Crusade
                    case 370:  // At War With The Scarlet Crusade
                    case 371:  // At War With The Scarlet Crusade
                        return true;
                    default:
                        return false;
                }

            // -----------------------------------------------------------------
            // TAUREN — Camp Narache + Mulgore
            // -----------------------------------------------------------------
            case RACE_TAUREN:
                switch (questId)
                {
                    // Camp Narache
                    case 747:  // The Hunt Begins
                    case 752:  // A Humble Task
                    case 750:  // The Hunt Continues
                    case 753:  // A Humble Task
                    case 755:  // Rites of the Earthmother
                    case 757:  // Rite of Strength
                    case 780:  // The Battleboars
                    case 781:  // Attack on Camp Narache
                    // Mulgore
                    case 745:  // Sharing the Land
                    case 748:  // Poison Water
                    case 767:  // Rite of Vision
                    case 771:  // Rite of Vision
                    case 772:  // Rite of Vision
                    case 754:  // Winterhoof Cleansing
                    case 761:  // Swoop Hunting
                    case 756:  // Thunderhorn Totem
                    case 758:  // Thunderhorn Cleansing
                    case 743:  // Dangers of the Windfury
                    case 749:  // The Ravaged Caravan
                    case 751:  // The Ravaged Caravan
                    case 766:  // Mazzranache
                    case 773:  // Rite of Wisdom
                    case 775:  // Journey into Thunder Bluff
                        return true;
                    default:
                        return false;
                }

            // -----------------------------------------------------------------
            // ORC + TROLL — Valley of Trials + Durotar
            // -----------------------------------------------------------------
            case RACE_ORC:
            case RACE_TROLL:
                switch (questId)
                {
                    // Valley of Trials
                    case 788:  // Cutting Teeth
                    case 789:  // Sting of the Scorpid
                    case 792:  // Vile Familiars
                    case 794:  // Burning Blade Medallion
                    case 5441: // Lazy Peons
                    // Durotar
                    case 2161: // A Peon's Burden
                    case 784:  // Vanquish the Betrayers
                    case 791:  // Carry Your Weight
                    case 823:  // Report to Orgnil
                    case 818:  // A Solvent Spirit
                    case 825:  // From The Wreckage....
                    case 785:  // A Strategic Alliance
                    case 786:  // Thwarting Kolkar Aggression
                    case 817:  // Practical Prey
                    case 808:  // Minshina's Skull
                    case 826:  // Zalazane
                        return true;
                    default:
                        return false;
                }

            default:
                return false;
        }
    }

    std::string FormatQuestIntentKind(QuestIntentKind kind)
    {
        switch (kind)
        {
            case QuestIntentKind::LevelOneClosestQuest: return "level_one_closest_quest";
            case QuestIntentKind::NearbyQuestTaker: return "nearby_quest_taker";
            case QuestIntentKind::NearbyQuestGiver: return "nearby_quest_giver";
            case QuestIntentKind::StarterLocalPriority: return "starter_local_priority";
            case QuestIntentKind::LocalObjective: return "local_objective";
            case QuestIntentKind::SameZoneQuestGiverContinuation: return "same_zone_local_questgiver";
            case QuestIntentKind::ClosestQuestFallback: return "closest_acceptable_quest";
            case QuestIntentKind::RemoteObjective: return "remote_objective";
            case QuestIntentKind::RemoteQuestTaker: return "remote_quest_taker";
            case QuestIntentKind::BlockedRemoteTurnIn: return "blocked_remote_turnin";
            case QuestIntentKind::BroadQuestGiver: return "broad_questgiver";
            default: return "none";
        }
    }

    bool IsStarterOrNewQuestBot(Player* bot)
    {
        return bot && (IsBotInStarterZone(bot) || bot->GetLevel() <= 10);
    }

    // Quest stacking: new/starter bots scan a wider radius (150 y) for
    // nearby quest givers so they batch-collect before doing objectives.
    bool IsNearbyQuestDistance(float distance, bool starterOrNew = false)
    {
        const float threshold = starterOrNew ? 150.0f : 80.0f;
        return distance <= threshold;
    }

    bool IsSameZoneDestination(Player* bot, WorldPosition const* position)
    {
        if (!bot || !position || position->getMapId() != bot->GetMapId())
            return false;

        uint32 botZoneId = 0;
        uint32 botAreaId = 0;
        sTerrainMgr.GetZoneAndAreaId(botZoneId, botAreaId, bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        uint32 destinationZoneId = 0;
        uint32 destinationAreaId = 0;
        sTerrainMgr.GetZoneAndAreaId(destinationZoneId, destinationAreaId,
            position->getMapId(), position->getX(), position->getY(), position->getZ());

        const uint32 botId = botZoneId ? botZoneId : botAreaId;
        const uint32 destinationId = destinationZoneId ? destinationZoneId : destinationAreaId;
        return botId && botId == destinationId;
    }

    bool IsLocalQuestDestination(Player* bot, WorldPosition const* position)
    {
        if (!bot || !position || position->getMapId() != bot->GetMapId())
            return false;

        if (IsBotInStarterZone(bot) && IsDestinationInAllowedStarterCluster(bot, position))
            return true;

        return IsSameZoneDestination(bot, position);
    }

    bool IsAcceptableQuestGiver(Player* bot, QuestRelationTravelDestination* destination, PlayerTravelInfo const& travelInfo)
    {
        if (!bot || !destination || destination->GetPurpose() != TravelDestinationPurpose::QuestGiver)
            return false;

        // Block quests bots can never satisfy autonomously (item/emote-gated).
        static const std::unordered_set<uint32> kUnsatisfiableQuests = {
            3861, // CLUCK! -- requires Special Chicken Feed in inventory
        };
        if (kUnsatisfiableQuests.count(destination->GetQuestId()))
            return false;

        Quest const* quest = sObjectMgr.GetQuestTemplate(destination->GetQuestId());
        if (!quest)
            return false;

        if (bot->GetQuestStatus(destination->GetQuestId()) != QUEST_STATUS_NONE || bot->GetQuestRewardStatus(destination->GetQuestId()))
            return false;

        if (!destination->IsPossible(travelInfo))
            return false;

        if (bot->GetLevel() == 1 && !HasQuestIntent(bot) &&
            !IsStarterFirstChainQuest(bot, destination->GetQuestId()))
            return false;

        return bot->CanTakeQuest(quest, false);
    }

    bool IsFollowupQuestGiver(Player* bot, TravelDestination* destination, EntryQuestRelationMap const* relationMap)
    {
        return bot && destination && relationMap && GetQuestFollowOnBonus(bot, destination, *relationMap) > 0.0f;
    }

    bool IsBlockedRemoteTurnIn(PlayerbotAI* ai, QuestRelationTravelDestination* destination)
    {
        if (!ai || !destination || destination->GetPurpose() != TravelDestinationPurpose::QuestTaker)
            return false;

        AiObjectContext* context = ai->GetAiObjectContext();
        if (!context)
            return false;

        const std::string suffix = std::to_string(destination->GetQuestId()) + ":" + std::to_string(destination->GetEntry());
        const int blockedCount = context->GetValue<int>("manual int", "blocked remote turnin count::" + suffix)->Get();
        const time_t blockedUntil = context->GetValue<time_t>("manual time", "blocked remote turnin until::" + suffix)->Get();
        return blockedCount > 0 && blockedUntil > time(nullptr);
    }

    QuestIntentContext BuildQuestIntentContext(Player* bot, PlayerbotAI* ai, PartitionedTravelList const& partitionedList,
        EntryQuestRelationMap const* relationMap)
    {
        QuestIntentContext context;
        if (!bot || !ai)
            return context;

        (void)relationMap;

        context.starterOrNewBot = IsStarterOrNewQuestBot(bot);
        context.levelOneNoQuest = bot->GetLevel() == 1 && !HasQuestIntent(bot);

        const PlayerTravelInfo travelInfo(bot);
        for (const auto& [partition, points] : partitionedList)
        {
            (void)partition;

            for (const auto& [destination, position, distance] : points)
            {
                (void)distance;

                QuestRelationTravelDestination* relationDestination = dynamic_cast<QuestRelationTravelDestination*>(destination);
                QuestObjectiveTravelDestination* objectiveDestination = dynamic_cast<QuestObjectiveTravelDestination*>(destination);

                if (relationDestination && relationDestination->GetPurpose() == TravelDestinationPurpose::QuestTaker)
                {
                    Quest const* quest = sObjectMgr.GetQuestTemplate(relationDestination->GetQuestId());
                    if (quest && bot->CanRewardQuest(quest, false))
                    {
                        context.hasUsableActiveQuestWork = true;
                        if (position && IsLocalQuestDestination(bot, position))
                            context.hasUsableLocalQuestWork = true;
                        continue;
                    }
                }

                if (objectiveDestination && objectiveDestination->IsActive(bot, travelInfo) && objectiveDestination->IsPossible(travelInfo))
                {
                    context.hasUsableActiveQuestWork = true;
                    if (position && IsLocalQuestDestination(bot, position))
                        context.hasUsableLocalQuestWork = true;
                    continue;
                }

                if (relationDestination && relationDestination->GetPurpose() == TravelDestinationPurpose::QuestGiver &&
                    IsAcceptableQuestGiver(bot, relationDestination, travelInfo))
                {
                    if (position && IsLocalQuestDestination(bot, position))
                        context.hasUsableLocalQuestWork = true;
                }
            }
        }

        return context;
    }

    QuestIntentKind ClassifyQuestIntent(Player* bot, PlayerbotAI* ai, TravelDestination* destination, WorldPosition const* position,
        EntryQuestRelationMap const* relationMap, QuestIntentContext const& context)
    {
        if (!bot || !destination)
            return QuestIntentKind::None;

        const PlayerTravelInfo travelInfo(bot);
        const float distance = position ? position->distance(bot) : destination->DistanceTo(bot);
        const bool nearby = IsNearbyQuestDistance(distance, context.starterOrNewBot);
        const bool local = position && IsLocalQuestDestination(bot, position);

        if (QuestRelationTravelDestination* relationDestination = dynamic_cast<QuestRelationTravelDestination*>(destination))
        {
            if (relationDestination->GetPurpose() == TravelDestinationPurpose::QuestTaker)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(relationDestination->GetQuestId());
                if (!quest || !bot->CanRewardQuest(quest, false))
                    return QuestIntentKind::None;

                if (!local && IsBlockedRemoteTurnIn(ai, relationDestination))
                    return QuestIntentKind::BlockedRemoteTurnIn;

                return nearby || local ? QuestIntentKind::NearbyQuestTaker : QuestIntentKind::RemoteQuestTaker;
            }

            if (relationDestination->GetPurpose() == TravelDestinationPurpose::QuestGiver &&
                IsAcceptableQuestGiver(bot, relationDestination, travelInfo))
            {
                if (context.levelOneNoQuest && local)
                    return QuestIntentKind::LevelOneClosestQuest;

                if (nearby || local)
                    return QuestIntentKind::NearbyQuestGiver;

                if (IsFollowupQuestGiver(bot, destination, relationMap))
                    return QuestIntentKind::SameZoneQuestGiverContinuation;

                if (context.starterOrNewBot && !context.hasUsableLocalQuestWork && !context.hasUsableActiveQuestWork)
                    return QuestIntentKind::ClosestQuestFallback;

                return QuestIntentKind::BroadQuestGiver;
            }
        }

        if (QuestObjectiveTravelDestination* objectiveDestination = dynamic_cast<QuestObjectiveTravelDestination*>(destination))
        {
            if (!objectiveDestination->IsPossible(travelInfo) || !objectiveDestination->IsActive(bot, travelInfo))
                return QuestIntentKind::None;

            if (local && IsBotInStarterZone(bot))
                return QuestIntentKind::StarterLocalPriority;

            if (local)
                return QuestIntentKind::LocalObjective;

            return QuestIntentKind::RemoteObjective;
        }

        return QuestIntentKind::None;
    }

    // Chain-head bonus: when the bot has < 5 active quests, reward
    // quest-giver NPCs that offer the first quest in a chain (no
    // prerequisite, has a follow-on).  This seeds the "oh look shiny!"
    // behaviour -- bots gravitate to chain-entry NPCs when fresh to a zone.
    float GetChainHeadBonus(Player* bot, TravelDestination* destination,
        EntryQuestRelationMap const* relationMap)
    {
        if (!bot || !destination || !relationMap ||
            destination->GetPurpose() != TravelDestinationPurpose::QuestGiver)
            return 0.0f;

        // Count active (non-rewarded) quests in the log.
        uint32 activeQuestCount = 0;
        const QuestStatusMap& questStatusMap = bot->GetQuestStatusMap();
        for (const auto& [questId, status] : questStatusMap)
        {
            if (!status.m_rewarded &&
                (status.m_status == QUEST_STATUS_INCOMPLETE ||
                 status.m_status == QUEST_STATUS_COMPLETE))
                ++activeQuestCount;
        }

        // Only apply when the bot is light on quests.
        if (activeQuestCount >= 5)
            return 0.0f;

        const float scale = std::max(0.0f, (5.0f - float(activeQuestCount)) / 5.0f);

        auto entryItr = relationMap->find(destination->GetEntry());
        if (entryItr == relationMap->end())
            return 0.0f;

        float maxBonus = 0.0f;
        for (const auto& [questId, flags] : entryItr->second)
        {
            if (!(flags & static_cast<uint8>(TravelDestinationPurpose::QuestGiver)))
                continue;

            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest || !bot->CanTakeQuest(quest, false) ||
                bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
                continue;

            // Chain-head: no prerequisite but has a follow-on quest.
            if (quest->GetPrevQuestId() != 0)
                continue;

            if (!quest->GetNextQuestId() && !quest->GetNextQuestInChain())
                continue;

            maxBonus = std::max(maxBonus, 0.5f * scale);
        }

        return maxBonus;
    }

    float GetQuestIntentBaseScore(QuestIntentKind kind)
    {
        switch (kind)
        {
            case QuestIntentKind::LevelOneClosestQuest: return 12000.0f;
            case QuestIntentKind::NearbyQuestTaker: return 11000.0f;
            case QuestIntentKind::NearbyQuestGiver: return 10000.0f;
            case QuestIntentKind::StarterLocalPriority: return 9000.0f;
            case QuestIntentKind::LocalObjective: return 8000.0f;
            case QuestIntentKind::SameZoneQuestGiverContinuation: return 7000.0f;
            case QuestIntentKind::ClosestQuestFallback: return 6000.0f;
            case QuestIntentKind::RemoteObjective: return 5000.0f;
            case QuestIntentKind::RemoteQuestTaker: return 4000.0f;
            case QuestIntentKind::BroadQuestGiver: return 3000.0f;
            case QuestIntentKind::BlockedRemoteTurnIn: return 500.0f;
            default: return 0.0f;
        }
    }

    float GetQuestIntentScore(Player* bot, PlayerbotAI* ai, TravelDestination* destination, WorldPosition const* position,
        EntryQuestRelationMap const* relationMap, std::vector<uint32> const* questIds, QuestIntentContext const& context,
        std::unordered_map<uint32, float>& questPriorityCache)
    {
        if (!bot || !destination)
            return 0.0f;

        const QuestIntentKind kind = ClassifyQuestIntent(bot, ai, destination, position, relationMap, context);
        float score = GetQuestIntentBaseScore(kind);
        if (!score)
            return score;

        score += GetQuestPriorityScore(ai, destination, questPriorityCache);

        if (position)
        {
            score += std::max(0.0f, 150.0f - std::min(150.0f, position->distance(bot))) * 0.5f;
            score += GetLocalQuestClusterScore(ai, destination, position) * 100.0f;
        }

        if (relationMap && questIds)
        {
            score += GetQuestHubBonus(bot, destination, *relationMap, *questIds) * 100.0f;
            score += GetQuestFollowOnBonus(bot, destination, *relationMap) * 100.0f;
            score += GetChainHeadBonus(bot, destination, relationMap) * 100.0f;
        }
// [patch_quest_chain_stacking applied]

        return score;
    }

    void LogQuestIntentFetches(PlayerbotAI* ai, Player* requester, Player* bot, std::vector<QuestIntentFetch> const& fetches)
    {
        if (!ai || !bot || !IsBotInStarterZone(bot))
            return;

        for (QuestIntentFetch const& fetch : fetches)
        {
            std::ostringstream out;
            out << "starter_quest_diag request_fetch intent=" << FormatQuestIntentKind(fetch.kind)
                << " purpose=" << fetch.purpose;
            if (fetch.questId > 0)
                out << " quest=" << DescribeQuest(bot, static_cast<uint32>(fetch.questId));
            else
                out << " quest=none";
            out << " range=" << std::fixed << std::setprecision(1) << fetch.range;
            TellTravelTrace(ai, requester, out.str());
        }
    }

    bool HasPendingTravelDestinations(FutureDestinations* futureDestinations)
    {
        // All travel futures use std::launch::deferred (LaunchTravelDestinations).
        // For deferred futures, wait_for() returns future_status::deferred (not
        // future_status::ready) until after .get() is called.  The old check
        //   != ready
        // was therefore always true, making HasPendingTravelDestinations() return
        // true for every deferred future — even orphaned ones never consumed after
        // a combat interrupt cleared TRAVEL_STATUS_PREPARE before
        // ChooseTravelTargetAction could call .get().
        //
        // Correct check: only block re-launch for futures that are genuinely
        // in-flight on another thread (future_status::timeout).  Deferred futures
        // are synchronous-on-demand and always instantly consumable; they must
        // NOT prevent a new fetch from being launched.
        return futureDestinations &&
            futureDestinations->valid() &&
            futureDestinations->wait_for(std::chrono::seconds(0)) == std::future_status::timeout;
    }

    template <typename Work>
    FutureDestinations LaunchTravelDestinations(Work work)
    {
        // Travel requests are high-frequency across many bots. Deferred launch
        // keeps the single pending calculation model without spawning a fresh
        // OS thread per request.
        return std::async(std::launch::deferred, work);
    }

    bool IsQuestPurpose(TravelDestinationPurpose purpose)
    {
        switch (purpose)
        {
            case TravelDestinationPurpose::QuestGiver:
            case TravelDestinationPurpose::QuestObjective1:
            case TravelDestinationPurpose::QuestObjective2:
            case TravelDestinationPurpose::QuestObjective3:
            case TravelDestinationPurpose::QuestObjective4:
            case TravelDestinationPurpose::QuestTaker:
                return true;
            default:
                return false;
        }
    }

    float GetQuestArchetypePriorityMultiplier(Player* bot, PlayerbotAI* ai, TravelDestinationPurpose purpose, const Quest* quest)
    {
        if (!bot || !ai)
            return 1.0f;

        const ArchetypeWeights& weights = ai->GetArchetypeWeights();
        float multiplier = 1.0f;

        switch (purpose)
        {
            case TravelDestinationPurpose::QuestGiver:
                multiplier *= weights.chainWeight;
                break;
            case TravelDestinationPurpose::QuestObjective1:
            case TravelDestinationPurpose::QuestObjective2:
            case TravelDestinationPurpose::QuestObjective3:
            case TravelDestinationPurpose::QuestObjective4:
                multiplier *= weights.progressWeight;
                break;
            case TravelDestinationPurpose::QuestTaker:
                multiplier *= (weights.progressWeight + weights.chainWeight) * 0.5f;
                break;
            default:
                break;
        }

        if (quest)
        {
            const int32 questLevel = std::max<int32>(quest->GetQuestLevel(), quest->GetMinLevel());
            const float levelDelta = std::fabs(float(bot->GetLevel()) - float(questLevel));
            multiplier *= std::max(0.25f, weights.levelWeight - (levelDelta * 0.03f));

            if (quest->GetZoneOrSort() > 0)
                multiplier *= weights.zoneWeight;

            if (quest->GetNextQuestId() || quest->GetNextQuestInChain())
                multiplier *= weights.chainWeight;
        }

        return std::max(0.25f, multiplier);
    }

    bool IsMaintenancePurpose(TravelDestinationPurpose purpose)
    {
        switch (purpose)
        {
            case TravelDestinationPurpose::Vendor:
            case TravelDestinationPurpose::AH:
            case TravelDestinationPurpose::Repair:
            case TravelDestinationPurpose::Mail:
            case TravelDestinationPurpose::Trainer:
                return true;
            default:
                return false;
        }
    }

    bool IsLowPriorityPurpose(TravelDestinationPurpose purpose)
    {
        switch (purpose)
        {
            case TravelDestinationPurpose::GenericRpg:
            case TravelDestinationPurpose::Explore:
            case TravelDestinationPurpose::Grind:
            case TravelDestinationPurpose::GatherFishing:
            case TravelDestinationPurpose::GatherHerbalism:
            case TravelDestinationPurpose::GatherMining:
            case TravelDestinationPurpose::GatherSkinning:
                return true;
            default:
                return false;
        }
    }

    bool HasNearbyQuestContinuation(Player* bot, float maxDistance = 900.0f)
    {
        if (!bot)
            return false;

        PlayerTravelInfo info(bot);
        const uint32 questPurposes =
            (uint32)TravelDestinationPurpose::QuestGiver |
            (uint32)TravelDestinationPurpose::QuestTaker |
            (uint32)TravelDestinationPurpose::QuestObjective1 |
            (uint32)TravelDestinationPurpose::QuestObjective2 |
            (uint32)TravelDestinationPurpose::QuestObjective3 |
            (uint32)TravelDestinationPurpose::QuestObjective4;

        for (TravelDestination* destination : sTravelMgr.GetDestinations(info, questPurposes, {}, false, maxDistance))
        {
            if (!destination)
                continue;

            const float distance = destination->DistanceTo(bot);
            if (distance == FLT_MAX || distance > maxDistance)
                continue;

            if (destination->IsPossible(info))
                return true;
        }

        return false;
    }

    bool HasQuestIntent(Player* bot)
    {
        if (!bot)
            return false;

        QuestStatusMap& questMap = bot->GetQuestStatusMap();
        for (auto const& [questId, questStatus] : questMap)
        {
            if (!questId || questStatus.m_rewarded)
                continue;

            return true;
        }

        return false;
    }

    bool ShouldSuppressGenericRpg(Player* bot)
    {
        if (!bot)
            return false;

        if (IsBotInStarterZone(bot))
            return true;

        if (HasQuestIntent(bot))
            return true;

        if (bot->GetLevel() <= 10 && HasNearbyQuestContinuation(bot))
            return true;

        return HasNearbyQuestContinuation(bot, 2500.0f);
    }

    bool SessionAllowsPurpose(SessionState state, TravelDestinationPurpose purpose)
    {
        switch (state)
        {
            case SessionState::IDLE:
            case SessionState::TRAVELLING:
                return true;
            case SessionState::QUESTING:
                return IsQuestPurpose(purpose);
            case SessionState::MAINTENANCE:
                return IsMaintenancePurpose(purpose);
            case SessionState::DUNGEON_RUN:
                return purpose == TravelDestinationPurpose::Boss;
            case SessionState::REP_FARMING:
            case SessionState::TOURNAMENT:
            case SessionState::RARE_HUNTING:
            case SessionState::CRAFTING_COOLDOWN:
            case SessionState::WORLD_PVP:
                return IsLowPriorityPurpose(purpose);
            default:
                return false;
        }
    }

    SessionState GetSessionStateForPurpose(TravelDestinationPurpose purpose)
    {
        switch (purpose)
        {
            case TravelDestinationPurpose::QuestGiver:
            case TravelDestinationPurpose::QuestObjective1:
            case TravelDestinationPurpose::QuestObjective2:
            case TravelDestinationPurpose::QuestObjective3:
            case TravelDestinationPurpose::QuestObjective4:
            case TravelDestinationPurpose::QuestTaker:
                return SessionState::QUESTING;
            case TravelDestinationPurpose::Vendor:
            case TravelDestinationPurpose::AH:
            case TravelDestinationPurpose::Repair:
            case TravelDestinationPurpose::Mail:
            case TravelDestinationPurpose::Trainer:
                return SessionState::MAINTENANCE;
            case TravelDestinationPurpose::Boss:
                return SessionState::DUNGEON_RUN;
            case TravelDestinationPurpose::None:
                return SessionState::IDLE;
            default:
                return SessionState::TRAVELLING;
        }
    }

    uint32 GetTravelTargetQuestId(const TravelTarget* target)
    {
        if (!target || !target->GetDestination())
            return 0;

        if (QuestTravelDestination* questDestination = dynamic_cast<QuestTravelDestination*>(target->GetDestination()))
            return questDestination->GetQuestId();

        return 0;
    }

    uint8 GetTravelTargetObjectiveIndex(const TravelTarget* target)
    {
        if (!target || !target->GetDestination())
            return 0;

        if (QuestObjectiveTravelDestination* objectiveDestination = dynamic_cast<QuestObjectiveTravelDestination*>(target->GetDestination()))
            return objectiveDestination->GetObjective();

        return 0;
    }

    uint32 GetKnowledgeCityId(Player* bot)
    {
        if (!bot)
            return 0;

        AreaTableEntry const* areaEntry = GetAreaEntryByAreaID(sServerFacade.GetAreaId(bot));
        while (areaEntry && areaEntry->ZoneId)
        {
            AreaTableEntry const* parentArea = GetAreaEntryByAreaID(areaEntry->ZoneId);
            if (!parentArea || parentArea == areaEntry)
                break;

            areaEntry = parentArea;
        }

        return areaEntry ? areaEntry->Id : bot->GetZoneId();
    }

    uint32 GetReagentRequirement(Player* bot, uint32 itemId)
    {
        if (!bot || !itemId)
            return 1;

        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(itemId);
        if (!proto)
            return 1;

        static const uint32 trackedSkills[] =
        {
            SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_COOKING, SKILL_ENCHANTING,
            SKILL_ENGINEERING, SKILL_FIRST_AID, SKILL_LEATHERWORKING, SKILL_TAILORING
        };

        for (uint32 skillId : trackedSkills)
        {
            if (bot->HasSkill(skillId) && ItemUsageValue::IsItemUsedBySkill(proto, static_cast<SkillType>(skillId)))
                return skillId;
        }

        return 1;
    }

    float GetNpcPreferenceScore(Player* bot, float knowledgeConfidence, uint32 purpose)
    {
        if (!bot || !bot->GetPlayerbotAI())
            return knowledgeConfidence;

        const ArchetypeWeights& weights = bot->GetPlayerbotAI()->GetArchetypeWeights();
        const float knowledgeMaturity = sServerSharedKnowledge.GetKnowledgeMaturity(purpose);
        const float routineBias = 0.75f + (0.25f * weights.routineTolerance);
        const float explorationBias = 0.60f + (0.40f * weights.explorationRadiusBias);
        const float knowledgeBonus = knowledgeConfidence * weights.knowledgeWeight * routineBias * (0.35f + (0.65f * knowledgeMaturity));

        if (knowledgeConfidence > 0.0f)
            return knowledgeBonus;

        return 0.10f * weights.curiosityWeight * explorationBias * (1.0f - (0.70f * knowledgeMaturity));
    }

    float GetQuestDestinationScore(Player* bot, TravelDestination* destination, uint32 mapId, uint32 cityId, std::vector<uint32> const& questIds)
    {
        if (!destination)
            return 0.0f;

        const int32 entry = destination->GetEntry();
        if (entry <= 0 || questIds.empty())
            return 0.0f;

        float knowledge = 0.0f;
        uint32 purpose = 0;
        switch (destination->GetPurpose())
        {
            case TravelDestinationPurpose::QuestGiver:
                purpose = static_cast<uint32>(NpcKnowledgePurpose::QUEST_GIVER);
                knowledge = sServerSharedKnowledge.GetQuestGiverConfidence(static_cast<uint32>(entry), questIds, mapId, cityId);
                break;
            case TravelDestinationPurpose::QuestTaker:
                purpose = static_cast<uint32>(NpcKnowledgePurpose::QUEST_TAKER);
                knowledge = sServerSharedKnowledge.GetQuestTakerConfidence(static_cast<uint32>(entry), questIds, mapId, cityId);
                break;
            default:
                return 0.0f;
        }

        return GetNpcPreferenceScore(bot, knowledge, purpose);
    }

    float GetSocialDestinationScore(Player* bot, TravelDestination* destination)
    {
        if (!bot || !destination || destination->GetPurpose() != TravelDestinationPurpose::GenericRpg)
            return 0.0f;

        const ArchetypeWeights& weights = bot->GetPlayerbotAI()->GetArchetypeWeights();
        const float archetypeBias = weights.curiosityWeight * (0.65f + (0.35f * weights.explorationRadiusBias));
        const uint32 areaId = sServerSocialMgr.NormalizeAreaId(bot);

        float score = 0.0f;
        EntryTravelDestination* entryDestination = dynamic_cast<EntryTravelDestination*>(destination);
        if (entryDestination && entryDestination->GetCreatureInfo())
        {
            const uint32 npcFlags = entryDestination->GetCreatureInfo()->npc_flags;
            if (npcFlags & UNIT_NPC_FLAG_INNKEEPER)
                score += 0.85f;
            if (npcFlags & UNIT_NPC_FLAG_FLIGHTMASTER)
                score += 0.55f;
            if (npcFlags & UNIT_NPC_FLAG_GOSSIP)
                score += 0.30f;
            if (npcFlags & UNIT_NPC_FLAG_VENDOR)
                score += 0.24f;
            if (npcFlags & UNIT_NPC_FLAG_BANKER)
                score += 0.12f;
        }
        else
        {
            score += 0.12f;
        }

        if (bot->GetGuildId())
            score += 0.50f * sServerSocialMgr.GetGuildAreaAlignment(bot->GetGuildId(), areaId);

        if (sServerSocialMgr.GetKnownContactCount(bot->GetObjectGuid().GetRawValue()) < 3 && !bot->GetGuildId())
            score *= 0.55f;

        BotSession const& session = bot->GetPlayerbotAI()->GetSession();
        if (session.state == SessionState::QUESTING || session.state == SessionState::MAINTENANCE)
            score *= 0.45f;
        else if (session.state == SessionState::IDLE)
            score *= 1.20f;

        return score * archetypeBias;
    }

    uint32 CountDestinationQuestTurnIns(Player* bot, TravelDestination* destination, EntryQuestRelationMap const& relationMap, std::vector<uint32> const& questIds)
    {
        if (!bot || !destination || destination->GetPurpose() != TravelDestinationPurpose::QuestTaker)
            return 0;

        auto entryItr = relationMap.find(destination->GetEntry());
        if (entryItr == relationMap.end())
            return 0;

        uint32 count = 0;
        for (uint32 questId : questIds)
        {
            if (!questId)
                continue;

            auto relationItr = entryItr->second.find(questId);
            if (relationItr == entryItr->second.end())
                continue;

            if (!(relationItr->second & static_cast<uint8>(TravelDestinationPurpose::QuestTaker)))
                continue;

            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            if (quest && bot->CanRewardQuest(quest, false))
                ++count;
        }

        return count;
    }

    float GetQuestHubBonus(Player* bot, TravelDestination* destination, EntryQuestRelationMap const& relationMap, std::vector<uint32> const& questIds)
    {
        const uint32 turnInCount = CountDestinationQuestTurnIns(bot, destination, relationMap, questIds);
        if (!turnInCount)
            return 0.0f;

        const float cappedTurnIns = std::min<float>(5.0f, static_cast<float>(turnInCount));
        return 0.18f * cappedTurnIns;
    }

    float GetQuestFollowOnBonus(Player* bot, TravelDestination* destination, EntryQuestRelationMap const& relationMap)
    {
        if (!bot || !destination || destination->GetPurpose() != TravelDestinationPurpose::QuestGiver)
            return 0.0f;

        auto entryItr = relationMap.find(destination->GetEntry());
        if (entryItr == relationMap.end())
            return 0.0f;

        uint32 followOnCount = 0;
        for (const auto& [questId, flags] : entryItr->second)
        {
            if (!(flags & static_cast<uint8>(TravelDestinationPurpose::QuestGiver)))
                continue;

            Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
            if (!quest || !bot->CanTakeQuest(quest, false) || bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
                continue;

            const int32 prevQuestId = std::abs(quest->GetPrevQuestId());
            if (!prevQuestId)
                continue;

            if (bot->GetQuestRewardStatus(prevQuestId) || bot->GetQuestStatus(prevQuestId) == QUEST_STATUS_COMPLETE)
                ++followOnCount;
        }

        if (!followOnCount)
            return 0.0f;

        float bonus = 0.20f * std::min<float>(2.0f, static_cast<float>(followOnCount));
        if (IsBotInStarterZone(bot))
            bonus *= 4.0f;

        return bonus;
    }

    InterruptTier GetTravelTargetInterruptTier(const TravelTarget* target)
    {
        CommittedTask tempTask;
        if (target && target->GetDestination())
            tempTask.purpose = target->GetDestination()->GetPurpose();

        return tempTask.GetInterruptTier();
    }

    void UpdateCommittedTaskFromTravelTarget(PlayerbotAI* ai, const TravelTarget* target)
    {
        if (!ai)
            return;

        CommittedTask& committedTask = ai->GetCommittedTask();
        committedTask.Clear();

        if (!target || !target->GetDestination())
            return;

        committedTask.purpose = target->GetDestination()->GetPurpose();
        committedTask.destinationEntry = target->GetEntry();
        committedTask.objectiveIndex = GetTravelTargetObjectiveIndex(target);
        committedTask.questId = GetTravelTargetQuestId(target);
        committedTask.retryCount = static_cast<uint8>(std::min<uint32>(target->GetRetryCount(false), std::numeric_limits<uint8>::max()));
        committedTask.lastValidityCheck = time(nullptr);
        committedTask.isValid = committedTask.purpose != TravelDestinationPurpose::None;
    }

    bool CanReplaceSession(const BotSession& session, SessionState candidateState)
    {
        if (candidateState == SessionState::IDLE)
            return true;

        if (session.isPaused)
            return true;

        if (session.state == SessionState::IDLE || session.state == SessionState::TRAVELLING)
            return true;

        return session.state == candidateState;
    }

    void UpdateSessionFromTravelTarget(PlayerbotAI* ai, const TravelTarget* target)
    {
        if (!ai || !target || !target->GetDestination())
            return;

        SessionState sessionState = GetSessionStateForPurpose(target->GetDestination()->GetPurpose());
        if (sessionState == SessionState::IDLE)
            return;

        BotSession& session = ai->GetSession();
        if (session.state != sessionState || session.isPaused)
        {
            session.Reset(sessionState);
            const ArchetypeWeights& weights = ai->GetArchetypeWeights();
            session.plannedDuration = (weights.minSessionMinutes + weights.maxSessionMinutes) / 2;
        }
    }

    float GetQuestPriorityScore(PlayerbotAI* ai, TravelDestination* destination, std::unordered_map<uint32, float>& cache)
    {
        QuestObjectiveTravelDestination* objectiveDestination = dynamic_cast<QuestObjectiveTravelDestination*>(destination);
        if (!objectiveDestination)
            return 100.0f;

        const uint32 questId = objectiveDestination->GetQuestId();
        std::unordered_map<uint32, float>::const_iterator cached = cache.find(questId);
        if (cached != cache.end())
            return cached->second;

        const float score = ai->GetAiObjectContext()->GetValue<float>("quest priority", std::to_string(questId))->Get();
        cache[questId] = score;
        return score;
    }

    uint32 GetDestinationQuestId(TravelDestination* destination)
    {
        if (QuestTravelDestination* questDestination = dynamic_cast<QuestTravelDestination*>(destination))
            return questDestination->GetQuestId();

        return 0;
    }

    float GetLocalQuestClusterScore(PlayerbotAI* ai, TravelDestination* destination, WorldPosition const* position)
    {
        if (!ai || !destination || !position || !IsQuestPurpose(destination->GetPurpose()))
            return 0.0f;

        Player* bot = ai->GetBot();
        if (!bot)
            return 0.0f;

        float score = 0.0f;
        const uint32 destinationQuestId = GetDestinationQuestId(destination);

        TravelTarget* currentTarget = ai->GetAiObjectContext()->GetValue<TravelTarget*>("travel target")->Get();
        if (currentTarget && currentTarget->GetDestination())
        {
            const uint32 currentQuestId = GetDestinationQuestId(currentTarget->GetDestination());
            if (destinationQuestId && currentQuestId && destinationQuestId == currentQuestId)
                score += 35.0f;

            if (currentTarget->GetPosition())
            {
                const float clusterDistance = currentTarget->GetPosition()->distance(*position);
                if (clusterDistance <= 75.0f)
                    score += 25.0f;
                else if (clusterDistance <= 150.0f)
                    score += 10.0f;
            }
        }

        CommittedTask& committedTask = ai->GetCommittedTask();
        if (committedTask.isValid && destinationQuestId && committedTask.questId == destinationQuestId)
            score += 20.0f;

        if (IsBotInStarterZone(bot))
        {
            if (IsDestinationInAllowedStarterCluster(bot, position))
                score += 40.0f;
            else
                score -= 250.0f;
        }

        return score;
    }
}

inline std::string GetTravelPurposeName(std::string purpose)
{
    if (Qualified::isValidNumberString(purpose) && TravelDestinationPurposeName.find(TravelDestinationPurpose(stoi(purpose))) != TravelDestinationPurposeName.end())
        return TravelDestinationPurposeName.at(TravelDestinationPurpose(stoi(purpose)));

    if (purpose.empty())
        return "quest";

    return purpose;
}

bool ChooseTravelTargetAction::Execute(Event& event)
{
    TravelTarget* travelTarget = AI_VALUE(TravelTarget*, "travel target");

    if(travelTarget->GetStatus() != TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    Player* requester = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);
    FutureDestinations* futureDestinations = AI_VALUE(FutureDestinations*, "future travel destinations");
    std::string futureTravelPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
    std::string futureTravelPurposeName = GetTravelPurposeName(futureTravelPurpose);
    uint32 targetRelevance = AI_VALUE2(int, "manual int", "future travel relevance");

    if (!futureDestinations->valid())
    {
        TellTravelTrace(ai, requester, "future destination fetch invalid; clearing prepared target");
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);
        context->ClearValues("no active travel destinations");        
        return false;
    }

    if (futureDestinations->wait_for(std::chrono::seconds(0)) == std::future_status::timeout)
        return false;

    PartitionedTravelList destinationList = futureDestinations->get();

    TellTravelTrace(ai, requester, "future destination fetch resolved; resetting prepared target before selection");
    travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_NONE);

    ai->TellDebug(ai->GetMaster(), "Got " + std::to_string(destinationList.size()) + " new destination ranges for " + futureTravelPurposeName, "debug travel");

    TravelTarget newTarget = TravelTarget(ai);

    if (futureTravelPurpose == "pvp")
        newTarget.SetForced(true);

    if (AI_VALUE2(std::string, "manual string", "future travel condition") == "should travel named::guild meeting")
    {
        newTarget.SetForced(true);
        newTarget.SetRelevance(std::max<uint32>(targetRelevance, 199u));
    }
    else if (AI_VALUE2(std::string, "manual string", "future travel condition") == "should travel named::guild order")
    {
        newTarget.SetForced(true);
        newTarget.SetRelevance(std::max<uint32>(targetRelevance, 198u));
    }
    else
    {
        newTarget.SetRelevance(targetRelevance);
    }

    EntryQuestRelationMap relationMap;
    std::vector<uint32> questIds;
    EntryQuestRelationMap const* relationMapPtr = nullptr;
    std::vector<uint32> const* questIdsPtr = nullptr;
    if (futureTravelPurpose == "quest")
    {
        relationMap = AI_VALUE_SAFE(EntryQuestRelationMap, "entry quest relation");
        QuestStatusMap& questStatusMap = bot->GetQuestStatusMap();
        for (const auto& [questId, questStatus] : questStatusMap)
        {
            if (!questStatus.m_rewarded)
                questIds.push_back(questId);
        }

        std::sort(questIds.begin(), questIds.end());
        questIds.erase(std::unique(questIds.begin(), questIds.end()), questIds.end());
        relationMapPtr = &relationMap;
        questIdsPtr = &questIds;
    }

    if (!SetBestTarget(requester, &newTarget, destinationList, true, relationMapPtr, questIdsPtr))
    {
        SET_AI_VALUE2(bool, "no active travel destinations", futureTravelPurpose, true);
        if (futureTravelPurpose == "quest")
            LogStarterQuestNoTargetDiagnostics(ai, requester, bot, destinationList);
        ai->TellDebug(ai->GetMaster(), "No target set", "debug travel");
        return false;
    }

    const TravelDestinationPurpose newPurpose = newTarget.GetDestination()->GetPurpose();
    SessionState candidateSessionState = GetSessionStateForPurpose(newPurpose);
    BotSession& currentSession = ai->GetSession();
    CommittedTask& committedTask = ai->GetCommittedTask();

    const bool hasValidCommittedTask = committedTask.ValidateTarget(ai);
    if (!hasValidCommittedTask && currentSession.state != SessionState::IDLE)
        currentSession.Reset(SessionState::IDLE);

    if ((currentSession.state != SessionState::IDLE && currentSession.state != SessionState::TRAVELLING) &&
        !currentSession.isPaused &&
        hasValidCommittedTask &&
        !SessionAllowsPurpose(currentSession.state, newPurpose))
    {
        ai->TellDebug(requester, "Blocking " + TravelDestinationPurposeName.at(newPurpose) + " during " + SessionStateToString(currentSession.state) + " session.", "debug travel");
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
        return false;
    }

    if (candidateSessionState == SessionState::MAINTENANCE &&
        currentSession.state != SessionState::MAINTENANCE &&
        !ai->HasActivePlayerMaster() &&
        !ai->HasMaintenanceBreakpoint())
    {
        ai->TellDebug(requester, "Delaying maintenance target until a maintenance breakpoint opens.", "debug travel");
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
        return false;
    }

    if (!CanReplaceSession(currentSession, candidateSessionState))
    {
        ai->TellDebug(requester, "Keeping current " + SessionStateToString(currentSession.state) + " session instead of switching to " + SessionStateToString(candidateSessionState) + ".", "debug travel");
        travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
        return false;
    }

    if (hasValidCommittedTask && !committedTask.MatchesTarget(&newTarget))
    {
        if (IsQuestPurpose(committedTask.purpose) && IsLowPriorityPurpose(newPurpose))
        {
            ai->TellDebug(requester, "Keeping committed quest task over low-priority target.", "debug travel");

            if (committedTask.MatchesTarget(travelTarget) && travelTarget->GetDestination() && travelTarget->IsDestinationActive() && travelTarget->IsConditionsActive())
            {
                travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
                return false;
            }

            committedTask.Clear();
        }

        if (IsQuestPurpose(committedTask.purpose) && IsMaintenancePurpose(newPurpose))
        {
            ai->TellDebug(requester, "Keeping committed quest task over maintenance target.", "debug travel");

            if (committedTask.MatchesTarget(travelTarget) && travelTarget->GetDestination() && travelTarget->IsDestinationActive() && travelTarget->IsConditionsActive())
            {
                travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
                return false;
            }

            committedTask.Clear();
        }

        InterruptTier newTier = GetTravelTargetInterruptTier(&newTarget);
        if (!committedTask.CanBePreemptedBy(newTier))
        {
            ai->TellDebug(requester, "Keeping committed " + InterruptTierToString(committedTask.GetInterruptTier()) + " task over new " + InterruptTierToString(newTier) + " target.", "debug travel");

            if (committedTask.MatchesTarget(travelTarget) && travelTarget->GetDestination() && travelTarget->IsDestinationActive() && travelTarget->IsConditionsActive())
            {
                travelTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
                return false;
            }

            committedTask.Clear();
        }
    }

    setNewTarget(requester, &newTarget, travelTarget);
    UpdateCommittedTaskFromTravelTarget(ai, travelTarget);
    UpdateSessionFromTravelTarget(ai, travelTarget);
    
    return true;
}

bool ChooseTravelTargetAction::isUseful()
{
    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (AI_VALUE(bool, "travel target active"))
        return false;

    return true;
}

void ChooseTravelTargetAction::setNewTarget(Player* requester, TravelTarget* newTarget, TravelTarget* oldTarget)
{
    if(AI_VALUE2(bool, "can free move to", newTarget->GetPosStr()))
        ReportTravelTarget(bot, requester, newTarget, oldTarget);

    //If we are heading to a creature/npc clear it from the ignore list. 
    if (oldTarget && oldTarget == newTarget && newTarget->GetEntry())
    {
        std::set<ObjectGuid>& ignoreList = context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Get();

        for (auto& i : ignoreList)
        {
            if (i.GetEntry() == newTarget->GetEntry())
            {
                ignoreList.erase(i);
            }
        }

        context->GetValue<std::set<ObjectGuid>&>("ignore rpg target")->Set(ignoreList);
    }

    //Actually apply the new target to the travel target used by the bot.
    oldTarget->CopyTarget(newTarget);

    if (oldTarget->IsForced()) //Make sure travel goes into cooldown after getting to the destination.
        oldTarget->SetExpireIn(HOUR * IN_MILLISECONDS);

    if(!AI_VALUE2(std::string, "manual string", "future travel condition").empty())
        AI_VALUE(TravelTarget*, "travel target")->SetConditions({ AI_VALUE2(std::string, "manual string", "future travel condition")});

    if (QuestObjectiveTravelDestination* dest = dynamic_cast<QuestObjectiveTravelDestination*>(oldTarget->GetDestination()))
    {
        std::string condition = "group or::{following party,need quest objective::{" + std::to_string(dest->GetQuestId()) + "," + std::to_string((uint8)dest->GetObjective()) + "}}";
        oldTarget->AddCondition(condition);
    }
    else if (QuestRelationTravelDestination* dest = dynamic_cast<QuestRelationTravelDestination*>(oldTarget->GetDestination()))
    {
        std::string condition, qualifier = std::to_string(dest->GetEntry());
        if (dest->GetPurpose() == TravelDestinationPurpose::QuestGiver)

            condition = "group or::{following party,or::{can accept quest npc::" + qualifier + ",can accept quest low level npc::" + qualifier + "}}";
        else
            condition = "group or::{following party,can turn in quest npc::" + qualifier + "}";

        oldTarget->AddCondition(condition);
    }

    oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_READY);

    //Clear rpg and attack/grind target. We want to travel, not hang around some more.
    RESET_AI_VALUE(GuidPosition,"rpg target");
    RESET_AI_VALUE(std::set<ObjectGuid>&, "ignore rpg target");
    RESET_AI_VALUE(ObjectGuid,"attack target");
    RESET_AI_VALUE(bool, "travel target active");
    context->ClearValues("no active travel destinations");
    SET_AI_VALUE2(std::string, "manual string", "future travel detail", std::string());
};

//Tell the master what travel target we are moving towards.
//This should at some point be rewritten to be denser or perhaps logic moved to ->getTitle()
void ChooseTravelTargetAction::ReportTravelTarget(Player* bot, Player* requester, TravelTarget* newTarget, TravelTarget* oldTarget)
{
    PlayerbotAI* ai = bot->GetPlayerbotAI();
    AiObjectContext* context = ai->GetAiObjectContext();

    TravelDestination* destination = newTarget->GetDestination();

    TravelDestination* oldDestination;

    if (oldTarget)
        oldDestination = oldTarget->GetDestination();

    std::ostringstream out;

    if (newTarget->IsForced())
        out << "(Forced) ";
        
    std::string futureTravelPurpose = AI_VALUE2(std::string, "manual string", "future travel purpose");
    std::string futureTravelPurposeName = GetTravelPurposeName(futureTravelPurpose);

    std::string futureTravelCondition = AI_VALUE2(std::string, "manual string", "future travel condition");
    bool isGuildMeeting = futureTravelCondition == "should travel named::guild meeting";

    std::string futureTravelDetail = AI_VALUE2(std::string, "manual string", "future travel detail");

    std::string shortName = destination->GetShortName();    

    if (typeid(*destination) == typeid(NullTravelDestination))
    {
        out.clear();
        if (!oldDestination || typeid(*oldDestination) != typeid(NullTravelDestination))
            out << "Nowhere to travel. Idling a bit.";
    }
    else
    {
        if (newTarget->GetStatus() == TravelStatus::TRAVEL_STATUS_WORK)
        {
            out << "Currently";

            if (newTarget->GetPosition() && !newTarget->GetPosition()->getAreaName().empty())
            {
                if (destination->DistanceTo(bot) < 100.0f)
                    out << " in ";
                else
                    out << " near ";

                out << newTarget->GetPosition()->getAreaName();
            }
            else
                out << " traveling";
        }
        else
        {
            if (bot->GetGroup() && !ai->IsGroupLeader() && (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("wander", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("stay", BotState::BOT_STATE_NON_COMBAT) || ai->HasStrategy("guard", BotState::BOT_STATE_NON_COMBAT)))
                out << "I want to travel";
            else if (newTarget->IsGroupCopy() && newTarget->GetGroupmember().GetPlayer())
                out << "Taking " << newTarget->GetGroupmember().GetPlayer()->GetName();
            else if (oldDestination && oldDestination == destination)
                out << "Continuing";
            else
                out << "Traveling";

            if (newTarget->GetPosition())
            {
                out << " " << round(newTarget->Distance(bot)) << "y";
                if (!newTarget->GetPosition()->getAreaName().empty())
                    out << " to " << newTarget->GetPosition()->getAreaName();
            }
        }

        if (shortName.find("quest") == 0)
        {
            QuestTravelDestination* QuestDestination = (QuestTravelDestination*)destination;
            out << " for " << QuestDestination->QuestTravelDestination::GetTitle();
            out << " to " << QuestDestination->GetTitle();
        }
        else if (shortName == "rpg")
        {
            out << " to " << destination->GetTitle();

            if (futureTravelPurpose == "city")
                out << " to hang around in the city";
            else if (futureTravelPurpose == "tabard")
                out << " to buy a tabard";
            else if (futureTravelPurpose == "petition")
                out << " to hand in a petition";
            else
                out << " to roleplay";
        }
        else
        {
            out << " to " << destination->GetTitle();
        }
    }

    if (newTarget->GetRetryCount(false))
        out << " (retry " << newTarget->GetRetryCount(false) << "/5)";
    if (out.str().empty())
        return;

    if (!isGuildMeeting)
        ai->TellPlayerNoFacing(requester, out, PlayerbotSecurityLevel::PLAYERBOT_SECURITY_TALK, false);

    if (!futureTravelDetail.empty())
        ai->TellDebug(requester, "Farming item: " + futureTravelDetail + " from " + destination->GetTitle(), "debug travel");

    std::string message = out.str().c_str();

    if (sPlayerbotAIConfig.hasLog("travel_map.csv"))
    {
        WorldPosition botPos(bot);
        WorldPosition destPos = *newTarget->GetPosition();

        std::ostringstream out;
        out << sPlayerbotAIConfig.GetTimestampStr() << "+00,";
        out << bot->GetName() << ",";
        out << std::fixed << std::setprecision(2);

        out << std::to_string(bot->GetRace()) << ",";
        out << std::to_string(bot->GetClass()) << ",";
        float subLevel = ai->GetLevelFloat();

        out << subLevel << ",";

        if (!destPos)
            destPos = botPos;

        botPos.printWKT({ botPos,destPos }, out, 1);

        if (typeid(*destination) == typeid(NullTravelDestination))
            out << "0,";
        else
            out << round(newTarget->GetDestination()->DistanceTo(botPos)) << ",";

        out << "new," << "\"" << destination->GetTitle() << "\",\"" << message << "\"";

        out << "," << futureTravelPurposeName;

        sPlayerbotAIConfig.log("travel_map.csv", out.str().c_str());        
    }
}

inline std::string PrintPartion(uint32 sqPartition)
{
    uint32 prevPartition = 0;
    for (auto& partition : travelPartitions)
    {
        if (sqrt(sqPartition) == partition)
            return std::to_string(prevPartition) + "-" + std::to_string(partition);

        prevPartition = partition;
    }

    return "> " + std::to_string(prevPartition);
}

//Sets the target to the best destination.
bool ChooseTravelTargetAction::SetBestTarget(Player* requester, TravelTarget* target, PartitionedTravelList& partitionedList, bool onlyActive,
    EntryQuestRelationMap const* relationMap, std::vector<uint32> const* questIds)
{
    bool distanceCheck = true;
    std::unordered_map<TravelDestination*, bool> isActive;
    std::unordered_map<uint32, float> questPriorityCache;
    const PlayerTravelInfo travelInfo(bot);
    const QuestIntentContext intentContext = BuildQuestIntentContext(bot, ai, partitionedList, relationMap);

    bool hasTarget = false;

    for (auto& [partition, travelPointList] : partitionedList)
    {
        ai->TellDebug(requester, "Found " + std::to_string(travelPointList.size()) + " points at range " + PrintPartion(partition), "debug travel");

        TravelPointList sortedPoints = travelPointList;
        std::stable_sort(sortedPoints.begin(), sortedPoints.end(), [&](const TravelPoint& left, const TravelPoint& right)
        {
            TravelDestination* leftDestination = std::get<0>(left);
            TravelDestination* rightDestination = std::get<0>(right);
            WorldPosition* leftPosition = std::get<1>(left);
            WorldPosition* rightPosition = std::get<1>(right);

            const float leftSocialScore = GetSocialDestinationScore(bot, leftDestination);
            const float rightSocialScore = GetSocialDestinationScore(bot, rightDestination);
            if (leftSocialScore != rightSocialScore)
                return leftSocialScore > rightSocialScore;

            float leftScore = GetQuestIntentScore(bot, ai, leftDestination, leftPosition, relationMap, questIds, intentContext, questPriorityCache);
            float rightScore = GetQuestIntentScore(bot, ai, rightDestination, rightPosition, relationMap, questIds, intentContext, questPriorityCache);

            if (leftScore == rightScore)
                return std::get<2>(left) < std::get<2>(right);

            return leftScore > rightScore;
        });

        for (auto& [destination, position, distance] : sortedPoints)
        {
            if (!target->IsForced() && isActive.find(destination) != isActive.end() && !isActive[destination])
                continue;

            const float questPriority = GetQuestPriorityScore(ai, destination, questPriorityCache);
            if (!target->IsForced() && questPriority < 20.0f)
            {
                ai->TellDebug(requester, "Suppressing low-priority quest target: " + destination->GetTitle() + " (" + std::to_string(uint32(questPriority)) + ")", "debug travel");
                continue;
            }

            if (distanceCheck) //Check if we have moved significantly after getting the destinations.
            {
                WorldPosition center(requester ? requester : bot);
                if (position->distance(center) > distance * 2 && position->distance(center) > 100)
                {
                    ai->TellDebug(requester, "We had some destinations but we moved too far since. Trying to get a new list.", "debug travel");
                    return false;
                }

                distanceCheck = false;
            }

            bool allowInactiveQuestContinuation = false;
            const bool destinationActive = destination->IsActive(bot, travelInfo);
            isActive[destination] = destinationActive;

            if (!destinationActive && !target->IsForced())
            {
                if (QuestRelationTravelDestination* questRelationDestination = dynamic_cast<QuestRelationTravelDestination*>(destination))
                {
                    if (questRelationDestination->IsPossible(travelInfo) &&
                        (questRelationDestination->GetPurpose() == TravelDestinationPurpose::QuestGiver ||
                         questRelationDestination->GetPurpose() == TravelDestinationPurpose::QuestTaker))
                    {
                        if (intentContext.hasUsableLocalQuestWork)
                        {
                            ai->TellDebug(requester, "Blocking inactive quest continuation because local quest work is available: " +
                                destination->GetTitle() + " " + std::to_string(round(destination->DistanceTo(bot))) + "y", "debug travel");
                        }
                        else if (IsBotInStarterZone(bot) && !IsDestinationInAllowedStarterCluster(bot, position))
                        {
                            ai->TellDebug(requester, "Blocking distant quest continuation outside starter cluster: " +
                                destination->GetTitle() + " " + std::to_string(round(destination->DistanceTo(bot))) + "y", "debug travel");
                        }
                        else
                        {
                            allowInactiveQuestContinuation = true;
                            ai->TellDebug(requester, "Allowing distant quest continuation target despite inactive check: " +
                                destination->GetTitle() + " " + std::to_string(round(destination->DistanceTo(bot))) + "y", "debug travel");
                        }
                    }
                }
            }

            if (target->IsForced() || destinationActive || allowInactiveQuestContinuation)
            {
                if (partition != std::prev(partitionedList.end())->first && !urand(0, 10)) //10% chance to skip to a longer partition.
                {
                    ai->TellDebug(requester, "Skipping range " + PrintPartion(partition), "debug travel");
                    break;
                }

#ifdef MANGOSBOT_TWO
                if (GuidPosition* guidP = static_cast<GuidPosition*>(position))
                {
                    if (!bot->InSamePhase(guidP->GetPhaseMask()))
                    {
                        ai->TellDebug(requester, "Not same phase: " + destination->GetTitle() + " " + std::to_string(round(destination->DistanceTo(bot))) + "y", "debug travel");
                        continue;
                    }
                }
#endif

                target->SetTarget(destination, position);
                if (QuestIntentKind selectedIntent = ClassifyQuestIntent(bot, ai, destination, position, relationMap, intentContext);
                    selectedIntent != QuestIntentKind::None)
                {
                    ai->TellDebug(requester,
                        "[PBTRACE] quest_intent selected intent=" + FormatQuestIntentKind(selectedIntent) +
                        " dest=\"" + destination->GetTitle() + "\"" +
                        " dist=" + std::to_string(static_cast<uint32>(round(destination->DistanceTo(bot)))),
                        "debug travel");
                }
                hasTarget = true;
                break;
            }
            else
            {
                ai->TellDebug(requester, "Not active: " + destination->GetTitle() + " " + std::to_string(round(destination->DistanceTo(bot))) + "y", "debug travel");
            }

        }

        if (hasTarget)
            break;
    }         
     
    if(hasTarget)
        ai->TellDebug(requester, "Point at " + std::to_string(uint32(target->Distance(bot))) + "y selected.", "debug travel");

    return hasTarget;
}

std::vector<std::string> split(const std::string& s, char delim);
char* strstri(const char* haystack, const char* needle);

//Find a destination based on (part of) it's name. Includes zones, ncps and mobs. Picks the closest one that matches.
DestinationList ChooseTravelTargetAction::FindDestination(PlayerTravelInfo info, std::string name, bool zones, bool npcs, bool quests, bool mobs, bool bosses, bool gather)
{
    DestinationList dests;

    //Quests
    if (quests)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::QuestGiver, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Zones
    if (zones)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Explore, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Npcs
    if (npcs)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GenericRpg, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Mobs
    if (mobs)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Grind, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Bosses
    if (bosses)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::Boss, {}, false, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    //Gather
    if (gather)
    {
        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherSkinning, {}, true, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }

        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherMining, {}, true, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }

        for (auto& d : sTravelMgr.GetDestinations(info, (uint32)TravelDestinationPurpose::GatherHerbalism, {}, true, 1000000.0f))
        {
            if (strstri(d->GetTitle().c_str(), name.c_str()))
                dests.push_back(d);
        }
    }

    if (dests.empty())
        return {};

    return dests;
};

bool ChooseGroupTravelTargetAction::Execute(Event& event)
{
    std::vector<ObjectGuid> groupPlayers;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        if (ref->getSource() != bot)
        {
            groupPlayers.push_back(ref->getSource()->GetObjectGuid());
        }
    }

    std::shuffle(groupPlayers.begin(), groupPlayers.end(), std::mt19937{std::random_device{}()});

    PlayerTravelInfo info(bot);

    std::vector<TravelTarget*> groupTargets;

    PartitionedTravelList travelList;

    std::unordered_map<TravelDestination*, std::vector<std::string>> conditions;
    std::unordered_map<TravelDestination*, Player*> playerDesitnations;

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    //Find targets of the group.
    for (auto& member : groupPlayers)
    {
        Player* player = sObjectMgr.GetPlayer(member);

        if (!player)
            continue;

        if (!ai->IsSafe(player))
            continue;

        if (!player->GetPlayerbotAI())
            continue;

        if (!player->GetPlayerbotAI()->GetAiObjectContext())
            continue;

        TravelTarget* groupTarget = PAI_VALUE(TravelTarget*, "travel target");

        if (groupTarget->IsGroupCopy())
            continue;

        if (!groupTarget->IsActive())
            continue;

        if (groupTarget->IsForced())
            continue;

        if (!groupTarget->GetDestination()->IsActive(player, PlayerTravelInfo(player)) || !groupTarget->IsConditionsActive())
        {
            player->GetPlayerbotAI()->TellDebug(requester,"Target is cooling down because a group member found it to be inactive.", "debug travel");
            groupTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
            continue;
        }

        groupTargets.push_back(groupTarget);        
        playerDesitnations[groupTarget->GetDestination()] = player;
        conditions[groupTarget->GetDestination()] = groupTarget->GetConditions();
    }

    std::sort(groupTargets.begin(), groupTargets.end(), [](TravelTarget* i, TravelTarget* j) {return i->GetRelevance() > j->GetRelevance(); });

    ai->TellDebug(requester, std::to_string(groupTargets.size()) + " group targets found.", "debug travel");

    for (auto& groupTarget : groupTargets)
    {
        travelList[0].push_back(TravelPoint(groupTarget->GetDestination(), groupTarget->GetPosition(), groupTarget->GetPosition()->distance(bot)));

        ai->TellDebug(requester, playerDesitnations[groupTarget->GetDestination()]->GetName() + std::string(": ") + groupTarget->GetDestination()->GetShortName() + std::string(" (") + std::to_string(groupTarget->GetRelevance()) + std::string(")"), "debug travel");
    }

    if (travelList[0].empty())
        return false;

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    TravelTarget newTarget = TravelTarget(ai);

    if (!SetBestTarget(requester, &newTarget, travelList))
        return false;
    
    newTarget.SetGroupCopy(playerDesitnations[newTarget.GetDestination()]);

    setNewTarget(requester, &newTarget, oldTarget);

    oldTarget->SetConditions(conditions[newTarget.GetDestination()]);

    return true;
}

bool ChooseGroupTravelTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!bot->GetGroup())
        return false;

    if (!ChooseTravelTargetAction::isUseful())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (urand(0, 100) < 50)
        return false;

    return true;
}

bool RefreshTravelTargetAction::Execute(Event& event)
{
    TravelTarget* target = AI_VALUE(TravelTarget*, "travel target");

    TravelDestination* oldDestination = target->GetDestination();

    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();

    if (target->IsMaxRetry(false))
    {
        ai->TellDebug(requester, "Old destination was tried too many times.", "debug travel");
        return false;
    }

    if (!oldDestination) //Does this target have a destination?
        return false;

    if (!target->IsDestinationActive()) //Is the destination still valid?
    {
        ai->TellDebug(requester, "Old destination was no longer valid.", "debug travel");
        return false;
    }

    PlayerTravelInfo info(bot);
    
    WorldPosition* newPosition;

    for (uint8 i = 0; i < 5; i++)
    {
        std::list<uint8> chancesToGoFar = { 10,20,90 }; //Closest map, grid, cell.
        newPosition = oldDestination->GetNextPoint(*target->GetPosition(), chancesToGoFar);
        if (newPosition && sTravelMgr.IsLocationLevelValid(*newPosition, info))
            break;        
    }

    if (!newPosition)
    {
        ai->TellDebug(requester, "No new locations found for old destination.", "debug travel");
        return false;
    }

    SET_AI_VALUE2(bool, "manual bool", "is travel refresh", true);
    bool conditionsStillActive = AI_VALUE(TravelTarget*, "travel target")->IsConditionsActive(true);
    RESET_AI_VALUE2(bool, "manual bool", "is travel refresh");

    if (!conditionsStillActive)
    {
        TellTravelTrace(ai, requester, "refresh aborted because travel target conditions no longer hold");
        return false;
    }

    target->SetTarget(oldDestination, newPosition);

    target->SetStatus(TravelStatus::TRAVEL_STATUS_READY);
    target->IncRetry(false);

    RESET_AI_VALUE(bool, "travel target active");    
    context->ClearValues("no active travel destinations");
    SET_AI_VALUE2(std::string, "manual string", "future travel detail", std::string());

    ai->TellDebug(requester, "Refreshed travel target", "debug travel");
    ReportTravelTarget(bot, requester, target, target);

    return false;
}

bool RefreshTravelTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!ChooseTravelTargetAction::isUseful())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (!WorldPosition(bot).isOverworld())
        return false;

    if (urand(1, 100) <= 10)
        return false;

    if (!AI_VALUE(TravelTarget*, "travel target")->GetDestination()->IsActive(bot, PlayerTravelInfo(bot)))
        return false;

    return true;
}

bool ResetTargetAction::Execute(Event& event)
{
    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    context->ClearValues("no active travel destinations");

    TravelTarget newTarget = TravelTarget(ai);
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    setNewTarget(requester, &newTarget, oldTarget);

    oldTarget->SetStatus(TravelStatus::TRAVEL_STATUS_COOLDOWN);
    oldTarget->SetExpireIn(60000); //1 minute;

    ai->TellDebug(requester, "Cleared travel target fetches", "debug travel");

    return true;
}

bool ResetTargetAction::isUseful()
{
    if (bot->InBattleGround())
        return false;

    if (!ChooseTravelTargetAction::isUseful())
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    return true;
}

bool RequestTravelTargetAction::Execute(Event& event)
{
    TravelDestinationPurpose actionPurpose = TravelDestinationPurpose(stoi(getQualifier()));
    FutureDestinations* futureDestinations = AI_VALUE(FutureDestinations*, "future travel destinations");

    if (HasPendingTravelDestinations(futureDestinations))
    {
        TellTravelTrace(ai, ai->GetMaster(),
            "travel_suppressed reason=pending_fetch candidate=" + FormatTravelPurpose(actionPurpose) +
            " source=" + getQualifier() + " " + FormatDecisionState(ai) + " " + FormatCommittedTaskSummary(ai));
        return false;
    }

    WorldPosition center = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for " + TravelDestinationPurposeName.at(actionPurpose), "debug travel");

    *futureDestinations = LaunchTravelDestinations([partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, purpose = actionPurpose]() { return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)purpose); });

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", getQualifier());
    SET_AI_VALUE2(std::string, "manual string", "future travel condition", event.getSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    return true;
}

bool RequestTravelTargetAction::isUseful() {
    if (bot->InBattleGround())
        return false;

    if (!ai->AllowActivity(TRAVEL_ACTIVITY))
        return false;

    if (AI_VALUE(TravelTarget*, "travel target")->GetStatus() == TravelStatus::TRAVEL_STATUS_PREPARE)
        return false;

    if (AI_VALUE(bool, "travel target active"))
        return false;

    // -----------------------------------------------------------------------
    // Idle → Tournament (fishing / STV Extravaganza) session transition.
    //
    // Entry conditions (all must be true):
    //   1. Bot is IDLE with no active quest work (HasQuestIntent = false)
    //   2. Bot can fish (has SKILL_FISHING + a fishing pole equipped or in bags)
    //   3. Either:
    //        a. Bot has the STV Fishing Extravaganza pickup quest (8194), OR
    //        b. It is Sunday (real server time tm_wday == 0) AND level >= 40
    //           (the weekly tournament attracts high-level fishers to STV)
    //   4. A stable 15%-per-hour seeded roll fires
    //
    // Once in TOURNAMENT, only fishing actions are allowed and fishing travel
    // is guaranteed (see RequestTravelTargetAction::isAllowed). The session
    // ends naturally when the bot has no committed task remaining.
    //
    // Note: HasQuestIntent() is a free function in this file's anonymous
    // namespace — it is NOT a registered AI value, so we call it directly.
    // -----------------------------------------------------------------------
    if (bot->GetPlayerbotAI()->GetSession().state == SessionState::IDLE &&
        !HasQuestIntent(bot) &&
        AI_VALUE(bool, "can fish"))
    {
        // Check STV tournament eligibility
        const bool hasTourneyQuest =
            bot->GetQuestStatus(8194) == QUEST_STATUS_INCOMPLETE ||  // Extravaganza pickup
            bot->GetQuestStatus(8193) == QUEST_STATUS_INCOMPLETE;    // Master Angler turn-in

        time_t now = time(nullptr);
        tm* lt = localtime(&now);
        const bool isSunday    = (lt && lt->tm_wday == 0);
        const bool highEnough  = (bot->GetLevel() >= 40);

        const bool eligibleForTourney = hasTourneyQuest || (isSunday && highEnough);

        if (eligibleForTourney)
        {
            // Stable per-hour roll — same bot makes same decision within a clock-hour
            const uint32 seed = bot->GetGUIDLow() ^ static_cast<uint32>(now / 3600);
            const bool wantsToFish = ((seed % 100) < 15);  // 15% per hour window

            if (wantsToFish)
            {
                bot->GetPlayerbotAI()->SetSessionState(SessionState::TOURNAMENT);
                ai->TellDebug(ai->GetMaster(),
                    "Entering STV fishing tournament session.", "debug travel");
            }
        }
    }

    if (AI_VALUE2(bool, "no active travel destinations", (getQualifier().empty() ? "quest" : getQualifier())))
        return false;

    if (!AI_VALUE(bool, "can move around"))
        return false;

    if (!isAllowed())
    {
        ai->TellDebug(ai->GetMaster(), "Skipped " + GetTravelPurposeName(qualifier) + " because of skip chance", "debug travel");
        return false;
    }

    return true;
}

bool RequestTravelTargetAction::isAllowed() const
{
    TravelDestinationPurpose actionPurpose = TravelDestinationPurpose(stoi(getQualifier()));

    if (actionPurpose == TravelDestinationPurpose::GenericRpg && ShouldSuppressGenericRpg(bot))
        return false;

    switch (actionPurpose)
    {
    case TravelDestinationPurpose::Repair:
    case TravelDestinationPurpose::Vendor:
    case TravelDestinationPurpose::AH:
        return urand(1, 100) < 90;
    case TravelDestinationPurpose::Mail:
        if (!AI_VALUE(bool, "should get money"))
            return urand(1, 100) < 30;
        else
            return true;
    case TravelDestinationPurpose::GatherSkinning:
    case TravelDestinationPurpose::GatherMining:
    case TravelDestinationPurpose::GatherHerbalism:
        if (bot->GetGroup())
            return urand(1, 100) < 50;
        else
            return urand(1, 100) < 90;
    case TravelDestinationPurpose::GatherFishing:
        // Fishing travel is guaranteed during an active TOURNAMENT session
        // so a bot that has committed to fishing doesn't randomly skip its spot.
        if (bot->GetPlayerbotAI()->GetSession().state == SessionState::TOURNAMENT)
            return true;
        if (bot->GetGroup())
            return urand(1, 100) < 50;
        else
            return urand(1, 100) < 90;
    case TravelDestinationPurpose::Boss:
        return urand(1, 100) < 50;
    case TravelDestinationPurpose::Explore:
        return urand(1, 100) < 10;
    case TravelDestinationPurpose::GenericRpg:
    {
        uint32 chance = 55;
        if (bot->GetPlayerbotAI()->GetSession().state == SessionState::IDLE)
            chance = 80;
        else if (bot->GetPlayerbotAI()->GetSession().state == SessionState::QUESTING || bot->GetPlayerbotAI()->GetSession().state == SessionState::MAINTENANCE)
            chance = 25;

        if (sServerSocialMgr.GetKnownContactCount(bot->GetObjectGuid().GetRawValue()) < 3 && !bot->GetGuildId())
            chance = std::max<uint32>(15, chance / 2);

        return urand(1, 100) < chance;
    }
    case TravelDestinationPurpose::Grind:
        return true;
    default:
        return true;
    }
}

bool RequestNamedTravelTargetAction::Execute(Event& event)
{
    std::string travelName = getQualifier();
    FutureDestinations* futureDestinations = AI_VALUE(FutureDestinations*, "future travel destinations");

    if (HasPendingTravelDestinations(futureDestinations))
    {
        TellTravelTrace(ai, ai->GetMaster(),
            "travel_suppressed reason=pending_fetch candidate=" + travelName +
            " source=" + event.getSource() + " " + FormatDecisionState(ai) + " " + FormatCommittedTaskSummary(ai));
        return false;
    }

    WorldPosition center = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for travel " + getQualifier(), "debug travel");

    if (travelName == "pvp")
    {
        std::string WorldPvpLocation;

        //Number between 0 and 100 synced for all bots that shifts 1 every 10 minutes.
        uint32 pvpLocationNumber = ai->GetFixedBotNumber(BotTypeNumber::WORLD_PVP_LOCATION, 100, 0.1f, true);

        if (pvpLocationNumber < 20) //First 200 minutes
            WorldPvpLocation = "Tarren Mill";
        else if (pvpLocationNumber >= 20 && pvpLocationNumber < 40) //Second 200 minutes
            WorldPvpLocation = "The Barrens";
        else if (pvpLocationNumber >= 40 && pvpLocationNumber < 60) //Third 200 minutes
            WorldPvpLocation = "Silithus";
        else if (pvpLocationNumber >= 60 && pvpLocationNumber < 80) //Fourth 200 minutes
            WorldPvpLocation = "Eastern Plaguelands";
        else                                                        //Last 200 minutes
            WorldPvpLocation = "Strangletorn Vale";

        *futureDestinations = LaunchTravelDestinations([travelInfo = PlayerTravelInfo(bot), center, WorldPvpLocation]()
            {
                PartitionedTravelList list;
                for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, WorldPvpLocation, true, false, false, false, false, false))
                {
                    std::list<uint8> chancesToGoFar = { 10,50,90 }; //Closest map, grid, cell.
                    WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);

                    if (!point)
                        continue;

                    list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                }

                return list;
            }
        );
    }
    else if (travelName == "guild meeting")
    {
        // Parse guild MOTD for the meeting time.
        // Meeting: <location> <start time> <end time>
        std::string meetingLocation;
        if (bot->GetGuildId())
        {
            Guild* guild = sGuildMgr.GetGuildById(bot->GetGuildId());
            if (guild)
            {
                std::string motd = guild->GetMOTD();
                auto pos = motd.find("Meeting:");
                if (pos != std::string::npos)
                {
                    std::string body = motd.substr(pos + 8);
                    body.erase(body.begin(), std::find_if(body.begin(), body.end(), [](unsigned char ch) { return !std::isspace(ch); }));
                    std::vector<std::string> tokens;
                    { std::istringstream iss(body); std::string t; while (iss >> t) tokens.push_back(t); }
                    if (tokens.size() >= 3)
                    {
                        tokens.pop_back(); // end time
                        tokens.pop_back(); // start time
                        std::ostringstream loc;
                        for (size_t i = 0; i < tokens.size(); ++i) { if (i) loc << " "; loc << tokens[i]; }
                        meetingLocation = loc.str();
                    }
                }
            }
        }

        if (meetingLocation.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No meeting location found in guild MOTD", "debug travel");
            return false;
        }

        *futureDestinations = LaunchTravelDestinations([travelInfo = PlayerTravelInfo(bot), center, meetingLocation]()
            {
                PartitionedTravelList list;
                for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, meetingLocation, true, false, false, false, false, false))
                {
                    std::list<uint8> chancesToGoFar = { 10,50,90 };
                    WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);

                    if (!point)
                        continue;

                    list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                }

                return list;
            }
        );
    }
    else if (travelName == "guild order")
    {
        GuildOrder order = AI_VALUE(GuildOrder, "guild order");

        if (!order.IsTravelOrder())
        {
            ai->TellDebug(ai->GetMaster(), "No valid guild travel order found", "debug travel");
            return false;
        }

        std::string orderTarget = order.target;

        ai->TellDebug(ai->GetMaster(), "Guild order: " + order.GetTypeName() + " " + orderTarget, "debug travel");

        if (order.type == GuildOrderType::QuestReward)
        {
            uint32 questId = order.questId;
            if (!questId)
            {
                ai->TellDebug(ai->GetMaster(), "QuestReward order has no questId", "debug travel");
                return false;
            }

            QuestStatus questStatus = bot->GetQuestStatus(questId);
            bool questComplete = false;
            bool questInProgress = false;

            if (questStatus == QUEST_STATUS_COMPLETE)
                questComplete = true;
            else if (questStatus == QUEST_STATUS_INCOMPLETE)
                questInProgress = true;

            if (!questComplete && questStatus == QUEST_STATUS_INCOMPLETE)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest && bot->CanRewardQuest(quest, false))
                    questComplete = true;
            }

            std::vector<int32> objectiveEntries;
            std::vector<int32> questGiverEntries;
            std::vector<int32> questTakerEntries;

            if (questInProgress && !questComplete)
            {
                Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                if (quest)
                {
                    for (uint32 objective = 0; objective < QUEST_OBJECTIVES_COUNT; objective++)
                    {
                        std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };
                        if (!AI_VALUE2(bool, "need quest objective", Qualified::MultiQualify(qualifier, ",")))
                            continue;

                        if (quest->ReqCreatureOrGOId[objective])
                            objectiveEntries.push_back(quest->ReqCreatureOrGOId[objective]);

                        if (quest->ReqItemId[objective])
                        {
                            std::list<int32> dropList = GAI_VALUE2(std::list<int32>, "item drop list", quest->ReqItemId[objective]);
                            for (int32 entry : dropList)
                                objectiveEntries.push_back(entry);

                            std::list<int32> vendorList = GAI_VALUE2(std::list<int32>, "item vendor list", quest->ReqItemId[objective]);
                            for (int32 entry : vendorList)
                                objectiveEntries.push_back(entry);
                        }
                    }
                }
            }

            *futureDestinations = LaunchTravelDestinations(
                [partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, questId,
                questComplete, questInProgress, objectiveEntries]()
                {
                    PartitionedTravelList list;

                    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);
                    if (!quest)
                        return list;

                    if (questComplete)
                    {
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            (uint32)TravelDestinationPurpose::QuestTaker, {}, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                        {
                            for (auto& point : points)
                            {
                                QuestTravelDestination* questDest = dynamic_cast<QuestTravelDestination*>(std::get<TravelDestination*>(point));
                                if (questDest && questDest->GetQuestId() == questId)
                                    list[partition].push_back(point);
                            }
                        }
                    }
                    else if (questInProgress && !objectiveEntries.empty())
                    {
                        uint32 allObjectiveFlags = (uint32)TravelDestinationPurpose::QuestAllObjective;
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            allObjectiveFlags, objectiveEntries, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                            list[partition].insert(list[partition].end(), points.begin(), points.end());

                        if (list.empty())
                        {
                            subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                                (uint32)TravelDestinationPurpose::Grind, objectiveEntries, false, 1000000.0f);
                            for (auto& [partition, points] : subList)
                                list[partition].insert(list[partition].end(), points.begin(), points.end());
                        }
                    }
                    else
                    {
                        PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo,
                            (uint32)TravelDestinationPurpose::QuestGiver, {}, false, 1000000.0f);
                        for (auto& [partition, points] : subList)
                        {
                            for (auto& point : points)
                            {
                                QuestTravelDestination* questDest = dynamic_cast<QuestTravelDestination*>(std::get<TravelDestination*>(point));
                                if (questDest && questDest->GetQuestId() == questId)
                                    list[partition].push_back(point);
                            }
                        }
                    }

                    return list;
                }
            );

            SET_AI_VALUE2(std::string, "manual string", "future travel detail", orderTarget);
        }
        else if (order.type == GuildOrderType::Farm || order.type == GuildOrderType::Kill)
        {
            *futureDestinations = LaunchTravelDestinations([travelInfo = PlayerTravelInfo(bot), center, orderTarget, partitions = travelPartitions]()
                {
                    PartitionedTravelList list;

                    uint32 foundItemId = GuildOrderValue::FindItemByName(orderTarget);

                    if (foundItemId)
                    {
                        std::list<int32> dropEntries = GAI_VALUE2(std::list<int32>, "item drop list", foundItemId);

                        if (!dropEntries.empty())
                        {
                            std::vector<int32> gatherEntries, mobEntries;
                            for (int32 entry : dropEntries)
                            {
                                if (entry < 0)
                                    gatherEntries.push_back(entry);
                                else
                                    mobEntries.push_back(entry);
                            }

                            // Check which gathering skills the bot actually has.
                            bool hasHerbalism = travelInfo.GetCurrentSkill(SKILL_HERBALISM) > 0;
                            bool hasMining = travelInfo.GetCurrentSkill(SKILL_MINING) > 0;
                            bool hasSkinning = travelInfo.GetCurrentSkill(SKILL_SKINNING) > 0;
                            bool hasAnyGathering = hasHerbalism || hasMining || hasSkinning;

                            // Bot has a gathering skill: prioritize gather nodes.
                            if (!gatherEntries.empty() && hasAnyGathering)
                            {
                                // Only query gather purposes the bot can actually use.
                                if (hasHerbalism)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherHerbalism, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (hasMining)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherMining, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (hasSkinning)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherSkinning, gatherEntries, true);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                            }

                            // If entry-based gather lookup failed, try unfiltered gather by purpose
                            // (the travel manager may index nodes by their own entry, not drop-source entry).
                            if (list.empty() && hasAnyGathering && !gatherEntries.empty())
                            {
                                if (hasHerbalism)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherHerbalism);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (list.empty() && hasMining)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherMining);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                                if (list.empty() && hasSkinning)
                                {
                                    PartitionedTravelList gatherList = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GatherSkinning);
                                    for (auto& [partition, points] : gatherList)
                                        list[partition].insert(list[partition].end(), points.begin(), points.end());
                                }
                            }

                            // Fall back to mob drops only if no gather nodes were found or bot has no gathering skill.
                            if (list.empty() && !mobEntries.empty() && !hasAnyGathering)
                            {
                                uint32 mobPurpose = (uint32)TravelDestinationPurpose::Grind;

                                list = sTravelMgr.GetPartitions(center, partitions, travelInfo, mobPurpose, mobEntries, false);
                            }
                        }
                    }

                    // Fall back by name: if bot has gathering skills, try gather-only first.
                    if (list.empty())
                    {
                        bool hasHerbalism = travelInfo.GetCurrentSkill(SKILL_HERBALISM) > 0;
                        bool hasMining = travelInfo.GetCurrentSkill(SKILL_MINING) > 0;
                        bool hasSkinning = travelInfo.GetCurrentSkill(SKILL_SKINNING) > 0;
                        bool hasAnyGathering = hasHerbalism || hasMining || hasSkinning;

                        // Try gather nodes by name first if bot can gather.
                        if (hasAnyGathering)
                        {
                            for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, false, false, false, false, false, true))
                            {
                                std::list<uint8> chancesToGoFar = { 10,50,90 };
                                WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                                if (!point) continue;
                                list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                            }
                        }

                        // If still empty, fall back to mobs, bosses and gather nodes.
                        if (list.empty())
                        {
                            bool includeMobs = !hasAnyGathering;
                            for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, false, includeMobs, false, includeMobs, includeMobs, true))
                            {
                                std::list<uint8> chancesToGoFar = { 10,50,90 };
                                WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                                if (!point) continue;
                                list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                            }
                        }
                    }

                    return list;
                }
            );
        }
        else if (order.type == GuildOrderType::Explore)
        {
            *futureDestinations = LaunchTravelDestinations([travelInfo = PlayerTravelInfo(bot), center, orderTarget]()
                {
                    PartitionedTravelList list;
                    for (auto& destination : ChooseTravelTargetAction::FindDestination(travelInfo, orderTarget, true, false, false, false, false, false))
                    {
                        std::list<uint8> chancesToGoFar = { 10,50,90 };
                        WorldPosition* point = destination->GetNextPoint(center, chancesToGoFar);
                        if (!point) continue;
                        list[0].push_back(TravelPoint(destination, point, point->distance(center)));
                    }

                    return list;
                }
            );
        }
        else if (order.type == GuildOrderType::AuctionHouse)
        {
            *futureDestinations = LaunchTravelDestinations([partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
                {
                    PartitionedTravelList list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GenericRpg);

                    for (auto& [partition, travelPoints] : list)
                    {
                        travelPoints.erase(std::remove_if(travelPoints.begin(), travelPoints.end(), [](TravelPoint point)
                            {
                                EntryTravelDestination* dest = (EntryTravelDestination*)std::get<TravelDestination*>(point);
                                if (!dest->GetCreatureInfo())
                                    return true;

                                if (dest->GetCreatureInfo()->npc_flags & UNIT_NPC_FLAG_AUCTIONEER)
                                    return false;

                                return true;
                            }), travelPoints.end());
                    }
                    return list;
                });
        }
        else
        {
            return false;
        }

        SET_AI_VALUE2(std::string, "manual string", "future travel detail", orderTarget);
    }
    else if (travelName.find("trainer") == 0)
    {
        TrainerType type = TRAINER_TYPE_CLASS;

        if (travelName == "trainer mount")
            type = TRAINER_TYPE_MOUNTS;
        if (travelName == "trainer trade")
            type = TRAINER_TYPE_TRADESKILLS;
        if (travelName == "trainer pet")
            type = TRAINER_TYPE_PETS;

        std::vector<int32> trainerEntries = AI_VALUE2(std::vector <int32>, "available trainers", type);

        if (trainerEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No trainer entries found for " + getQualifier(), "debug travel");
            return false;
        }

        *futureDestinations = LaunchTravelDestinations([entries = trainerEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Trainer, entries, false);
            });
    }
    else if (travelName == "mount")
    {
        std::vector<int32> mountVendorEntries = AI_VALUE(std::vector <int32>, "available mount vendors");

        if (mountVendorEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No vendor entries found for " + getQualifier(), "debug travel");
            return false;
        }

        *futureDestinations = LaunchTravelDestinations([entries = mountVendorEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Vendor, entries, false);
            });
    }
    else if (travelName == "reagent vendor")
    {
        std::set<int32> reagentVendorEntrySet;
        std::vector<uint32> missingReagents = NeedsProfessionReagentsValue::GetMissingReagents(ai);
        const uint32 cityId = GetKnowledgeCityId(bot);
        for (uint32 reagentId : missingReagents)
        {
            std::list<int32> vendorEntries = GAI_VALUE2(std::list<int32>, "item vendor list", reagentId);
            for (int32 entry : vendorEntries)
                reagentVendorEntrySet.insert(entry);
        }

        std::vector<int32> reagentVendorEntries(reagentVendorEntrySet.begin(), reagentVendorEntrySet.end());
        std::unordered_map<int32, float> vendorScores;

        for (int32 entry : reagentVendorEntries)
        {
            float vendorConfidence = 0.0f;
            for (uint32 reagentId : missingReagents)
            {
                vendorConfidence = std::max(vendorConfidence,
                    sServerSharedKnowledge.GetReagentVendorItemConfidence(
                        entry, GetReagentRequirement(bot, reagentId), reagentId, bot->GetMapId(), cityId));
            }

            vendorScores[entry] = GetNpcPreferenceScore(bot, vendorConfidence, static_cast<uint32>(NpcKnowledgePurpose::REAGENT_VENDOR_ITEM));
        }

        std::stable_sort(reagentVendorEntries.begin(), reagentVendorEntries.end(), [&](int32 left, int32 right)
        {
            return vendorScores[left] > vendorScores[right];
        });

        if (reagentVendorEntries.empty())
        {
            ai->TellDebug(ai->GetMaster(), "No reagent vendor entries found", "debug travel");
            return false;
        }

        *futureDestinations = LaunchTravelDestinations([entries = reagentVendorEntries, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                return sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::Vendor, entries, false);
            });
    }
    else
    {
        uint32 useFlags;

        if (travelName == "city")
            useFlags = NPCFlags::UNIT_NPC_FLAG_BANKER | NPCFlags::UNIT_NPC_FLAG_BATTLEMASTER | NPCFlags::UNIT_NPC_FLAG_AUCTIONEER;
        else if (travelName == "tabard")
            useFlags = NPCFlags::UNIT_NPC_FLAG_TABARDDESIGNER;
        else if (travelName == "petition")
            useFlags = NPCFlags::UNIT_NPC_FLAG_PETITIONER;


        *futureDestinations = LaunchTravelDestinations([cityFlags = useFlags, partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center]()
            {
                PartitionedTravelList list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::GenericRpg);

                for (auto& [partition, travelPoints] : list)
                {
                    travelPoints.erase(std::remove_if(travelPoints.begin(), travelPoints.end(), [cityFlags](TravelPoint point)
                        {
                            EntryTravelDestination* dest = (EntryTravelDestination*)std::get<TravelDestination*>(point);
                            if (!dest->GetCreatureInfo())
                                return true;

                            if (dest->GetCreatureInfo()->npc_flags & cityFlags)
                                return false;

                            return true;
                        }), travelPoints.end());
                }
                return list;
            });
    }

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", getQualifier());
    SET_AI_VALUE2(std::string, "manual string", "future travel condition",
        travelName == "guild meeting" ? "should travel named::guild meeting" :
        travelName == "guild order" ? "should travel named::guild order" :
        event.getSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", relevance * 100);

    return true;
}

bool RequestNamedTravelTargetAction::isAllowed() const
{
    std::string name = getQualifier();
    if (name == "city")
    {
        if (urand(1, 100) > 10)
            return false;
        return true;
    }
    else if (name == "pvp")
    {
        if (urand(0, 4))
            return false;
        return true;
    }
    else if (name == "guild meeting")
        return true;
    else if (name == "reagent vendor")
        return true;
    else if (name == "guild order")
        return true;
    else if (name == "mount")
    {
        if (urand(1, 100) > 100)
            return false;
        return true;
    }
    else if (name.find("trainer") == 0)
    {
        if (urand(1, 100) > 100)
            return false;
        return true;
    }
    else if (name == "tabard")
        return true;
    else if (name == "petition")
        return true;

    return false;
}

bool RequestQuestTravelTargetAction::Execute(Event& event)
{
    if (!ai || !bot || !context)
        return false;

    FutureDestinations* futureDestinations = AI_VALUE(FutureDestinations*, "future travel destinations");
    if (HasPendingTravelDestinations(futureDestinations))
    {
        TellTravelTrace(ai, ai->GetMaster(),
            "travel_suppressed reason=pending_fetch candidate=quest source=" + event.getSource() +
            " " + FormatDecisionState(ai) + " " + FormatCommittedTaskSummary(ai));
        return false;
    }

    WorldPosition center = event.getOwner() ? event.getOwner() : (GetMaster() ? GetMaster() : bot);
    const EntryQuestRelationMap relationMap = AI_VALUE_SAFE(EntryQuestRelationMap, "entry quest relation");
    const ArchetypeWeights& weights = ai->GetArchetypeWeights();
    const bool starterOrNewBot = IsStarterOrNewQuestBot(bot);
    const float nearbyQuestRange = std::max(120.0f, 120.0f * std::max(0.5f, weights.chainWeight));
    const float localQuestRange = std::max(450.0f, static_cast<float>((400 + bot->GetLevel() * 10) * std::max(0.5f, weights.chainWeight)));
    const float continuationRange = std::max(localQuestRange, starterOrNewBot ? 1500.0f : 900.0f);

    ai->TellDebug(ai->GetMaster(), "Getting new destination ranges for travel quest", "debug travel");

    std::vector<QuestIntentFetch> intentFetches;
    auto pushFetch = [&](QuestIntentKind kind, uint32 purpose, int32 questId, float range)
    {
        intentFetches.push_back({ kind, purpose, questId, range });
    };

    pushFetch(QuestIntentKind::NearbyQuestGiver, (uint32)TravelDestinationPurpose::QuestGiver, 0, nearbyQuestRange);
    pushFetch(QuestIntentKind::SameZoneQuestGiverContinuation, (uint32)TravelDestinationPurpose::QuestGiver, 0, continuationRange);

    for (ObjectGuid guid : AI_VALUE_SAFE(std::list<ObjectGuid>, "group members"))
    {
        Player* player = sObjectMgr.GetPlayer(guid);

        if (!player)
            continue;

        if (player->GetMapId() != bot->GetMapId())
            continue;

        if (!player->GetPlayerbotAI())
            continue;

        QuestStatusMap& questMap = player->GetQuestStatusMap();

        bool onlyClassQuest = bot == player && !urand(0, 10);

        //Find destinations related to the active quests.
        for (auto& [questId, questStatus] : questMap)
        {
            uint32 flag = 0;
            if (questStatus.m_rewarded)
                continue;

            Quest const* questTemplate = sObjectMgr.GetQuestTemplate(questId);

            if (!questTemplate)
                continue;

            if (player->CanRewardQuest(questTemplate, false))
                flag = (uint32)TravelDestinationPurpose::QuestTaker;
            else
            {
                for (uint32 objective = 0; objective < 4; objective++)
                {
                    TravelDestinationPurpose purposeFlag = (TravelDestinationPurpose)(1 << (objective + 1));

                    std::vector<std::string> qualifier = { std::to_string(questId), std::to_string(objective) };

                    if (AI_VALUE2(bool, "group or", "following party,need quest objective::" + Qualified::MultiQualify(qualifier, ","))) //Noone needs the quest objective.
                        flag = flag | (uint32)purposeFlag;
                }
            }

            if (!flag)
                continue;

            const TravelDestinationPurpose dominantPurpose =
                flag == (uint32)TravelDestinationPurpose::QuestTaker ? TravelDestinationPurpose::QuestTaker :
                (flag & (uint32)TravelDestinationPurpose::QuestObjective1) ? TravelDestinationPurpose::QuestObjective1 :
                TravelDestinationPurpose::QuestGiver;
            const float weightedRange = static_cast<float>(1000 + (bot->GetLevel() * bot->GetLevel()) * 75) *
                GetQuestArchetypePriorityMultiplier(bot, ai, dominantPurpose, questTemplate);

            if (flag == (uint32)TravelDestinationPurpose::QuestTaker)
            {
                pushFetch(QuestIntentKind::NearbyQuestTaker, flag, static_cast<int32>(questId), std::max(nearbyQuestRange, 250.0f));
                pushFetch(QuestIntentKind::RemoteQuestTaker, flag, static_cast<int32>(questId), weightedRange);
            }
            else
            {
                pushFetch(IsBotInStarterZone(bot) ? QuestIntentKind::StarterLocalPriority : QuestIntentKind::LocalObjective,
                    flag, static_cast<int32>(questId), std::max(localQuestRange, 350.0f));
                pushFetch(QuestIntentKind::RemoteObjective, flag, static_cast<int32>(questId), weightedRange);
            }

            if (onlyClassQuest && intentFetches.size() > 2) //Only do class quests if we have any.
            {
                Quest const* firstQuest = nullptr;
                for (QuestIntentFetch const& fetch : intentFetches)
                {
                    if (fetch.questId > 0)
                    {
                        firstQuest = sObjectMgr.GetQuestTemplate(static_cast<uint32>(fetch.questId));
                        if (firstQuest)
                            break;
                    }
                }

                if (!firstQuest)
                    continue;

                if (firstQuest->GetRequiredClasses() && !questTemplate->GetRequiredClasses())
                    continue;

                if (!firstQuest->GetRequiredClasses() && questTemplate->GetRequiredClasses())
                    intentFetches.erase(std::remove_if(intentFetches.begin(), intentFetches.end(),
                        [](QuestIntentFetch const& fetch) { return fetch.questId > 0; }), intentFetches.end());
            }
        }
    }

    if (!HasQuestIntent(bot))
    {
        pushFetch(bot->GetLevel() == 1 ? QuestIntentKind::LevelOneClosestQuest : QuestIntentKind::ClosestQuestFallback,
            (uint32)TravelDestinationPurpose::QuestGiver, 0, continuationRange);
    }
    else if (starterOrNewBot)
    {
        pushFetch(QuestIntentKind::ClosestQuestFallback, (uint32)TravelDestinationPurpose::QuestGiver, 0, continuationRange);
    }

    pushFetch(QuestIntentKind::BroadQuestGiver, (uint32)TravelDestinationPurpose::QuestGiver, 0, std::max(continuationRange, 2500.0f));

    std::vector<uint32> questIds;
    questIds.reserve(intentFetches.size());
    for (const auto& fetch : intentFetches)
    {
        if (fetch.questId > 0)
            questIds.push_back(static_cast<uint32>(fetch.questId));
    }

    std::sort(questIds.begin(), questIds.end());
    questIds.erase(std::unique(questIds.begin(), questIds.end()), questIds.end());

    LogStarterQuestState(ai, ai->GetMaster(), bot, "request");
    LogQuestIntentFetches(ai, ai->GetMaster(), bot, intentFetches);

    *futureDestinations = LaunchTravelDestinations([partitions = travelPartitions, travelInfo = PlayerTravelInfo(bot), center, intentFetches, questIds, relationMap, bot = bot]()
        {
            PartitionedTravelList list;
            for (QuestIntentFetch const& fetch : intentFetches)
            {
                PartitionedTravelList subList = sTravelMgr.GetPartitions(center, partitions, travelInfo, fetch.purpose, { fetch.questId }, true, fetch.range);

                for (auto& [partition, points] : subList)
                    list[partition].insert(list[partition].end(), points.begin(), points.end());
            }

            if (list.empty())
                list = sTravelMgr.GetPartitions(center, partitions, travelInfo, (uint32)TravelDestinationPurpose::QuestGiver);

            const uint32 mapId = bot ? bot->GetMapId() : 0;
            const uint32 cityId = GetKnowledgeCityId(bot);
            PlayerbotAI* botAi = bot ? bot->GetPlayerbotAI() : nullptr;
            std::unordered_map<uint32, float> questPriorityCache;
            const QuestIntentContext intentContext = BuildQuestIntentContext(bot, botAi, list, &relationMap);
            for (auto& [partition, points] : list)
            {
                std::stable_sort(points.begin(), points.end(), [&](const TravelPoint& left, const TravelPoint& right)
                {
                    TravelDestination* leftDestination = std::get<0>(left);
                    TravelDestination* rightDestination = std::get<0>(right);
                    WorldPosition* leftPosition = std::get<1>(left);
                    WorldPosition* rightPosition = std::get<1>(right);

                    const float leftScore =
                        GetQuestIntentScore(bot, botAi, leftDestination, leftPosition, &relationMap, &questIds, intentContext, questPriorityCache) +
                        GetQuestDestinationScore(bot, leftDestination, mapId, cityId, questIds);
                    const float rightScore =
                        GetQuestIntentScore(bot, botAi, rightDestination, rightPosition, &relationMap, &questIds, intentContext, questPriorityCache) +
                        GetQuestDestinationScore(bot, rightDestination, mapId, cityId, questIds);

                    if (leftScore == rightScore)
                        return std::get<2>(left) < std::get<2>(right);

                    return leftScore > rightScore;
                });
            }

            return list;
        }
    );

    AI_VALUE(TravelTarget*, "travel target")->SetStatus(TravelStatus::TRAVEL_STATUS_PREPARE);
    SET_AI_VALUE2(std::string, "manual string", "future travel purpose", "quest");
    SET_AI_VALUE2(std::string, "manual string", "future travel condition", event.getSource());
    SET_AI_VALUE2(int, "manual int", "future travel relevance", int(relevance * 100 * std::max(0.5f, weights.progressWeight)));

    return true;
}

bool RequestQuestTravelTargetAction::isAllowed() const
{
    if (AI_VALUE(bool, "should get money"))
        return urand(1, 100) < 90;
    else
        return urand(1, 100) < 95;

    return false;
}

bool FocusTravelTargetAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();

    if (text == "?")
    {
        std::set<uint32> questIds = AI_VALUE(focusQuestTravelList, "focus travel target");
        std::ostringstream out;
        if (questIds.empty())
            out << "No quests selected.";
        else
        {
            out << "I will try to only do the following " << questIds.size() << " quests:";

            for (auto questId : questIds)
            {
                const Quest* quest = sObjectMgr.GetQuestTemplate(questId);

                if (quest)
                    out << ChatHelper::formatQuest(quest);
            }

        }
        ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);
        return true;
    }

    std::set<uint32> questIds = ChatHelper::ExtractAllQuestIds(text);

    if (questIds.empty() && !text.empty())
    {
        if (Qualified::isValidNumberString(text))
            questIds.insert(stoi(text));
        else
        {
            std::vector<std::string> qualifiers = Qualified::getMultiQualifiers(text, ",");

            for (auto& qualifier : qualifiers)
                if (Qualified::isValidNumberString(qualifier))
                    questIds.insert(stoi(text));
        }
    }

    SET_AI_VALUE(focusQuestTravelList, "focus travel target", questIds);

    if (!ai->HasStrategy("travel", BotState::BOT_STATE_NON_COMBAT))
        ai->TellError(requester, "travel strategy disabled bot needs this to actually do the quest.");

    if (!ai->HasStrategy("rpg quest", BotState::BOT_STATE_NON_COMBAT))
        ai->TellError(requester, "rpg quest strategy disabled bot needs this to actually do the quest.");

    std::ostringstream out;
    if (questIds.empty())
        out << "I will now do all quests.";
    else
    {
        out << "I will now only try to do the following " << questIds.size() << " quests:";

        for (auto questId : questIds)
        {
            const Quest* quest = sObjectMgr.GetQuestTemplate(questId);

            if (quest)
                out << ChatHelper::formatQuest(quest);
        }

    }
    ai->TellPlayerNoFacing(requester, out.str(), PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, false);

    TravelTarget* oldTarget = AI_VALUE(TravelTarget*, "travel target");

    oldTarget->SetExpireIn(1000);
    
    return true;
}

// [patch_travel_deferred_future applied]
