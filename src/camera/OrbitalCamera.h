#pragma once
#include <glm/glm.hpp>

class OrbitalCamera {
public:
    // Called once per frame with raw input deltas.
    // mouseDelta: pixels moved this frame (only applied when dragging).
    // scrollDelta: scroll wheel ticks this frame.
    // dragging: whether the orbit mouse button is held.
    void update(glm::vec2 mouseDelta, float scrollDelta, bool dragging);

    glm::mat4 viewMatrix()               const;
    glm::mat4 projectionMatrix(float aspect) const;

    // World-space position of the camera.
    glm::vec3 position() const;

    float distance() const { return m_Distance; }

private:
    float m_Azimuth   =  0.0f;                  // radians, horizontal angle
    float m_Elevation =  glm::radians(25.0f);   // radians, clamped ±85°
    float m_Distance  =  2.8f;                  // units from origin
    float m_Fov       =  glm::radians(45.0f);

    static constexpr float k_OrbitSensitivity = 0.008f;
    static constexpr float k_ZoomSensitivity  = 0.12f;
    static constexpr float k_MinDistance      = 1.05f;
    static constexpr float k_MaxDistance      = 20.0f;
};
