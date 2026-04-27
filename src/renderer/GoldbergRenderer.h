#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>
#include "world/GoldbergGrid.h"

class Shader;

class GoldbergRenderer {
public:
    GoldbergRenderer(const GoldbergGrid& grid,
                     const glm::vec4& defaultColor = { 0.2f, 0.5f, 0.2f, 0.25f });
    ~GoldbergRenderer();

    GoldbergRenderer(const GoldbergRenderer&)            = delete;
    GoldbergRenderer& operator=(const GoldbergRenderer&) = delete;

    void setCellColor(int cellId, const glm::vec4& color);
    void resetColors(const glm::vec4& color);
    void setHoverCell(int cellId);   // -1 to clear

    void draw(const Shader& shader) const;  // shader must already be bound

private:
    const GoldbergGrid& m_Grid;

    struct Vertex { glm::vec3 pos; glm::vec4 color; };

    // Static geometry: per-vertex cell ID and base position (at radius 1.002)
    std::vector<glm::vec3> m_BasePos;   // position for each VBO vertex
    std::vector<int>       m_VertCell;  // which cell each vertex belongs to

    // Per-cell color source of truth
    std::vector<glm::vec4> m_CellColors;

    int m_HoverCell     = -1;
    int m_VertexCount   = 0;

    GLuint m_VAO = 0;
    GLuint m_VBO = 0;

    mutable bool m_Dirty = true;

    void buildGeometry();
    void flush() const;
};
