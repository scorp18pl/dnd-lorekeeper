#pragma once
#include <string>

enum class EntityType { City, Town, POI };

struct WorldEntity {
    std::string id;
    std::string name;
    EntityType  type    = EntityType::POI;
    float       lat_deg = 0.0f;   // -90..90
    float       lon_deg = 0.0f;   // -180..180
    std::string media_ref;        // path to .md lore file, empty if none
};
