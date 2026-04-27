#include "QuadSphere.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <cmath>

static constexpr int N = QuadSphere::kPatchRes;

QuadSphere::QuadSphere()  { buildPatchMesh(); }
QuadSphere::~QuadSphere() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
    glDeleteBuffers(1, &m_EBO);
}

void QuadSphere::buildPatchMesh() {
    // (N+1)×(N+1) vertices with UV in [0,1]²
    std::vector<glm::vec2> verts;
    verts.reserve((N + 1) * (N + 1));
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i)
            verts.push_back({ float(i) / N, float(j) / N });

    // N×N quads, CCW winding matching CubeSphere
    std::vector<uint32_t> idx;
    idx.reserve(N * N * 6);
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            uint32_t tl = uint32_t(j * (N + 1) + i);
            uint32_t tr = tl + 1;
            uint32_t bl = tl + uint32_t(N + 1);
            uint32_t br = bl + 1;
            idx.insert(idx.end(), { tl, bl, tr, tr, bl, br });
        }
    }
    m_IndexCount = static_cast<int>(idx.size());

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);
    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(glm::vec2)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)),
                 idx.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

// Matches faceDir() in planet.vert and face basis in CubeSphere.cpp.
glm::vec3 QuadSphere::faceToSphere(int face, float u, float v) {
    float s = u * 2.0f - 1.0f;
    float t = v * 2.0f - 1.0f;
    switch (face) {
        case 0: return glm::normalize(glm::vec3( 1,-t,-s));
        case 1: return glm::normalize(glm::vec3(-1,-t, s));
        case 2: return glm::normalize(glm::vec3( s, 1, t));
        case 3: return glm::normalize(glm::vec3( s,-1,-t));
        case 4: return glm::normalize(glm::vec3( s,-t, 1));
        default: return glm::normalize(glm::vec3(-s,-t,-1));
    }
}

void QuadSphere::update(const glm::vec3& camPos, float splitThreshold) {
    m_DrawList.clear();
    m_DrawList.reserve(512);
    float     camDist = glm::length(camPos);
    glm::vec3 camDir  = (camDist > 1e-4f) ? camPos / camDist : glm::vec3(0, 1, 0);
    for (int face = 0; face < 6; ++face)
        traverse(face, 0.0f, 0.0f, 1.0f, 0, camPos, splitThreshold, camDist, camDir);
}

void QuadSphere::traverse(int face, float u0, float v0, float sz, int depth,
                          const glm::vec3& camPos, float threshold,
                          float camDist, const glm::vec3& camDir) {
    float     hf     = sz * 0.5f;
    glm::vec3 center = faceToSphere(face, u0 + hf, v0 + hf);

    // Horizon cull: point P on unit sphere is visible when dot(P, camDir) > 1/camDist.
    // Use a conservative patch-size margin so we don't cull partially-visible patches.
    float horizonCos = (camDist > 1.001f) ? 1.0f / camDist : 0.0f;
    if (glm::dot(center, camDir) < horizonCos - 1.5f * sz)
        return;

    float dist = glm::length(camPos - center);
    if (depth < kMaxDepth && sz / dist > threshold) {
        traverse(face, u0,    v0,    hf, depth+1, camPos, threshold, camDist, camDir);
        traverse(face, u0+hf, v0,    hf, depth+1, camPos, threshold, camDist, camDir);
        traverse(face, u0,    v0+hf, hf, depth+1, camPos, threshold, camDist, camDir);
        traverse(face, u0+hf, v0+hf, hf, depth+1, camPos, threshold, camDist, camDir);
    } else {
        m_DrawList.push_back({ face, u0, v0, sz });
    }
}

void QuadSphere::draw(const Shader& shader) const {
    glBindVertexArray(m_VAO);
    for (const auto& cmd : m_DrawList) {
        shader.setInt  ("u_Face",        cmd.face);
        shader.setVec2 ("u_PatchOrigin", { cmd.u0, cmd.v0 });
        shader.setFloat("u_PatchSize",   cmd.sz);
        glDrawElements(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}
