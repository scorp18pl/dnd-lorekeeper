#include "Application.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <tinyfiledialogs.h>

#include "io/WorldSerializer.h"
#include "import/MapImporter.h"
#include "command/PlaceEntityCommand.h"
#include "command/DeleteEntityCommand.h"
#include "command/MoveEntityCommand.h"
#include "command/AddRoadEdgeCommand.h"
#include "command/SplitEdgeCommand.h"
#include "command/DeleteEdgeCommand.h"
#include "command/ChangeEdgeTypeCommand.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

// ── ImGui UI ──────────────────────────────────────────────────────────────────

void Application::renderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();

    // ── Planet hover ray cast ─────────────────────────────────────────────────
    m_HoverLat = m_HoverLon = -1000.0f;
    if (!io.WantCaptureMouse) {
        ImVec2 pos = ImGui::GetMousePos();
        if (auto hit = castRay(pos.x, pos.y)) {
            m_HoverLat = hit->x;
            m_HoverLon = hit->y;
        }
    }

    // ── Hover node / edge highlight ───────────────────────────────────────────
    m_HoverNodeId.clear();
    m_HoverEdgeId.clear();
    if (!io.WantCaptureMouse && m_World && m_HoverLat > -999.0f) {
        ImVec2    mpos   = ImGui::GetMousePos();
        glm::vec2 cursor(mpos.x, mpos.y);
        auto&     net    = m_World->body.network;
        auto      camDir = glm::normalize(m_Camera.position());

        float bestNode = 14.0f;
        for (const auto& n : net.nodes) {
            glm::vec3 wp = latLonToWorld(n.lat_deg, n.lon_deg);
            if (glm::dot(glm::normalize(wp), camDir) < 0.05f) continue;
            glm::vec2 sp = worldToScreen(wp);
            float d = glm::length(sp - cursor);
            if (d < bestNode) { bestNode = d; m_HoverNodeId = n.id; }
        }

        if (m_HoverNodeId.empty()) {
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
                              std::sin(t * omega)           / so * bn);
            };
            constexpr int kSeg = 24;
            float bestEdge = 12.0f;
            for (const auto& edge : net.edges) {
                if (edge.type == RouteType::Road && !m_ShowRoads) continue;
                if (edge.type == RouteType::Sea  && !m_ShowSea)   continue;
                const MapNode* na = net.findNode(edge.from_id);
                const MapNode* nb = net.findNode(edge.to_id);
                if (!na || !nb) continue;
                glm::vec3 pa      = latLonToWorld(na->lat_deg, na->lon_deg);
                glm::vec3 pb      = latLonToWorld(nb->lat_deg, nb->lon_deg);
                glm::vec3 prev    = pa;
                bool      prevVis = glm::dot(glm::normalize(pa), camDir) > 0.05f;
                for (int i = 1; i <= kSeg; ++i) {
                    glm::vec3 cur    = slerp3(pa, pb, (float)i / kSeg);
                    bool      curVis = glm::dot(glm::normalize(cur), camDir) > 0.05f;
                    if (prevVis && curVis) {
                        glm::vec2 s0   = worldToScreen(prev);
                        glm::vec2 s1   = worldToScreen(cur);
                        glm::vec2 ab   = s1 - s0;
                        float     len2 = glm::dot(ab, ab);
                        float     t2   = (len2 > 1e-6f)
                            ? glm::clamp(glm::dot(cursor - s0, ab) / len2, 0.0f, 1.0f)
                            : 0.0f;
                        float dist = glm::length(cursor - (s0 + t2 * ab));
                        if (dist < bestEdge) { bestEdge = dist; m_HoverEdgeId = edge.id; }
                    }
                    prev = cur; prevVis = curVis;
                }
            }
        }
    }

    // ── Globe click ───────────────────────────────────────────────────────────
    if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        m_HoverLat > -999.0f && m_World) {

        auto& net    = m_World->body.network;
        auto  camDir = glm::normalize(m_Camera.position());
        ImVec2 mpos  = ImGui::GetMousePos();

        // Helper: find nearest visible node within screen-px threshold
        auto nearestNode = [&](float threshold) -> std::string {
            float best = threshold;
            std::string id;
            for (const auto& n : net.nodes) {
                glm::vec3 wp = latLonToWorld(n.lat_deg, n.lon_deg);
                if (glm::dot(glm::normalize(wp), camDir) < 0.05f) continue;
                glm::vec2 sp = worldToScreen(wp);
                float d = glm::length(sp - glm::vec2(mpos.x, mpos.y));
                if (d < best) { best = d; id = n.id; }
            }
            return id;
        };

        if (m_EditMode == EditMode::Place) {
            MapNode n;
            for (int i = 1; ; ++i) {
                n.id = "mn_" + std::to_string(i);
                if (!net.findNode(n.id)) break;
            }
            n.name        = (m_PlaceType == EntityType::City) ? "New City" :
                            (m_PlaceType == EntityType::Town) ? "New Town" : "New POI";
            n.entity_type = m_PlaceType;
            n.lat_deg     = m_HoverLat;
            n.lon_deg     = m_HoverLon;
            m_CommandStack.execute(std::make_unique<PlaceNodeCommand>(net.nodes, n));
            m_SelectedNodeId = n.id;
            m_SelectedOverlayId.clear();
            WorldSerializer::save(*m_World);
            m_EditMode = EditMode::Navigate;

        } else if (m_EditMode == EditMode::Navigate) {
            std::string hit = nearestNode(14.0f);
            auto findParty = [&]() -> TravelRecord* {
                for (auto& p : m_World->parties)
                    if (p.id == m_SelectedPartyId) return &p;
                return nullptr;
            };

            if (!hit.empty() && m_PartyPlaceMode) {
                if (TravelRecord* p = findParty()) {
                    const MapNode* n = net.findNode(hit);
                    TravelWaypoint wp;
                    wp.node_id = hit;
                    wp.day     = m_CurrentDay;
                    if (n) { wp.lat_deg = n->lat_deg; wp.lon_deg = n->lon_deg; }
                    p->waypoints.push_back(wp);
                    std::sort(p->waypoints.begin(), p->waypoints.end(),
                              [](const auto& a, const auto& b){ return a.day < b.day; });
                    m_PartyPlaceMode = false;
                    WorldSerializer::save(*m_World);
                }
            } else if (!hit.empty() && m_PartyTravelMode) {
                if (TravelRecord* p = findParty()) {
                    if (!p->waypoints.empty()) {
                        const std::string& fromId = p->waypoints.back().node_id;
                        if (!fromId.empty() && hit != fromId) {
                            m_PartyTravelDest = hit;
                            m_PartyRouteKm = net.shortestPathEdges(
                                fromId, hit, m_PartyRouteEdgeIds);
                        }
                    }
                }
            } else if (!hit.empty() && m_RouteMode) {
                // Route tool: first click = From, second = To, third resets From
                if (m_RouteFrom.empty() || (!m_RouteTo.empty())) {
                    m_RouteFrom = hit;
                    m_RouteTo.clear();
                    m_RouteEdgeIds.clear();
                    m_RouteKm = -1.f;
                } else {
                    m_RouteTo = hit;
                    std::optional<RouteType> tf;
                    if (m_RouteTypeFilter == 1) tf = RouteType::Road;
                    if (m_RouteTypeFilter == 2) tf = RouteType::Sea;
                    m_RouteKm = net.shortestPathEdges(m_RouteFrom, m_RouteTo,
                                                      m_RouteEdgeIds, tf);
                }
            } else if (!hit.empty()) {
                if (m_RelocateMode && hit != m_SelectedNodeId)
                    m_RelocateMode = false;
                m_SelectedNodeId   = hit;
                m_SelectedEdgeId.clear();
                m_SelectedOverlayId.clear();
                m_SelectedPartyId.clear();
            } else {
                // Check edge proximity → select edge
                constexpr float kEdgeSel  = 12.0f;
                constexpr int   kArcSeg   = 24;
                glm::vec2 cursor(mpos.x, mpos.y);
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
                                  std::sin(t * omega)           / so * bn);
                };
                std::string nearEdge;
                float bestDist = kEdgeSel;
                for (const auto& edge : net.edges) {
                    const MapNode* na = net.findNode(edge.from_id);
                    const MapNode* nb = net.findNode(edge.to_id);
                    if (!na || !nb) continue;
                    glm::vec3 pa = latLonToWorld(na->lat_deg, na->lon_deg);
                    glm::vec3 pb = latLonToWorld(nb->lat_deg, nb->lon_deg);
                    glm::vec3 prev = pa;
                    bool prevVis = glm::dot(glm::normalize(pa), camDir) > 0.05f;
                    for (int i = 1; i <= kArcSeg; ++i) {
                        glm::vec3 cur    = slerp3(pa, pb, (float)i / kArcSeg);
                        bool      curVis = glm::dot(glm::normalize(cur), camDir) > 0.05f;
                        if (prevVis && curVis) {
                            glm::vec2 s0 = worldToScreen(prev);
                            glm::vec2 s1 = worldToScreen(cur);
                            glm::vec2 ab = s1 - s0;
                            float len2 = glm::dot(ab, ab);
                            float t = (len2 > 1e-6f)
                                ? glm::clamp(glm::dot(cursor - s0, ab) / len2, 0.0f, 1.0f)
                                : 0.0f;
                            float dist = glm::length(cursor - (s0 + t * ab));
                            if (dist < bestDist) { bestDist = dist; nearEdge = edge.id; }
                        }
                        prev = cur; prevVis = curVis;
                    }
                }
                m_RelocateMode = false;
                m_SelectedNodeId.clear();
                m_SelectedEdgeId    = nearEdge;
                m_SelectedOverlayId.clear();
                m_SelectedPartyId.clear();
            }

        } else if (m_EditMode == EditMode::NetworkEdit && m_World) {
            std::string hit = nearestNode(16.0f);

            if (m_NetworkSubMode == NetworkSubMode::PlaceNode) {
                if (!hit.empty()) {
                    m_SelectedNodeId = hit;
                    m_SelectedOverlayId.clear();
                } else {
                    // Check screen-space proximity to any road arc → split edge
                    constexpr float kEdgeThresh = 12.0f;
                    constexpr int   kArcSeg     = 24;
                    glm::vec2 cursor(mpos.x, mpos.y);

                    auto slerp3 = [](glm::vec3 a, glm::vec3 b, float t) -> glm::vec3 {
                        float len = glm::length(a);
                        if (len < 1e-7f) return a;
                        glm::vec3 an = a / len;
                        glm::vec3 bn = b / std::max(glm::length(b), 1e-7f);
                        float d     = glm::clamp(glm::dot(an, bn), -1.0f, 1.0f);
                        float omega = std::acos(d);
                        if (omega < 1e-5f) return glm::mix(a, b, t);
                        float so = std::sin(omega);
                        return len * (std::sin((1.0f - t) * omega) / so * an +
                                      std::sin(t * omega)           / so * bn);
                    };

                    std::string splitEdgeId;
                    float       splitLat = m_HoverLat, splitLon = m_HoverLon;
                    float       bestDist = kEdgeThresh;

                    for (const auto& edge : net.edges) {
                        const MapNode* na = net.findNode(edge.from_id);
                        const MapNode* nb = net.findNode(edge.to_id);
                        if (!na || !nb) continue;

                        glm::vec3 pa      = latLonToWorld(na->lat_deg, na->lon_deg);
                        glm::vec3 pb      = latLonToWorld(nb->lat_deg, nb->lon_deg);
                        glm::vec3 prev    = pa;
                        bool      prevVis = glm::dot(glm::normalize(pa), camDir) > 0.05f;

                        for (int i = 1; i <= kArcSeg; ++i) {
                            float     t      = (float)i / kArcSeg;
                            glm::vec3 cur    = slerp3(pa, pb, t);
                            bool      curVis = glm::dot(glm::normalize(cur), camDir) > 0.05f;
                            if (prevVis && curVis) {
                                glm::vec2 s0   = worldToScreen(prev);
                                glm::vec2 s1   = worldToScreen(cur);
                                glm::vec2 ab   = s1 - s0;
                                float     len2 = glm::dot(ab, ab);
                                float     tt   = (len2 > 1e-6f)
                                    ? glm::clamp(glm::dot(cursor - s0, ab) / len2, 0.0f, 1.0f)
                                    : 0.0f;
                                float dist = glm::length(cursor - (s0 + tt * ab));
                                if (dist < bestDist) {
                                    bestDist    = dist;
                                    splitEdgeId = edge.id;
                                    float     gt = ((float)(i - 1) + tt) / kArcSeg;
                                    glm::vec3 wp = glm::normalize(slerp3(pa, pb, gt));
                                    splitLat = glm::degrees(std::asin(glm::clamp(wp.y, -1.0f, 1.0f)));
                                    splitLon = glm::degrees(std::atan2(-wp.z, wp.x));
                                }
                            }
                            prev    = cur;
                            prevVis = curVis;
                        }
                    }

                    // Generate unique node ID
                    MapNode n;
                    for (int i = 1; ; ++i) {
                        n.id = "mn_" + std::to_string(i);
                        if (!net.findNode(n.id)) break;
                    }
                    n.lat_deg = splitLat;
                    n.lon_deg = splitLon;

                    if (!splitEdgeId.empty()) {
                        // Find the edge to split (do it before SplitEdgeCommand mutates the graph)
                        const RouteEdge* orig = nullptr;
                        for (const auto& e : net.edges)
                            if (e.id == splitEdgeId) { orig = &e; break; }

                        if (orig) {
                            float radius = (float)m_World->body.radius_km;

                            // Generate two unique edge IDs
                            auto nextEdgeId = [&](int& ctr) -> std::string {
                                for (;; ++ctr) {
                                    std::string id = "re_" + std::to_string(ctr);
                                    bool used = false;
                                    for (const auto& e : net.edges)
                                        if (e.id == id) { used = true; break; }
                                    if (!used) return id;
                                }
                            };
                            int ctr = 1;

                            RouteEdge e1, e2;
                            e1.id      = nextEdgeId(ctr); ++ctr;
                            e1.from_id = orig->from_id;
                            e1.to_id   = n.id;
                            e1.type    = orig->type;
                            e2.id      = nextEdgeId(ctr);
                            e2.from_id = n.id;
                            e2.to_id   = orig->to_id;
                            e2.type    = orig->type;

                            const MapNode* na2 = net.findNode(e1.from_id);
                            const MapNode* nb2 = net.findNode(e2.to_id);
                            e1.distance_km = na2
                                ? greatCircleKm(na2->lat_deg, na2->lon_deg, n.lat_deg, n.lon_deg, radius)
                                : 0.f;
                            e2.distance_km = nb2
                                ? greatCircleKm(n.lat_deg, n.lon_deg, nb2->lat_deg, nb2->lon_deg, radius)
                                : 0.f;

                            m_CommandStack.execute(std::make_unique<SplitEdgeCommand>(
                                net, splitEdgeId, n, std::move(e1), std::move(e2)));
                        } else {
                            m_CommandStack.execute(std::make_unique<PlaceNodeCommand>(net.nodes, n));
                        }
                    } else {
                        m_CommandStack.execute(std::make_unique<PlaceNodeCommand>(net.nodes, n));
                    }

                    m_SelectedNodeId = n.id;
                    m_SelectedOverlayId.clear();
                    WorldSerializer::save(*m_World);
                }

            } else if (m_NetworkSubMode == NetworkSubMode::Connect && !hit.empty()) {
                if (m_NetworkConnectFrom.empty()) {
                    m_NetworkConnectFrom = hit;
                    m_SelectedNodeId     = hit;
                    m_SelectedOverlayId.clear();
                } else if (m_NetworkConnectFrom != hit) {
                    const MapNode* na = net.findNode(m_NetworkConnectFrom);
                    const MapNode* nb = net.findNode(hit);
                    if (na && nb) {
                        RouteEdge edge;
                        for (int i = 1; ; ++i) {
                            edge.id = "re_" + std::to_string(i);
                            bool used = false;
                            for (const auto& e : net.edges)
                                if (e.id == edge.id) { used = true; break; }
                            if (!used) break;
                        }
                        edge.from_id     = m_NetworkConnectFrom;
                        edge.to_id       = hit;
                        edge.distance_km = greatCircleKm(
                            na->lat_deg, na->lon_deg,
                            nb->lat_deg, nb->lon_deg,
                            (float)m_World->body.radius_km);
                        edge.type = m_RouteType;
                        m_CommandStack.execute(
                            std::make_unique<AddRouteEdgeCommand>(net.edges, edge));
                        WorldSerializer::save(*m_World);
                    }
                    m_NetworkConnectFrom = hit;
                    m_SelectedNodeId     = hit;
                    m_SelectedOverlayId.clear();
                }
            }

        } else if (m_EditMode == EditMode::Measure) {
            glm::vec2 cursor(mpos.x, mpos.y);

            // Click on an existing waypoint → start drag instead of placing
            int nearWpt = -1;
            {
                float best = 8.0f;
                for (int i = 0; i < (int)m_MeasurePath.size(); ++i) {
                    glm::vec3 wp = latLonToWorld(m_MeasurePath[i].x, m_MeasurePath[i].y);
                    if (glm::dot(glm::normalize(wp), camDir) < 0.05f) continue;
                    glm::vec2 sp = worldToScreen(wp);
                    float d = glm::length(sp - cursor);
                    if (d < best) { best = d; nearWpt = i; }
                }
            }

            if (nearWpt >= 0) {
                m_MeasureDragIdx = nearWpt;
            } else {
                // Snap to nearest visible network node within 16 px
                float lat = m_HoverLat, lon = m_HoverLon;
                {
                    float best = 16.0f;
                    for (const auto& n : net.nodes) {
                        glm::vec3 wp = latLonToWorld(n.lat_deg, n.lon_deg);
                        if (glm::dot(glm::normalize(wp), camDir) < 0.05f) continue;
                        glm::vec2 sp = worldToScreen(wp);
                        float d = glm::length(sp - cursor);
                        if (d < best) { best = d; lat = n.lat_deg; lon = n.lon_deg; }
                    }
                }

                // Check if cursor is near an existing segment → insert waypoint there
                constexpr float kInsertThresh = 12.0f;
                constexpr float kEndpointDead = 8.0f;
                int   insertIdx   = -1;
                float bestSegDist = kInsertThresh;

                if ((int)m_MeasurePath.size() >= 2) {
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
                        float dist = glm::length(cursor - (sa + t * ab));

                        float dA = glm::length(cursor - sa);
                        float dB = glm::length(cursor - sb);
                        if (dA < kEndpointDead || dB < kEndpointDead) continue;

                        if (dist < bestSegDist) { bestSegDist = dist; insertIdx = i; }
                    }
                }

                float radius = (float)m_World->body.radius_km;
                if (insertIdx >= 0) {
                    m_MeasurePath.insert(m_MeasurePath.begin() + insertIdx + 1, {lat, lon});
                    m_MeasureTotalKm = 0.f;
                    for (int i = 1; i < (int)m_MeasurePath.size(); ++i)
                        m_MeasureTotalKm += greatCircleKm(
                            m_MeasurePath[i-1].x, m_MeasurePath[i-1].y,
                            m_MeasurePath[i  ].x, m_MeasurePath[i  ].y, radius);
                } else {
                    if (!m_MeasurePath.empty())
                        m_MeasureTotalKm += greatCircleKm(
                            m_MeasurePath.back().x, m_MeasurePath.back().y, lat, lon, radius);
                    m_MeasurePath.push_back({lat, lon});
                }

                int segs = (int)m_MeasurePath.size() - 1;
                if (segs <= 0) {
                    m_MeasureResult = "Click to add points \xe2\x80\x93 right-click to undo";
                } else {
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "%.0f km  (%d seg%s)",
                                  m_MeasureTotalKm, segs, segs == 1 ? "" : "s");
                    m_MeasureResult = buf;
                }
            }
        }
    }

    // ── Measure: right-click removes last waypoint ────────────────────────────
    if (!io.WantCaptureMouse && m_EditMode == EditMode::Measure &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        !m_MeasurePath.empty()) {
        m_MeasurePath.pop_back();
        float radius = m_World ? (float)m_World->body.radius_km : 6371.f;
        m_MeasureTotalKm = 0.f;
        for (int i = 1; i < (int)m_MeasurePath.size(); ++i)
            m_MeasureTotalKm += greatCircleKm(
                m_MeasurePath[i-1].x, m_MeasurePath[i-1].y,
                m_MeasurePath[i  ].x, m_MeasurePath[i  ].y, radius);
        int segs = (int)m_MeasurePath.size() - 1;
        if (segs <= 0)
            m_MeasureResult = "Click to add points \xe2\x80\x93 right-click to undo";
        else {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.0f km  (%d seg%s)",
                          m_MeasureTotalKm, segs, segs == 1 ? "" : "s");
            m_MeasureResult = buf;
        }
    }

    // ── Measure waypoint drag (live update while left button held) ────────────
    if (!io.WantCaptureMouse && m_EditMode == EditMode::Measure &&
        m_MeasureDragIdx >= 0 && m_MeasureDragIdx < (int)m_MeasurePath.size() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        m_HoverLat > -999.0f && m_World) {
        m_MeasurePath[m_MeasureDragIdx] = { m_HoverLat, m_HoverLon };
        float radius = (float)m_World->body.radius_km;
        m_MeasureTotalKm = 0.f;
        for (int i = 1; i < (int)m_MeasurePath.size(); ++i)
            m_MeasureTotalKm += greatCircleKm(
                m_MeasurePath[i-1].x, m_MeasurePath[i-1].y,
                m_MeasurePath[i  ].x, m_MeasurePath[i  ].y, radius);
        int segs = (int)m_MeasurePath.size() - 1;
        if (segs > 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%.0f km  (%d seg%s)",
                          m_MeasureTotalKm, segs, segs == 1 ? "" : "s");
            m_MeasureResult = buf;
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_MeasureDragIdx >= 0)
        m_MeasureDragIdx = -1;

    // ── Live node drag (only when Relocate mode is active) ───────────────────
    if (!io.WantCaptureMouse && m_RelocateMode &&
        !m_SelectedNodeId.empty() && m_EditMode == EditMode::Navigate &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f) &&
        m_HoverLat > -999.0f && m_World) {
        if (!m_DraggingNode) {
            // First frame of drag — lock in the node and capture orig position
            m_DragNodeId = m_SelectedNodeId;
            MapNode* n = m_World->body.network.findNode(m_DragNodeId);
            if (n) { m_DragOrigLat = n->lat_deg; m_DragOrigLon = n->lon_deg; }
        }
        MapNode* n = m_World->body.network.findNode(m_DragNodeId);
        if (n) {
            n->lat_deg     = m_HoverLat;
            n->lon_deg     = m_HoverLon;
            m_DraggingNode = true;
        }
    }

    // ── Commit drag on mouse release ──────────────────────────────────────────
    if (!io.WantCaptureMouse && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        m_DraggingNode) {
        if (m_World) {
            MapNode* n = m_World->body.network.findNode(m_DragNodeId);
            if (n) {
                float newLat = n->lat_deg;
                float newLon = n->lon_deg;
                n->lat_deg = m_DragOrigLat;
                n->lon_deg = m_DragOrigLon;
                m_CommandStack.execute(std::make_unique<MoveNodeCommand>(
                    m_World->body.network.nodes, m_DragNodeId,
                    newLat, newLon, m_DragOrigLat, m_DragOrigLon));
                WorldSerializer::save(*m_World);
            }
        }
        m_DraggingNode = false;
        m_DragNodeId.clear();
        m_RelocateMode = false;
    }

    // ── Dockspace host ────────────────────────────────────────────────────────
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags hostFlags =
        ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoTitleBar  |
        ImGuiWindowFlags_NoCollapse   | ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   {0, 0});
    ImGui::Begin("##host", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockId = ImGui::GetID("MainDock");
    if (ImGui::DockBuilderGetNode(dockId) == nullptr || m_ResetDockLayout) {
        m_ResetDockLayout = false;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, vp->Size);

        ImGuiID left, right, bottom, center;
        right  = ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Right, 0.25f, nullptr, &center);
        left   = ImGui::DockBuilderSplitNode(center,  ImGuiDir_Left,  0.22f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center,  ImGuiDir_Down,  0.20f, nullptr, &center);

        ImGui::DockBuilderDockWindow("World",     left);
        ImGui::DockBuilderDockWindow("Layers",    left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Timeline",  bottom);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::DockSpace(dockId, {0, 0}, ImGuiDockNodeFlags_PassthruCentralNode);
    m_DockId = dockId;

    renderMenuBar();
    ImGui::End();

    renderNewWorldDialog();
    renderCalendarDialog();
    renderMapToolsDialog();
    renderPanels();

    renderRoads();
    renderLabels();

    renderHUD();

    m_TextRenderer.beginFrame(m_Window.width(), m_Window.height());
    m_TextRenderer.flush();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Menu bar ──────────────────────────────────────────────────────────────────

void Application::renderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New World..."))
            m_OpenNewWorldDialog = true;

        if (ImGui::MenuItem("Open World...")) {
            const char* picked = tinyfd_selectFolderDialog("Select world folder", nullptr);
            if (picked)
                openWorld(picked);
        }

        if (ImGui::BeginMenu("Open Recent", !m_RecentProjects.empty())) {
            for (const auto& p : m_RecentProjects) {
                namespace fs = std::filesystem;
                fs::path fp(p);
                if (fp.filename().empty()) fp = fp.parent_path();
                std::string label = fp.filename().string();
                if (label.empty()) label = p;
                std::string itemId = label + "##" + p;
                if (ImGui::MenuItem(itemId.c_str()))
                    openWorld(p);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", p.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Recent"))
                m_RecentProjects.clear(), saveRecentProjects();
            ImGui::EndMenu();
        }

        bool hasSave = m_World.has_value();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, hasSave)) {
            if (WorldSerializer::save(*m_World))
                std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                              "Saved: %s", m_World->rootPath.string().c_str());
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Quit"))
            glfwSetWindowShouldClose(m_Window.handle(), GLFW_TRUE);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_CommandStack.canUndo())) {
            m_CommandStack.undo();
            if (m_World) WorldSerializer::save(*m_World);
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_CommandStack.canRedo())) {
            m_CommandStack.redo();
            if (m_World) WorldSerializer::save(*m_World);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset Layout"))
            m_ResetDockLayout = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Map Import..."))
            m_ShowMapTools = true;
        ImGui::EndMenu();
    }

    if (m_StatusMsg[0]) {
        float msgW = ImGui::CalcTextSize(m_StatusMsg).x + 16.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - msgW);
        ImGui::TextDisabled("%s", m_StatusMsg);
    }

    ImGui::EndMenuBar();
}

