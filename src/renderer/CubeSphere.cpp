#include "CubeSphere.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

CubeSphere::CubeSphere(int subdivisions) { build(subdivisions); }

CubeSphere::~CubeSphere() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
    glDeleteBuffers(1, &m_EBO);
}

void CubeSphere::draw() const {
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void CubeSphere::build(int N) {
    // Each face defined by forward (outward normal direction), right, up vectors.
    // Winding chosen so each face is CCW when viewed from outside the sphere.
    struct Face { glm::vec3 fwd, right, up; };
    // Invariant: up × right == fwd  (CCW from outside with winding {tl,bl,tr,tr,bl,br})
    static constexpr Face faces[6] = {
        {{ 1, 0, 0}, { 0, 0,-1}, { 0,-1, 0}}, // +X
        {{-1, 0, 0}, { 0, 0, 1}, { 0,-1, 0}}, // -X
        {{ 0, 1, 0}, { 1, 0, 0}, { 0, 0, 1}}, // +Y
        {{ 0,-1, 0}, { 1, 0, 0}, { 0, 0,-1}}, // -Y
        {{ 0, 0, 1}, { 1, 0, 0}, { 0,-1, 0}}, // +Z
        {{ 0, 0,-1}, {-1, 0, 0}, { 0,-1, 0}}, // -Z
    };

    std::vector<glm::vec3>  positions;
    std::vector<uint32_t>   indices;
    positions.reserve(6 * (N + 1) * (N + 1));
    indices.reserve(6 * N * N * 6);

    for (const auto& face : faces) {
        auto base = static_cast<uint32_t>(positions.size());

        for (int j = 0; j <= N; ++j) {
            for (int i = 0; i <= N; ++i) {
                float s = static_cast<float>(i) / N * 2.0f - 1.0f;
                float t = static_cast<float>(j) / N * 2.0f - 1.0f;
                positions.push_back(glm::normalize(face.fwd + s * face.right + t * face.up));
            }
        }

        for (int j = 0; j < N; ++j) {
            for (int i = 0; i < N; ++i) {
                uint32_t tl = base + j * (N + 1) + i;
                uint32_t tr = tl + 1;
                uint32_t bl = tl + (N + 1);
                uint32_t br = bl + 1;
                // CCW winding (front-face = outside sphere)
                indices.insert(indices.end(), {tl, bl, tr, tr, bl, br});
            }
        }
    }

    m_IndexCount = static_cast<int>(indices.size());

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(positions.size() * sizeof(glm::vec3)),
                 positions.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}
