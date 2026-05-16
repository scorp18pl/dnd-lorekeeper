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

static const char* routeTypeName(RouteType t) {
    return (t == RouteType::Sea) ? "sea" : "road";
}

static RouteType routeTypeFromString(const std::string& s) {
    return (s == "sea") ? RouteType::Sea : RouteType::Road;
}

// ── Calendar ──────────────────────────────────────────────────────────────────

static json serializeCalendar(const CalendarSystem& cal) {
    json j;
    j["epoch_name"] = cal.epoch_name;

    json erasArr = json::array();
    for (const auto& era : cal.eras) {
        json ej;
        ej["name"]      = era.name;
        ej["start_day"] = era.start_day;
        erasArr.push_back(ej);
    }
    j["eras"] = erasArr;

    json monthsArr = json::array();
    for (const auto& m : cal.months) {
        json mj;
        mj["name"] = m.name;
        mj["days"] = m.days;
        monthsArr.push_back(mj);
    }
    j["months"] = monthsArr;

    j["week_days"] = cal.week_days;

    if (cal.leap_rule) {
        json lj;
        lj["every_n_years"] = cal.leap_rule->every_n_years;
        lj["extra_days"]    = cal.leap_rule->extra_days;
        j["leap_rule"] = lj;
    }

    return j;
}

static CalendarSystem deserializeCalendar(const json& j) {
    CalendarSystem cal;
    cal.epoch_name = j.value("epoch_name", "Year");

    if (j.contains("eras") && j["eras"].is_array()) {
        for (const auto& ej : j["eras"]) {
            CalendarEra era;
            era.name      = ej.value("name", "");
            era.start_day = ej.value("start_day", 0);
            cal.eras.push_back(era);
        }
    }

    if (j.contains("months") && j["months"].is_array()) {
        for (const auto& mj : j["months"]) {
            CalendarMonth m;
            m.name = mj.value("name", "Month");
            m.days = mj.value("days", 30);
            cal.months.push_back(m);
        }
    }

    if (j.contains("week_days") && j["week_days"].is_array()) {
        for (const auto& wd : j["week_days"])
            cal.week_days.push_back(wd.get<std::string>());
    }

    if (j.contains("leap_rule") && j["leap_rule"].is_object()) {
        LeapRule lr;
        lr.every_n_years = j["leap_rule"].value("every_n_years", 4);
        lr.extra_days    = j["leap_rule"].value("extra_days", 1);
        cal.leap_rule = lr;
    }

    return cal;
}

// ── Network ───────────────────────────────────────────────────────────────────

static json serializeNetwork(const RouteGraph& g) {
    json j;

    json nodesArr = json::array();
    for (const auto& n : g.nodes) {
        json nj;
        nj["id"]          = n.id;
        nj["lat_deg"]     = n.lat_deg;
        nj["lon_deg"]     = n.lon_deg;
        nj["name"]        = n.name;
        nj["entity_type"] = entityTypeName(n.entity_type);
        nj["media_ref"]   = n.media_ref;
        if (n.born_day) nj["born_day"] = *n.born_day;
        if (n.died_day) nj["died_day"] = *n.died_day;
        nodesArr.push_back(nj);
    }
    j["nodes"] = nodesArr;

    json edgesArr = json::array();
    for (const auto& e : g.edges) {
        json ej;
        ej["id"]          = e.id;
        ej["from_id"]     = e.from_id;
        ej["to_id"]       = e.to_id;
        ej["distance_km"] = e.distance_km;
        ej["type"]        = routeTypeName(e.type);
        edgesArr.push_back(ej);
    }
    j["edges"] = edgesArr;

    return j;
}

static RouteGraph deserializeNetwork(const json& j) {
    RouteGraph g;
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& nj : j["nodes"]) {
            MapNode n;
            n.id          = nj.value("id",          "");
            n.lat_deg     = nj.value("lat_deg",     0.f);
            n.lon_deg     = nj.value("lon_deg",     0.f);
            n.name        = nj.value("name",        "");
            n.entity_type = entityTypeFromString(nj.value("entity_type", "poi"));
            n.media_ref   = nj.value("media_ref",   "");
            if (nj.contains("born_day") && nj["born_day"].is_number())
                n.born_day = nj["born_day"].get<int>();
            if (nj.contains("died_day") && nj["died_day"].is_number())
                n.died_day = nj["died_day"].get<int>();
            g.nodes.push_back(n);
        }
    }
    if (j.contains("edges") && j["edges"].is_array()) {
        for (const auto& ej : j["edges"]) {
            RouteEdge e;
            e.id          = ej.value("id",          "");
            e.from_id     = ej.value("from_id",     "");
            e.to_id       = ej.value("to_id",       "");
            e.distance_km = ej.value("distance_km", 0.f);
            e.type        = routeTypeFromString(ej.value("type", "road"));
            g.edges.push_back(e);
        }
    }
    return g;
}

