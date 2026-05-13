#pragma once
#include <string>
#include <vector>
#include "WorldEntity.h"
#include "RegionOverlay.h"
#include "RoadGraph.h"

struct CelestialBody {
    std::string id;
    std::string name;
    double      radius_km    = 6371.0;

    std::string texture_path;

    std::vector<WorldEntity>   entities;
    std::vector<RegionOverlay> overlays;
    RoadGraph                  roads;
    RoadGraph                  sea_routes;
};
