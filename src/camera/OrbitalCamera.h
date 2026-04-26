#pragma once
#include <glm/glm.hpp>

class OrbitalCamera {
public:
    // Called once per frame with raw input deltas.
    // mouseDelta: pixels moved this frame (only applied when dragging).
    // scrollDelta: scroll wheel ticks this frame.
    // dragging: whether the orbit mouse button is held.
    void update(glm::vec2 mouseDelta, float scrollDelta, bool dragging, float viewportH = 900.0f);

    glm::mat4 viewMatrix()               const;
    glm::mat4 projectionMatrix(float aspect) const;

    // World-space position of the camera.
    glm::vec3 position() const;

    float distance() const { return m_Distance; }
    float fov()      const { return m_Fov; }

    void setDistance(float d)                        { m_Distance = d; }
    void setDistanceLimits(float minD, float maxD)   { m_MinDistance = minD; m_MaxDistance = maxD; }
    void setElevation(float radians)                 { m_Elevation = radians; }
    void setAzimuth(float radians)                   { m_Azimuth = radians; }

private:
    float m_Azimuth    =  0.0f;
    float m_Elevation  =  glm::radians(25.0f);
    float m_Distance   =  2.8f;
    float m_Fov        =  glm::radians(45.0f);
    float m_MinDistance =  1.05f;
    float m_MaxDistance = 20.0f;

    static constexpr float k_OrbitSensitivity = 0.008f;
    static constexpr float k_ZoomSensitivity  = 0.12f;
};
