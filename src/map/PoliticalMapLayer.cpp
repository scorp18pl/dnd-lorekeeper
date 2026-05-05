#include "PoliticalMapLayer.h"
#include "world/CelestialBody.h"
#include "renderer/Shader.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Texture resolution ────────────────────────────────────────────────────────
static constexpr int kTexW = 2048;
static constexpr int kTexH = 1024;

// ── Neighbour walk (warm-start) ───────────────────────────────────────────────
static int neighborWalk(const GoldbergGrid& grid, glm::vec3 dir, int seed) {
    const auto& cells = grid.cells();
    if (cells.empty()) return 0;
    int cur = (seed >= 0 && seed < (int)cells.size()) ? seed : 0;
    float bestDot = glm::dot(cells[cur].centroid, dir);
    bool improved = true;
    while (improved) {
        improved = false;
        for (int nb : cells[cur].neighbor_ids) {
            float d = glm::dot(cells[nb].centroid, dir);
            if (d > bestDot) { bestDot = d; cur = nb; improved = true; }
        }
    }
    return cur;
}

// ── PoliticalMapLayer ─────────────────────────────────────────────────────────

PoliticalMapLayer::PoliticalMapLayer() {
    initTex();
}

PoliticalMapLayer::~PoliticalMapLayer() {
    if (m_Tex) { glDeleteTextures(1, &m_Tex); m_Tex = 0; }
}

