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
        bj["id"]               = b.id;
        bj["name"]             = b.name;
        bj["type"]             = bodyTypeName(b.type);
        bj["radius_km"]        = b.radius_km;
        bj["axial_tilt_deg"]   = b.axial_tilt_deg;
        bj["rotation_h"]       = b.rotation_h;
        bj["orbital_period_d"] = b.orbital_period_d;
        bodiesArr.push_back(bj);
    }
    j["bodies"] = bodiesArr;

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

    if (j.contains("bodies") && j["bodies"].is_array()) {
        for (const auto& bj : j["bodies"]) {
            CelestialBody b;
            b.id               = bj.value("id", "");
            b.name             = bj.value("name", "Unnamed");
            b.type             = bodyTypeFromString(bj.value("type", "planet"));
            b.radius_km        = bj.value("radius_km",        6371.0);
            b.axial_tilt_deg   = bj.value("axial_tilt_deg",   23.5);
            b.rotation_h       = bj.value("rotation_h",       24.0);
            b.orbital_period_d = bj.value("orbital_period_d", 365.25);
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
