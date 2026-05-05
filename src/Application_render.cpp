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
    if (m_ViewMode == ViewMode::SolarSystem)
        renderSolarSystem();
    else
        renderPlanet();
}

void Application::renderPlanet() {
    if (m_ActiveBodyIdx != m_LastActiveBodyIdx) {
        m_LastActiveBodyIdx = m_ActiveBodyIdx;
        reloadBodyTexture();
        syncPoliticalRenderer();
    }

    glClearColor(0.04f, 0.04f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_Window.width(), m_Window.height());

    // Sum worst-case terrain displacement: global heightmap + all visible overlay heightmaps.
    // Used for both clip planes and camera minimum distance.
    float heightScale = 0.0f;
    float maxDisp     = 0.0f;
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& b = m_World->bodies[m_ActiveBodyIdx];
        heightScale   = b.height_scale;
        maxDisp       = heightScale;
        for (const auto& ov : b.overlays)
            if (ov.visible)
                maxDisp += ov.height_scale;
    }

    // Near plane tracks true camera-to-surface gap (above displaced terrain, not unit sphere).
    float trueAlt = m_Camera.distance() - 1.0f - maxDisp;
    float nearZ   = std::max(0.0001f, trueAlt * 0.1f);
    float farZ    = std::max(100.0f,  m_Camera.distance() * 100.0f);
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

    m_PlanetShader->setBool ("u_HasHeightmap",   m_HasHeightmap);
    m_PlanetShader->setFloat("u_HeightScale",    heightScale);
    m_PlanetShader->setFloat("u_HeightmapWidth", static_cast<float>(m_HeightmapWidth));
    if (m_HasHeightmap) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_HeightmapId);
        m_PlanetShader->setInt("u_Heightmap", 1);
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

        // Overlay heightmaps — units 6-9 (additive displacement)
        float ovHmScale[4] = {};
        if (m_World && m_ActiveBodyIdx >= 0 &&
            m_ActiveBodyIdx < (int)m_World->bodies.size()) {
            int slot = 0;
            for (const auto& ov : m_World->bodies[m_ActiveBodyIdx].overlays) {
                if (!ov.visible || slot >= 4) continue;
                ovHmScale[slot++] = ov.height_scale;
            }
        }
        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE6 + i);
            glBindTexture(GL_TEXTURE_2D, m_OvHeightmapIds[i]);
        }
        static const int ovHmSamplers[4] = {6, 7, 8, 9};
        m_PlanetShader->setInt1v  ("u_OvHeightmap", 4, ovHmSamplers);
        m_PlanetShader->setFloat1v("u_OvHmScale",   4, ovHmScale);
    }

    // ── Political map: view-dependent bake + bind ─────────────────────────────
    m_PlanetShader->setFloat("u_PoliticalLOD", 0.0f);
    if (m_ShowPoliticalMap) {
        // Trigger rebake when camera moves > 3 deg or > 5% distance change
        glm::vec3 camPos  = m_Camera.position();
        float     lenCam  = glm::length(camPos);
        float     lenLast = glm::length(m_LastBakeCamPos);
        bool moved = false;
        if (lenCam > 0.0001f && lenLast > 0.0001f) {
            float cosA = glm::dot(glm::normalize(camPos), glm::normalize(m_LastBakeCamPos));
            if (cosA < 0.9986f) moved = true;
        }
        if (glm::abs(lenCam - lenLast) > lenCam * 0.05f) moved = true;
        if (moved || m_LastBakeCamPos == glm::vec3(0.0f)) {
            m_PolMap.markDirty();
            m_LastBakeCamPos = camPos;
        }
        if (m_PolMap.isDirty())
            rebakePoliticalMapTex();

        glActiveTexture(GL_TEXTURE10);
        glBindTexture(GL_TEXTURE_2D, m_PolMap.texId());
        m_PlanetShader->setInt ("u_PoliticalMap",    10);
        m_PlanetShader->setBool("u_HasPoliticalMap", true);
    } else {
        m_PlanetShader->setBool("u_HasPoliticalMap", false);
    }

    m_QuadSphere->draw(*m_PlanetShader);
    m_PlanetShader->unbind();

    // Draw Goldberg cell overlay for the active paint level
    if (m_ShowPoliticalMap && m_PolMap.hasGrid(m_PaintLevel))
        m_PolMap.draw(m_PaintLevel, *m_GoldbergShader, vp, model);
}

