#include "PoliticalMapLayer.h"
#include "renderer/Shader.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

PoliticalMapLayer::PoliticalMapLayer() = default;

PoliticalMapLayer::~PoliticalMapLayer() {
    for (auto& s : m_Slots)
        if (s.tex) glDeleteTextures(1, &s.tex);
}

void PoliticalMapLayer::initSlotTex(Slot& s) {
    if (s.tex) return;
    glGenTextures(1, &s.tex);
    glBindTexture(GL_TEXTURE_2D, s.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2048, 1024, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PoliticalMapLayer::rebuildSlots(int finestSubdiv) {
    const int subdivs[3] = {
        finestSubdiv,
        std::max(4, finestSubdiv / 2),
        std::max(4, finestSubdiv / 4)
    };
    constexpr int N = 3;

    while ((int)m_Slots.size() < N) m_Slots.emplace_back();
    m_Slots.resize(N);

    for (int i = 0; i < N; ++i) {
        Slot& s = m_Slots[i];
        initSlotTex(s);
        int sd = subdivs[i];
        if (!s.grid || s.grid->subdiv() != sd) {
            s.grid     = std::make_unique<GoldbergGrid>(sd);
            s.renderer = std::make_unique<GoldbergRenderer>(*s.grid);
            s.dirty    = true;
        }
    }
}

std::unordered_map<int, std::string> PoliticalMapLayer::deriveOwnership(
    int coarseSlot, int fineSlot,
    const std::unordered_map<int, std::string>& fineOwnership) const
{
    if (coarseSlot < 0 || coarseSlot >= (int)m_Slots.size()) return {};
    if (fineSlot   < 0 || fineSlot   >= (int)m_Slots.size()) return {};
    const GoldbergGrid* coarseGrid = m_Slots[coarseSlot].grid.get();
    const GoldbergGrid* fineGrid   = m_Slots[fineSlot].grid.get();
    if (!coarseGrid || !fineGrid) return {};

    std::unordered_map<int, std::string> result;
    const auto& coarseCells = coarseGrid->cells();
    for (int ci = 0; ci < (int)coarseCells.size(); ++ci) {
        int fineCell = fineGrid->findCellNearest(coarseCells[ci].centroid);
        auto it = fineOwnership.find(fineCell);
        if (it != fineOwnership.end() && !it->second.empty())
            result[ci] = it->second;
    }
    return result;
}

void PoliticalMapLayer::syncSlot(int slot,
                                  const std::unordered_map<int, std::string>& ownership,
                                  const std::vector<PoliticalEntity>& entities) {
    if (slot < 0 || slot >= (int)m_Slots.size()) return;
    Slot& s = m_Slots[slot];
    s.dirty = true;
    if (s.renderer)
        s.renderer->syncFromPoliticalMap(ownership, entities);
}

bool PoliticalMapLayer::slotDirty(int slot) const {
    if (slot < 0 || slot >= (int)m_Slots.size()) return false;
    return m_Slots[slot].dirty;
}

GLuint PoliticalMapLayer::texId(int slot) const {
    if (slot < 0 || slot >= (int)m_Slots.size()) return 0;
    return m_Slots[slot].tex;
}

const GoldbergGrid* PoliticalMapLayer::grid(int slot) const {
    if (slot < 0 || slot >= (int)m_Slots.size()) return nullptr;
    return m_Slots[slot].grid.get();
}

bool PoliticalMapLayer::hasGrid(int slot) const {
    if (slot < 0 || slot >= (int)m_Slots.size()) return false;
    return m_Slots[slot].grid != nullptr;
}

void PoliticalMapLayer::setHoverCell(int slot, int cellId) {
    if (slot < 0 || slot >= (int)m_Slots.size()) return;
    if (m_Slots[slot].renderer)
        m_Slots[slot].renderer->setHoverCell(cellId);
}

int PoliticalMapLayer::findCellNearest(int slot, glm::vec3 dir) const {
    if (slot < 0 || slot >= (int)m_Slots.size()) return -1;
    if (!m_Slots[slot].grid) return -1;
    return m_Slots[slot].grid->findCellNearest(dir);
}

void PoliticalMapLayer::draw(int slot, Shader& shader,
                              const glm::mat4& vp, const glm::mat4& model) const {
    if (slot < 0 || slot >= (int)m_Slots.size()) return;
    const Slot& s = m_Slots[slot];
    if (!s.renderer) return;
    shader.bind();
    shader.setMat4 ("u_VP",            vp);
    shader.setMat4 ("u_Model",         model);
    shader.setBool ("u_HasHeightmap",  false);
    shader.setFloat("u_HeightScale",   0.0f);
    shader.setInt  ("u_OvCount",       0);
    shader.setFloat("u_PlanetRadiusKm", 6371.0f);
    s.renderer->draw(shader);
    shader.unbind();
}

void PoliticalMapLayer::bakeSlot(int slot,
                                  const std::unordered_map<int, std::string>& ownership,
                                  const std::vector<PoliticalEntity>& entities) {
    if (slot < 0 || slot >= (int)m_Slots.size()) return;
    initSlotTex(m_Slots[slot]);
    bakeSlotImpl(m_Slots[slot], ownership, entities);
    m_Slots[slot].dirty = false;
}

void PoliticalMapLayer::bakeSlotImpl(Slot& s,
    const std::unordered_map<int, std::string>& ownership,
    const std::vector<PoliticalEntity>& entities)
{
    constexpr int W = 2048, H = 1024;

    s.bakeData.assign(W * H * 4, 0);
    s.bakeMap.assign(W * H, -1);

    auto& data    = s.bakeData;
    auto& cellMap = s.bakeMap;

    glBindTexture(GL_TEXTURE_2D, s.tex);

    if (s.grid && !ownership.empty()) {
        const auto& cells    = s.grid->cells();
        const int   numCells = (int)cells.size();

        std::unordered_map<std::string, glm::vec4> colorMap;
        colorMap.reserve(entities.size());
        for (const auto& pe : entities)
            colorMap[pe.id] = pe.color;

        std::unordered_map<std::string, float> darkenMap;
        {
            std::unordered_map<std::string, std::string> liegeOf;
            for (const auto& pe : entities)
                liegeOf[pe.id] = pe.liege_id;
            for (const auto& pe : entities) {
                int depth = 0;
                std::string cur = pe.liege_id;
                while (!cur.empty() && depth < 8) {
                    auto it = liegeOf.find(cur);
                    if (it == liegeOf.end()) break;
                    cur = it->second;
                    ++depth;
                }
                darkenMap[pe.id] = std::max(0.3f, 1.0f - depth * 0.12f);
            }
        }

        struct CellColor { uint8_t r, g, b, a; };
        std::vector<CellColor> cellColors(numCells, {0, 0, 0, 0});
        std::vector<bool>      cellOwned(numCells, false);
        for (const auto& [cellId, ownerId] : ownership) {
            if (cellId < 0 || cellId >= numCells || ownerId.empty()) continue;
            auto colIt = colorMap.find(ownerId);
            if (colIt == colorMap.end()) continue;
            const glm::vec4& c = colIt->second;
            float dk = 1.0f;
            auto dkit = darkenMap.find(ownerId);
            if (dkit != darkenMap.end()) dk = dkit->second;
            cellColors[cellId] = {
                (uint8_t)(glm::clamp(c.r * dk, 0.f, 1.f) * 255.f),
                (uint8_t)(glm::clamp(c.g * dk, 0.f, 1.f) * 255.f),
                (uint8_t)(glm::clamp(c.b * dk, 0.f, 1.f) * 255.f),
                (uint8_t)(glm::clamp(c.a, 0.f, 1.f) * 200.f)
            };
            cellOwned[cellId] = true;
        }

        constexpr float PI = glm::pi<float>();

        int curCell = 0;
        for (int py = 0; py < H; ++py) {
            float lon0 = (0.5f / W) * 2.0f * PI - PI;
            float lat0 = ((py + 0.5f) / H - 0.5f) * PI;
            glm::vec3 rowDir(std::cos(lat0) * std::cos(lon0),
                             std::sin(lat0),
                             -std::cos(lat0) * std::sin(lon0));
            curCell = s.grid->findCellNearest(rowDir);

            for (int px = 0; px < W; ++px) {
                float lon = ((px + 0.5f) / W) * 2.0f * PI - PI;
                float lat = lat0;
                glm::vec3 dir(std::cos(lat) * std::cos(lon),
                              std::sin(lat),
                              -std::cos(lat) * std::sin(lon));

                float bestDot = glm::dot(cells[curCell].centroid, dir);
                bool improved = true;
                while (improved) {
                    improved = false;
                    int bestNb = curCell;
                    for (int nb : cells[curCell].neighbor_ids) {
                        float d = glm::dot(cells[nb].centroid, dir);
                        if (d > bestDot) { bestDot = d; bestNb = nb; }
                    }
                    if (bestNb != curCell) { curCell = bestNb; improved = true; }
                }

                cellMap[py * W + px] = curCell;

                if (!cellOwned[curCell]) continue;
                uint8_t* p = &data[(py * W + px) * 4];
                p[0] = cellColors[curCell].r; p[1] = cellColors[curCell].g;
                p[2] = cellColors[curCell].b; p[3] = cellColors[curCell].a;
            }
        }

        auto getOwner = [&](int cell) -> const std::string& {
            static const std::string empty;
            if (cell < 0) return empty;
            auto it = ownership.find(cell);
            return it != ownership.end() ? it->second : empty;
        };

        constexpr int bdx[] = {-1, 1,  0, 0};
        constexpr int bdy[] = { 0, 0, -1, 1};

        for (int py = 0; py < H; ++py) {
            for (int px = 0; px < W; ++px) {
                int ci = cellMap[py * W + px];
                if (ci < 0) continue;
                const std::string& ownerA = getOwner(ci);

                bool isBorder = false;
                for (int d = 0; d < 4 && !isBorder; ++d) {
                    int nx = (px + bdx[d] + W) % W;
                    int ny = py + bdy[d];
                    if (ny < 0 || ny >= H) continue;
                    int cn = cellMap[ny * W + nx];
                    if (cn == ci) continue;
                    if (getOwner(cn) != ownerA) isBorder = true;
                }

                if (isBorder) {
                    uint8_t* p = &data[(py * W + px) * 4];
                    p[0] = 15; p[1] = 15; p[2] = 15; p[3] = 255;
                }
            }
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data.data());

        std::vector<int> prevMap = cellMap;
        int prevW = W, prevH = H, mipLevel = 1;

        while (prevW > 1 || prevH > 1) {
            const int mipW = std::max(1, prevW / 2);
            const int mipH = std::max(1, prevH / 2);

            std::vector<uint8_t> mipData(mipW * mipH * 4, 0);
            std::vector<int>     mipMap(mipW * mipH, -1);

            for (int my = 0; my < mipH; ++my) {
                for (int mx = 0; mx < mipW; ++mx) {
                    int cands[4], cnts[4], n = 0;
                    for (int sy = my * 2; sy < my * 2 + 2; ++sy) {
                        for (int sx = mx * 2; sx < mx * 2 + 2; ++sx) {
                            if (sx >= prevW || sy >= prevH) continue;
                            const int ci = prevMap[sy * prevW + sx];
                            if (ci < 0) continue;
                            bool found = false;
                            for (int k = 0; k < n; ++k)
                                if (cands[k] == ci) { cnts[k]++; found = true; break; }
                            if (!found && n < 4) { cands[n] = ci; cnts[n] = 1; ++n; }
                        }
                    }
                    int bestCell = -1, bestCount = 0;
                    for (int k = 0; k < n; ++k)
                        if (cnts[k] > bestCount) { bestCount = cnts[k]; bestCell = cands[k]; }
                    mipMap[my * mipW + mx] = bestCell;
                    if (bestCell >= 0 && bestCell < numCells && cellOwned[bestCell]) {
                        uint8_t* p = &mipData[(my * mipW + mx) * 4];
                        p[0] = cellColors[bestCell].r; p[1] = cellColors[bestCell].g;
                        p[2] = cellColors[bestCell].b; p[3] = cellColors[bestCell].a;
                    }
                }
            }

            glTexImage2D(GL_TEXTURE_2D, mipLevel, GL_RGBA8, mipW, mipH, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, mipData.data());
            prevMap = std::move(mipMap);
            prevW   = mipW;
            prevH   = mipH;
            ++mipLevel;
        }

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  mipLevel - 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, data.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

std::string PoliticalMapLayer::makeEntityId(const std::vector<PoliticalEntity>& entities) {
    int next = 0;
    for (const auto& pe : entities) {
        if (pe.id.size() > 3 && pe.id.substr(0, 3) == "pe_") {
            try { next = std::max(next, std::stoi(pe.id.substr(3)) + 1); }
            catch (...) {}
        }
    }
    return "pe_" + std::to_string(next);
}
