#include "OrbitalCamera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

void OrbitalCamera::update(glm::vec2 mouseDelta, float scrollDelta, bool dragging) {
    if (dragging) {
        m_Azimuth   -= mouseDelta.x * k_OrbitSensitivity;
        m_Elevation += mouseDelta.y * k_OrbitSensitivity;
        m_Elevation  = std::clamp(m_Elevation,
                                  glm::radians(-85.0f),
                                  glm::radians( 85.0f));
    }

    m_Distance -= scrollDelta * k_ZoomSensitivity * m_Distance;
    m_Distance  = std::clamp(m_Distance, k_MinDistance, k_MaxDistance);
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
    return glm::perspective(m_Fov, aspect, 0.01f, 100.0f);
}