void Application::renderSolarSystem() {
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_Window.width(), m_Window.height());

    if (!m_World || m_World->bodies.empty()) return;

    auto      infos = computeSolarPositions();
    glm::mat4 vp    = m_SolarCam.projectionMatrix(m_Window.aspect())
                    * m_SolarCam.viewMatrix();

    m_SphereShader->bind();
    m_SphereShader->setMat4("u_VP", vp);
    m_SphereShader->setBool("u_HasTexture", false);

    for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
        const auto& b   = m_World->bodies[i];
        const auto& inf = infos[i];

        glm::vec3 col;
        switch (b.type) {
            case BodyType::Star:   col = {1.0f, 0.85f, 0.30f}; break;
            case BodyType::Moon:   col = {0.55f, 0.55f, 0.55f}; break;
            default:               col = {0.20f, 0.45f, 0.80f}; break;
        }
        if (i == m_HoverBodyIdx)
            col = glm::mix(col, glm::vec3(1.0f), 0.35f);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), inf.pos)
                        * glm::scale(glm::mat4(1.0f), glm::vec3(inf.radius));

        m_SphereShader->setMat4("u_Model",    model);
        m_SphereShader->setVec3("u_BaseColor", col);
        m_Sphere->draw();
    }

    m_SphereShader->unbind();
}

// ── Solar system positions ────────────────────────────────────────────────────

std::vector<SolarBodyInfo> Application::computeSolarPositions() const {
    if (!m_World) return {};
    const auto& bodies = m_World->bodies;
    const int   n      = (int)bodies.size();

    std::vector<SolarBodyInfo> result(n);

    std::unordered_map<std::string, int> idxOf;
    for (int i = 0; i < n; ++i)
        if (!bodies[i].id.empty()) idxOf[bodies[i].id] = i;

    // Process in dependency order: Stars (pass 0), Planets (pass 1), Moons (pass 2).
    auto passOf = [](BodyType t) -> int {
        switch (t) {
            case BodyType::Star: return 0;
            case BodyType::Moon: return 2;
            default:             return 1;
        }
    };

    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < n; ++i) {
            if (passOf(bodies[i].type) != pass) continue;
            const auto& b = bodies[i];

            glm::vec3 parentPos(0.0f);
            int       parentIdx = -1;
            if (!b.parent_id.empty()) {
                auto it = idxOf.find(b.parent_id);
                if (it != idxOf.end()) {
                    int       candidate   = it->second;
                    BodyType  parentType  = bodies[candidate].type;
                    bool valid = (b.type == BodyType::Planet && parentType == BodyType::Star)
                              || (b.type == BodyType::Moon   && parentType == BodyType::Planet);
                    if (valid) {
                        parentIdx = candidate;
                        parentPos = result[parentIdx].pos;
                    }
                }
            }
            if (parentIdx < 0 && b.type == BodyType::Planet) {
                for (int j = 0; j < n; ++j) {
                    if (bodies[j].type == BodyType::Star) { parentPos = result[j].pos; break; }
                }
            }
            if (parentIdx < 0 && b.type == BodyType::Moon) {
                for (int j = 0; j < n; ++j) {
                    if (bodies[j].type == BodyType::Planet) { parentPos = result[j].pos; break; }
                }
            }

            int siblingIdx  = 0;
            int numSiblings = 0;
            for (int j = 0; j < n; ++j) {
                if (bodies[j].parent_id == b.parent_id && passOf(bodies[j].type) == pass) {
                    if (j < i) ++siblingIdx;
                    ++numSiblings;
                }
            }

            float orbitR, radius;

            if (m_RealisticScale) {
                radius = std::clamp((float)(b.radius_km / 6371.0) * 0.07f, 0.02f, 0.8f);
                orbitR = (float)(b.orbital_radius_au * 10.0);
            } else {
                switch (b.type) {
                    case BodyType::Star:
                        radius = 0.25f;
                        orbitR = (numSiblings > 1) ? (float)siblingIdx * 5.0f : 0.0f;
                        break;
                    case BodyType::Moon:
                        radius = 0.03f;
                        orbitR = 0.18f + (float)siblingIdx * 0.12f;
                        break;
                    default:
                        radius = 0.07f;
                        orbitR = 3.0f + (float)siblingIdx * 2.5f;
                        break;
                }
            }

            float angle = (numSiblings > 1)
                ? (float)siblingIdx * glm::two_pi<float>() / (float)numSiblings
                : 0.0f;

            glm::vec3 pos = (b.type == BodyType::Star && orbitR < 0.0001f)
                ? parentPos
                : parentPos + glm::vec3(orbitR * std::cos(angle), 0.0f,
                                        orbitR * std::sin(angle));

            result[i] = { pos, radius, parentPos, orbitR };
        }
    }

    return result;
}

