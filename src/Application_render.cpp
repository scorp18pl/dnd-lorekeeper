#include "Application.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

// ── 3-D scene ─────────────────────────────────────────────────────────────────

void Application::renderScene() {
    renderPlanet();
}

void Application::renderPlanet() {
    if (m_ActiveBodyIdx != m_LastActiveBodyIdx) {
        m_LastActiveBodyIdx = m_ActiveBodyIdx;
        reloadBodyTexture();
    }

    glClearColor(0.04f, 0.04f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_Window.width(), m_Window.height());

    float nearZ = std::max(0.0001f, (m_Camera.distance() - 1.0f) * 0.1f);
    float farZ  = std::max(100.0f,  m_Camera.distance() * 100.0f);
    glm::mat4 proj = glm::perspective(m_Camera.fov(), m_Window.aspect(), nearZ, farZ);
    glm::mat4 vp    = proj * m_Camera.viewMatrix();
    glm::mat4 model(1.0f);

    // Rebuild LOD patches for this frame
    m_QuadSphere->update(m_Camera.position());

    m_PlanetShader->bind();
    m_PlanetShader->setMat4("u_VP",         vp);
    m_PlanetShader->setMat4("u_Model",      model);
    m_PlanetShader->setBool("u_HasTexture", m_HasTexture);
    m_PlanetShader->setVec3("u_BaseColor",  {0.15f, 0.35f, 0.65f});

    if (m_HasTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_TextureId);
        m_PlanetShader->setInt("u_Texture", 0);
    }

    // Overlay uniforms
    {
        int   ovCount        = 0;
        float ovCenterLat[4] = {}, ovCenterLon[4] = {};
        float ovExtentKm[4]  = {}, ovOpacity[4]   = {};
        float radiusKm       = 6371.0f;

        if (m_World && m_ActiveBodyIdx >= 0 &&
            m_ActiveBodyIdx < (int)m_World->bodies.size()) {
            const auto& b = m_World->bodies[m_ActiveBodyIdx];
            radiusKm = (float)b.radius_km;
            for (const auto& ov : b.overlays) {
                if (!ov.visible || ovCount >= 4) continue;
                ovCenterLat[ovCount] = ov.center_lat;
                ovCenterLon[ovCount] = ov.center_lon;
                ovExtentKm [ovCount] = ov.extent_km;
                ovOpacity  [ovCount] = ov.opacity;
                ++ovCount;
            }
        }

        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE2 + i);
            glBindTexture(GL_TEXTURE_2D, m_OverlayTexIds[i]);
        }
        static const int ovSamplers[4] = {2, 3, 4, 5};
        m_PlanetShader->setInt1v  ("u_OvTex",        4, ovSamplers);
        m_PlanetShader->setInt    ("u_OvCount",       ovCount);
        m_PlanetShader->setFloat1v("u_OvCenterLat",   4, ovCenterLat);
        m_PlanetShader->setFloat1v("u_OvCenterLon",   4, ovCenterLon);
        m_PlanetShader->setFloat1v("u_OvExtentKm",    4, ovExtentKm);
        m_PlanetShader->setFloat1v("u_OvOpacity",     4, ovOpacity);
        m_PlanetShader->setFloat  ("u_PlanetRadiusKm", radiusKm);
    }

    m_QuadSphere->draw(*m_PlanetShader);
    m_PlanetShader->unbind();
}

// ── Labels (screen-projected entity markers) ──────────────────────────────────