// ── New World dialog ──────────────────────────────────────────────────────────

void Application::renderNewWorldDialog() {
    if (m_OpenNewWorldDialog) {
        ImGui::OpenPopup("New World");
        m_OpenNewWorldDialog = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({480.0f, 0.0f}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("New World", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("Name", m_NewWorldName, sizeof(m_NewWorldName));
    ImGui::InputText("Location", m_NewWorldPath, sizeof(m_NewWorldPath));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        const char* picked = tinyfd_selectFolderDialog(
            "Choose parent folder for the new world", m_NewWorldPath);
        if (picked)
            strncpy_s(m_NewWorldPath, sizeof(m_NewWorldPath), picked, _TRUNCATE);
    }

    ImGui::TextDisabled("World will be created at: %s%c%s",
                        m_NewWorldPath, std::filesystem::path::preferred_separator,
                        m_NewWorldName);
    ImGui::Separator();

    bool canCreate = m_NewWorldName[0] != '\0' && m_NewWorldPath[0] != '\0';
    if (!canCreate) ImGui::BeginDisabled();
    if (ImGui::Button("Create", {120, 0})) {
        std::filesystem::path root =
            std::filesystem::path(m_NewWorldPath) / m_NewWorldName;
        World w;
        if (WorldSerializer::createNew(root, m_NewWorldName, w)) {
            m_World              = w;
            m_NeedsTextureReload = true;
            m_SelectedNodeId.clear();
            m_CommandStack.clear();
            std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                          "Created: %s", w.name.c_str());
        } else {
            std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                          "Failed to create world at %s", root.string().c_str());
        }
        ImGui::CloseCurrentPopup();
    }
    if (!canCreate) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ── World panel ───────────────────────────────────────────────────────────────

void Application::renderWorldPanel() {
    if (!m_World) {
        ImGui::TextDisabled("No world loaded.");
        ImGui::TextDisabled("File > New World  or  Open World...");
        return;
    }

    ImGui::Text("%s", m_World->name.c_str());
    ImGui::TextDisabled("%s", m_World->rootPath.string().c_str());
    ImGui::Separator();

    {
        auto& b = m_World->body;
        ImGui::TextUnformatted("Texture");
        ImGui::SameLine();
        std::string texName = b.texture_path.empty()
            ? "(none)" : std::filesystem::path(b.texture_path).filename().string();
        ImGui::TextDisabled("%s", texName.c_str());
        if (ImGui::SmallButton("Browse##tex")) {
            static const char* filters[] = { "*.jpg", "*.jpeg", "*.png" };
            const char* picked = tinyfd_openFileDialog(
                "Select equirectangular texture", nullptr, 3, filters, "Image files", 0);
            if (picked) {
                b.texture_path = picked;
                WorldSerializer::save(*m_World);
                m_NeedsTextureReload = true;
            }
        }
        if (!b.texture_path.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##tex")) {
                b.texture_path.clear();
                WorldSerializer::save(*m_World);
                m_NeedsTextureReload = true;
            }
        }
    }
    ImGui::Separator();

    // ── Place named node ──────────────────────────────────────────────────────
    ImGui::TextUnformatted("Place");
    ImGui::SameLine();

    auto placeBtn = [&](const char* lbl, EntityType type) {
        bool active = (m_EditMode == EditMode::Place && m_PlaceType == type);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::SmallButton(lbl)) { m_PlaceType = type; m_EditMode = EditMode::Place; }
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };

    placeBtn("City", EntityType::City);
    placeBtn("Town", EntityType::Town);
    placeBtn("POI",  EntityType::POI);

    if (m_EditMode == EditMode::Place) {
        if (ImGui::SmallButton("Cancel##pl")) m_EditMode = EditMode::Navigate;
        ImGui::TextColored({1.0f, 0.9f, 0.2f, 1.0f}, "Click globe to place");
    } else {
        ImGui::NewLine();
    }

    // ── Named places list ─────────────────────────────────────────────────────
    const auto& body = m_World->body;
    bool hasNamed = false;
    for (const auto& n : body.network.nodes)
        if (!n.name.empty()) { hasNamed = true; break; }

    if (hasNamed) {
        ImGui::Separator();
        ImGui::TextUnformatted("Named Places");
        static const char* eIcon[] = { "[C]", "[T]", "[P]" };
        for (const auto& n : body.network.nodes) {
            if (n.name.empty()) continue;
            char label[320];
            std::snprintf(label, sizeof(label), "%s %s##%s",
                          eIcon[(int)n.entity_type], n.name.c_str(), n.id.c_str());
            if (ImGui::Selectable(label, n.id == m_SelectedNodeId)) {
                m_SelectedNodeId = n.id;
                m_SelectedOverlayId.clear();
            }
        }
    }

    // ── Overlays ──────────────────────────────────────────────────────────────
    {
        auto& b = m_World->body;
        ImGui::Separator();
        ImGui::TextUnformatted("Overlays");
        ImGui::SameLine();
        if (ImGui::SmallButton("+##ov")) {
            RegionOverlay ov;
            ov.id   = b.id + "_ov" + std::to_string(b.overlays.size() + 1);
            ov.name = "New Overlay";
            b.overlays.push_back(ov);
            m_SelectedOverlayId = ov.id;
            m_SelectedNodeId.clear();
            WorldSerializer::save(*m_World);
            reloadBodyOverlays();
        }
        int swapA = -1, swapB = -1;
        for (int i = 0; i < (int)b.overlays.size(); ++i) {
            auto& ov = b.overlays[i];
            ImGui::PushID(i);

            bool vis = ov.visible;
            if (ImGui::Checkbox("##ovis", &vis)) {
                ov.visible = vis;
                WorldSerializer::save(*m_World);
                reloadBodyOverlays();
            }
            ImGui::SameLine();

            bool canUp   = i > 0;
            bool canDown = i < (int)b.overlays.size() - 1;
            if (!canUp) ImGui::BeginDisabled();
            if (ImGui::SmallButton("^")) { swapA = i - 1; swapB = i; }
            if (!canUp) ImGui::EndDisabled();
            ImGui::SameLine();
            if (!canDown) ImGui::BeginDisabled();
            if (ImGui::SmallButton("v")) { swapA = i; swapB = i + 1; }
            if (!canDown) ImGui::EndDisabled();
            ImGui::SameLine();

            if (ImGui::Selectable(ov.name.c_str(), ov.id == m_SelectedOverlayId)) {
                m_SelectedOverlayId = ov.id;
                m_SelectedNodeId.clear();
            }
            ImGui::PopID();
        }
        if (swapA >= 0) {
            std::swap(b.overlays[swapA], b.overlays[swapB]);
            WorldSerializer::save(*m_World);
            reloadBodyOverlays();
        }
    }

    // ── Network editing ───────────────────────────────────────────────────────
    {
        auto& net = m_World->body.network;
        ImGui::Separator();
        ImGui::TextUnformatted("Network");
        ImGui::SameLine();

        static const char* kRouteNames[] = { "Road", "Sea Route" };
        int rtIdx = (int)m_RouteType;
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::Combo("##rt", &rtIdx, kRouteNames, 2))
            m_RouteType = static_cast<RouteType>(rtIdx);
        ImGui::SameLine();

        auto modeBtn = [&](const char* lbl, NetworkSubMode sub) {
            bool active = (m_EditMode == EditMode::NetworkEdit && m_NetworkSubMode == sub);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::SmallButton(lbl)) {
                if (m_EditMode == EditMode::NetworkEdit && m_NetworkSubMode == sub) {
                    m_EditMode = EditMode::Navigate;
                    m_NetworkConnectFrom.clear();
                } else {
                    m_EditMode       = EditMode::NetworkEdit;
                    m_NetworkSubMode = sub;
                    m_NetworkConnectFrom.clear();
                }
            }
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        modeBtn("+ Node",  NetworkSubMode::PlaceNode);
        modeBtn("Connect", NetworkSubMode::Connect);
        ImGui::NewLine();

        if (m_EditMode == EditMode::NetworkEdit) {
            if (m_NetworkSubMode == NetworkSubMode::Connect) {
                if (m_NetworkConnectFrom.empty())
                    ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Click first node");
                else
                    ImGui::TextColored({.5f, 1.f, .5f, 1.f}, "Click second node");
            } else {
                ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Click to place / select");
            }
        }

        if (!net.nodes.empty()) {
            int roads = 0, sea = 0;
            for (const auto& e : net.edges)
                (e.type == RouteType::Road ? roads : sea)++;
            ImGui::TextDisabled("%d nodes  |  %d road  %d sea edges",
                                (int)net.nodes.size(), roads, sea);
        }
    }
}

