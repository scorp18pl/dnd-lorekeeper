#pragma once
#include <string>
#include <vector>

// C++ port of .ignored/python_script/worldmap.py.
// Original Python script is left untouched; this mirrors its operations.
class MapImporter {
public:
    enum class Projection { AEQD, Ortho, Gnomonic };

    struct PaletteStop {
        float r, g, b;  // 0–1
        float value;    // 0–1 elevation
    };

    // Default 7-stop palette matching worldmap.py (deep ocean → mountain peak).
    static const std::vector<PaletteStop>& defaultPalette();

    // Convert a color-coded elevation image to an 8-bit greyscale heightmap.
    // Returns "" on success, error string on failure.
    static std::string colorToGray(
        const std::string& inputPath,
        const std::string& outputPath,
        const std::vector<PaletteStop>& palette = {});

    // Project an equirectangular map to a local tile (AEQD, ortho, or gnomonic).
    // sizeKm: full side length of the tile in km (required for AEQD / gnomonic).
    static std::string project(
        const std::string& inputPath,
        const std::string& outputPath,
        Projection proj,
        double lat0Deg, double lon0Deg,
        double sizeKm,
        int resolution = 1024);

    // Unproject a local tile back to equirectangular.
    static std::string unproject(
        const std::string& inputPath,
        const std::string& outputPath,
        Projection proj,
        double lat0Deg, double lon0Deg,
        double sizeKm,
        int outWidth = 4096, int outHeight = 2048);

    // Generate an 8-bit greyscale hillshade from a greyscale heightmap.
    static std::string hillshade(
        const std::string& inputPath,
        const std::string& outputPath,
        double azimuthDeg = 315.0,
        double altitudeDeg = 45.0,
        double zScale = 5.0);
};
