#include "GoldbergRenderer.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <algorithm>

static constexpr float kCellScale   = 1.002f;
static constexpr float kBorderScale = 1.0025f;

GoldbergRenderer::GoldbergRenderer(const GoldbergGrid& grid, const glm::vec4& defaultColor)
    : m_Grid(grid)
    , m_CellColors(grid.cells().size(), defaultColor)
{
    buildGeometry();

    // Cell fill VAO/VBO
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(m_VertexCount * sizeof(Vertex)),
                 nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Border VAO/VBO (dynamic, starts empty)
    glGenVertexArrays(1, &m_BorderVAO);
    glGenBuffers(1, &m_BorderVBO);
    glBindVertexArray(m_BorderVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_BorderVBO);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

GoldbergRenderer::~GoldbergRenderer() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
    glDeleteVertexArrays(1, &m_BorderVAO);
    glDeleteBuffers(1, &m_BorderVBO);
}

void GoldbergRenderer::buildGeometry() {
    const auto& cells = m_Grid.cells();
    int total = 0;
    for (const auto& c : cells) total += c.num_sides * 3;
    m_BasePos.reserve(total);
    m_VertCell.reserve(total);

    for (int ci = 0; ci < (int)cells.size(); ++ci) {
        const auto& c = cells[ci];
        glm::vec3 cen = c.centroid * kCellScale;
        int k = c.num_sides;
        for (int i = 0; i < k; ++i) {
            m_BasePos.push_back(cen);
            m_BasePos.push_back(c.poly[i] * kCellScale);
            m_BasePos.push_back(c.poly[(i + 1) % k] * kCellScale);
            m_VertCell.push_back(ci);
            m_VertCell.push_back(ci);
            m_VertCell.push_back(ci);
        }
    }
    m_VertexCount = static_cast<int>(m_BasePos.size());
}

void GoldbergRenderer::flush() const {
    std::vector<Vertex> verts(m_VertexCount);
    for (int i = 0; i < m_VertexCount; ++i) {
        int ci = m_VertCell[i];
        glm::vec4 color = m_CellColors[ci];
        if (ci == m_HoverCell)
            color = glm::mix(color, glm::vec4(1.0f, 1.0f, 1.0f, 0.7f), 0.35f);
        verts[i] = { m_BasePos[i], color };
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(m_VertexCount * sizeof(Vertex)),
                    verts.data());
    m_Dirty = false;
}

// ── Border geometry ──────────────────────────────────────────────────────────

void GoldbergRenderer::buildBorders(
    const std::unordered_map<int, std::string>& ownership,
    const std::vector<PoliticalEntity>& entities)
{
    // Build entity id → color lookup
    std::unordered_map<std::string, glm::vec4> entityColor;
    for (const auto& pe : entities)
        entityColor[pe.id] = pe.color;

    static const glm::vec4 kBorderColor { 0.08f, 0.08f, 0.08f, 1.0f };

    const auto& cells = m_Grid.cells();
    m_BorderVerts.clear();

    // For each cell, check every neighbor. If they have different owners, add
    // the shared border edge (two consecutive polygon vertices shared by both
    // cells). To avoid duplicates, only emit when cell index < neighbor index.
    for (int ci = 0; ci < (int)cells.size(); ++ci) {
        const auto& cell = cells[ci];

        auto ownerA = [&]() -> std::string {
            auto it = ownership.find(ci);
            return it != ownership.end() ? it->second : "";
        };

        for (int ni : cell.neighbor_ids) {
            if (ni <= ci) continue; // emit each edge once

            auto ownerB = [&]() -> std::string {
                auto it = ownership.find(ni);
                return it != ownership.end() ? it->second : "";
            };

            if (ownerA() == ownerB()) continue; // same territory, no border

            // Find the two polygon vertices shared by cell ci and cell ni.
            // The shared edge corresponds to the one primal triangle adjacent
            // to both vertices. We identify shared verts by position equality
            // (they're the exact same float values from the same face centroid).
            const auto& polyA = cell.poly;
            const auto& polyB = cells[ni].poly;

            glm::vec3 shared[2];
            int found = 0;
            for (const auto& pA : polyA) {
                for (const auto& pB : polyB) {
                    if (glm::length(pA - pB) < 1e-5f) {
                        if (found < 2) shared[found] = pA;
                        ++found;
                        break;
                    }
                }
                if (found == 2) break;
            }

            if (found < 2) continue; // shouldn't happen in a valid grid

            m_BorderVerts.push_back({ shared[0] * kBorderScale, kBorderColor });
            m_BorderVerts.push_back({ shared[1] * kBorderScale, kBorderColor });
        }
    }

    m_BorderVertexCount = static_cast<int>(m_BorderVerts.size());
    m_BorderDirty = true;
}

void GoldbergRenderer::flushBorders() const {
    glBindBuffer(GL_ARRAY_BUFFER, m_BorderVBO);
    if (m_BorderVertexCount > 0) {
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(m_BorderVertexCount * sizeof(Vertex)),
                     m_BorderVerts.data(), GL_DYNAMIC_DRAW);
    }
    m_BorderDirty = false;
}

// ── Public API ───────────────────────────────────────────────────────────────

void GoldbergRenderer::syncFromPoliticalMap(
    const std::unordered_map<int, std::string>& ownership,
    const std::vector<PoliticalEntity>& entities,
    const glm::vec4& unownedColor)
{
    std::unordered_map<std::string, glm::vec4> colorMap;
    for (const auto& pe : entities)
        colorMap[pe.id] = pe.color;

    const int n = static_cast<int>(m_CellColors.size());
    for (int i = 0; i < n; ++i) {
        auto it = ownership.find(i);
        if (it == ownership.end() || it->second.empty()) {
            m_CellColors[i] = unownedColor;
        } else {
            auto cit = colorMap.find(it->second);
            m_CellColors[i] = (cit != colorMap.end()) ? cit->second : unownedColor;
        }
    }
    m_Dirty = true;
}

void GoldbergRenderer::setCellColor(int cellId, const glm::vec4& color) {
    if (cellId < 0 || cellId >= (int)m_CellColors.size()) return;
    m_CellColors[cellId] = color;
    m_Dirty = true;
}

void GoldbergRenderer::resetColors(const glm::vec4& color) {
    for (auto& c : m_CellColors) c = color;
    m_Dirty = true;
}

void GoldbergRenderer::setHoverCell(int cellId) {
    if (m_HoverCell != cellId) {
        m_HoverCell = cellId;
        m_Dirty = true;
    }
}

void GoldbergRenderer::draw(const Shader& shader) const {
    (void)shader;
    if (m_Dirty) flush();
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);
    glBindVertexArray(0);
}

void GoldbergRenderer::drawBorders(const Shader& shader) const {
    (void)shader;
    if (m_BorderVertexCount == 0) return;
    if (m_BorderDirty) flushBorders();
    glBindVertexArray(m_BorderVAO);
    glDrawArrays(GL_LINES, 0, m_BorderVertexCount);
    glBindVertexArray(0);
}
