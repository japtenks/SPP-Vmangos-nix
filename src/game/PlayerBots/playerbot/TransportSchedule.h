#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "WorldPosition.h"

class GenericTransport;

namespace ai
{
    struct TransportStop
    {
        uint32 mapId = 0;
        float dockX = 0.0f;
        float dockY = 0.0f;
        float dockZ = 0.0f;
        uint32 arrivalTimeMs = 0;
        uint32 departureTimeMs = 0;
    };

    struct TransportRoute
    {
        uint32 transportEntry = 0;
        uint32 periodMs = 0;
        std::vector<TransportStop> stops = {};
        std::string name;
    };

    class TransportSchedule
    {
    public:
        static constexpr uint32 kMaxWaitMs = 180000;

        static bool IsClassicTransport(uint32 transportEntry);
        static const TransportRoute* GetRoute(uint32 transportEntry);
        static uint32 GetAverageWaitMs(uint32 transportEntry);
        static uint32 GetExpectedWaitMs(uint32 transportEntry, const WorldPosition& dockPosition, GenericTransport* liveTransport = nullptr);
        static uint32 GetWaitWindowMs(uint32 transportEntry, const WorldPosition& dockPosition, GenericTransport* liveTransport = nullptr);

    private:
        static const std::unordered_map<uint32, TransportRoute>& GetRoutes();
    };
}
