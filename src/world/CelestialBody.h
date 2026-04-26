#pragma once
#include <string>
#include <vector>
#include "WorldEntity.h"

enum class BodyType { Star, Planet, Moon };

struct CelestialBody {
    std::string id;
    std::string name;
    BodyType    type             = BodyType::Planet;
    std::string parent_id;               // empty = top-level (star or orphan planet)
    double      radius_km        = 6371.0;
    double      axial_tilt_deg   = 23.5;
    double      rotation_h       = 24.0;
    double      orbital_period_d = 365.25;  // 0 for stars
    double      orbital_radius_au = 1.0;    // distance from parent in AU; 0 for stars

    std::string texture_path;    // absolute or relative path to colour image
    std::string heightmap_path;  // greyscale heightmap (r=0 low, r=1 high)
    float       height_scale = 0.05f; // displacement in scene units (sphere r=1)

    std::vector<WorldEntity> entities;
};
