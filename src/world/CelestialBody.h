#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "WorldEntity.h"
#include "RegionOverlay.h"

enum class BodyType { Star, Planet, Moon };

struct PoliticalLODLevel {
    int subdiv = 8;
    std::unordered_map<int, std::string> cell_ownership;
};

struct CelestialBody {
    std::string id;
    std::string name;
    BodyType    type             = BodyType::Planet;
    std::string parent_id;
    double      radius_km        = 6371.0;
    double      axial_tilt_deg   = 23.5;
    double      rotation_h       = 24.0;
    double      orbital_period_d = 365.25;
    double      orbital_radius_au = 1.0;

    std::string texture_path;
    std::string heightmap_path;
    float       height_scale = 0.05f;

    // LOD levels: index 0 = finest (shown when close), back() = coarsest (shown far).
    // Each level has its own Goldberg subdivision and independent cell ownership.
    std::vector<PoliticalLODLevel> political_levels;

    std::vector<WorldEntity>   entities;
    std::vector<RegionOverlay> overlays;

    void ensureDefaultPoliticalLevels() {
        if (!political_levels.empty()) return;
        political_levels = {{ 32, {} }, { 16, {} }, { 8, {} }};
    }
};
