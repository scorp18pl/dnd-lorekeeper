#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>

class Shader;

// Adaptive quadtree LOD sphere mesh.
// Each frame: call update() to rebuild the draw list, then draw().
// One shared (N+1)^2 patch mesh; face/bounds passed as per-draw uniforms.
class QuadSphere {
public:
    static constexpr int   kPatchRes         = 16;   // quads per patch side
    static constexpr int   kMaxDepth         = 10;
    static constexpr float kDefaultThreshold = 0.3f; // split when sz/dist > this

    QuadSphere();
    ~QuadSphere();

    QuadSphere(const QuadSphere&)            = delete;
    QuadSphere& operator=(const QuadSphere&) = delete;

    // Rebuild draw list. camPos must be in planet model space (unit-sphere coords).
    void update(const glm::vec3& camPos, float splitThreshold = kDefaultThreshold);

    // Draw all patches. Shader must already be bound.
    // Sets u_Face, u_PatchOrigin, u_PatchSize per draw call.
    void draw(const Shader& shader) const;

    int patchCount() const { return static_cast<int>(m_DrawList.size()); }

private:
    GLuint m_VAO        = 0;
    GLuint m_VBO        = 0;
    GLuint m_EBO        = 0;
    int    m_IndexCount = 0;

    struct DrawCmd { int face; float u0, v0, sz; };
    std::vector<DrawCmd> m_DrawList;

    void buildPatchMesh();
    void traverse(int face, float u0, float v0, float sz, int depth,
                  const glm::vec3& camPos, float threshold,
                  float camDist, const glm::vec3& camDir);

    static glm::vec3 faceToSphere(int face, float u, float v);
};
