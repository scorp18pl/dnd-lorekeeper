#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "CelestialBody.h"

struct World {
    std::string              name;
    std::filesystem::path    rootPath;
    std::vector<CelestialBody>   bodies;

    bool isLoaded() const { return !rootPath.empty(); }
};