void Application::renderLabels() {
    if (!m_World || m_ActiveBodyIdx < 0 ||
        m_ActiveBodyIdx >= (int)m_World->bodies.size()) return;

    const auto& body   = m_World->bodies[m_ActiveBodyIdx];
    glm::vec3   camDir = glm::normalize(m_Camera.position());
    ImDrawList* dl     = ImGui::GetBackgroundDrawList();

    for (const auto& e : body.entities) {
        glm::vec3 wp = latLonToWorld(e.lat_deg, e.lon_deg);
        if (glm::dot(wp, camDir) < 0.05f) continue;

        glm::vec2 sp = worldToScreen(wp);

        ImU32 fillColor;
        float r;
        switch (e.type) {
            case EntityType::City: fillColor = IM_COL32(255, 200,  60, 255); r = 6.0f; break;
            case EntityType::Town: fillColor = IM_COL32(140, 200, 255, 255); r = 4.5f; break;
            default:               fillColor = IM_COL32(140, 255, 160, 255); r = 3.5f; break;
        }

        if (e.id == m_SelectedEntityId)
            dl->AddCircle({sp.x, sp.y}, r + 3.0f,
                          IM_COL32(255, 255, 255, 220), 0, 2.0f);

        dl->AddCircleFilled({sp.x, sp.y}, r, fillColor);
        if (m_TextRenderer.ready()) {
            constexpr float kLabelSize = 14.0f;
            m_TextRenderer.drawText(e.name.c_str(),
                sp.x + r + 4.0f,
                sp.y - m_TextRenderer.ascent(kLabelSize) * 0.5f,
                kLabelSize,
                { 1.f, 1.f, 1.f, 0.85f });
        } else {
            dl->AddText({sp.x + r + 4.0f, sp.y - 7.0f},
                        IM_COL32(255, 255, 255, 210), e.name.c_str());
        }
    }
}

// ── HUD (lat/lon + scale bar) ─────────────────────────────────────────────────

void Application::renderHUD() {
    ImGuiDockNode* central = (m_DockId != 0)
        ? ImGui::DockBuilderGetCentralNode(m_DockId) : nullptr;

    float cx2 = central ? central->Pos.x + central->Size.x : (float)m_Window.width();
    float cy2 = central ? central->Pos.y + central->Size.y : (float)m_Window.height();
    float cH  = central ? central->Size.y                  : (float)m_Window.height();

    ImDrawList* dl  = ImGui::GetForegroundDrawList();
    ImU32       col = IM_COL32(210, 210, 210, 200);

    float radius_km = 6371.0f;
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size())
        radius_km = (float)m_World->bodies[m_ActiveBodyIdx].radius_km;

    float km_per_px = (2.0f * radius_km *
                       std::tan(m_Camera.fov() * 0.5f) *
                       (m_Camera.distance() - 1.0f)) / cH;

    static const float niceKm[] = {
        1, 2, 5, 10, 20, 50, 100, 200, 500,
        1000, 2000, 5000, 10000, 20000
    };
    float barKm = niceKm[0];
    for (float v : niceKm) {
        if (v / km_per_px <= 120.0f) barKm = v;
        else break;
    }
    float barPx = barKm / km_per_px;

    const float margin = 10.0f;
    const float textH  = ImGui::GetTextLineHeight();
    const float tickH  = 4.0f;
    const float rowGap = 8.0f;

    char coord[80];
    if (m_HoverLat > -999.0f)
        std::snprintf(coord, sizeof(coord),
                      "%.2f\xc2\xb0 %c   %.2f\xc2\xb0 %c",
                      std::abs(m_HoverLat), m_HoverLat >= 0 ? 'N' : 'S',
                      std::abs(m_HoverLon), m_HoverLon >= 0 ? 'E' : 'W');
    else
        std::snprintf(coord, sizeof(coord), "-- --");

    ImVec2 csz    = ImGui::CalcTextSize(coord);
    float  coordY = cy2 - margin - textH;
    dl->AddText({cx2 - csz.x - margin, coordY}, col, coord);

    float barY  = coordY - rowGap - tickH;
    float barX2 = cx2 - margin;
    float barX1 = barX2 - barPx;
    dl->AddLine({barX1, barY},          {barX2, barY},          col, 2.0f);
    dl->AddLine({barX1, barY - tickH},  {barX1, barY + tickH},  col, 2.0f);
    dl->AddLine({barX2, barY - tickH},  {barX2, barY + tickH},  col, 2.0f);

    char scaleLabel[32];
    if (barKm >= 1000.0f)
        std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f,000 km", barKm / 1000.0f);
    else
        std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f km", barKm);

    ImVec2 lsz    = ImGui::CalcTextSize(scaleLabel);
    float  labelY = barY - tickH - rowGap - textH;
    dl->AddText({barX1 + (barPx - lsz.x) * 0.5f, labelY}, col, scaleLabel);
}
