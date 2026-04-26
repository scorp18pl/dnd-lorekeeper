#pragma once
#include <filesystem>
#include <string>
#include "world/World.h"

class WorldSerializer {
public:
    // Write world.json into world.rootPath.
    static bool save(const World& world);

    // Read world.json from rootPath, populate out. Returns false if not found.
    static bool load(const std::filesystem::path& rootPath, World& out);

    // Create directory skeleton + initial world.json, populate out.
    static bool createNew(const std::filesystem::path& rootPath,
                          const std::string& name,
                          World& out);
};
