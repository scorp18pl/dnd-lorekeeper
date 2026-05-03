#include "WorldSerializer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

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
    j["version"] = "0.1";

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
        bj["orbital_radius_au"]    = b.orbital_radius_au;
        json polLevels = json::array();
        for (const auto& lvl : b.political_levels) {
            json lj;
            lj["subdiv"] = lvl.subdiv;
            json ownerObj = json::object();
            for (const auto& [cell_id, entity_id] : lvl.cell_ownership)
                ownerObj[std::to_string(cell_id)] = entity_id;
            lj["cell_ownership"] = ownerObj;
            polLevels.push_back(lj);
        }
        bj["political_levels"] = polLevels;

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

    json polArr = json::array();
    for (const auto& pe : world.political_entities) {
        json pj;
        pj["id"]       = pe.id;
        pj["name"]     = pe.name;
        pj["color"]    = { pe.color.r, pe.color.g, pe.color.b, pe.color.a };
        pj["type"]     = pe.type;
        pj["liege_id"] = pe.liege_id;
        polArr.push_back(pj);
    }
    j["political_entities"] = polArr;

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

    if (j.contains("political_entities") && j["political_entities"].is_array()) {
        for (const auto& pj : j["political_entities"]) {
            PoliticalEntity pe;
            pe.id       = pj.value("id",       "");
            pe.name     = pj.value("name",     "Unnamed");
            pe.type     = pj.value("type",     "");
            pe.liege_id = pj.value("liege_id", "");
            if (pj.contains("color") && pj["color"].is_array() && pj["color"].size() == 4)
                pe.color = { pj["color"][0], pj["color"][1], pj["color"][2], pj["color"][3] };
            out.political_entities.push_back(pe);
        }
    }

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
            b.orbital_radius_au    = bj.value("orbital_radius_au",   1.0);

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

            if (bj.contains("political_levels") && bj["political_levels"].is_array()) {
                for (const auto& lj : bj["political_levels"]) {
                    PoliticalLODLevel lvl;
                    lvl.subdiv = lj.value("subdiv", 8);
                    if (lj.contains("cell_ownership") && lj["cell_ownership"].is_object())
                        for (const auto& [key, val] : lj["cell_ownership"].items())
                            if (val.is_string())
                                lvl.cell_ownership[std::stoi(key)] = val.get<std::string>();
                    b.political_levels.push_back(lvl);
                }
            } else {
                // Migrate old single-level format
                PoliticalLODLevel lvl;
                lvl.subdiv = bj.value("goldberg_resolution", 8);
                if (bj.contains("cell_ownership") && bj["cell_ownership"].is_object())
                    for (const auto& [key, val] : bj["cell_ownership"].items())
                        if (val.is_string())
                            lvl.cell_ownership[std::stoi(key)] = val.get<std::string>();
                b.political_levels.push_back(lvl);
            }
            b.ensureDefaultPoliticalLevels();

            out.bodies.push_back(b);
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
