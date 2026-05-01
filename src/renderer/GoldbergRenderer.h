#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include "world/GoldbergGrid.h"
#include "world/PoliticalEntity.h"

class Shader;

class GoldbergRenderer {
public:
    GoldbergRenderer(const GoldbergGrid& grid,
                     const glm::vec4& defaultColor = { 0.2f, 0.5f, 0.2f, 0.0f });
    ~GoldbergRenderer();

    GoldbergRenderer(const GoldbergRenderer&)            = delete;
    GoldbergRenderer& operator=(const GoldbergRenderer&) = delete;

    // Recolor all cells from ownership map + entity palette.
    // Unowned cells get unownedColor (default fully transparent).
    void syncFromPoliticalMap(
        const std::unordered_map<int, std::string>& ownership,
        const std::vector<PoliticalEntity>& entities,
        const glm::vec4& unownedColor = { 0.0f, 0.0f, 0.0f, 0.0f });

    void setCellColor(int cellId, const glm::vec4& color);
    void resetColors(const glm::vec4& color);
    void setHoverCell(int cellId);   // -1 to clear

    void draw(const Shader& shader) const;          // cell fills
    void drawBorders(const Shader& shader) const;   // ownership borders

private:
    const GoldbergGrid& m_Grid;

    struct Vertex { glm::vec3 pos; glm::vec4 color; };

    std::vector<glm::vec3> m_BasePos;
    std::vector<int>       m_VertCell;
    std::vector<glm::vec4> m_CellColors;

    int    m_HoverCell   = -1;
    int    m_VertexCount = 0;
    GLuint m_VAO = 0;
    GLuint m_VBO = 0;
    mutable bool m_Dirty = true;

    // Border line geometry (rebuilt when ownership changes)
    std::vector<Vertex> m_BorderVerts;
    int    m_BorderVertexCount = 0;
    GLuint m_BorderVAO = 0;
    GLuint m_BorderVBO = 0;
    mutable bool m_BorderDirty = true;

    void buildGeometry();
    void flush() const;

    void buildBorders(const std::unordered_map<int, std::string>& ownership,
                      const std::vector<PoliticalEntity>& entities);
    void flushBorders() const;
};
