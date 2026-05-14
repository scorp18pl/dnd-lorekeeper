#include "Application.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>

#include "io/WorldSerializer.h"

#include <cmath>

Application::Application() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    initImGui();

    m_PlanetShader = std::make_unique<Shader>("shaders/planet.vert",
                                              "shaders/sphere.frag");
    m_QuadSphere  = std::make_unique<QuadSphere>();

    m_TextRenderer.init();

    loadRecentProjects();
    if (!m_RecentProjects.empty())
        openWorld(m_RecentProjects.front(), /*silent=*/true);

    // 1×1 transparent texture bound to unused overlay slots
    {
        glGenTextures(1, &m_NullTex);
        glBindTexture(GL_TEXTURE_2D, m_NullTex);
        static const unsigned char zero[4] = {0, 0, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        for (int i = 0; i < 4; ++i)
            m_OverlayTexIds[i] = m_NullTex;
    }
}

Application::~Application() {
    if (m_TextureId) glDeleteTextures(1, &m_TextureId);
    for (int i = 0; i < 4; ++i) {
        if (m_OverlayTexIds[i] && m_OverlayTexIds[i] != m_NullTex)
            glDeleteTextures(1, &m_OverlayTexIds[i]);
    }
    if (m_NullTex) glDeleteTextures(1, &m_NullTex);
    shutdownImGui();
}

void Application::run() {
    while (!m_Window.shouldClose()) {
        m_Window.pollEvents();
        processInput();
        renderScene();
        renderUI();
        m_Window.swapBuffers();
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────

void Application::processInput() {
    ImGuiIO& io = ImGui::GetIO();

    bool  dragging = !io.WantCaptureMouse &&
                     m_Window.mouseButton(GLFW_MOUSE_BUTTON_MIDDLE);
    float scroll   = io.WantCaptureMouse ? 0.0f : m_Window.scrollDelta();

    float vpH = (float)m_Window.height();
    m_Camera.update(m_Window.cursorDelta(), scroll, dragging, vpH);

    if (!io.WantCaptureKeyboard) {
        bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                    ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            m_CommandStack.undo();
            if (m_World) WorldSerializer::save(*m_World);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            m_CommandStack.redo();
            if (m_World) WorldSerializer::save(*m_World);
        }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S) && m_World)
            WorldSerializer::save(*m_World);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_EditMode = EditMode::Navigate;
            m_RelocateMode = false;
            m_DraggingNode = false;
            m_DragNodeId.clear();
            m_NetworkConnectFrom.clear();
        }
    }
}

// ── Ray casting ───────────────────────────────────────────────────────────────

std::optional<glm::vec2> Application::castRay(float mouseX, float mouseY) const {
    float ndcX =  (mouseX / m_Window.width())  * 2.0f - 1.0f;
    float ndcY = 1.0f - (mouseY / m_Window.height()) * 2.0f;

    glm::mat4 invPV = glm::inverse(
        m_Camera.projectionMatrix(m_Window.aspect()) * m_Camera.viewMatrix());

    glm::vec4 near4 = invPV * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    near4 /= near4.w;
    glm::vec4 far4  = invPV * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    far4  /= far4.w;

    glm::vec3 ro = m_Camera.position();
    glm::vec3 rd = glm::normalize(glm::vec3(far4) - glm::vec3(near4));

    float a   = glm::dot(rd, rd);
    float b   = 2.0f * glm::dot(ro, rd);
    float c   = glm::dot(ro, ro) - 1.0f;
    float dis = b * b - 4.0f * a * c;
    if (dis < 0.0f) return std::nullopt;

    float t = (-b - std::sqrt(dis)) / (2.0f * a);
    if (t < 0.0f) return std::nullopt;

    glm::vec3 hit = ro + t * rd;
    float lat = glm::degrees(std::asin(std::clamp(hit.y, -1.0f, 1.0f)));
    float lon = glm::degrees(std::atan2(-hit.z, hit.x));
    return glm::vec2(lat, lon);
}

glm::vec3 Application::latLonToWorld(float latDeg, float lonDeg) const {
    float lat = glm::radians(latDeg);
    float lon = glm::radians(lonDeg);
    glm::vec3 n { std::cos(lat) * std::cos(lon),
                  std::sin(lat),
                 -std::cos(lat) * std::sin(lon) };
    return n;
}

glm::vec2 Application::worldToScreen(glm::vec3 worldPos) const {
    glm::mat4 vp   = m_Camera.projectionMatrix(m_Window.aspect()) * m_Camera.viewMatrix();
    glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return { -10000.0f, -10000.0f };
    clip /= clip.w;
    return { (clip.x * 0.5f + 0.5f) * m_Window.width(),
             (1.0f - (clip.y * 0.5f + 0.5f)) * m_Window.height() };
}
