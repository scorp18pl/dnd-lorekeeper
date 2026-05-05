#include "WorldSerializer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cmath>

using json = nlohmann::json;

static std::filesystem::path worldJsonPath(const std::filesystem::path& root) {
    return root / "world.json";
}

static const char* bodyTypeName(BodyType t) {
    switch (t) {
        case BodyType::Star:   return "star";
        case BodyType::Moon:   return "moon";
        default:               return "planet";
    }
}

static BodyType bodyTypeFromString(const std::string& s) {
    if (s == "star")  return BodyType::Star;
    if (s == "moon")  return BodyType::Moon;
    return BodyType::Planet;
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

bool WorldSerializer::save(const World& world) {
    std::error_code ec;
    std::filesystem::create_directories(world.rootPath, ec);
    if (ec) {
        std::cerr << "WorldSerializer: cannot create " << world.rootPath << ": " << ec.message() << "\n";
        return false;
    }

    json j;
    j["name"]    = world.name;
    j["version"] = "0.2";

    json bodiesArr = json::array();
    for (const auto& b : world.bodies) {
        json bj;
        bj["id"]                = b.id;
        bj["name"]              = b.name;
        bj["type"]              = bodyTypeName(b.type);
        bj["parent_id"]         = b.parent_id;
        bj["texture_path"]      = b.texture_path;
        bj["heightmap_path"]    = b.heightmap_path;
        bj["height_scale"]      = b.height_scale;
        bj["radius_km"]         = b.radius_km;
        bj["axial_tilt_deg"]    = b.axial_tilt_deg;
        bj["rotation_h"]        = b.rotation_h;
        bj["orbital_period_d"]  = b.orbital_period_d;
        bj["orbital_radius_au"] = b.orbital_radius_au;

        // Political LOD ownership (new format)
        bj["lod_min_cell_km"] = b.lod_min_cell_km;
        json lodObj = json::object();
        for (int L = 0; L < (int)b.lod_ownership.size(); ++L) {
            if (b.lod_ownership[L].empty()) continue;
            json ownerObj = json::object();
            for (const auto& [cell_id, entity_id] : b.lod_ownership[L])
                ownerObj[std::to_string(cell_id)] = entity_id;
            lodObj[std::to_string(L)] = ownerObj;
        }
        bj["lod_ownership"] = lodObj;

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
            oj["id"]             = ov.id;
            oj["name"]           = ov.name;
            oj["center_lat"]     = ov.center_lat;
            oj["center_lon"]     = ov.center_lon;
            oj["extent_km"]      = ov.extent_km;
            oj["opacity"]        = ov.opacity;
            oj["visible"]        = ov.visible;
            oj["image_path"]     = ov.image_path;
            oj["heightmap_path"] = ov.heightmap_path;
            oj["height_scale"]   = ov.height_scale;
            ovsArr.push_back(oj);
        }
        bj["overlays"] = ovsArr;

        bodiesArr.push_back(bj);
    }
    j["bodies"] = bodiesArr;

    // Political entities
    json politArr = json::array();
    for (const auto& pe : world.political_entities) {
        json pj;
        pj["id"]      = pe.id;
        pj["name"]    = pe.name;
        pj["color_r"] = pe.color_r;
        pj["color_g"] = pe.color_g;
        pj["color_b"] = pe.color_b;
        politArr.push_back(pj);
    }
    j["political_entities"] = politArr;

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
    out.bodies.clear();
    out.political_entities.clear();

    if (j.contains("bodies") && j["bodies"].is_array()) {
        for (const auto& bj : j["bodies"]) {
            CelestialBody b;
            b.id                = bj.value("id", "");
            b.name              = bj.value("name", "Unnamed");
            b.type              = bodyTypeFromString(bj.value("type", "planet"));
            b.parent_id         = bj.value("parent_id",         "");
            b.texture_path      = bj.value("texture_path",      "");
            b.heightmap_path    = bj.value("heightmap_path",    "");
            b.height_scale      = (float)bj.value("height_scale",    0.05);
            b.radius_km         = bj.value("radius_km",         6371.0);
            b.axial_tilt_deg    = bj.value("axial_tilt_deg",    23.5);
            b.rotation_h        = bj.value("rotation_h",        24.0);
            b.orbital_period_d  = bj.value("orbital_period_d",  365.25);
            b.orbital_radius_au = bj.value("orbital_radius_au", 1.0);

            if (bj.contains("overlays") && bj["overlays"].is_array()) {
                for (const auto& oj : bj["overlays"]) {
                    RegionOverlay ov;
                    ov.id             = oj.value("id",             "");
                    ov.name           = oj.value("name",           "New Overlay");
                    ov.center_lat     = (float)oj.value("center_lat",   0.0);
                    ov.center_lon     = (float)oj.value("center_lon",   0.0);
                    ov.extent_km      = (float)oj.value("extent_km",    100.0);
                    ov.opacity        = (float)oj.value("opacity",       1.0);
                    ov.visible        = oj.value("visible",        true);
                    ov.image_path     = oj.value("image_path",     "");
                    ov.heightmap_path = oj.value("heightmap_path", "");
                    ov.height_scale   = (float)oj.value("height_scale",  0.05);
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

            // Political LOD ownership — handle all legacy formats
            b.lod_min_cell_km = bj.value("lod_min_cell_km", 1);

            if (bj.contains("lod_ownership") && bj["lod_ownership"].is_object()) {
                // New format (v0.2+)
                for (const auto& [lStr, ownerObj] : bj["lod_ownership"].items()) {
                    int L = std::stoi(lStr);
                    if (L < 0) continue;
                    while ((int)b.lod_ownership.size() <= L) b.lod_ownership.emplace_back();
                    if (ownerObj.is_object())
                        for (const auto& [key, val] : ownerObj.items())
                            if (val.is_string())
                                b.lod_ownership[L][std::stoi(key)] = val.get<std::string>();
                }
            } else {
                // Legacy: goldberg_resolution + cell_ownership (or political_levels[0])
                int subdiv = 32;
                std::unordered_map<int, std::string> old_ownership;
                if (bj.contains("political_levels") && bj["political_levels"].is_array()
                    && !bj["political_levels"].empty()) {
                    const auto& lj = bj["political_levels"][0];
                    subdiv = lj.value("subdiv", 8);
                    if (lj.contains("cell_ownership") && lj["cell_ownership"].is_object())
                        for (const auto& [key, val] : lj["cell_ownership"].items())
                            if (val.is_string())
                                old_ownership[std::stoi(key)] = val.get<std::string>();
                } else {
                    subdiv = bj.value("goldberg_resolution", 32);
                    if (bj.contains("cell_ownership") && bj["cell_ownership"].is_object())
                        for (const auto& [key, val] : bj["cell_ownership"].items())
                            if (val.is_string())
                                old_ownership[std::stoi(key)] = val.get<std::string>();
                }
                int level = std::max(0, (int)std::round(std::log2(std::max(1, subdiv))));
                while ((int)b.lod_ownership.size() <= level) b.lod_ownership.emplace_back();
                b.lod_ownership[level] = std::move(old_ownership);
            }

            out.bodies.push_back(b);
        }
    }

    // Political entities (new simple format)
    if (j.contains("political_entities") && j["political_entities"].is_array()) {
        for (const auto& pj : j["political_entities"]) {
            PoliticalEntity pe;
            pe.id      = pj.value("id",      "");
            pe.name    = pj.value("name",    "Unnamed");
            // New format: color_r/g/b fields
            if (pj.contains("color_r")) {
                pe.color_r = (uint8_t)pj.value("color_r", 128);
                pe.color_g = (uint8_t)pj.value("color_g", 128);
                pe.color_b = (uint8_t)pj.value("color_b", 128);
            } else if (pj.contains("color") && pj["color"].is_array() && pj["color"].size() >= 3) {
                // Legacy: color array [r,g,b,a]
                pe.color_r = (uint8_t)(int)pj["color"][0];
                pe.color_g = (uint8_t)(int)pj["color"][1];
                pe.color_b = (uint8_t)(int)pj["color"][2];
            }
            out.political_entities.push_back(pe);
        }
    }

    return true;
}

bool WorldSerializer::createNew(const std::filesystem::path& rootPath,
                                 const std::string& name,
                                 World& out) {
    for (auto& sub : {
            "bodies",
            "media/notes",
            "assets/textures",
            "assets/heightmaps"}) {
        std::error_code ec;
        std::filesystem::create_directories(rootPath / sub, ec);
        if (ec) {
            std::cerr << "WorldSerializer: cannot create " << sub << ": " << ec.message() << "\n";
            return false;
        }
    }

    out.name     = name;
    out.rootPath = rootPath;
    out.bodies.clear();
    return save(out);
}
