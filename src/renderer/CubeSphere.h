#pragma once
#include <glad/gl.h>

// Sphere mesh built from 6 cube faces, each subdivided into an N×N grid,
// with vertices projected onto the unit sphere. Positions only — UV is
// computed from the sphere normal in the fragment shader.
class CubeSphere {
public:
    explicit CubeSphere(int subdivisions = 64);
    ~CubeSphere();

    CubeSphere(const CubeSphere&)            = delete;
    CubeSphere& operator=(const CubeSphere&) = delete;

    void draw() const;

private:
    GLuint m_VAO        = 0;
    GLuint m_VBO        = 0;
    GLuint m_EBO        = 0;
    int    m_IndexCount = 0;

    void build(int N);
};