// ── Panels ────────────────────────────────────────────────────────────────────

void Application::renderPanels() {
    ImGui::Begin("World");
    renderWorldPanel();
    ImGui::End();

    // ── Inspector ─────────────────────────────────────────────────────────────
    ImGui::Begin("Inspector");

    if (m_World && !m_SelectedNodeId.empty()) {
        auto& net  = m_World->body.network;
        MapNode* node = net.findNode(m_SelectedNodeId);
        if (node) {
            bool isNamed = !node->name.empty();

            static char        nameEdit[256]  = {};
            static char        mediaEdit[512] = {};
            static std::string lastId;
            if (lastId != m_SelectedNodeId) {
                lastId = m_SelectedNodeId;
                m_RelocateMode = false;
                strncpy_s(nameEdit,  sizeof(nameEdit),  node->name.c_str(),      _TRUNCATE);
                strncpy_s(mediaEdit, sizeof(mediaEdit), node->media_ref.c_str(), _TRUNCATE);
            }

            static const char* typeLabels[] = { "City", "Town", "POI" };
            ImGui::Text("%s", isNamed ? typeLabels[(int)node->entity_type] : "Waypoint");
            ImGui::Separator();

            if (ImGui::InputText("Name##node", nameEdit, sizeof(nameEdit)))
                node->name = nameEdit;
            if (ImGui::IsItemDeactivatedAfterEdit())
                WorldSerializer::save(*m_World);

            if (!node->name.empty()) {
                int typeIdx = (int)node->entity_type;
                if (ImGui::Combo("Type##node", &typeIdx, typeLabels, 3)) {
                    node->entity_type = static_cast<EntityType>(typeIdx);
                    WorldSerializer::save(*m_World);
                }
            }

            ImGui::LabelText("Lat", "%.4f\xc2\xb0", node->lat_deg);
            ImGui::LabelText("Lon", "%.4f\xc2\xb0", node->lon_deg);

            // Relocate toggle
            if (m_RelocateMode) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button("Cancel Move", {-1, 0}))
                    m_RelocateMode = false;
                ImGui::PopStyleColor();
                ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Drag node to new position");
            } else {
                if (ImGui::Button("Move", {-1, 0}))
                    m_RelocateMode = true;
            }

            // Connections
            ImGui::Spacing();
            ImGui::TextUnformatted("Connections");
            bool any = false;
            std::string deleteEdgeId;
            for (const auto& e : net.edges) {
                if (e.from_id != node->id && e.to_id != node->id) continue;
                const std::string& otherId = (e.from_id == node->id) ? e.to_id : e.from_id;
                const MapNode* other = net.findNode(otherId);
                const char* otherLabel = (other && !other->name.empty())
                    ? other->name.c_str() : otherId.c_str();
                const char* typeStr = (e.type == RouteType::Road) ? "[Rd]" : "[Sea]";
                ImGui::PushID(e.id.c_str());
                if (ImGui::SmallButton("x")) deleteEdgeId = e.id;
                ImGui::SameLine();
                ImGui::TextDisabled("%s %s  (%.0f km)", typeStr, otherLabel, e.distance_km);
                ImGui::PopID();
                any = true;
            }
            if (!any) ImGui::TextDisabled("(none)");
            if (!deleteEdgeId.empty()) {
                m_CommandStack.execute(
                    std::make_unique<DeleteEdgeCommand>(net.edges, deleteEdgeId));
                WorldSerializer::save(*m_World);
            }

            // Lifespan (named nodes only)
            if (!node->name.empty()) {
                ImGui::Separator();
                ImGui::TextUnformatted("Lifespan");
                {
                    bool hasBorn = node->born_day.has_value();
                    if (ImGui::Checkbox("Born##cb", &hasBorn)) {
                        node->born_day = hasBorn ? std::optional<int>(m_CurrentDay) : std::nullopt;
                        WorldSerializer::save(*m_World);
                    }
                    if (hasBorn) {
                        ImGui::SameLine();
                        int bday = *node->born_day;
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputInt("##born", &bday)) {
                            node->born_day = bday;
                            WorldSerializer::save(*m_World);
                        }
                        if (m_World->calendar.defined())
                            ImGui::TextDisabled("  %s", m_World->calendar.formatDay(*node->born_day).c_str());
                    }

                    bool hasDied = node->died_day.has_value();
                    if (ImGui::Checkbox("Died##cb", &hasDied)) {
                        node->died_day = hasDied ? std::optional<int>(m_CurrentDay) : std::nullopt;
                        WorldSerializer::save(*m_World);
                    }
                    if (hasDied) {
                        ImGui::SameLine();
                        int dday = *node->died_day;
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputInt("##died", &dday)) {
                            node->died_day = dday;
                            WorldSerializer::save(*m_World);
                        }
                        if (m_World->calendar.defined())
                            ImGui::TextDisabled("  %s", m_World->calendar.formatDay(*node->died_day).c_str());
                    }
                }

                // Lore file
                ImGui::Separator();
                ImGui::TextUnformatted("Lore file");
                if (ImGui::InputText("##media", mediaEdit, sizeof(mediaEdit)))
                    node->media_ref = mediaEdit;
                if (ImGui::IsItemDeactivatedAfterEdit())
                    WorldSerializer::save(*m_World);

                bool hasMedia = !node->media_ref.empty();
                if (!hasMedia) ImGui::BeginDisabled();
                if (ImGui::Button("Open##lore")) {
#ifdef _WIN32
                    ShellExecuteW(nullptr, L"open",
                        std::filesystem::path(node->media_ref).wstring().c_str(),
                        nullptr, nullptr, SW_SHOW);
#endif
                }
                if (!hasMedia) ImGui::EndDisabled();

                ImGui::SameLine();
                if (ImGui::Button("Create##lore")) {
                    std::filesystem::path p =
                        m_World->rootPath / "media" / (node->id + ".md");
                    if (!std::filesystem::exists(p)) {
                        std::ofstream f(p);
                        f << "# " << node->name << "\n\n";
                    }
                    node->media_ref = p.string();
                    strncpy_s(mediaEdit, sizeof(mediaEdit), node->media_ref.c_str(), _TRUNCATE);
                    WorldSerializer::save(*m_World);
#ifdef _WIN32
                    ShellExecuteW(nullptr, L"open", p.wstring().c_str(),
                                  nullptr, nullptr, SW_SHOW);
#endif
                }
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Delete Node", {-1, 0})) {
                std::string delId = m_SelectedNodeId;
                m_CommandStack.execute(
                    std::make_unique<DeleteNodeCommand>(net, delId));
                if (m_NetworkConnectFrom == delId) m_NetworkConnectFrom.clear();
                m_SelectedNodeId.clear();
                lastId.clear();
                WorldSerializer::save(*m_World);
            }
            ImGui::PopStyleColor(3);
        } else {
            m_SelectedNodeId.clear();
        }

    } else if (m_World && !m_SelectedEdgeId.empty()) {
        auto& net = m_World->body.network;
        auto  eit = std::find_if(net.edges.begin(), net.edges.end(),
                       [&](const RouteEdge& e){ return e.id == m_SelectedEdgeId; });
        if (eit != net.edges.end()) {
            const auto& e = *eit;
            const MapNode* na = net.findNode(e.from_id);
            const MapNode* nb = net.findNode(e.to_id);
            auto nodeLabel = [](const MapNode* n, const std::string& id) -> std::string {
                return (n && !n->name.empty()) ? n->name : id;
            };

            static const char* kEdgeTypes[] = { "Road", "Sea Route" };
            int typeIdx = (e.type == RouteType::Road) ? 0 : 1;
            if (ImGui::Combo("Type##edge", &typeIdx, kEdgeTypes, 2)) {
                RouteType newType = (typeIdx == 0) ? RouteType::Road : RouteType::Sea;
                m_CommandStack.execute(
                    std::make_unique<ChangeEdgeTypeCommand>(net.edges, e.id, newType));
                WorldSerializer::save(*m_World);
            }
            ImGui::Separator();
            ImGui::LabelText("From", "%s", nodeLabel(na, e.from_id).c_str());
            ImGui::LabelText("To",   "%s", nodeLabel(nb, e.to_id  ).c_str());
            ImGui::LabelText("Distance", "%.0f km", e.distance_km);

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Delete Connection", {-1, 0})) {
                std::string delId = m_SelectedEdgeId;
                m_SelectedEdgeId.clear();
                m_CommandStack.execute(
                    std::make_unique<DeleteEdgeCommand>(net.edges, delId));
                WorldSerializer::save(*m_World);
            }
            ImGui::PopStyleColor(3);
        } else {
            m_SelectedEdgeId.clear();
        }

    } else if (m_World && !m_SelectedOverlayId.empty()) {
        auto& body = m_World->body;
        auto  ovIt = std::find_if(body.overlays.begin(), body.overlays.end(),
                         [&](const RegionOverlay& o){ return o.id == m_SelectedOverlayId; });
        if (ovIt != body.overlays.end()) {
            auto& ov = *ovIt;

            static char        ovNameEdit[256] = {};
            static std::string lastOvId;
            if (lastOvId != m_SelectedOverlayId) {
                lastOvId = m_SelectedOverlayId;
                strncpy_s(ovNameEdit, sizeof(ovNameEdit), ov.name.c_str(), _TRUNCATE);
            }

            ImGui::Text("Overlay");
            ImGui::Separator();

            if (ImGui::InputText("Name##ov", ovNameEdit, sizeof(ovNameEdit)))
                ov.name = ovNameEdit;
            if (ImGui::IsItemDeactivatedAfterEdit())
                WorldSerializer::save(*m_World);

            if (ImGui::InputFloat("Center Lat##ov", &ov.center_lat, 0.0f, 0.0f, "%.4f"))
                WorldSerializer::save(*m_World);
            if (ImGui::InputFloat("Center Lon##ov", &ov.center_lon, 0.0f, 0.0f, "%.4f"))
                WorldSerializer::save(*m_World);
            if (ImGui::InputFloat("Extent km##ov",  &ov.extent_km,  0.0f, 0.0f, "%.1f"))
                WorldSerializer::save(*m_World);
            if (ImGui::SliderFloat("Opacity##ov",   &ov.opacity,    0.0f, 1.0f, "%.2f"))
                WorldSerializer::save(*m_World);

            ImGui::Separator();
            ImGui::TextUnformatted("Image");
            std::string imgDisplay = ov.image_path.empty()
                ? "(none)" : std::filesystem::path(ov.image_path).filename().string();
            ImGui::TextDisabled("%s", imgDisplay.c_str());

            if (ImGui::Button("Browse...##ov")) {
                static const char* ovFilters[] = {"*.jpg", "*.jpeg", "*.png"};
                const char* picked = tinyfd_openFileDialog(
                    "Select overlay image", nullptr, 3, ovFilters, "Image files", 0);
                if (picked) {
                    ov.image_path = picked;
                    WorldSerializer::save(*m_World);
                    reloadBodyOverlays();
                }
            }
            if (!ov.image_path.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##ov")) {
                    ov.image_path.clear();
                    WorldSerializer::save(*m_World);
                    reloadBodyOverlays();
                }
            }

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Delete##ov", {-1, 0})) {
                body.overlays.erase(ovIt);
                m_SelectedOverlayId.clear();
                lastOvId.clear();
                WorldSerializer::save(*m_World);
                reloadBodyOverlays();
            }
            ImGui::PopStyleColor(3);
        } else {
            m_SelectedOverlayId.clear();
        }

    } else if (m_World && !m_SelectedPartyId.empty()) {
        auto pit = std::find_if(m_World->parties.begin(), m_World->parties.end(),
                       [&](const TravelRecord& p){ return p.id == m_SelectedPartyId; });
        if (pit != m_World->parties.end()) {
            auto& p   = *pit;
            auto& net = m_World->body.network;

            auto nodeLabel = [&](const std::string& id) -> std::string {
                const MapNode* n = net.findNode(id);
                return (n && !n->name.empty()) ? n->name : id.empty() ? "(pos)" : id;
            };

            static char        pNameBuf[256] = {};
            static std::string lastPId;
            if (lastPId != p.id) {
                lastPId = p.id;
                strncpy_s(pNameBuf, sizeof(pNameBuf), p.name.c_str(), _TRUNCATE);
            }

            ImGui::Text("Party");
            ImGui::Separator();
            if (ImGui::InputText("Name##pn", pNameBuf, sizeof(pNameBuf)))
                p.name = pNameBuf;
            if (ImGui::IsItemDeactivatedAfterEdit()) WorldSerializer::save(*m_World);

            if (ImGui::ColorEdit3("Color##pc", p.color))
                WorldSerializer::save(*m_World);

            float spd = p.speed_kmday;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##pspd2", &spd, 5.f, 20.f, "%.0f km/day"))
                p.speed_kmday = std::max(1.f, spd);
            if (ImGui::IsItemDeactivatedAfterEdit()) WorldSerializer::save(*m_World);

            // Waypoints list
            ImGui::Separator();
            ImGui::TextUnformatted("Waypoints");
            int deleteWpIdx = -1;
            for (int i = 0; i < (int)p.waypoints.size(); ++i) {
                auto& wp = p.waypoints[i];
                ImGui::PushID(i);
                if (ImGui::SmallButton("x")) deleteWpIdx = i;
                ImGui::SameLine();
                ImGui::TextDisabled("Day %d  %s", wp.day, nodeLabel(wp.node_id).c_str());
                ImGui::PopID();
            }
            if (deleteWpIdx >= 0) {
                p.waypoints.erase(p.waypoints.begin() + deleteWpIdx);
                WorldSerializer::save(*m_World);
            }
            if (p.waypoints.empty()) ImGui::TextDisabled("(no waypoints)");

            // Place / Travel actions
            ImGui::Separator();
            if (!m_PartyPlaceMode && !m_PartyTravelMode) {
                if (ImGui::Button("Add Waypoint", {-1, 0})) {
                    m_PartyPlaceMode  = true;
                    m_PartyTravelMode = false;
                }
                bool hasLastNode = !p.waypoints.empty() && !p.waypoints.back().node_id.empty();
                if (!hasLastNode) ImGui::BeginDisabled();
                if (ImGui::Button("Travel to...", {-1, 0})) {
                    m_PartyTravelMode = true;
                    m_PartyTravelDest.clear();
                    m_PartyRouteEdgeIds.clear();
                    m_PartyRouteKm    = -1.f;
                }
                if (!hasLastNode) ImGui::EndDisabled();
            }

            if (m_PartyPlaceMode) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button("Cancel Placement", {-1, 0}))
                    m_PartyPlaceMode = false;
                ImGui::PopStyleColor();
                ImGui::TextColored({1.f, .9f, .2f, 1.f},
                    "Click a node (day %d)", m_CurrentDay);
            }

            if (m_PartyTravelMode) {
                if (m_PartyTravelDest.empty()) {
                    ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Click destination node");
                } else {
                    ImGui::TextDisabled("To: %s", nodeLabel(m_PartyTravelDest).c_str());
                    if (m_PartyRouteKm < 0.f) {
                        ImGui::TextColored({1.f, .4f, .4f, 1.f}, "No path found");
                    } else {
                        int days = std::max(1, (int)std::ceil(m_PartyRouteKm / p.speed_kmday));
                        ImGui::TextColored({.4f, 1.f, .5f, 1.f},
                            "%.0f km  |  %d day%s",
                            m_PartyRouteKm, days, days == 1 ? "" : "s");
                        if (ImGui::Button("Confirm##ptrav", {-1, 0})) {
                            const MapNode* dn = net.findNode(m_PartyTravelDest);
                            TravelWaypoint wp;
                            wp.node_id = m_PartyTravelDest;
                            wp.day     = (p.waypoints.empty()
                                          ? m_CurrentDay
                                          : p.waypoints.back().day) + days;
                            if (dn) { wp.lat_deg = dn->lat_deg; wp.lon_deg = dn->lon_deg; }
                            p.waypoints.push_back(wp);
                            m_CurrentDay      = wp.day;
                            m_PartyTravelMode = false;
                            m_PartyTravelDest.clear();
                            m_PartyRouteEdgeIds.clear();
                            m_PartyRouteKm    = -1.f;
                            WorldSerializer::save(*m_World);
                        }
                    }
                }
                if (ImGui::SmallButton("Cancel##ptrav")) {
                    m_PartyTravelMode = false;
                    m_PartyTravelDest.clear();
                    m_PartyRouteEdgeIds.clear();
                    m_PartyRouteKm    = -1.f;
                }
            }

            // Delete
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Delete Party", {-1, 0})) {
                m_World->parties.erase(pit);
                m_SelectedPartyId.clear();
                lastPId.clear();
                m_PartyPlaceMode  = false;
                m_PartyTravelMode = false;
                WorldSerializer::save(*m_World);
            }
            ImGui::PopStyleColor(3);
        } else {
            m_SelectedPartyId.clear();
        }

    } else if (m_World) {
        auto& b = m_World->body;
        ImGui::Text("%s", b.name.c_str());
        ImGui::Separator();
        ImGui::LabelText("Radius", "%.0f km", b.radius_km);

        ImGui::Separator();
        ImGui::TextUnformatted("Texture");
        std::string texDisplay = b.texture_path.empty()
            ? "(none)" : std::filesystem::path(b.texture_path).filename().string();
        ImGui::TextDisabled("%s", texDisplay.c_str());

        if (ImGui::Button("Browse...##tex")) {
            static const char* filters[] = { "*.jpg", "*.jpeg", "*.png" };
            const char* picked = tinyfd_openFileDialog(
                "Select texture", nullptr, 3, filters, "Image files", 0);
            if (picked) {
                b.texture_path = picked;
                WorldSerializer::save(*m_World);
                m_NeedsTextureReload = true;
            }
        }
        if (!b.texture_path.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Clear##tex")) {
                b.texture_path.clear();
                WorldSerializer::save(*m_World);
                m_NeedsTextureReload = true;
            }
        }

    } else {
        ImGui::TextDisabled("Nothing selected.");
    }

    ImGui::End();

    // ── Layers ────────────────────────────────────────────────────────────────
    ImGui::Begin("Layers");
    if (m_World) {
        ImGui::SeparatorText("Visibility");
        ImGui::Checkbox("Roads",      &m_ShowRoads);
        ImGui::Checkbox("Sea Routes", &m_ShowSea);

        ImGui::SeparatorText("Route");
        if (ImGui::Checkbox("Route tool", &m_RouteMode)) {
            m_RouteFrom.clear(); m_RouteTo.clear();
            m_RouteEdgeIds.clear(); m_RouteKm = -1.f;
        }
        if (m_RouteMode) {
            static const char* kTypeLabels[] = { "Any", "Road only", "Sea only" };
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##rtype", &m_RouteTypeFilter, kTypeLabels, 3)) {
                if (!m_RouteFrom.empty() && !m_RouteTo.empty() && m_World) {
                    std::optional<RouteType> tf;
                    if (m_RouteTypeFilter == 1) tf = RouteType::Road;
                    if (m_RouteTypeFilter == 2) tf = RouteType::Sea;
                    m_RouteKm = m_World->body.network.shortestPathEdges(
                        m_RouteFrom, m_RouteTo, m_RouteEdgeIds, tf);
                }
            }

            auto nodeLabel = [&](const std::string& id) -> std::string {
                if (id.empty()) return "(none)";
                if (!m_World) return id;
                const MapNode* n = m_World->body.network.findNode(id);
                return (n && !n->name.empty()) ? n->name : id;
            };
            ImGui::TextDisabled("From: %s", nodeLabel(m_RouteFrom).c_str());
            ImGui::TextDisabled("To:   %s", nodeLabel(m_RouteTo).c_str());

            if (m_RouteFrom.empty())
                ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Click a node to set start");
            else if (m_RouteTo.empty())
                ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Click a node to set end");
            else if (m_RouteKm < 0.f)
                ImGui::TextColored({1.f, .4f, .4f, 1.f}, "No path found");
            else
                ImGui::TextColored({.4f, 1.f, .4f, 1.f}, "%.0f km  (%d hops)",
                                   m_RouteKm, (int)m_RouteEdgeIds.size());

            if (!m_RouteFrom.empty()) {
                if (ImGui::SmallButton("Clear##route")) {
                    m_RouteFrom.clear(); m_RouteTo.clear();
                    m_RouteEdgeIds.clear(); m_RouteKm = -1.f;
                }
            }
        }

        ImGui::SeparatorText("Party");
        {
            static const float kPartyColors[][3] = {
                {0.86f, 0.39f, 1.0f}, {0.39f, 1.0f, 0.55f},
                {1.0f,  0.55f, 0.25f}, {0.25f, 0.75f, 1.0f}
            };
            if (ImGui::SmallButton("+ Party")) {
                TravelRecord p;
                int idx = (int)m_World->parties.size();
                p.id   = "party_" + std::to_string(idx);
                p.name = "Party " + std::to_string(idx + 1);
                const float* c = kPartyColors[idx % 4];
                p.color[0] = c[0]; p.color[1] = c[1]; p.color[2] = c[2];
                m_World->parties.push_back(p);
                m_SelectedPartyId = p.id;
                m_SelectedNodeId.clear();
                m_SelectedEdgeId.clear();
                m_SelectedOverlayId.clear();
                WorldSerializer::save(*m_World);
            }
            for (int i = 0; i < (int)m_World->parties.size(); ++i) {
                auto& p = m_World->parties[i];
                ImGui::PushID(p.id.c_str());
                bool vis = p.visible;
                if (ImGui::Checkbox("##pvis", &vis)) {
                    p.visible = vis;
                    WorldSerializer::save(*m_World);
                }
                ImGui::SameLine();
                ImVec4 dc{p.color[0], p.color[1], p.color[2], 1.f};
                ImGui::ColorButton("##pcol", dc,
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, {14, 14});
                ImGui::SameLine();
                bool sel = (p.id == m_SelectedPartyId);
                if (ImGui::Selectable(p.name.c_str(), sel)) {
                    m_SelectedPartyId  = sel ? "" : p.id;
                    m_SelectedNodeId.clear();
                    m_SelectedEdgeId.clear();
                    m_SelectedOverlayId.clear();
                    if (!sel) { m_PartyPlaceMode = false; m_PartyTravelMode = false; }
                }
                ImGui::PopID();
            }
            if (m_World->parties.empty())
                ImGui::TextDisabled("No parties. Click + Party to add one.");
        }

        ImGui::SeparatorText("Measure");
        bool measActive = (m_EditMode == EditMode::Measure);
        if (ImGui::Checkbox("Measure tool", &measActive)) {
            m_EditMode       = measActive ? EditMode::Measure : EditMode::Navigate;
            m_MeasureDragIdx = -1;
        }
        if (m_EditMode == EditMode::Measure && m_MeasurePath.empty())
            ImGui::TextDisabled("Click to start path");
        if (!m_MeasureResult.empty())
            ImGui::TextColored({1.f, .9f, .3f, 1.f}, "%s", m_MeasureResult.c_str());
        if (!m_MeasurePath.empty()) {
            if (ImGui::SmallButton("Clear")) {
                m_MeasurePath.clear();
                m_MeasureTotalKm = 0.f;
                m_MeasureResult.clear();
                m_MeasureDragIdx = -1;
            }
        }
    }
    ImGui::End();

    // ── Timeline ──────────────────────────────────────────────────────────────
    ImGui::Begin("Timeline");
    if (m_World) {
        const auto& cal = m_World->calendar;

        std::string dateStr = cal.formatDay(m_CurrentDay);
        ImGui::TextUnformatted(dateStr.c_str());

        ImGui::SetNextItemWidth(-1);
        ImGui::DragInt("##day", &m_CurrentDay, 1.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drag to scrub through time (1 day per pixel)");

        ImGui::Spacing();
        if (ImGui::SmallButton("Define Calendar..."))
            m_ShowCalendarDialog = true;
    } else {
        ImGui::TextDisabled("No world loaded.");
    }
    ImGui::End();
}

