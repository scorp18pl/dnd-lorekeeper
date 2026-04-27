#include "MapImporter.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>

static constexpr double PI   = 3.14159265358979323846;
static constexpr double R_KM = 6371.0;

// ── Internal image type ────────────────────────────────────────────────────────

struct Img {
    int w = 0, h = 0, ch = 0;
    std::vector<float> px;  // row-major, 0–1

    float  get(int y, int x, int c) const { return px[(y * w + x) * ch + c]; }
    float& at (int y, int x, int c)       { return px[(y * w + x) * ch + c]; }
};

static Img loadImg(const std::string& path, int forceCh = 0) {
    stbi_set_flip_vertically_on_load(false);
    int w, h, ch;
    unsigned char* raw = stbi_load(path.c_str(), &w, &h, &ch, forceCh);
    if (!raw) throw std::runtime_error("Cannot load: " + path);
    int ac = forceCh ? forceCh : ch;
    Img img{w, h, ac, std::vector<float>(w * h * ac)};
    for (int i = 0; i < w * h * ac; ++i) img.px[i] = raw[i] / 255.0f;
    stbi_image_free(raw);
    return img;
}

static void saveImg(const Img& img, const std::string& path) {
    std::vector<uint8_t> buf(img.w * img.h * img.ch);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = static_cast<uint8_t>(std::clamp(img.px[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    if (!stbi_write_png(path.c_str(), img.w, img.h, img.ch, buf.data(), img.w * img.ch))
        throw std::runtime_error("Cannot write: " + path);
}

// ── Sampling ───────────────────────────────────────────────────────────────────

// Bilinear sample of a local tile. Returns false if (px,py) is out of bounds.
static bool sampleLocal(const Img& img, double px, double py, float* out) {
    if (px < 0 || py < 0 || px >= img.w - 1 || py >= img.h - 1) return false;
    int   x0 = (int)px, y0 = (int)py;
    float dx = (float)(px - x0), dy = (float)(py - y0);
    for (int c = 0; c < img.ch; ++c)
        out[c] = img.get(y0, x0, c) * (1 - dx) * (1 - dy)
               + img.get(y0, x0 + 1, c) * dx * (1 - dy)
               + img.get(y0 + 1, x0, c) * (1 - dx) * dy
               + img.get(y0 + 1, x0 + 1, c) * dx * dy;
    return true;
}

// Bilinear sample of an equirectangular map at (lat, lon) in radians.
// Wraps longitude, clamps latitude.
static void sampleGlobal(const Img& img, double lat, double lon, float* out) {
    lat = std::clamp(lat, -PI / 2 + 1e-6, PI / 2 - 1e-6);
    double xf = (lon + PI) / (2 * PI) * (img.w - 1);
    double yf = (1.0 - (lat + PI / 2) / PI) * (img.h - 1);
    int x0 = (int)xf, y0 = (int)yf;
    int x1 = (x0 + 1) % img.w;
    int y1 = std::min(y0 + 1, img.h - 1);
    float dx = (float)(xf - x0), dy = (float)(yf - y0);
    for (int c = 0; c < img.ch; ++c)
        out[c] = img.get(y0, x0, c) * (1 - dx) * (1 - dy)
               + img.get(y0, x1, c) * dx * (1 - dy)
               + img.get(y1, x0, c) * (1 - dx) * dy
               + img.get(y1, x1, c) * dx * dy;
}

// ── Projection math ────────────────────────────────────────────────────────────

struct LL { double lat, lon; };
struct XY { double x, y; bool valid = true; };

static LL aeqdInverse(double lat0, double lon0, double x_km, double y_km) {
    double rho = std::sqrt(x_km * x_km + y_km * y_km);
    if (rho < 1e-10) return {lat0, lon0};
    double c = rho / R_KM, sin_c = std::sin(c), cos_c = std::cos(c);
    double lat = std::asin(std::clamp(
        cos_c * std::sin(lat0) + y_km * sin_c * std::cos(lat0) / rho, -1.0, 1.0));
    double lon = lon0 + std::atan2(
        x_km * sin_c,
        rho * std::cos(lat0) * cos_c - y_km * std::sin(lat0) * sin_c);
    return {lat, lon};
}

static XY aeqdForward(double lat0, double lon0, double lat, double lon) {
    double dlon  = lon - lon0;
    double cos_c = std::clamp(
        std::sin(lat0) * std::sin(lat) + std::cos(lat0) * std::cos(lat) * std::cos(dlon),
        -1.0, 1.0);
    double c = std::acos(cos_c);
    if (c < 1e-10) return {0, 0};
    double k = c / std::sin(c);
    return {k * std::cos(lat) * std::sin(dlon) * R_KM,
            k * (std::cos(lat0) * std::sin(lat)
                 - std::sin(lat0) * std::cos(lat) * std::cos(dlon)) * R_KM};
}

static LL orthoInverse(double lat0, double lon0, double x_n, double y_n, bool& inside) {
    double rho = std::sqrt(x_n * x_n + y_n * y_n);
    inside = (rho <= 1.0);
    double cos_c = std::sqrt(std::max(0.0, 1.0 - rho * rho));
    double lat = std::asin(std::clamp(cos_c * std::sin(lat0) + y_n * std::cos(lat0), -1.0, 1.0));
    double lon = lon0 + std::atan2(x_n, std::cos(lat0) * cos_c - y_n * std::sin(lat0));
    return {lat, lon};
}

static XY orthoForward(double lat0, double lon0, double lat, double lon) {
    double dlon    = lon - lon0;
    double cos_lat = std::cos(lat);
    double cos_c   = std::sin(lat0) * std::sin(lat) + std::cos(lat0) * cos_lat * std::cos(dlon);
    bool   visible = (cos_c >= 0.0);
    return {cos_lat * std::sin(dlon),
            std::cos(lat0) * std::sin(lat) - std::sin(lat0) * cos_lat * std::cos(dlon),
            visible};
}

static LL gnomonicInverse(double lat0, double lon0, double x_km, double y_km) {
    double x = x_km / R_KM, y = y_km / R_KM;
    double rho = std::sqrt(x * x + y * y);
    if (rho < 1e-10) return {lat0, lon0};
    double c = std::atan(rho), sin_c = std::sin(c), cos_c = std::cos(c);
    double lat = std::asin(std::clamp(
        cos_c * std::sin(lat0) + y * sin_c * std::cos(lat0) / rho, -1.0, 1.0));
    double lon = lon0 + std::atan2(
        x * sin_c,
        rho * std::cos(lat0) * cos_c - y * std::sin(lat0) * sin_c);
    return {lat, lon};
}

static XY gnomonicForward(double lat0, double lon0, double lat, double lon) {
    double dlon    = lon - lon0;
    double cos_lat = std::cos(lat);
    double cos_c   = std::sin(lat0) * std::sin(lat) + std::cos(lat0) * cos_lat * std::cos(dlon);
    if (cos_c <= 1e-10) return {0, 0, false};
    double k = R_KM / cos_c;
    return {k * cos_lat * std::sin(dlon),
            k * (std::cos(lat0) * std::sin(lat) - std::sin(lat0) * cos_lat * std::cos(dlon)),
            true};
}

// ── Palette ────────────────────────────────────────────────────────────────────

const std::vector<MapImporter::PaletteStop>& MapImporter::defaultPalette() {
    static const std::vector<PaletteStop> pal = {
        {0x4A/255.f, 0x50/255.f, 0xAD/255.f, 0.0f/6},
        {0x65/255.f, 0x8B/255.f, 0xC2/255.f, 1.0f/6},
        {0x9A/255.f, 0xCC/255.f, 0xD7/255.f, 2.0f/6},
        {0xD1/255.f, 0xB7/255.f, 0x69/255.f, 3.0f/6},
        {0xD5/255.f, 0x90/255.f, 0x3C/255.f, 4.0f/6},
        {0xD5/255.f, 0x7E/255.f, 0x3F/255.f, 5.0f/6},
        {0xDC/255.f, 0x52/255.f, 0x3B/255.f, 6.0f/6},
    };
    return pal;
}

// ── Operations ─────────────────────────────────────────────────────────────────

std::string MapImporter::colorToGray(
    const std::string& inputPath,
    const std::string& outputPath,
    const std::vector<PaletteStop>& paletteIn)
{
    try {
        const auto& pal = paletteIn.empty() ? defaultPalette() : paletteIn;
        Img in = loadImg(inputPath, 3);
        Img out{in.w, in.h, 1, std::vector<float>(in.w * in.h)};

        static const float kW[3] = {0.3f, 0.59f, 0.11f};
        for (int y = 0; y < in.h; ++y) {
            for (int x = 0; x < in.w; ++x) {
                float r = in.get(y, x, 0), g = in.get(y, x, 1), b = in.get(y, x, 2);

                // Find two nearest palette stops by perceptual distance.
                int   best1 = 0, best2 = 1;
                float d1 = 1e9f, d2 = 1e9f;
                for (int k = 0; k < (int)pal.size(); ++k) {
                    float dr = r - pal[k].r, dg = g - pal[k].g, db = b - pal[k].b;
                    float d  = std::sqrt(dr*dr*kW[0] + dg*dg*kW[1] + db*db*kW[2]);
                    if (d < d1) { d2 = d1; best2 = best1; d1 = d; best1 = k; }
                    else if (d < d2) { d2 = d; best2 = k; }
                }
                float t      = d1 / (d1 + d2 + 1e-6f);
                out.at(y, x, 0) = pal[best1].value * (1 - t) + pal[best2].value * t;
            }
        }
        saveImg(out, outputPath);
        return "";
    } catch (const std::exception& e) { return e.what(); }
}

std::string MapImporter::project(
    const std::string& inputPath,
    const std::string& outputPath,
    Projection proj,
    double lat0Deg, double lon0Deg,
    double sizeKm,
    int resolution)
{
    try {
        Img in = loadImg(inputPath);
        double lat0 = lat0Deg * PI / 180.0, lon0 = lon0Deg * PI / 180.0;
        double km_per_px = sizeKm / resolution;
        double half      = resolution / 2.0;

        Img out{resolution, resolution, in.ch, std::vector<float>(resolution * resolution * in.ch, 0.f)};
        std::vector<float> sample(in.ch);

        for (int j = 0; j < resolution; ++j) {
            for (int i = 0; i < resolution; ++i) {
                double x_km = (i - half) * km_per_px;
                double y_km = (half - j) * km_per_px;  // north-up

                LL ll;
                bool inside = true;
                if (proj == Projection::AEQD)
                    ll = aeqdInverse(lat0, lon0, x_km, y_km);
                else if (proj == Projection::Ortho) {
                    double x_n = x_km / (R_KM), y_n = y_km / (R_KM);
                    ll = orthoInverse(lat0, lon0, x_n, y_n, inside);
                } else
                    ll = gnomonicInverse(lat0, lon0, x_km, y_km);

                if (!inside) continue;
                sampleGlobal(in, ll.lat, ll.lon, sample.data());
                for (int c = 0; c < in.ch; ++c)
                    out.at(j, i, c) = sample[c];
            }
        }
        saveImg(out, outputPath);
        return "";
    } catch (const std::exception& e) { return e.what(); }
}

std::string MapImporter::unproject(
    const std::string& inputPath,
    const std::string& outputPath,
    Projection proj,
    double lat0Deg, double lon0Deg,
    double sizeKm,
    int outWidth, int outHeight)
{
    try {
        Img in = loadImg(inputPath);
        double lat0      = lat0Deg * PI / 180.0, lon0 = lon0Deg * PI / 180.0;
        double km_per_px = sizeKm / in.w;

        Img out{outWidth, outHeight, in.ch, std::vector<float>(outWidth * outHeight * in.ch, 0.f)};
        std::vector<float> sample(in.ch);

        for (int j = 0; j < outHeight; ++j) {
            double lat = (1.0 - (double)j / (outHeight - 1)) * PI - PI / 2;
            for (int i = 0; i < outWidth; ++i) {
                double lon = (double)i / (outWidth - 1) * 2 * PI - PI;

                double px, py;
                if (proj == Projection::AEQD) {
                    auto [x, y, ok] = aeqdForward(lat0, lon0, lat, lon);
                    (void)ok;
                    px = x / km_per_px + in.w / 2.0;
                    py = in.h / 2.0 - y / km_per_px;
                } else if (proj == Projection::Ortho) {
                    auto [x, y, vis] = orthoForward(lat0, lon0, lat, lon);
                    if (!vis) continue;
                    px = x * (in.w / 2.0) + in.w / 2.0;
                    py = in.h / 2.0 - y * (in.h / 2.0);
                } else {
                    auto [x, y, vis] = gnomonicForward(lat0, lon0, lat, lon);
                    if (!vis) continue;
                    px = x / km_per_px + in.w / 2.0;
                    py = in.h / 2.0 - y / km_per_px;
                }

                if (!sampleLocal(in, px, py, sample.data())) continue;
                for (int c = 0; c < in.ch; ++c)
                    out.at(j, i, c) = sample[c];
            }
        }
        saveImg(out, outputPath);
        return "";
    } catch (const std::exception& e) { return e.what(); }
}

std::string MapImporter::hillshade(
    const std::string& inputPath,
    const std::string& outputPath,
    double azimuthDeg, double altitudeDeg, double zScale)
{
    try {
        Img in  = loadImg(inputPath, 1);
        Img out{in.w, in.h, 1, std::vector<float>(in.w * in.h)};

        // Normalize to [0,1] (already done by loadImg).
        double az  = azimuthDeg  * PI / 180.0;
        double alt = altitudeDeg * PI / 180.0;
        float  lx  = (float)(std::cos(alt) * std::sin(az));
        float  ly  = (float)(std::cos(alt) * std::cos(az));
        float  lz  = (float)std::sin(alt);

        auto elev = [&](int y, int x) {
            y = std::clamp(y, 0, in.h - 1);
            x = std::clamp(x, 0, in.w - 1);
            return in.get(y, x, 0);
        };

        for (int y = 0; y < in.h; ++y) {
            for (int x = 0; x < in.w; ++x) {
                float dzdx = (elev(y, x + 1) - elev(y, x - 1)) * (float)zScale;
                float dzdy = (elev(y + 1, x) - elev(y - 1, x)) * (float)zScale;
                float nx   = -dzdx, ny = -dzdy, nz = 1.0f;
                float len  = std::sqrt(nx*nx + ny*ny + nz*nz);
                nx /= len; ny /= len; nz /= len;
                out.at(y, x, 0) = std::clamp(nx*lx + ny*ly + nz*lz, 0.0f, 1.0f);
            }
        }
        saveImg(out, outputPath);
        return "";
    } catch (const std::exception& e) { return e.what(); }
}
