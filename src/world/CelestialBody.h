#pragma once
#include <string>
#include <vector>
#include "WorldEntity.h"

enum class BodyType { Star, Planet, Moon };

struct CelestialBody {
    std::string id;
    std::string name;
    BodyType    type             = BodyType::Planet;
    double      radius_km        = 6371.0;
    double      axial_tilt_deg   = 23.5;
    double      rotation_h       = 24.0;
    double      orbital_period_d = 365.25;  // 0 for stars

    std::vector<WorldEntity> entities;
};
