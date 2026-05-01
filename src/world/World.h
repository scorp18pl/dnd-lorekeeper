#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "CelestialBody.h"
#include "PoliticalEntity.h"

struct World {
    std::string              name;
    std::filesystem::path    rootPath;
    std::vector<CelestialBody>   bodies;
    std::vector<PoliticalEntity> political_entities;

    bool isLoaded() const { return !rootPath.empty(); }
};
