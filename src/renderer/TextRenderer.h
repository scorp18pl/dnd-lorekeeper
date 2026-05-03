#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Shader;

class TextRenderer {
public:
    // fontPath: path to a .ttf file; empty = auto-detect system font.
    // bakedSize: pixel height used when generating the SDF atlas.
    bool init(const std::string& fontPath = "", float bakedSize = 32.0f);
    void shutdown();

    // Call once per frame before any drawText calls.
    void beginFrame(int screenW, int screenH);

    // Queue text at screen position (x, y = top-left of text area).
    // pixelSize: desired on-screen character height in pixels.
    // outlineW:  outline thickness as SDF threshold offset (0 = none, ~0.15 = moderate).
    void drawText(const char* text, float x, float y, float pixelSize,
                  glm::vec4 fillColor,
                  glm::vec4 outlineColor = { 0.f, 0.f, 0.f, 0.85f },
                  float outlineW = 0.15f);

    // Submit all queued quads to the GPU and clear the batch.
    void flush();

    // Ascender height in pixels at the given render size (baseline-to-top).
    float ascent(float pixelSize) const { return m_Ascent * (pixelSize / m_BakedSize); }

    bool ready() const { return m_AtlasTex != 0; }

    ~TextRenderer();

private:
    struct GlyphInfo {
        float u0, v0, u1, v1;   // UV in atlas [0,1]
        float xBearing;          // pen-to-left of bitmap (pixels at baked size, typically ≤ 0)
        float yBearing;          // baseline-to-top of bitmap (pixels at baked size, negative = above baseline)
        float width, height;     // bitmap size at baked size
        float advance;           // horizontal advance at baked size
    };

    struct Vertex {
        float pos[2];
        float uv[2];
        float fill[4];
        float outline[4];
        float outlineW;
    };

    std::unordered_map<int, GlyphInfo> m_Glyphs;
    GLuint m_AtlasTex = 0;
    int    m_AtlasW   = 0, m_AtlasH = 0;
    float  m_BakedSize = 32.0f;
    float  m_Ascent    = 0.0f;  // ascender height in baked pixels

    std::unique_ptr<Shader> m_Shader;
    GLuint m_VAO = 0, m_VBO = 0;

    std::vector<Vertex> m_Verts;
    int m_ScreenW = 1, m_ScreenH = 1;

    static std::string pickFontPath(const std::string& hint);
};
