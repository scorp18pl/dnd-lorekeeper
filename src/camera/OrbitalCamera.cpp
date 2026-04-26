#include "OrbitalCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

void OrbitalCamera::update(glm::vec2 mouseDelta, float scrollDelta, bool dragging, float viewportH) {
    if (dragging) {
        // rad/pixel that keeps a grabbed surface point under the cursor
        float rpp    = 2.0f * std::tan(m_Fov * 0.5f) * (m_Distance - 1.0f) / viewportH;
        m_Azimuth   += mouseDelta.x * rpp;
        m_Elevation += mouseDelta.y * rpp;
        m_Elevation  = std::clamp(m_Elevation,
                                  glm::radians(-85.0f),
                                  glm::radians( 85.0f));
    }

    // Zoom scales altitude (distance above surface) so the camera can never
    // overshoot through the sphere regardless of scroll speed.
    float alt = m_Distance - 1.0f;
    alt -= scrollDelta * k_ZoomSensitivity * alt;
    alt = std::clamp(alt, m_MinDistance - 1.0f, m_MaxDistance - 1.0f);
    m_Distance = 1.0f + alt;
}

glm::vec3 OrbitalCamera::position() const {
    return {
        m_Distance * std::cos(m_Elevation) * std::cos(m_Azimuth),
        m_Distance * std::sin(m_Elevation),
        m_Distance * std::cos(m_Elevation) * std::sin(m_Azimuth)
    };
}

glm::mat4 OrbitalCamera::viewMatrix() const {
    return glm::lookAt(position(), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 OrbitalCamera::projectionMatrix(float aspect) const {
    return glm::perspective(m_Fov, aspect, 0.01f, 10000.0f);
}
