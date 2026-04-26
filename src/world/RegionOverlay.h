#pragma once
#include <string>

struct RegionOverlay {
    std::string id;
    std::string name;
    float       center_lat   = 0.0f;    // degrees
    float       center_lon   = 0.0f;    // degrees
    float       extent_km    = 100.0f;  // side length of the square region in km
    float       opacity      = 1.0f;    // 0–1
    bool        visible      = true;
    std::string image_path;             // absolute path to RGBA/RGB image
    std::string heightmap_path;         // greyscale heightmap for this region
    float       height_scale = 0.05f;   // displacement in scene units (sphere r=1)
};