// ── Calendar definition dialog ────────────────────────────────────────────────

void Application::renderCalendarDialog() {
    if (m_ShowCalendarDialog) {
        ImGui::OpenPopup("Calendar");
        m_ShowCalendarDialog = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({460.0f, 0.0f}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Calendar", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (!m_World) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    auto& cal  = m_World->calendar;
    bool  changed = false;

    {
        char buf[128];
        strncpy_s(buf, sizeof(buf), cal.epoch_name.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("Epoch name", buf, sizeof(buf)))
            cal.epoch_name = buf;
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Months");

    constexpr ImGuiTableFlags kTbl = ImGuiTableFlags_BordersInnerV
                                   | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##months", 4, kTbl)) {
        ImGui::TableSetupColumn("#",     ImGuiTableColumnFlags_WidthFixed,   24);
        ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Days",  ImGuiTableColumnFlags_WidthFixed,   52);
        ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthFixed,   20);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)cal.months.size(); ++i) {
            auto& m = cal.months[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            char nb[64]; strncpy_s(nb, sizeof(nb), m.name.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##mn", nb, sizeof(nb))) m.name = nb;
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##md", &m.days, 0)) { m.days = std::max(1, m.days); changed = true; }

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("x")) { cal.months.erase(cal.months.begin() + i); changed = true; ImGui::PopID(); break; }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("+ Month")) {
        CalendarMonth nm;
        nm.name = "Month " + std::to_string(cal.months.size() + 1);
        cal.months.push_back(nm);
        changed = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Weekdays");

    if (ImGui::BeginTable("##wdays", 3, kTbl)) {
        ImGui::TableSetupColumn("#",    ImGuiTableColumnFlags_WidthFixed,   24);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed,   20);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)cal.week_days.size(); ++i) {
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%d", i + 1);

            ImGui::TableSetColumnIndex(1);
            char wb[64]; strncpy_s(wb, sizeof(wb), cal.week_days[i].c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##wd", wb, sizeof(wb))) cal.week_days[i] = wb;
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("x")) { cal.week_days.erase(cal.week_days.begin() + i); changed = true; ImGui::PopID(); break; }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("+ Weekday")) {
        cal.week_days.push_back("Day " + std::to_string(cal.week_days.size() + 1));
        changed = true;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Eras");
    ImGui::TextDisabled("Contiguous time periods sorted by start day.");

    std::sort(cal.eras.begin(), cal.eras.end(),
              [](const CalendarEra& a, const CalendarEra& b){ return a.start_day < b.start_day; });

    if (ImGui::BeginTable("##eras", 4, kTbl)) {
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("From day", ImGuiTableColumnFlags_WidthFixed,  70);
        ImGui::TableSetupColumn("Ends",     ImGuiTableColumnFlags_WidthFixed,  80);
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthFixed,  20);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)cal.eras.size(); ++i) {
            auto& era = cal.eras[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char eb[128]; strncpy_s(eb, sizeof(eb), era.name.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##en", eb, sizeof(eb))) era.name = eb;
            if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

            ImGui::TableSetColumnIndex(1);
            if (i == 0) {
                ImGui::TextDisabled("-\xe2\x88\x9e");
            } else {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputInt("##es", &era.start_day, 0)) changed = true;
            }

            ImGui::TableSetColumnIndex(2);
            bool isLast = (i == (int)cal.eras.size() - 1);
            if (isLast)
                ImGui::TextDisabled("ongoing");
            else
                ImGui::TextDisabled("day %d", cal.eras[i + 1].start_day - 1);

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("x")) { cal.eras.erase(cal.eras.begin() + i); changed = true; ImGui::PopID(); break; }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::SmallButton("+ Era")) {
        CalendarEra e;
        e.name      = "Era " + std::to_string(cal.eras.size() + 1);
        e.start_day = cal.eras.empty() ? 0 : cal.eras.back().start_day + 1;
        cal.eras.push_back(e);
        changed = true;
    }

    if (changed) WorldSerializer::save(*m_World);

    ImGui::Spacing();
    ImGui::Separator();
    if (cal.defined())
        ImGui::TextDisabled("Preview: %s", cal.formatDay(m_CurrentDay).c_str());
    ImGui::Spacing();
    if (ImGui::Button("Close", {100, 0})) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ── Map Tools dialog ───────────────────────────────────────────────────────────

