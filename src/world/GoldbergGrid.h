#pragma once
#include <glm/glm.hpp>
#include <vector>

// Goldberg polyhedron grid on a unit sphere.
// Cells are the dual of a subdivided icosahedron: 12 pentagons, rest hexagons.
// Total cells = 10*subdiv^2 + 2.
class GoldbergGrid {
public:
    struct Cell {
        glm::vec3          centroid;     // unit-sphere position (triangulation vertex)
        float              lat, lon;    // degrees, lat in [-90,90], lon in (-180,180]
        int                num_sides;   // 5 (pentagon) or 6 (hexagon)
        std::vector<glm::vec3> poly;    // cell polygon vertices in CCW order from outside
        std::vector<int>   neighbor_ids;
    };

    explicit GoldbergGrid(int subdiv);

    const std::vector<Cell>& cells() const { return m_Cells; }
    int findCellNearest(const glm::vec3& dir) const;

private:
    std::vector<Cell> m_Cells;
    void generate(int n);
};
