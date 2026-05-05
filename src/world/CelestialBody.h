#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include "WorldEntity.h"
#include "RegionOverlay.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

    // Political LOD quadtree (sparse).
    // lod_ownership[L][cell_id] = entity_id — only explicitly painted cells.
    // Level L uses GoldbergGrid(1 << L). Absent = inherit from ancestor.
    int lod_min_cell_km = 1;
    std::vector<std::unordered_map<int, std::string>> lod_ownership;

    std::vector<WorldEntity>   entities;
    std::vector<RegionOverlay> overlays;

    int maxPoliticalLevel() const {
        double ratio = (2.0 * M_PI * radius_km) / (lod_min_cell_km * std::sqrt(10.0));
        return (int)std::ceil(std::log2(std::max(ratio, 2.0)));
    }

    int maxPaintedLevel() const {
        int m = -1;
        for (int L = 0; L < (int)lod_ownership.size(); ++L)
            if (!lod_ownership[L].empty()) m = L;
        return m;
    }
};
