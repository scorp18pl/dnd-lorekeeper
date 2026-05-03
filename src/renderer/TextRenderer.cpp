#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_rect_pack.h>
#include <stb_truetype.h>

#include "TextRenderer.h"
#include "Shader.h"

#include <fstream>
#include <filesystem>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

static std::vector<uint8_t> readFileBin(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data((size_t)sz);
    f.read(reinterpret_cast<char*>(data.data()), sz);
    return data;
}

std::string TextRenderer::pickFontPath(const std::string& hint) {
    if (!hint.empty() && std::filesystem::exists(hint)) return hint;

    if (std::filesystem::exists("assets/fonts/font.ttf")) return "assets/fonts/font.ttf";

#ifdef _WIN32
    const char* candidates[] = {
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/verdana.ttf",
    };
    for (const char* p : candidates)
        if (std::filesystem::exists(p)) return p;
#endif
    return {};
}

bool TextRenderer::init(const std::string& fontPath, float bakedSize) {
    m_BakedSize = bakedSize;

    std::string fp = pickFontPath(fontPath);
    if (fp.empty()) {
        std::cerr << "TextRenderer: no font found\n";
        return false;
    }

    auto fontData = readFileBin(fp);
    if (fontData.empty()) {
        std::cerr << "TextRenderer: failed to read " << fp << "\n";
        return false;
    }

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, fontData.data(),
                        stbtt_GetFontOffsetForIndex(fontData.data(), 0))) {
        std::cerr << "TextRenderer: stbtt_InitFont failed\n";
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, bakedSize);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    m_Ascent = ascent * scale;

    // Generate SDF for printable ASCII
    const int   padding      = 4;
    const int   onedgeValue  = 128;
    const float pxDistScale  = 8.0f;

    struct RawGlyph {
        int cp, sdfW, sdfH, xOff, yOff, advance;
        std::vector<uint8_t> data;
    };

    std::vector<RawGlyph> rawGlyphs;
    rawGlyphs.reserve(96);

    for (int cp = 32; cp < 127; ++cp) {
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* sdf = stbtt_GetCodepointSDF(
            &font, scale, cp, padding, (unsigned char)onedgeValue, pxDistScale,
            &w, &h, &xo, &yo);

        int adv, lsb;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);

        RawGlyph g;
        g.cp = cp; g.sdfW = w; g.sdfH = h;
        g.xOff = xo; g.yOff = yo;
        g.advance = adv;
        if (sdf && w > 0 && h > 0) {
            g.data.assign(sdf, sdf + w * h);
            stbtt_FreeSDF(sdf, nullptr);
        }
        rawGlyphs.push_back(std::move(g));
    }

    // Pack into atlas
    m_AtlasW = 512; m_AtlasH = 512;

    std::vector<stbrp_rect> rects(rawGlyphs.size());
    for (size_t i = 0; i < rawGlyphs.size(); ++i) {
        rects[i].id = (int)i;
        rects[i].w  = (stbrp_coord)(rawGlyphs[i].sdfW + 1);
        rects[i].h  = (stbrp_coord)(rawGlyphs[i].sdfH + 1);
    }

    std::vector<stbrp_node> nodes(m_AtlasW);
    stbrp_context ctx;
    stbrp_init_target(&ctx, m_AtlasW, m_AtlasH, nodes.data(), m_AtlasW);
    stbrp_pack_rects(&ctx, rects.data(), (int)rects.size());

    std::vector<uint8_t> atlas((size_t)(m_AtlasW * m_AtlasH), 0u);

    for (size_t i = 0; i < rawGlyphs.size(); ++i) {
        const auto& g = rawGlyphs[i];
        if (!rects[i].was_packed || g.data.empty()) continue;

        for (int y = 0; y < g.sdfH; ++y)
            for (int x = 0; x < g.sdfW; ++x)
                atlas[(rects[i].y + y) * m_AtlasW + (rects[i].x + x)] =
                    g.data[(size_t)(y * g.sdfW + x)];

        GlyphInfo info;
        info.u0       = rects[i].x              / (float)m_AtlasW;
        info.v0       = rects[i].y              / (float)m_AtlasH;
        info.u1       = (rects[i].x + g.sdfW)  / (float)m_AtlasW;
        info.v1       = (rects[i].y + g.sdfH)  / (float)m_AtlasH;
        info.xBearing = (float)g.xOff;
        info.yBearing = (float)g.yOff;   // negative = above baseline in stb's y-down coords
        info.width    = (float)g.sdfW;
        info.height   = (float)g.sdfH;
        info.advance  = g.advance * scale;
        m_Glyphs[g.cp] = info;
    }

    // Upload atlas
    glGenTextures(1, &m_AtlasTex);
    glBindTexture(GL_TEXTURE_2D, m_AtlasTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_AtlasW, m_AtlasH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Shader
    m_Shader = std::make_unique<Shader>("shaders/text.vert", "shaders/text.frag");

    // VAO / VBO
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    const int stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, fill));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, outline));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride,
                          (void*)offsetof(Vertex, outlineW));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void TextRenderer::shutdown() {
    m_Verts.clear();
    m_Glyphs.clear();
    m_Shader.reset();
    if (m_VAO) { glDeleteVertexArrays(1, &m_VAO); m_VAO = 0; }
    if (m_VBO) { glDeleteBuffers(1, &m_VBO);      m_VBO = 0; }
    if (m_AtlasTex) { glDeleteTextures(1, &m_AtlasTex); m_AtlasTex = 0; }
}

