#pragma once
#include <filesystem>
#include <string>
#include "CelestialBody.h"
#include "CalendarSystem.h"

struct World {
    std::string           name;
    std::filesystem::path rootPath;
    CelestialBody         body;
    CalendarSystem        calendar;

    bool isLoaded() const { return !rootPath.empty(); }
};
