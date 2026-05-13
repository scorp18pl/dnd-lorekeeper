#include "WorldSerializer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

static std::filesystem::path worldJsonPath(const std::filesystem::path& root) {
    return root / "world.json";
}

static const char* entityTypeName(EntityType t) {
    switch (t) {
        case EntityType::City: return "city";
        case EntityType::Town: return "town";
        default:               return "poi";
    }
}

static EntityType entityTypeFromString(const std::string& s) {
    if (s == "city") return EntityType::City;
    if (s == "town") return EntityType::Town;
    return EntityType::POI;
}

static json serializeBody(const CelestialBody& b) {
    json bj;
    bj["id"]           = b.id;
    bj["name"]         = b.name;
    bj["texture_path"] = b.texture_path;
    bj["radius_km"]    = b.radius_km;

    json entsArr = json::array();
    for (const auto& e : b.entities) {
        json ej;
        ej["id"]        = e.id;
        ej["name"]      = e.name;
        ej["type"]      = entityTypeName(e.type);
        ej["lat_deg"]   = e.lat_deg;
        ej["lon_deg"]   = e.lon_deg;
        ej["media_ref"] = e.media_ref;
        entsArr.push_back(ej);
    }
    bj["entities"] = entsArr;

    json ovsArr = json::array();
    for (const auto& ov : b.overlays) {
        json oj;
        oj["id"]         = ov.id;
        oj["name"]       = ov.name;
        oj["center_lat"] = ov.center_lat;
        oj["center_lon"] = ov.center_lon;
        oj["extent_km"]  = ov.extent_km;
        oj["opacity"]    = ov.opacity;
        oj["visible"]    = ov.visible;
        oj["image_path"] = ov.image_path;
        ovsArr.push_back(oj);
    }
    bj["overlays"] = ovsArr;

    return bj;
}

static CelestialBody deserializeBody(const json& bj) {
    CelestialBody b;
    b.id           = bj.value("id",           "");
    b.name         = bj.value("name",         "Unnamed");
    b.texture_path = bj.value("texture_path", "");
    b.radius_km    = bj.value("radius_km",    6371.0);

    if (bj.contains("overlays") && bj["overlays"].is_array()) {
        for (const auto& oj : bj["overlays"]) {
            RegionOverlay ov;
            ov.id         = oj.value("id",         "");
            ov.name       = oj.value("name",       "New Overlay");
            ov.center_lat = (float)oj.value("center_lat", 0.0);
            ov.center_lon = (float)oj.value("center_lon", 0.0);
            ov.extent_km  = (float)oj.value("extent_km",  100.0);
            ov.opacity    = (float)oj.value("opacity",    1.0);
            ov.visible    = oj.value("visible",    true);
            ov.image_path = oj.value("image_path", "");
            b.overlays.push_back(ov);
        }
    }

    if (bj.contains("entities") && bj["entities"].is_array()) {
        for (const auto& ej : bj["entities"]) {
            WorldEntity e;
            e.id        = ej.value("id",        "");
            e.name      = ej.value("name",      "Unnamed");
            e.type      = entityTypeFromString(ej.value("type", "poi"));
            e.lat_deg   = ej.value("lat_deg",   0.0f);
            e.lon_deg   = ej.value("lon_deg",   0.0f);
            e.media_ref = ej.value("media_ref", "");
            b.entities.push_back(e);
        }
    }

    return b;
}

bool WorldSerializer::save(const World& world) {
    std::error_code ec;
    std::filesystem::create_directories(world.rootPath, ec);
    if (ec) {
        std::cerr << "WorldSerializer: cannot create " << world.rootPath << ": " << ec.message() << "\n";
        return false;
    }

    json j;
    j["name"]    = world.name;
    j["version"] = "0.4";
    j["body"]    = serializeBody(world.body);

    std::ofstream f(worldJsonPath(world.rootPath));
    if (!f) {
        std::cerr << "WorldSerializer: cannot write world.json\n";
        return false;
    }
    f << j.dump(2) << "\n";
    return true;
}

bool WorldSerializer::load(const std::filesystem::path& rootPath, World& out) {
    auto path = worldJsonPath(rootPath);
    std::ifstream f(path);
    if (!f) {
        std::cerr << "WorldSerializer: " << path << " not found\n";
        return false;
    }

    json j = json::parse(f, nullptr, /*exceptions=*/false);
    if (j.is_discarded()) {
        std::cerr << "WorldSerializer: world.json parse error\n";
        return false;
    }

    out.name     = j.value("name", "Untitled World");
    out.rootPath = rootPath;

    // New format: single "body" object
    if (j.contains("body") && j["body"].is_object()) {
        out.body = deserializeBody(j["body"]);
    }
    // Legacy: "bodies" array — load first entry
    else if (j.contains("bodies") && j["bodies"].is_array() && !j["bodies"].empty()) {
        out.body = deserializeBody(j["bodies"][0]);
    }

    return true;
}

bool WorldSerializer::createNew(const std::filesystem::path& rootPath,
                                 const std::string& name,
                                 World& out) {
    for (auto& sub : {"media/notes", "assets/textures"}) {
        std::error_code ec;
        std::filesystem::create_directories(rootPath / sub, ec);
        if (ec) {
            std::cerr << "WorldSerializer: cannot create " << sub << ": " << ec.message() << "\n";
            return false;
        }
    }

    out.name        = name;
    out.rootPath    = rootPath;
    out.body        = CelestialBody{};
    out.body.id     = "world";
    out.body.name   = name;
    return save(out);
}
