#include "Application.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ── 3-D scene ─────────────────────────────────────────────────────────────────

void Application::renderScene() {
    renderPlanet();
}

void Application::renderPlanet() {
    if (m_NeedsTextureReload) {
        m_NeedsTextureReload = false;
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

        if (m_World) {
            const auto& b = m_World->body;
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

// ── Labels (text only for named nodes) ───────────────────────────────────────

void Application::renderLabels() {
    if (!m_World) return;

    const auto& body   = m_World->body;
    glm::vec3   camDir = glm::normalize(m_Camera.position());
    ImDrawList* dl     = ImGui::GetBackgroundDrawList();

    for (const auto& n : body.network.nodes) {
        if (n.name.empty()) continue;
        if (n.born_day && m_CurrentDay < *n.born_day) continue;
        if (n.died_day && m_CurrentDay > *n.died_day) continue;
        glm::vec3 wp = latLonToWorld(n.lat_deg, n.lon_deg);
        if (glm::dot(wp, camDir) < 0.05f) continue;

        glm::vec2 sp = worldToScreen(wp);
        float r;
        switch (n.entity_type) {
            case EntityType::City: r = 6.0f; break;
            case EntityType::Town: r = 4.5f; break;
            default:               r = 3.5f; break;
        }

        constexpr float kLabelSize = 14.0f;
        if (m_TextRenderer.ready()) {
            m_TextRenderer.drawText(n.name.c_str(),
                sp.x + r + 4.0f,
                sp.y - m_TextRenderer.ascent(kLabelSize) * 0.5f,
                kLabelSize,
                { 1.f, 1.f, 1.f, 0.85f });
        } else {
            dl->AddText({sp.x + r + 4.0f, sp.y - 7.0f},
                        IM_COL32(255, 255, 255, 210), n.name.c_str());
        }
    }
}

// ── Network (nodes + edges) ───────────────────────────────────────────────────

void Application::renderRoads() {
    if (!m_World) return;

    ImDrawList*   dl     = ImGui::GetBackgroundDrawList();
    glm::vec3     camDir = glm::normalize(m_Camera.position());
    constexpr int kSeg   = 24;
    const auto&   net    = m_World->body.network;

    auto slerp3 = [](glm::vec3 a, glm::vec3 b, float t) -> glm::vec3 {
        float len = glm::length(a);
        if (len < 1e-7f) return a;
        glm::vec3 an = a / len;
        glm::vec3 bn = b / std::max(glm::length(b), 1e-7f);
        float d = glm::clamp(glm::dot(an, bn), -1.0f, 1.0f);
        float omega = std::acos(d);
        if (omega < 1e-5f) return glm::mix(a, b, t);
        float so = std::sin(omega);
        return len * (std::sin((1.0f - t) * omega) / so * an +
                      std::sin(t           * omega) / so * bn);
    };

    // Resolve party route color for selected party
    ImU32 partyRouteCol = IM_COL32(200, 80, 255, 230);
    if (!m_SelectedPartyId.empty()) {
        for (const auto& p : m_World->parties) {
            if (p.id == m_SelectedPartyId) {
                partyRouteCol = IM_COL32((int)(p.color[0]*255),
                                         (int)(p.color[1]*255),
                                         (int)(p.color[2]*255), 230);
                break;
            }
        }
    }

    // Draw edges
    for (const auto& edge : net.edges) {
        if (edge.type == RouteType::Road && !m_ShowRoads) continue;
        if (edge.type == RouteType::Sea  && !m_ShowSea)  continue;

        const MapNode* na = net.findNode(edge.from_id);
        const MapNode* nb = net.findNode(edge.to_id);
        if (!na || !nb) continue;

        bool  isSel        = (edge.id == m_SelectedEdgeId);
        bool  isRoute      = m_RouteMode && std::find(
                                 m_RouteEdgeIds.begin(), m_RouteEdgeIds.end(),
                                 edge.id) != m_RouteEdgeIds.end();
        bool  isPartyRoute = m_PartyTravelMode && std::find(
                                 m_PartyRouteEdgeIds.begin(), m_PartyRouteEdgeIds.end(),
                                 edge.id) != m_PartyRouteEdgeIds.end();
        bool  isHover      = (edge.id == m_HoverEdgeId);
        ImU32 col = isSel        ? IM_COL32(255, 255, 255, 230)
                  : isRoute      ? IM_COL32( 80, 255, 120, 230)
                  : isPartyRoute ? partyRouteCol
                  : isHover      ? ((edge.type == RouteType::Road) ? IM_COL32(255, 190,  80, 255)
                                                                   : IM_COL32(120, 220, 255, 255))
                  : (edge.type == RouteType::Road) ? IM_COL32(255, 160,  60, 200)
                                                   : IM_COL32( 80, 200, 255, 200);
        float lineW = (isSel || isRoute || isPartyRoute) ? 3.0f : isHover ? 2.5f : 1.5f;

        glm::vec3 pa = latLonToWorld(na->lat_deg, na->lon_deg);
        glm::vec3 pb = latLonToWorld(nb->lat_deg, nb->lon_deg);
        glm::vec3 prev    = pa;
        bool      prevVis = glm::dot(glm::normalize(pa), camDir) > 0.05f;

        for (int i = 1; i <= kSeg; ++i) {
            float     t      = (float)i / kSeg;
            glm::vec3 cur    = slerp3(pa, pb, t);
            bool      curVis = glm::dot(glm::normalize(cur), camDir) > 0.05f;
            if (prevVis && curVis) {
                glm::vec2 s0 = worldToScreen(prev);
                glm::vec2 s1 = worldToScreen(cur);
                dl->AddLine({s0.x, s0.y}, {s1.x, s1.y}, col, lineW);
            }
            prev    = cur;
            prevVis = curVis;
        }

        // Distance label at arc midpoint for selected / hovered / route edges
        if (isSel || isHover || isRoute || isPartyRoute) {
            glm::vec3 pm = slerp3(pa, pb, 0.5f);
            if (glm::dot(glm::normalize(pm), camDir) > 0.05f) {
                glm::vec2 sm = worldToScreen(pm);
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f km", edge.distance_km);
                ImVec2 tsz = ImGui::CalcTextSize(label);
                dl->AddRectFilled(
                    {sm.x - tsz.x * 0.5f - 3.0f, sm.y - tsz.y * 0.5f - 2.0f},
                    {sm.x + tsz.x * 0.5f + 3.0f, sm.y + tsz.y * 0.5f + 2.0f},
                    IM_COL32(0, 0, 0, 160), 3.0f);
                dl->AddText(
                    {sm.x - tsz.x * 0.5f, sm.y - tsz.y * 0.5f},
                    IM_COL32(255, 255, 255, 220), label);
            }
        }
    }

    // Draw nodes
    for (const auto& n : net.nodes) {
        glm::vec3 wp = latLonToWorld(n.lat_deg, n.lon_deg);
        if (glm::dot(glm::normalize(wp), camDir) < 0.05f) continue;
        glm::vec2 sp = worldToScreen(wp);

        bool isSel   = (n.id == m_SelectedNodeId);
        bool isFrom  = (n.id == m_NetworkConnectFrom);
        bool isHover = (n.id == m_HoverNodeId);

        ImU32 col;
        float r;
        if (n.name.empty()) {
            col = IM_COL32(180, 180, 180, 200);
            r   = 3.0f;
        } else {
            switch (n.entity_type) {
                case EntityType::City: col = IM_COL32(255, 200,  60, 255); r = 6.0f; break;
                case EntityType::Town: col = IM_COL32(140, 200, 255, 255); r = 4.5f; break;
                default:               col = IM_COL32(140, 255, 160, 255); r = 3.5f; break;
            }
        }

        if (isSel) r = std::max(r, 5.0f);
        dl->AddCircleFilled({sp.x, sp.y}, r, col);
        if (isHover && !isSel && !isFrom)
            dl->AddCircle({sp.x, sp.y}, r + 3.0f, IM_COL32(255, 255, 255, 130), 0, 1.5f);
        if (isSel || isFrom)
            dl->AddCircle({sp.x, sp.y}, r + 3.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
    }

    // Parties — trail + current-day marker
    {
        // Resolve node-snapped waypoint position
        auto wptPos = [&](const TravelWaypoint& wp) -> std::pair<float,float> {
            if (!wp.node_id.empty()) {
                const MapNode* n = net.findNode(wp.node_id);
                if (n) return {n->lat_deg, n->lon_deg};
            }
            return {wp.lat_deg, wp.lon_deg};
        };

        for (const auto& p : m_World->parties) {
            if (!p.visible || p.waypoints.empty()) continue;

            ImU32 pCol = IM_COL32((int)(p.color[0]*255),
                                   (int)(p.color[1]*255),
                                   (int)(p.color[2]*255), 255);

            // Count past segments for fade calculation
            int pastSegs = 0;
            for (int i = 0; i + 1 < (int)p.waypoints.size(); ++i)
                if (p.waypoints[i].day <= m_CurrentDay) ++pastSegs;

            // Draw trail arcs
            int segIdx = 0;
            for (int i = 0; i + 1 < (int)p.waypoints.size(); ++i) {
                if (p.waypoints[i].day > m_CurrentDay) break;

                auto [la, loa] = wptPos(p.waypoints[i]);
                auto [lb, lob] = wptPos(p.waypoints[i+1]);

                // Clamp last segment to current day position
                if (p.waypoints[i+1].day > m_CurrentDay) {
                    int span = p.waypoints[i+1].day - p.waypoints[i].day;
                    float t = span > 0 ? (float)(m_CurrentDay - p.waypoints[i].day) / span : 1.f;
                    lb = la + t * (lb - la);
                    lob = loa + t * (lob - loa);
                }

                float fadeT = (pastSegs > 1) ? (float)(segIdx + 1) / pastSegs : 1.f;
                int   alpha = (int)(55 + fadeT * 185);
                ImU32 trailCol = IM_COL32((int)(p.color[0]*255),
                                           (int)(p.color[1]*255),
                                           (int)(p.color[2]*255), alpha);

                glm::vec3 pa   = latLonToWorld(la, loa);
                glm::vec3 pb   = latLonToWorld(lb, lob);
                glm::vec3 prev = pa;
                bool prevVis   = glm::dot(glm::normalize(pa), camDir) > 0.05f;
                for (int s = 1; s <= kSeg; ++s) {
                    glm::vec3 cur  = slerp3(pa, pb, (float)s / kSeg);
                    bool curVis    = glm::dot(glm::normalize(cur), camDir) > 0.05f;
                    if (prevVis && curVis) {
                        glm::vec2 s0 = worldToScreen(prev);
                        glm::vec2 s1 = worldToScreen(cur);
                        dl->AddLine({s0.x, s0.y}, {s1.x, s1.y}, trailCol, 2.5f);
                    }
                    prev = cur; prevVis = curVis;
                }
                ++segIdx;
            }

            // Compute position at current day
            float lat = -1000.f, lon = -1000.f;
            if (m_CurrentDay <= p.waypoints.front().day) {
                auto [l, lo] = wptPos(p.waypoints.front());
                lat = l; lon = lo;
            } else if (m_CurrentDay >= p.waypoints.back().day) {
                auto [l, lo] = wptPos(p.waypoints.back());
                lat = l; lon = lo;
            } else {
                for (int i = 0; i + 1 < (int)p.waypoints.size(); ++i) {
                    if (m_CurrentDay >= p.waypoints[i].day &&
                        m_CurrentDay <= p.waypoints[i+1].day) {
                        int   span = p.waypoints[i+1].day - p.waypoints[i].day;
                        float t    = span > 0
                            ? (float)(m_CurrentDay - p.waypoints[i].day) / span : 0.f;
                        auto [la, loa] = wptPos(p.waypoints[i]);
                        auto [lb, lob] = wptPos(p.waypoints[i+1]);
                        lat = la + t * (lb - la);
                        lon = loa + t * (lob - loa);
                        break;
                    }
                }
            }

            if (lat > -999.f) {
                glm::vec3 wp3 = latLonToWorld(lat, lon);
                if (glm::dot(glm::normalize(wp3), camDir) > 0.05f) {
                    glm::vec2 sp = worldToScreen(wp3);
                    dl->AddCircleFilled({sp.x, sp.y}, 8.f, pCol);
                    bool isSel = (p.id == m_SelectedPartyId);
                    dl->AddCircle({sp.x, sp.y}, 11.f,
                        IM_COL32(255, 255, 255, isSel ? 220 : 150), 0, 2.f);
                }
            }
        }

        // Travel destination ring for selected party
        if (m_PartyTravelMode && !m_PartyTravelDest.empty()) {
            const MapNode* dn = net.findNode(m_PartyTravelDest);
            if (dn) {
                glm::vec3 wp = latLonToWorld(dn->lat_deg, dn->lon_deg);
                if (glm::dot(glm::normalize(wp), camDir) > 0.05f) {
                    glm::vec2 sp = worldToScreen(wp);
                    dl->AddCircle({sp.x, sp.y}, 11.f, partyRouteCol, 0, 2.f);
                }
            }
        }
    }

    // Measure: committed segments + waypoint dots + preview arc
    if (m_EditMode == EditMode::Measure && !m_MeasurePath.empty()) {
        constexpr ImU32 kMeasCol     = IM_COL32(255, 255, 100, 200);
        constexpr ImU32 kMeasDotCol  = IM_COL32(255, 255, 100, 230);
        constexpr ImU32 kPreviewCol  = IM_COL32(255, 255, 100, 120);

        auto drawArc = [&](glm::vec3 pa, glm::vec3 pb, ImU32 col, float thick) {
            glm::vec3 prev    = pa;
            bool      prevVis = glm::dot(glm::normalize(pa), camDir) > 0.05f;
            for (int i = 1; i <= kSeg; ++i) {
                float     t      = (float)i / kSeg;
                glm::vec3 cur    = slerp3(pa, pb, t);
                bool      curVis = glm::dot(glm::normalize(cur), camDir) > 0.05f;
                if (prevVis && curVis) {
                    glm::vec2 s0 = worldToScreen(prev);
                    glm::vec2 s1 = worldToScreen(cur);
                    dl->AddLine({s0.x, s0.y}, {s1.x, s1.y}, col, thick);
                }
                prev    = cur;
                prevVis = curVis;
            }
        };

        // Committed segments
        for (int i = 1; i < (int)m_MeasurePath.size(); ++i) {
            glm::vec3 pa = latLonToWorld(m_MeasurePath[i-1].x, m_MeasurePath[i-1].y);
            glm::vec3 pb = latLonToWorld(m_MeasurePath[i  ].x, m_MeasurePath[i  ].y);
            drawArc(pa, pb, kMeasCol, 1.5f);
        }

        // Waypoint dots
        for (const auto& pt : m_MeasurePath) {
            glm::vec3 wp = latLonToWorld(pt.x, pt.y);
            if (glm::dot(glm::normalize(wp), camDir) > 0.05f) {
                glm::vec2 sp = worldToScreen(wp);
                dl->AddCircleFilled({sp.x, sp.y}, 4.0f, kMeasDotCol);
            }
        }

        // Preview arc from last point to cursor (only when still appending)
        if (!m_MeasureFinished && m_HoverLat > -999.0f) {
            glm::vec3 pa = latLonToWorld(m_MeasurePath.back().x, m_MeasurePath.back().y);
            glm::vec3 pb = latLonToWorld(m_HoverLat, m_HoverLon);
            drawArc(pa, pb, kPreviewCol, 1.5f);
        }

        // Insertion indicator: hollow ring on the closest hovered segment
        if ((int)m_MeasurePath.size() >= 2 && m_HoverLat > -999.0f) {
            constexpr float kInsertThresh = 12.0f;
            constexpr float kEndpointDead = 8.0f;
            ImVec2    imMpos = ImGui::GetMousePos();
            glm::vec2 cursor(imMpos.x, imMpos.y);
            glm::vec2 insertPt  = {};
            float     bestDist  = kInsertThresh;
            bool      found     = false;

            for (int i = 0; i < (int)m_MeasurePath.size() - 1; ++i) {
                glm::vec3 wa = latLonToWorld(m_MeasurePath[i  ].x, m_MeasurePath[i  ].y);
                glm::vec3 wb = latLonToWorld(m_MeasurePath[i+1].x, m_MeasurePath[i+1].y);
                if (glm::dot(glm::normalize(wa), camDir) < 0.05f) continue;
                if (glm::dot(glm::normalize(wb), camDir) < 0.05f) continue;
                glm::vec2 sa = worldToScreen(wa);
                glm::vec2 sb = worldToScreen(wb);

                glm::vec2 ab   = sb - sa;
                float     len2 = glm::dot(ab, ab);
                float     t    = (len2 > 1e-6f)
                    ? glm::clamp(glm::dot(cursor - sa, ab) / len2, 0.0f, 1.0f)
                    : 0.0f;
                glm::vec2 closest = sa + t * ab;
                float     dist    = glm::length(cursor - closest);

                float dA = glm::length(cursor - sa);
                float dB = glm::length(cursor - sb);
                if (dA < kEndpointDead || dB < kEndpointDead) continue;

                if (dist < bestDist) { bestDist = dist; insertPt = closest; found = true; }
            }

            if (found)
                dl->AddCircle({insertPt.x, insertPt.y}, 6.0f,
                              IM_COL32(255, 255, 100, 220), 0, 1.5f);
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
    if (m_World)
        radius_km = (float)m_World->body.radius_km;

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