// ── Solar system overlay (orbital lines + labels) ─────────────────────────────

void Application::renderSolarSystemOverlay() {
    if (!m_World || m_World->bodies.empty()) return;

    auto      infos = computeSolarPositions();
    glm::mat4 vp    = m_SolarCam.projectionMatrix(m_Window.aspect())
                    * m_SolarCam.viewMatrix();

    ImDrawList* dl       = ImGui::GetBackgroundDrawList();
    ImU32       orbitCol = IM_COL32(70, 80, 130, 150);
    ImU32       labelCol = IM_COL32(220, 220, 220, 210);

    constexpr int N = 64;

    // Orbital ellipses
    for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
        const auto& inf = infos[i];
        if (inf.orbitRadius < 0.001f) continue;

        std::vector<ImVec2> pts;
        pts.reserve(N + 1);

        for (int k = 0; k <= N; ++k) {
            float     angle = (float)k * glm::two_pi<float>() / (float)N;
            glm::vec3 p     = inf.orbitCenter
                            + glm::vec3(inf.orbitRadius * std::cos(angle), 0.0f,
                                        inf.orbitRadius * std::sin(angle));
            glm::vec4 clip  = vp * glm::vec4(p, 1.0f);
            if (clip.w <= 0.01f) {
                if (pts.size() >= 2)
                    dl->AddPolyline(pts.data(), (int)pts.size(), orbitCol, 0, 1.0f);
                pts.clear();
                continue;
            }
            clip /= clip.w;
            pts.push_back({
                (clip.x * 0.5f + 0.5f) * (float)m_Window.width(),
                (1.0f - (clip.y * 0.5f + 0.5f)) * (float)m_Window.height()
            });
        }
        if (pts.size() >= 2)
            dl->AddPolyline(pts.data(), (int)pts.size(), orbitCol, 0, 1.0f);
    }

    // Body labels + hover ring
    float halfH = (float)m_Window.height() * 0.5f;
    float tanHalfFov = std::tan(glm::radians(22.5f));

    for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
        const auto& inf  = infos[i];
        glm::vec4   clip = vp * glm::vec4(inf.pos, 1.0f);
        if (clip.w <= 0.0f) continue;
        clip /= clip.w;
        float sx = (clip.x * 0.5f + 0.5f) * (float)m_Window.width();
        float sy = (1.0f - (clip.y * 0.5f + 0.5f)) * (float)m_Window.height();

        float screenR = std::max(inf.radius / (m_SolarCam.distance() * tanHalfFov) * halfH,
                                 4.0f);

        if (i == m_HoverBodyIdx)
            dl->AddCircle({sx, sy}, screenR + 4.0f, IM_COL32(255, 220, 80, 200), 0, 2.0f);

        dl->AddText({sx + screenR + 5.0f, sy - 7.0f}, labelCol,
                    m_World->bodies[i].name.c_str());
    }
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

    if (m_ViewMode == ViewMode::SolarSystem) {
        const char* scaleMode = m_RealisticScale ? "Scale: Realistic" : "Scale: Illustrative";
        ImVec2 sz = ImGui::CalcTextSize(scaleMode);
        dl->AddText({cx2 - sz.x - 10.0f, cy2 - sz.y - 10.0f},
                    IM_COL32(180, 180, 180, 180), scaleMode);
        return;
    }

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

    if (m_ShowPoliticalMap) {
        char lodStr[48];
        std::snprintf(lodStr, sizeof(lodStr), "Paint LOD %d", m_PaintLevel);
        dl->AddText({10.0f, 10.0f}, IM_COL32(255, 220, 60, 230), lodStr);
    }
}
