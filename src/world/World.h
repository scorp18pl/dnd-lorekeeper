#pragma once
#include <filesystem>
#include <string>
#include "CelestialBody.h"
#include "CalendarSystem.h"

struct TravelWaypoint {
    float       lat_deg = 0.f;
    float       lon_deg = 0.f;
    int         day     = 0;
    std::string node_id;           // snapped to network node when non-empty
};

struct TravelRecord {
    std::string                  id;
    std::string                  name        = "Party";
    float                        color[3]    = { 0.86f, 0.39f, 1.0f };
    bool                         visible     = true;
    float                        speed_kmday = 40.0f;
    std::vector<TravelWaypoint>  waypoints;
};

struct World {
    std::string                  name;
    std::filesystem::path        rootPath;
    CelestialBody                body;
    CalendarSystem               calendar;
    std::vector<TravelRecord>    parties;

    bool isLoaded() const { return !rootPath.empty(); }
};