void Application::renderMapToolsDialog() {
    if (m_ShowMapTools) {
        ImGui::OpenPopup("Map Import");
        m_ShowMapTools = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({540, 360}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Map Import", nullptr, ImGuiWindowFlags_NoResize)) return;

    static const char* kTabs[] = {"Color \xe2\x86\x92 Gray", "Project", "Unproject", "Hillshade"};
    if (ImGui::BeginTabBar("##mttabs")) {
        for (int t = 0; t < 4; ++t)
            if (ImGui::BeginTabItem(kTabs[t])) { m_MapToolsTab = t; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::Spacing();

    auto filePicker = [&](const char* label, char* buf, bool save) {
        ImGui::SetNextItemWidth(360);
        ImGui::InputText(label, buf, 1024);
        ImGui::SameLine();
        if (ImGui::Button(save ? "Save##fp" : "Open##fp")) {
            const char* f = save
                ? tinyfd_saveFileDialog("Output", buf, 0, nullptr, nullptr)
                : tinyfd_openFileDialog("Input",  buf, 0, nullptr, nullptr, 0);
            if (f) { strncpy_s(buf, 1024, f, _TRUNCATE); }
        }
    };
    filePicker("Input",  m_MtInPath,  false);
    filePicker("Output", m_MtOutPath, true);
    ImGui::Spacing();

    static const char* kProj[] = {"AEQD", "Ortho", "Gnomonic"};
    if (m_MapToolsTab == 1 || m_MapToolsTab == 2) {
        ImGui::Combo("Projection", &m_MapToolsProj, kProj, 3);
        ImGui::InputFloat("Center Lat (deg)", &m_MtLat0, 0, 0, "%.4f");
        ImGui::InputFloat("Center Lon (deg)", &m_MtLon0, 0, 0, "%.4f");
        if (m_MapToolsProj != 1)
            ImGui::InputFloat("Size (km)", &m_MtSizeKm, 0, 0, "%.1f");
    }
    if (m_MapToolsTab == 1)
        ImGui::InputInt("Resolution (px)", &m_MtResolution);
    if (m_MapToolsTab == 2) {
        ImGui::InputInt("Out Width",  &m_MtOutWidth);
        ImGui::InputInt("Out Height", &m_MtOutHeight);
    }
    if (m_MapToolsTab == 3) {
        ImGui::SliderFloat("Azimuth (deg)",  &m_MtAzimuth,  0, 360, "%.0f");
        ImGui::SliderFloat("Altitude (deg)", &m_MtAltitude, 0,  90, "%.0f");
        ImGui::SliderFloat("Z Scale",        &m_MtZScale,   0.1f, 20.f, "%.1f");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_MtStatus[0])
        ImGui::TextColored({1, 0.35f, 0.35f, 1}, "%s", m_MtStatus);

    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 116);
    if (ImGui::Button("Run", {52, 0})) {
        m_MtStatus[0] = '\0';
        auto proj = static_cast<MapImporter::Projection>(m_MapToolsProj);
        std::string err;
        switch (m_MapToolsTab) {
            case 0: err = MapImporter::colorToGray(m_MtInPath, m_MtOutPath); break;
            case 1: err = MapImporter::project  (m_MtInPath, m_MtOutPath, proj,
                              m_MtLat0, m_MtLon0, m_MtSizeKm, m_MtResolution); break;
            case 2: err = MapImporter::unproject(m_MtInPath, m_MtOutPath, proj,
                              m_MtLat0, m_MtLon0, m_MtSizeKm,
                              m_MtOutWidth, m_MtOutHeight); break;
            case 3: err = MapImporter::hillshade(m_MtInPath, m_MtOutPath,
                              m_MtAzimuth, m_MtAltitude, m_MtZScale); break;
        }
        snprintf(m_MtStatus, sizeof(m_MtStatus),
                 err.empty() ? "Done." : "Error: %s", err.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", {54, 0})) {
        m_MtStatus[0] = '\0';
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
