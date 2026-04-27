#include "GoldbergRenderer.h"
#include "Shader.h"
#include <glm/glm.hpp>

static constexpr float kCellScale = 1.002f; // slightly above terrain to avoid z-fighting

GoldbergRenderer::GoldbergRenderer(const GoldbergGrid& grid, const glm::vec4& defaultColor)
    : m_Grid(grid)
    , m_CellColors(grid.cells().size(), defaultColor)
{
    buildGeometry();

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
}

GoldbergRenderer::~GoldbergRenderer() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

void GoldbergRenderer::buildGeometry() {
    const auto& cells = m_Grid.cells();
    // Each cell: num_sides fan triangles × 3 vertices
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
    std::vector<Vertex> verts;
    verts.resize(m_VertexCount);
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
    (void)shader; // uniforms set by caller
    if (m_Dirty) flush();
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, m_VertexCount);
    glBindVertexArray(0);
}
