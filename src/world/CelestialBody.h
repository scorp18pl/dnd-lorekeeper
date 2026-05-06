#pragma once
#include <string>
#include <unordered_set>
#include <vector>
#include <cmath>
#include "WorldEntity.h"
#include "RegionOverlay.h"

enum class BodyType { Star, Planet, Moon };

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

    std::vector<WorldEntity>   entities;
    std::vector<RegionOverlay> overlays;
};