// ── Body ──────────────────────────────────────────────────────────────────────

static json serializeBody(const CelestialBody& b) {
    json bj;
    bj["id"]           = b.id;
    bj["name"]         = b.name;
    bj["texture_path"] = b.texture_path;
    bj["radius_km"]    = b.radius_km;

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
    bj["network"]  = serializeNetwork(b.network);

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

    // Current format
    if (bj.contains("network") && bj["network"].is_object()) {
        b.network = deserializeNetwork(bj["network"]);
        return b;
    }

    // Migration from v0.5 format (separate entities + roads + sea_routes)
    if (bj.contains("entities") && bj["entities"].is_array()) {
        for (const auto& ej : bj["entities"]) {
            MapNode n;
            n.id          = ej.value("id",        "");
            n.name        = ej.value("name",      "Unnamed");
            n.entity_type = entityTypeFromString(ej.value("type", "poi"));
            n.lat_deg     = ej.value("lat_deg",   0.0f);
            n.lon_deg     = ej.value("lon_deg",   0.0f);
            n.media_ref   = ej.value("media_ref", "");
            if (ej.contains("born_day") && ej["born_day"].is_number())
                n.born_day = ej["born_day"].get<int>();
            if (ej.contains("died_day") && ej["died_day"].is_number())
                n.died_day = ej["died_day"].get<int>();
            b.network.nodes.push_back(n);
        }
    }

    auto migrateGraph = [&](const json& gj, RouteType rt) {
        if (!gj.is_object()) return;
        if (gj.contains("nodes") && gj["nodes"].is_array()) {
            for (const auto& nj : gj["nodes"]) {
                std::string entityRef = nj.value("entity_ref", "");
                if (!entityRef.empty()) continue; // already in network as named node
                MapNode n;
                n.id      = nj.value("id",      "");
                n.lat_deg = nj.value("lat_deg", 0.f);
                n.lon_deg = nj.value("lon_deg", 0.f);
                b.network.nodes.push_back(n);
            }
        }
        if (gj.contains("edges") && gj["edges"].is_array()) {
            for (const auto& ej : gj["edges"]) {
                RouteEdge e;
                e.id          = ej.value("id",          "");
                e.from_id     = ej.value("from_id",     "");
                e.to_id       = ej.value("to_id",       "");
                e.distance_km = ej.value("distance_km", 0.f);
                e.type        = rt;
                b.network.edges.push_back(e);
            }
        }
    };
    if (bj.contains("roads"))      migrateGraph(bj["roads"],      RouteType::Road);
    if (bj.contains("sea_routes")) migrateGraph(bj["sea_routes"], RouteType::Sea);

    return b;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool WorldSerializer::save(const World& world) {
    std::error_code ec;
    std::filesystem::create_directories(world.rootPath, ec);
    if (ec) {
        std::cerr << "WorldSerializer: cannot create " << world.rootPath << ": " << ec.message() << "\n";
        return false;
    }

    json j;
    j["name"]     = world.name;
    j["version"]  = "0.6";
    j["body"]     = serializeBody(world.body);
    j["calendar"] = serializeCalendar(world.calendar);
    j["party"]    = { {"node_id", world.party.node_id}, {"speed_kmday", world.party.speed_kmday} };

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

    if (j.contains("body") && j["body"].is_object()) {
        out.body = deserializeBody(j["body"]);
    }
    else if (j.contains("bodies") && j["bodies"].is_array() && !j["bodies"].empty()) {
        out.body = deserializeBody(j["bodies"][0]);
    }

    if (j.contains("calendar") && j["calendar"].is_object())
        out.calendar = deserializeCalendar(j["calendar"]);

    if (j.contains("party") && j["party"].is_object()) {
        out.party.node_id     = j["party"].value("node_id",     "");
        out.party.speed_kmday = j["party"].value("speed_kmday", 40.0f);
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

    out.name     = name;
    out.rootPath = rootPath;
    out.body     = CelestialBody{};
    out.body.id  = "world";
    out.body.name = name;
    return save(out);
}
