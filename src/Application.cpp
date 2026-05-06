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

    m_SphereShader = std::make_unique<Shader>("shaders/sphere.vert",
                                              "shaders/sphere.frag");
    m_PlanetShader = std::make_unique<Shader>("shaders/planet.vert",
                                              "shaders/sphere.frag");
    m_Sphere      = std::make_unique<CubeSphere>(64);
    m_QuadSphere  = std::make_unique<QuadSphere>();

    m_TextRenderer.init();

    m_SolarCam.setDistanceLimits(2.0f, 500.0f);
    m_SolarCam.setDistance(20.0f);
    m_SolarCam.setElevation(glm::radians(30.0f));

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
        for (int i = 0; i < 4; ++i) {
            m_OverlayTexIds[i]  = m_NullTex;
            m_OvHeightmapIds[i] = m_NullTex;
        }
    }
}

Application::~Application() {
    if (m_TextureId)   glDeleteTextures(1, &m_TextureId);
    if (m_HeightmapId) glDeleteTextures(1, &m_HeightmapId);
    for (int i = 0; i < 4; ++i) {
        if (m_OverlayTexIds[i]  && m_OverlayTexIds[i]  != m_NullTex)
            glDeleteTextures(1, &m_OverlayTexIds[i]);
        if (m_OvHeightmapIds[i] && m_OvHeightmapIds[i] != m_NullTex)
            glDeleteTextures(1, &m_OvHeightmapIds[i]);
    }
    if (m_NullTex)          glDeleteTextures(1, &m_NullTex);
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
    if (m_ViewMode == ViewMode::SolarSystem)
        m_SolarCam.update(m_Window.cursorDelta(), scroll, dragging, vpH);
    else
        m_Camera.update(m_Window.cursorDelta(), scroll, dragging, vpH);

    if (!io.WantCaptureKeyboard) {
        bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                    ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_CommandStack.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_CommandStack.redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S) && m_World)
            WorldSerializer::save(*m_World);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (m_ViewMode == ViewMode::Planet)
                m_EditMode = EditMode::Navigate;
            m_DraggingEntity = false;
            m_DragEntityIdx  = -1;
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

int Application::castRaySolarSystem(float mouseX, float mouseY,
                                    const std::vector<SolarBodyInfo>& infos) const {
    float ndcX =  (mouseX / m_Window.width())  * 2.0f - 1.0f;
    float ndcY = 1.0f - (mouseY / m_Window.height()) * 2.0f;

    glm::mat4 invVP = glm::inverse(
        m_SolarCam.projectionMatrix(m_Window.aspect()) * m_SolarCam.viewMatrix());

    glm::vec4 near4 = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    near4 /= near4.w;
    glm::vec4 far4  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    far4  /= far4.w;

    glm::vec3 ro = m_SolarCam.position();
    glm::vec3 rd = glm::normalize(glm::vec3(far4) - glm::vec3(near4));

    int   bestIdx = -1;
    float bestT   = 1e30f;

    for (int i = 0; i < (int)infos.size(); ++i) {
        glm::vec3 oc  = ro - infos[i].pos;
        float     r   = infos[i].radius;
        float     a   = glm::dot(rd, rd);
        float     bv  = 2.0f * glm::dot(oc, rd);
        float     cv  = glm::dot(oc, oc) - r * r;
        float     dis = bv * bv - 4.0f * a * cv;
        if (dis < 0.0f) continue;
        float t = (-bv - std::sqrt(dis)) / (2.0f * a);
        if (t < 0.0f) continue;
        if (t < bestT) { bestT = t; bestIdx = i; }
    }
    return bestIdx;
}

float Application::sampleHeightCPU(float latDeg, float lonDeg) const {
    if (m_HeightmapCPU.empty()) return 0.0f;
    constexpr float PI = glm::pi<float>();
    float u = (glm::radians(lonDeg) + PI) / (2.0f * PI);
    float v = glm::radians(latDeg)  / PI + 0.5f;
    u -= std::floor(u);
    v  = glm::clamp(v, 0.0f, 1.0f);
    int px = glm::clamp((int)(u * m_HeightmapCPU_W), 0, m_HeightmapCPU_W - 1);
    int py = glm::clamp((int)(v * m_HeightmapCPU_H), 0, m_HeightmapCPU_H - 1);
    return m_HeightmapCPU[py * m_HeightmapCPU_W + px] / 255.0f;
}

glm::vec3 Application::latLonToWorld(float latDeg, float lonDeg) const {
    float lat = glm::radians(latDeg);
    float lon = glm::radians(lonDeg);
    glm::vec3 n { std::cos(lat) * std::cos(lon),
                  std::sin(lat),
                 -std::cos(lat) * std::sin(lon) };
    if (m_HasHeightmap && m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        float h = sampleHeightCPU(latDeg, lonDeg);
        float s = (float)m_World->bodies[m_ActiveBodyIdx].height_scale;
        n *= (1.0f + h * s);
    }
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
