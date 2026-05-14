#pragma once
#include <string>
#include <vector>
#include "RoadGraph.h"
#include "RegionOverlay.h"

struct CelestialBody {
    std::string id;
    std::string name;
    double      radius_km = 6371.0;
    std::string texture_path;

    std::vector<RegionOverlay> overlays;
    RouteGraph                 network;
};