TextRenderer::~TextRenderer() { shutdown(); }

void TextRenderer::beginFrame(int screenW, int screenH) {
    m_ScreenW = screenW;
    m_ScreenH = screenH;
}

void TextRenderer::drawText(const char* text, float x, float y, float pixelSize,
                            glm::vec4 fill, glm::vec4 outline, float outlineW) {
    if (!m_AtlasTex) return;

    float sr       = pixelSize / m_BakedSize;          // scale ratio
    float baseline = y + m_Ascent * sr;
    float pen      = x;

    for (const char* p = text; *p; ++p) {
        int cp = (unsigned char)*p;
        auto it = m_Glyphs.find(cp);
        if (it == m_Glyphs.end()) continue;
        const GlyphInfo& g = it->second;

        float gx = pen + g.xBearing * sr;
        float gy = baseline + g.yBearing * sr;  // yBearing < 0 → above baseline
        float gw = g.width  * sr;
        float gh = g.height * sr;

        auto addV = [&](float vx, float vy, float vu, float vv) {
            Vertex v;
            v.pos[0] = vx; v.pos[1] = vy;
            v.uv[0]  = vu; v.uv[1]  = vv;
            v.fill[0] = fill.r;    v.fill[1] = fill.g;
            v.fill[2] = fill.b;    v.fill[3] = fill.a;
            v.outline[0] = outline.r; v.outline[1] = outline.g;
            v.outline[2] = outline.b; v.outline[3] = outline.a;
            v.outlineW = outlineW;
            m_Verts.push_back(v);
        };

        addV(gx,      gy,      g.u0, g.v0);
        addV(gx + gw, gy,      g.u1, g.v0);
        addV(gx + gw, gy + gh, g.u1, g.v1);
        addV(gx,      gy,      g.u0, g.v0);
        addV(gx + gw, gy + gh, g.u1, g.v1);
        addV(gx,      gy + gh, g.u0, g.v1);

        pen += g.advance * sr;
    }
}

void TextRenderer::flush() {
    if (m_Verts.empty() || !m_Shader) return;

    // Orthographic projection: top-left origin, y increases downward
    glm::mat4 proj = glm::ortho(0.f, (float)m_ScreenW,
                                 (float)m_ScreenH, 0.f);

    // Save minimal GL state we touch
    GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendWasOn = glIsEnabled(GL_BLEND);
    GLboolean cullWasOn  = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);  // ortho y-flip reverses winding; disable culling for 2D quads
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_Shader->bind();
    m_Shader->setMat4("u_Proj",  proj);
    m_Shader->setInt ("u_Atlas", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_AtlasTex);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_Verts.size() * sizeof(Vertex)),
                 m_Verts.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_Verts.size());

    glBindVertexArray(0);
    m_Shader->unbind();

    if (depthWasOn) glEnable(GL_DEPTH_TEST);
    if (!blendWasOn) glDisable(GL_BLEND);
    if (cullWasOn)  glEnable(GL_CULL_FACE);

    m_Verts.clear();
}