void PoliticalMapLayer::initTex() {
    if (m_Tex) { glDeleteTextures(1, &m_Tex); m_Tex = 0; }

    glGenTextures(1, &m_Tex);
    glBindTexture(GL_TEXTURE_2D, m_Tex);
    std::vector<uint8_t> zeros(kTexW * kTexH * 4, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTexW, kTexH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, zeros.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── Cache management ──────────────────────────────────────────────────────────

PoliticalMapLayer::CacheEntry* PoliticalMapLayer::findEntry(int level) const {
    for (auto& e : m_Cache)
        if (e.level == level) return const_cast<CacheEntry*>(&e);
    return nullptr;
}

PoliticalMapLayer::CacheEntry& PoliticalMapLayer::evictLRU() const {
    for (auto& e : m_Cache)
        if (e.level == -1) return e;
    CacheEntry* oldest = &m_Cache[0];
    for (auto& e : m_Cache)
        if (e.lru < oldest->lru) oldest = &e;
    oldest->level    = -1;
    oldest->grid.reset();
    oldest->renderer.reset();
    return *oldest;
}

const GoldbergGrid* PoliticalMapLayer::ensureGrid(int level) const {
    if (level < 0) return nullptr;
    auto* e = findEntry(level);
    if (e) {
        e->lru = ++m_Tick;
        return e->grid.get();
    }
    auto& slot   = evictLRU();
    int   subdiv = 1 << level;
    slot.level   = level;
    slot.grid    = std::make_unique<GoldbergGrid>(subdiv);
    slot.lru     = ++m_Tick;
    return slot.grid.get();
}

GoldbergRenderer* PoliticalMapLayer::ensureRenderer(
    int level,
    const std::unordered_map<int,std::string>& ownership,
    const std::vector<PoliticalEntity>& entities) const
{
    ensureGrid(level);
    auto* e = findEntry(level);
    if (!e || !e->grid) return nullptr;
    if (!e->renderer)
        e->renderer = std::make_unique<GoldbergRenderer>(*e->grid);
    e->renderer->syncFromPoliticalMap(ownership, entities);
    e->renderer->setHoverCell(m_HoverCell);
    return e->renderer.get();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool PoliticalMapLayer::hasGrid(int level) const {
    return findEntry(level) != nullptr;
}

const GoldbergGrid* PoliticalMapLayer::grid(int level) const {
    return ensureGrid(level);
}

int PoliticalMapLayer::findCellNearest(int level, glm::vec3 dir) const {
    const GoldbergGrid* g = ensureGrid(level);
    if (!g) return -1;
    return g->findCellNearest(dir, 0);
}

void PoliticalMapLayer::setHoverCell(int cellId) {
    m_HoverCell = cellId;
    m_Dirty     = true;
}

void PoliticalMapLayer::sync(const CelestialBody& body, int paintLevel,
                              const std::vector<PoliticalEntity>& entities) {
    const std::unordered_map<int,std::string> emptyMap;
    const auto& ownership = (paintLevel >= 0 && paintLevel < (int)body.lod_ownership.size())
        ? body.lod_ownership[paintLevel] : emptyMap;
    ensureRenderer(paintLevel, ownership, entities);
    m_PaintLevelCached = paintLevel;
}

void PoliticalMapLayer::draw(int level, Shader& shader,
                              const glm::mat4& vp, const glm::mat4& model) const {
    auto* e = findEntry(level);
    if (!e || !e->renderer) return;

    shader.bind();
    shader.setMat4("u_VP",         vp);
    shader.setMat4("u_Model",      model);
    shader.setBool("u_HasHeightmap", false);
    shader.setInt ("u_OvCount",      0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);

    e->renderer->draw(shader);
    e->renderer->drawBorders(shader);

    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    shader.unbind();
}

// ── Static helpers ────────────────────────────────────────────────────────────

int PoliticalMapLayer::computeMaxLevel(double radius_km, double min_cell_km) {
    if (min_cell_km <= 0) min_cell_km = 1.0;
    double ratio = (2.0 * M_PI * radius_km) / (min_cell_km * std::sqrt(10.0));
    return (int)std::ceil(std::log2(std::max(ratio, 2.0)));
}

std::string PoliticalMapLayer::makeEntityId(const std::vector<PoliticalEntity>& entities) {
    for (int n = (int)entities.size(); ; ++n) {
        std::string id = "pe_" + std::to_string(n);
        bool clash = false;
        for (const auto& pe : entities)
            if (pe.id == id) { clash = true; break; }
        if (!clash) return id;
    }
}

// ── buildHasDescendants ───────────────────────────────────────────────────────

std::vector<std::unordered_set<int>> PoliticalMapLayer::buildHasDescendants(
    const CelestialBody& body, int maxLevel) const
{
    if (maxLevel < 0) return {};
    std::vector<std::unordered_set<int>> hasDesc(maxLevel + 1);

    for (int L = 1; L <= maxLevel; ++L) {
        if (L >= (int)body.lod_ownership.size()) continue;
        if (body.lod_ownership[L].empty()) continue;

        const GoldbergGrid* gL = ensureGrid(L);
        if (!gL) continue;

        for (const auto& [cellId, entityId] : body.lod_ownership[L]) {
            if (entityId.empty()) continue;
            const GoldbergGrid* gCur = gL;
            int cur = cellId;
            for (int ancL = L - 1; ancL >= 0; --ancL) {
                const GoldbergGrid* gAnc = ensureGrid(ancL);
                if (!gAnc || cur < 0 || cur >= (int)gCur->cells().size()) break;
                glm::vec3 dir = gCur->cells()[cur].centroid;
                int anc = neighborWalk(*gAnc, dir, 0);
                hasDesc[ancL].insert(anc);
                gCur = gAnc;
                cur  = anc;
            }
        }
    }

    return hasDesc;
}

// ── resolveOwnership ──────────────────────────────────────────────────────────

std::string PoliticalMapLayer::resolveOwnership(
    const CelestialBody& body,
    const std::vector<std::unordered_set<int>>& /*hasDesc*/,
    int level, int cellId) const
{
    for (int L = level; L >= 0; --L) {
        if (L < (int)body.lod_ownership.size()) {
            auto it = body.lod_ownership[L].find(cellId);
            if (it != body.lod_ownership[L].end() && !it->second.empty())
                return it->second;
        }
        if (L == 0) break;
        const GoldbergGrid* gL   = ensureGrid(L);
        const GoldbergGrid* gAnc = ensureGrid(L - 1);
        if (!gL || !gAnc) break;
        if (cellId < 0 || cellId >= (int)gL->cells().size()) break;
        glm::vec3 dir = gL->cells()[cellId].centroid;
        cellId = neighborWalk(*gAnc, dir, 0);
    }
    return {};
}

// ── bake ──────────────────────────────────────────────────────────────────────

void PoliticalMapLayer::bake(
    const CelestialBody& body,
    const std::vector<PoliticalEntity>& entities)
{
    m_Dirty = false;

    int maxPainted = body.maxPaintedLevel();
    if (maxPainted < 0) {
        std::vector<uint8_t> zeros(kTexW * kTexH * 4, 0);
        glBindTexture(GL_TEXTURE_2D, m_Tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTexW, kTexH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, zeros.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

    std::unordered_map<std::string, uint32_t> colMap;
    for (const auto& pe : entities) {
        uint32_t rgba = (uint32_t)pe.color_r
                      | ((uint32_t)pe.color_g << 8)
                      | ((uint32_t)pe.color_b << 16)
                      | (180u << 24);
        colMap[pe.id] = rgba;
    }

    for (int L = 0; L <= maxPainted; ++L)
        ensureGrid(L);

    auto hasDesc = buildHasDescendants(body, maxPainted);

    std::vector<uint32_t> pixels(kTexW * kTexH, 0);
    std::vector<int> rowCur(maxPainted + 1, 0);

    for (int py = 0; py < kTexH; ++py) {
        for (int L = 0; L <= maxPainted; ++L) rowCur[L] = 0;

        float v      = (py + 0.5f) / (float)kTexH;
        float lat    = (float)M_PI * (0.5f - v);
        float cosLat = std::cos(lat);
        float sinLat = std::sin(lat);

        for (int px = 0; px < kTexW; ++px) {
            float u   = (px + 0.5f) / (float)kTexW;
            float lon = (float)(2.0 * M_PI) * u - (float)M_PI;

            glm::vec3 dir = {
                cosLat * std::cos(lon),
                sinLat,
               -cosLat * std::sin(lon)
            };

            int level = 0;
            const GoldbergGrid* g0 = ensureGrid(0);
            if (g0) rowCur[0] = neighborWalk(*g0, dir, rowCur[0]);

            while (level < maxPainted) {
                int next = level + 1;
                const GoldbergGrid* gNext = ensureGrid(next);
                if (!gNext) break;

                if (level < (int)hasDesc.size() && !hasDesc[level].count(rowCur[level]))
                    break;

                const GoldbergGrid* gCur = ensureGrid(level);
                if (!gCur || rowCur[level] < 0 || rowCur[level] >= (int)gCur->cells().size()) break;

                int seed = (rowCur[next] >= 0 && rowCur[next] < (int)gNext->cells().size())
                         ? rowCur[next] : 0;
                rowCur[next] = neighborWalk(*gNext, dir, seed);
                level = next;
            }

            std::string owner = resolveOwnership(body, hasDesc, level, rowCur[level]);
            uint32_t pix = 0;
            if (!owner.empty()) {
                auto cit = colMap.find(owner);
                if (cit != colMap.end()) pix = cit->second;
            }
            pixels[py * kTexW + px] = pix;
        }
    }

    // Border darkening pass
    std::vector<uint32_t> bordered = pixels;
    for (int py = 1; py < kTexH - 1; ++py) {
        for (int px = 1; px < kTexW - 1; ++px) {
            uint32_t c = pixels[py * kTexW + px];
            if ((c >> 24) == 0) continue;
            uint32_t cL = pixels[py * kTexW + px - 1];
            uint32_t cR = pixels[py * kTexW + px + 1];
            uint32_t cU = pixels[(py - 1) * kTexW + px];
            uint32_t cD = pixels[(py + 1) * kTexW + px];
            if (cL != c || cR != c || cU != c || cD != c) {
                uint8_t r = (uint8_t)(( c        & 0xFF) * 60 / 100);
                uint8_t g = (uint8_t)(((c >>  8) & 0xFF) * 60 / 100);
                uint8_t b = (uint8_t)(((c >> 16) & 0xFF) * 60 / 100);
                uint8_t a = 0xFF;
                bordered[py * kTexW + px] = r | ((uint32_t)g << 8)
                                              | ((uint32_t)b << 16)
                                              | ((uint32_t)a << 24);
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, m_Tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kTexW, kTexH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, bordered.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}
