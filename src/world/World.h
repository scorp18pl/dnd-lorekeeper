#pragma once
#include <filesystem>
#include <string>
#include "CelestialBody.h"
#include "CalendarSystem.h"

struct Party {
    std::string node_id;
    float       speed_kmday = 40.0f;
};

struct World {
    std::string           name;
    std::filesystem::path rootPath;
    CelestialBody         body;
    CalendarSystem        calendar;
    Party                 party;

    bool isLoaded() const { return !rootPath.empty(); }
};
