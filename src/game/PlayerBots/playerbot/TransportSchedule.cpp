#include "playerbot/TransportSchedule.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ObjectMgr.h"
#include "Transports/Transport.h"
#include "Transports/TransportMgr.h"

using namespace ai;

namespace
{
    struct ClassicTransportSeed
    {
        uint32 entry;
        const char* name;
        uint32 fallbackPeriodMs;
    };

    const ClassicTransportSeed kClassicTransportSeeds[] =
    {
        {20808, "Ratchet and Booty Bay", 350818},
        {164871, "Orgrimmar and Undercity", 356284},
        {175080, "Grom'Gol Base Camp and Orgrimmar", 303463},
        {176231, "Menethil Harbor and Theramore Isle", 329313},
        {176244, "Teldrassil and Auberdine", 316251},
        {176310, "Menethil Harbor and Auberdine", 295579},
        {176495, "Grom'Gol Base Camp and Undercity", 333044}
    };

    TransportRoute BuildRoute(const ClassicTransportSeed& seed)
    {
        TransportRoute route;
        route.transportEntry = seed.entry;
        route.periodMs = seed.fallbackPeriodMs;
        route.name = seed.name;

        if (TransportTemplate* transportTemplate = sTransportMgr.GetTransportTemplate(seed.entry))
        {
            if (transportTemplate->pathTime)
                route.periodMs = transportTemplate->pathTime;

            for (const KeyFrame& keyFrame : transportTemplate->keyFrames)
            {
                if (!keyFrame.IsStopFrame() || !keyFrame.Node)
                    continue;

                TransportStop stop;
                stop.mapId = keyFrame.Node->mapid;
                stop.dockX = keyFrame.Node->x;
                stop.dockY = keyFrame.Node->y;
                stop.dockZ = keyFrame.Node->z;
                stop.arrivalTimeMs = keyFrame.ArriveTime;
                stop.departureTimeMs = keyFrame.DepartureTime;

                const bool duplicateStop = !route.stops.empty() &&
                    route.stops.back().mapId == stop.mapId &&
                    fabs(route.stops.back().dockX - stop.dockX) < 1.0f &&
                    fabs(route.stops.back().dockY - stop.dockY) < 1.0f &&
                    fabs(route.stops.back().dockZ - stop.dockZ) < 2.0f;

                if (!duplicateStop)
                    route.stops.push_back(stop);
            }
        }

        if (route.periodMs == 0)
            route.periodMs = seed.fallbackPeriodMs;

        return route;
    }

    const TransportStop* FindClosestStop(const TransportRoute& route, const WorldPosition& dockPosition)
    {
        const TransportStop* closest = nullptr;
        float bestDistanceSq = std::numeric_limits<float>::max();

        for (const TransportStop& stop : route.stops)
        {
            if (stop.mapId != dockPosition.getMapId())
                continue;

            const float dx = dockPosition.getX() - stop.dockX;
            const float dy = dockPosition.getY() - stop.dockY;
            const float dz = dockPosition.getZ() - stop.dockZ;
            const float distanceSq = dx * dx + dy * dy + dz * dz;
            if (distanceSq < bestDistanceSq)
            {
                bestDistanceSq = distanceSq;
                closest = &stop;
            }
        }

        return closest;
    }
}

const std::unordered_map<uint32, TransportRoute>& TransportSchedule::GetRoutes()
{
    static const std::unordered_map<uint32, TransportRoute> routes = []()
    {
        std::unordered_map<uint32, TransportRoute> builtRoutes;
        for (const ClassicTransportSeed& seed : kClassicTransportSeeds)
            builtRoutes.emplace(seed.entry, BuildRoute(seed));
        return builtRoutes;
    }();

    return routes;
}

bool TransportSchedule::IsClassicTransport(uint32 transportEntry)
{
    return GetRoutes().find(transportEntry) != GetRoutes().end();
}

const TransportRoute* TransportSchedule::GetRoute(uint32 transportEntry)
{
    const auto& routes = GetRoutes();
    const auto itr = routes.find(transportEntry);
    return itr != routes.end() ? &itr->second : nullptr;
}

uint32 TransportSchedule::GetAverageWaitMs(uint32 transportEntry)
{
    const TransportRoute* route = GetRoute(transportEntry);
    if (!route || !route->periodMs)
        return 45000;

    return std::min(route->periodMs / 2, kMaxWaitMs);
}

uint32 TransportSchedule::GetExpectedWaitMs(uint32 transportEntry, const WorldPosition& dockPosition, GenericTransport* liveTransport)
{
    if (liveTransport && liveTransport->GetEntry() == transportEntry)
    {
        WorldPosition transportPosition(liveTransport);
        if (transportPosition.getMapId() == dockPosition.getMapId())
        {
            const float distance = dockPosition.distance(transportPosition);
            if (distance < INTERACTION_DISTANCE * 3.0f)
                return 0;
        }
    }

    const TransportRoute* route = GetRoute(transportEntry);
    if (!route || !route->periodMs)
        return 45000;

    const TransportStop* stop = FindClosestStop(*route, dockPosition);
    if (!stop)
        return GetAverageWaitMs(transportEntry);

    if (stop->departureTimeMs >= stop->arrivalTimeMs)
    {
        const uint32 dwellTime = stop->departureTimeMs - stop->arrivalTimeMs;
        const uint32 expectedWait = route->periodMs > dwellTime ? (route->periodMs - dwellTime) / 2 : route->periodMs / 2;
        return std::min(expectedWait, kMaxWaitMs);
    }

    return GetAverageWaitMs(transportEntry);
}

uint32 TransportSchedule::GetWaitWindowMs(uint32 transportEntry, const WorldPosition& dockPosition, GenericTransport* liveTransport)
{
    const uint32 expectedWait = GetExpectedWaitMs(transportEntry, dockPosition, liveTransport);
    const uint32 paddedWait = expectedWait + 30000;
    return std::min(std::max<uint32>(paddedWait, 45000), kMaxWaitMs);
}
