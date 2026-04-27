#include "GoldbergGrid.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

static constexpr float PHI = 1.61803398874989484820f; // (1 + sqrt(5)) / 2

// Three.js IcosahedronGeometry vertex/face table — verified CCW from outside.
// ak6 vertex ordering — must match kIcoFaces indices exactly.
static const glm::vec3 kIcoRaw[12] = {
    {-1, PHI, 0}, { 1, PHI, 0}, {-1,-PHI, 0}, { 1,-PHI, 0},
    { 0, -1, PHI}, { 0,  1, PHI}, { 0, -1,-PHI}, { 0,  1,-PHI},
    { PHI, 0,-1}, { PHI, 0, 1}, {-PHI, 0,-1}, {-PHI, 0, 1}
};
static const int kIcoFaces[20][3] = {
    {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
    {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
    {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
    {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,3}
};

// Quantize unit-sphere position to a 60-bit key for vertex welding.
static int64_t posKey(const glm::vec3& p) {
    auto qi = [](float x) -> int64_t {
        return static_cast<int64_t>((x + 1.0f) * float(1 << 19) + 0.5f);
    };
    return (qi(p.x) << 40) | (qi(p.y) << 20) | qi(p.z);
}

GoldbergGrid::GoldbergGrid(int subdiv) { generate(subdiv); }

void GoldbergGrid::generate(int n) {
    glm::vec3 icoV[12];
    for (int i = 0; i < 12; ++i) icoV[i] = glm::normalize(kIcoRaw[i]);

    // ── Step 1: Subdivide icosahedron, project to sphere, weld shared verts ──
    std::vector<glm::vec3>           verts;
    std::unordered_map<int64_t, int> vertIdx;
    std::vector<std::array<int,3>>   trifaces;

    auto addVert = [&](glm::vec3 p) -> int {
        p = glm::normalize(p);
        int64_t key = posKey(p);
        auto it = vertIdx.find(key);
        if (it != vertIdx.end()) return it->second;
        int idx = static_cast<int>(verts.size());
        verts.push_back(p);
        vertIdx[key] = idx;
        return idx;
    };

    for (int f = 0; f < 20; ++f) {
        const glm::vec3 A = icoV[kIcoFaces[f][0]];
        const glm::vec3 B = icoV[kIcoFaces[f][1]];
        const glm::vec3 C = icoV[kIcoFaces[f][2]];

        // Triangular grid: grid[a][b] = vertex index for bary (n-a-b, a, b) / n.
        std::vector<std::vector<int>> grid(n + 1);
        for (int a = 0; a <= n; ++a) {
            grid[a].resize(n - a + 1);
            for (int b = 0; b <= n - a; ++b) {
                int c = n - a - b;
                glm::vec3 p = (float(c) * A + float(a) * B + float(b) * C) / float(n);
                grid[a][b] = addVert(p);
            }
        }

        for (int a = 0; a < n; ++a) {
            for (int b = 0; b < n - a; ++b) {
                trifaces.push_back({ grid[a][b], grid[a+1][b], grid[a][b+1] });
                if (a + b + 2 <= n)
                    trifaces.push_back({ grid[a+1][b], grid[a+1][b+1], grid[a][b+1] });
            }
        }
    }

    // ── Step 2: Vertex → incident face list ──────────────────────────────────
    int nVerts = static_cast<int>(verts.size());
    std::vector<std::vector<int>> vertFaces(nVerts);
    for (int fi = 0; fi < (int)trifaces.size(); ++fi)
        for (int k = 0; k < 3; ++k)
            vertFaces[trifaces[fi][k]].push_back(fi);

    // ── Step 3: Face centroids projected to unit sphere ───────────────────────
    std::vector<glm::vec3> fc(trifaces.size());
    for (int fi = 0; fi < (int)trifaces.size(); ++fi) {
        glm::vec3 sum = verts[trifaces[fi][0]] + verts[trifaces[fi][1]] + verts[trifaces[fi][2]];
        fc[fi] = glm::normalize(sum / 3.0f);
    }

    // ── Step 4: Build Goldberg cells (dual polygon per triangulation vertex) ──
    m_Cells.resize(nVerts);
    for (int vi = 0; vi < nVerts; ++vi) {
        Cell& cell    = m_Cells[vi];
        cell.centroid = verts[vi];
        cell.lat      = glm::degrees(std::asin(glm::clamp(verts[vi].y, -1.0f, 1.0f)));
        cell.lon      = glm::degrees(std::atan2(-verts[vi].z, verts[vi].x));
        cell.num_sides = static_cast<int>(vertFaces[vi].size());

        // Build right-handed tangent frame at centroid: cross(tx, ty) == up
        glm::vec3 up  = cell.centroid;
        glm::vec3 ref = (std::abs(up.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 tx  = glm::normalize(glm::cross(ref, up));
        glm::vec3 ty  = glm::cross(up, tx);

        // Sort incident face centroids by angle in tangent plane (CCW from outside)
        std::vector<std::pair<float, int>> angled;
        angled.reserve(cell.num_sides);
        for (int fi : vertFaces[vi]) {
            glm::vec3 d = fc[fi] - up;
            angled.push_back({ std::atan2(glm::dot(d, ty), glm::dot(d, tx)), fi });
        }
        std::sort(angled.begin(), angled.end());

        cell.poly.reserve(cell.num_sides);
        for (auto& [angle, fi] : angled)
            cell.poly.push_back(fc[fi]);
    }

    // ── Step 5: Neighbor adjacency (one per shared triangulation edge) ────────
    for (const auto& f : trifaces) {
        m_Cells[f[0]].neighbor_ids.push_back(f[1]);
        m_Cells[f[0]].neighbor_ids.push_back(f[2]);
        m_Cells[f[1]].neighbor_ids.push_back(f[0]);
        m_Cells[f[1]].neighbor_ids.push_back(f[2]);
        m_Cells[f[2]].neighbor_ids.push_back(f[0]);
        m_Cells[f[2]].neighbor_ids.push_back(f[1]);
    }
    for (auto& cell : m_Cells) {
        auto& ids = cell.neighbor_ids;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
}

int GoldbergGrid::findCellNearest(const glm::vec3& dir) const {
    int   best    = 0;
    float bestDot = -2.0f;
    for (int i = 0; i < (int)m_Cells.size(); ++i) {
        float d = glm::dot(m_Cells[i].centroid, dir);
        if (d > bestDot) { bestDot = d; best = i; }
    }
    return best;
}
