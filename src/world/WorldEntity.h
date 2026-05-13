#pragma once
#include <optional>
#include <string>

enum class EntityType { City, Town, POI };

struct WorldEntity {
    std::string        id;
    std::string        name;
    EntityType         type    = EntityType::POI;
    float              lat_deg = 0.0f;
    float              lon_deg = 0.0f;
    std::string        media_ref;
    std::optional<int> born_day;  // nullopt = exists from beginning of time
    std::optional<int> died_day;  // nullopt = still exists
};
