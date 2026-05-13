#pragma once
#include <filesystem>
#include <string>
#include "CelestialBody.h"

struct World {
    std::string           name;
    std::filesystem::path rootPath;
    CelestialBody         body;

    bool isLoaded() const { return !rootPath.empty(); }
};
