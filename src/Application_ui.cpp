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

    // ── Globe click (place / select / start drag) ─────────────────────────────
    if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        m_HoverLat > -999.0f) {

        if (m_EditMode == EditMode::Place && m_World) {
            auto& bodyEnts = m_World->body.entities;
            WorldEntity e;
            e.id      = m_World->body.id + "_e" + std::to_string(bodyEnts.size() + 1);
            e.name    = m_PlaceType == EntityType::City ? "New City" :
                        m_PlaceType == EntityType::Town ? "New Town" : "New POI";
            e.type    = m_PlaceType;
            e.lat_deg = m_HoverLat;
            e.lon_deg = m_HoverLon;
            m_CommandStack.execute(
                std::make_unique<PlaceEntityCommand>(bodyEnts, e));
            m_SelectedEntityId = e.id;
            WorldSerializer::save(*m_World);
            m_EditMode = EditMode::Navigate;

        } else if (m_EditMode == EditMode::Navigate && m_World) {
            auto&     body   = m_World->body;
            glm::vec3 camDir = glm::normalize(m_Camera.position());
            ImVec2    mpos   = ImGui::GetMousePos();
            float     best   = 14.0f;
            int       bestIdx = -1;
            for (int i = 0; i < (int)body.entities.size(); ++i) {
                const auto& e  = body.entities[i];
                glm::vec3   wp = latLonToWorld(e.lat_deg, e.lon_deg);
                if (glm::dot(wp, camDir) < 0.05f) continue;
                glm::vec2 sp = worldToScreen(wp);
                float     d  = glm::length(sp - glm::vec2(mpos.x, mpos.y));
                if (d < best) { best = d; bestIdx = i; }
            }
            m_DragEntityIdx = bestIdx;
            if (bestIdx >= 0) {
                m_DragOrigLat      = body.entities[bestIdx].lat_deg;
                m_DragOrigLon      = body.entities[bestIdx].lon_deg;
                m_SelectedEntityId = body.entities[bestIdx].id;
                m_SelectedOverlayId.clear();
                m_SelectedRoadNodeId.clear();
            } else {
                m_SelectedEntityId.clear();
                m_SelectedOverlayId.clear();
            }

        } else if ((m_EditMode == EditMode::RoadEdit || m_EditMode == EditMode::SeaEdit) && m_World) {
            bool       isSea  = (m_EditMode == EditMode::SeaEdit);
            RoadGraph& graph  = isSea ? m_World->body.sea_routes : m_World->body.roads;

            glm::vec3 camDir = glm::normalize(m_Camera.position());
            ImVec2    mpos   = ImGui::GetMousePos();
            float     best   = 16.0f;
            std::string bestNodeId;
            for (const auto& n : graph.nodes) {
                glm::vec3 wp = latLonToWorld(n.lat_deg, n.lon_deg);
                if (glm::dot(glm::normalize(wp), camDir) < 0.05f) continue;
                glm::vec2 sp = worldToScreen(wp);
                float d = glm::length(sp - glm::vec2(mpos.x, mpos.y));
                if (d < best) { best = d; bestNodeId = n.id; }
            }

            if (m_RoadSubMode == RoadSubMode::PlaceNode) {
                if (!bestNodeId.empty()) {
                    m_SelectedRoadNodeId = bestNodeId;
                    m_SelectedRoadIsSea  = isSea;
                    m_SelectedEntityId.clear();
                    m_SelectedOverlayId.clear();
                } else {
                    std::string newId;
                    const char* prefix = isSea ? "sr_" : "rn_";
                    for (int i = 1; ; ++i) {
                        newId = prefix + std::to_string(i);
                        if (!graph.findNode(newId)) break;
                    }
                    RoadNode n;
                    n.id      = newId;
                    n.lat_deg = m_HoverLat;
                    n.lon_deg = m_HoverLon;
                    graph.nodes.push_back(n);
                    m_SelectedRoadNodeId = newId;
                    m_SelectedRoadIsSea  = isSea;
                    m_SelectedEntityId.clear();
                    m_SelectedOverlayId.clear();
                    WorldSerializer::save(*m_World);
                }

            } else if (m_RoadSubMode == RoadSubMode::Connect && !bestNodeId.empty()) {
                if (m_RoadConnectFrom.empty()) {
                    m_RoadConnectFrom    = bestNodeId;
                    m_SelectedRoadNodeId = bestNodeId;
                    m_SelectedRoadIsSea  = isSea;
                    m_SelectedEntityId.clear();
                    m_SelectedOverlayId.clear();
                } else if (m_RoadConnectFrom != bestNodeId) {
                    const RoadNode* na = graph.findNode(m_RoadConnectFrom);
                    const RoadNode* nb = graph.findNode(bestNodeId);
                    if (na && nb) {
                        std::string edgeId;
                        const char* ep = isSea ? "se_" : "re_";
                        for (int i = 1; ; ++i) {
                            edgeId = ep + std::to_string(i);
                            bool used = false;
                            for (const auto& e : graph.edges)
                                if (e.id == edgeId) { used = true; break; }
                            if (!used) break;
                        }
                        RoadEdge edge;
                        edge.id          = edgeId;
                        edge.from_id     = m_RoadConnectFrom;
                        edge.to_id       = bestNodeId;
                        edge.distance_km = greatCircleKm(
                            na->lat_deg, na->lon_deg,
                            nb->lat_deg, nb->lon_deg,
                            (float)m_World->body.radius_km);
                        graph.edges.push_back(edge);
                        WorldSerializer::save(*m_World);
                    }
                    m_RoadConnectFrom    = bestNodeId;
                    m_SelectedRoadNodeId = bestNodeId;
                }
            }

        } else if (m_EditMode == EditMode::Measure && m_World) {
            if (!m_MeasureHasFirst) {
                m_MeasureFirstLat = m_HoverLat;
                m_MeasureFirstLon = m_HoverLon;
                m_MeasureHasFirst = true;
                m_MeasureResult   = "Click second point...";
            } else {
                float radius = (float)m_World->body.radius_km;
                float gcDist = greatCircleKm(m_MeasureFirstLat, m_MeasureFirstLon,
                                             m_HoverLat, m_HoverLon, radius);
                char buf[256];
                std::snprintf(buf, sizeof(buf), "Direct: %.0f km", gcDist);

                auto& roads = m_World->body.roads;
                if (roads.nodes.size() >= 2) {
                    auto nearestNode = [&](float lat, float lon) -> std::string {
                        float b2 = 1e9f; std::string id;
                        for (const auto& n : roads.nodes) {
                            float d = greatCircleKm(lat, lon, n.lat_deg, n.lon_deg, radius);
                            if (d < b2) { b2 = d; id = n.id; }
                        }
                        return id;
                    };
                    std::string fromId = nearestNode(m_MeasureFirstLat, m_MeasureFirstLon);
                    std::string toId   = nearestNode(m_HoverLat, m_HoverLon);
                    if (!fromId.empty() && fromId != toId) {
                        float pathKm = roads.shortestPath(fromId, toId);
                        char extra[128];
                        if (pathKm >= 0.0f)
                            std::snprintf(extra, sizeof(extra), " / Road: %.0f km", pathKm);
                        else
                            std::snprintf(extra, sizeof(extra), " / No road path");
                        strncat_s(buf, sizeof(buf), extra, _TRUNCATE);
                    }
                }
                m_MeasureResult   = buf;
                m_MeasureHasFirst = false;
            }
        }
    }

    // ── Live entity drag ──────────────────────────────────────────────────────
    if (!io.WantCaptureMouse && m_EditMode == EditMode::Navigate &&
        m_DragEntityIdx >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f) &&
        m_HoverLat > -999.0f && m_World &&
        m_DragEntityIdx < (int)m_World->body.entities.size()) {
        auto& e = m_World->body.entities[m_DragEntityIdx];
        e.lat_deg      = m_HoverLat;
        e.lon_deg      = m_HoverLon;
        m_DraggingEntity = true;
    }

    // ── Commit drag on mouse release ──────────────────────────────────────────
    if (!io.WantCaptureMouse && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        m_DraggingEntity) {
        if (m_World &&
            m_DragEntityIdx < (int)m_World->body.entities.size()) {
            auto& ents   = m_World->body.entities;
            auto& entity = ents[m_DragEntityIdx];
            float newLat = entity.lat_deg;
            float newLon = entity.lon_deg;
            entity.lat_deg = m_DragOrigLat;
            entity.lon_deg = m_DragOrigLon;
            m_CommandStack.execute(std::make_unique<MoveEntityCommand>(
                ents, entity.id, newLat, newLon, m_DragOrigLat, m_DragOrigLon));
            WorldSerializer::save(*m_World);
        }
        m_DraggingEntity = false;
        m_DragEntityIdx  = -1;
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
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_CommandStack.canUndo()))
            m_CommandStack.undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_CommandStack.canRedo()))
            m_CommandStack.redo();
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
            m_SelectedEntityId.clear();
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

    const auto& body = m_World->body;
    if (!body.entities.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Entities");
        static const char* eIcon[] = { "[C]", "[T]", "[P]" };
        for (const auto& e : body.entities) {
            char label[320];
            std::snprintf(label, sizeof(label), "%s %s##%s",
                          eIcon[(int)e.type], e.name.c_str(), e.id.c_str());
            if (ImGui::Selectable(label, e.id == m_SelectedEntityId)) {
                m_SelectedEntityId  = e.id;
                m_SelectedOverlayId.clear();
            }
        }
    }

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
            m_SelectedEntityId.clear();
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
                m_SelectedEntityId.clear();
            }
            ImGui::PopID();
        }
        if (swapA >= 0) {
            std::swap(b.overlays[swapA], b.overlays[swapB]);
            WorldSerializer::save(*m_World);
            reloadBodyOverlays();
        }
    }

    // ── Roads ─────────────────────────────────────────────────────────────────
    auto renderGraphSection = [&](const char* label, EditMode mode, bool isSea) {
        RoadGraph& g = isSea ? m_World->body.sea_routes : m_World->body.roads;
        ImGui::Separator();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();

        auto modeBtn = [&](const char* lbl, RoadSubMode sub) {
            bool active = (m_EditMode == mode && m_RoadSubMode == sub);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::SmallButton(lbl)) {
                if (m_EditMode == mode && m_RoadSubMode == sub) {
                    m_EditMode = EditMode::Navigate;
                    m_RoadConnectFrom.clear();
                } else {
                    m_EditMode    = mode;
                    m_RoadSubMode = sub;
                    m_RoadConnectFrom.clear();
                }
            }
            if (active) ImGui::PopStyleColor();
            ImGui::SameLine();
        };
        modeBtn(isSea ? "+ Node##sr" : "+ Node##rd",  RoadSubMode::PlaceNode);
        modeBtn(isSea ? "Connect##sr" : "Connect##rd", RoadSubMode::Connect);
        ImGui::NewLine();

        if (m_EditMode == mode) {
            if (m_RoadSubMode == RoadSubMode::Connect) {
                if (m_RoadConnectFrom.empty())
                    ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Click first node");
                else
                    ImGui::TextColored({.5f, 1.f, .5f, 1.f}, "Click second node");
            } else {
                ImGui::TextColored({1.f, .9f, .2f, 1.f}, "Click to place / click node to select");
            }
        }
        if (!g.nodes.empty())
            ImGui::TextDisabled("%d nodes, %d edges", (int)g.nodes.size(), (int)g.edges.size());
    };

    renderGraphSection("Roads",      EditMode::RoadEdit, false);
    renderGraphSection("Sea Routes", EditMode::SeaEdit,  true);
}

// ── Panels ────────────────────────────────────────────────────────────────────

void Application::renderPanels() {
    ImGui::Begin("World");
    renderWorldPanel();
    ImGui::End();

    // ── Inspector ─────────────────────────────────────────────────────────────
    ImGui::Begin("Inspector");

    WorldEntity* ent = nullptr;
    if (m_World && !m_SelectedEntityId.empty()) {
        auto& ents = m_World->body.entities;
        auto  it   = std::find_if(ents.begin(), ents.end(),
                        [&](const WorldEntity& e) { return e.id == m_SelectedEntityId; });
        if (it != ents.end()) ent = &(*it);
    }

    if (ent) {
        static char        nameEdit[256]  = {};
        static char        mediaEdit[512] = {};
        static std::string lastId;
        if (lastId != m_SelectedEntityId) {
            lastId = m_SelectedEntityId;
            strncpy_s(nameEdit,  sizeof(nameEdit),  ent->name.c_str(),      _TRUNCATE);
            strncpy_s(mediaEdit, sizeof(mediaEdit), ent->media_ref.c_str(), _TRUNCATE);
        }

        static const char* typeLabels[] = { "City", "Town", "POI" };
        ImGui::Text("%s", typeLabels[(int)ent->type]);
        ImGui::Separator();

        if (ImGui::InputText("Name##ent", nameEdit, sizeof(nameEdit)))
            ent->name = nameEdit;
        if (ImGui::IsItemDeactivatedAfterEdit())
            WorldSerializer::save(*m_World);

        int typeIdx = (int)ent->type;
        if (ImGui::Combo("Type##ent", &typeIdx, typeLabels, 3)) {
            ent->type = static_cast<EntityType>(typeIdx);
            WorldSerializer::save(*m_World);
        }

        ImGui::LabelText("Lat", "%.4f\xc2\xb0", ent->lat_deg);
        ImGui::LabelText("Lon", "%.4f\xc2\xb0", ent->lon_deg);

        ImGui::Separator();
        ImGui::TextUnformatted("Lifespan");
        {
            // Born day
            bool hasBorn = ent->born_day.has_value();
            if (ImGui::Checkbox("Born##cb", &hasBorn)) {
                ent->born_day = hasBorn ? std::optional<int>(m_CurrentDay) : std::nullopt;
                WorldSerializer::save(*m_World);
            }
            if (hasBorn) {
                ImGui::SameLine();
                int bday = *ent->born_day;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputInt("##born", &bday)) {
                    ent->born_day = bday;
                    WorldSerializer::save(*m_World);
                }
                if (m_World->calendar.defined())
                    ImGui::TextDisabled("  %s", m_World->calendar.formatDay(*ent->born_day).c_str());
            }

            // Died day
            bool hasDied = ent->died_day.has_value();
            if (ImGui::Checkbox("Died##cb", &hasDied)) {
                ent->died_day = hasDied ? std::optional<int>(m_CurrentDay) : std::nullopt;
                WorldSerializer::save(*m_World);
            }
            if (hasDied) {
                ImGui::SameLine();
                int dday = *ent->died_day;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputInt("##died", &dday)) {
                    ent->died_day = dday;
                    WorldSerializer::save(*m_World);
                }
                if (m_World->calendar.defined())
                    ImGui::TextDisabled("  %s", m_World->calendar.formatDay(*ent->died_day).c_str());
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Lore file");

        if (ImGui::InputText("##media", mediaEdit, sizeof(mediaEdit)))
            ent->media_ref = mediaEdit;
        if (ImGui::IsItemDeactivatedAfterEdit())
            WorldSerializer::save(*m_World);

        bool hasMedia = !ent->media_ref.empty();
        if (!hasMedia) ImGui::BeginDisabled();
        if (ImGui::Button("Open##lore")) {
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open",
                std::filesystem::path(ent->media_ref).wstring().c_str(),
                nullptr, nullptr, SW_SHOW);
#endif
        }
        if (!hasMedia) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Create##lore")) {
            std::filesystem::path p =
                m_World->rootPath / "media" / (ent->id + ".md");
            if (!std::filesystem::exists(p)) {
                std::ofstream f(p);
                f << "# " << ent->name << "\n\n";
            }
            ent->media_ref = p.string();
            strncpy_s(mediaEdit, sizeof(mediaEdit), ent->media_ref.c_str(), _TRUNCATE);
            WorldSerializer::save(*m_World);
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", p.wstring().c_str(),
                          nullptr, nullptr, SW_SHOW);
#endif
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("Delete", {-1, 0})) {
            std::string idToDelete = ent->id;
            m_CommandStack.execute(
                std::make_unique<DeleteEntityCommand>(
                    m_World->body.entities, idToDelete));
            m_SelectedEntityId.clear();
            lastId.clear();
            WorldSerializer::save(*m_World);
        }
        ImGui::PopStyleColor(3);

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

    } else if (m_World && !m_SelectedRoadNodeId.empty()) {
        RoadGraph& g = m_SelectedRoadIsSea ? m_World->body.sea_routes : m_World->body.roads;
        RoadNode*  node = g.findNode(m_SelectedRoadNodeId);
        if (node) {
            ImGui::Text(m_SelectedRoadIsSea ? "Sea Route Node" : "Road Node");
            ImGui::Separator();
            ImGui::LabelText("ID",  "%s", node->id.c_str());
            ImGui::LabelText("Lat", "%.4f\xc2\xb0", node->lat_deg);
            ImGui::LabelText("Lon", "%.4f\xc2\xb0", node->lon_deg);

            ImGui::Spacing();
            ImGui::TextUnformatted("Connections");
            bool any = false;
            for (const auto& e : g.edges) {
                if (e.from_id != node->id && e.to_id != node->id) continue;
                const std::string& otherId = (e.from_id == node->id) ? e.to_id : e.from_id;
                ImGui::TextDisabled("\xe2\x86\x92 %s  (%.0f km)", otherId.c_str(), e.distance_km);
                any = true;
            }
            if (!any) ImGui::TextDisabled("(none)");

            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
            if (ImGui::Button("Delete Node", {-1, 0})) {
                std::string delId = m_SelectedRoadNodeId;
                g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(),
                    [&](const RoadEdge& e){ return e.from_id == delId || e.to_id == delId; }),
                    g.edges.end());
                g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
                    [&](const RoadNode& n){ return n.id == delId; }),
                    g.nodes.end());
                m_SelectedRoadNodeId.clear();
                if (m_RoadConnectFrom == delId) m_RoadConnectFrom.clear();
                WorldSerializer::save(*m_World);
            }
            ImGui::PopStyleColor(3);
        } else {
            m_SelectedRoadNodeId.clear();
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

    ImGui::Begin("Layers");
    if (m_World) {
        ImGui::SeparatorText("Visibility");
        ImGui::Checkbox("Land Roads",  &m_ShowRoads);
        ImGui::Checkbox("Sea Routes",  &m_ShowSeaRoutes);

        ImGui::SeparatorText("Measure");
        bool measActive = (m_EditMode == EditMode::Measure);
        if (ImGui::Checkbox("Measure tool", &measActive)) {
            m_EditMode        = measActive ? EditMode::Measure : EditMode::Navigate;
            m_MeasureHasFirst = false;
            m_MeasureResult.clear();
        }
        if (m_EditMode == EditMode::Measure) {
            if (!m_MeasureResult.empty() && m_MeasureResult != "Click second point...")
                ImGui::TextColored({1.f, .9f, .3f, 1.f}, "%s", m_MeasureResult.c_str());
            else if (m_MeasureHasFirst)
                ImGui::TextDisabled("Click second point");
            else
                ImGui::TextDisabled("Click first point");
        }
    }
    ImGui::End();

    ImGui::Begin("Timeline");
    if (m_World) {
        const auto& cal = m_World->calendar;

        // Formatted date
        std::string dateStr = cal.formatDay(m_CurrentDay);
        ImGui::TextUnformatted(dateStr.c_str());

        // Scrubber — DragInt allows unlimited range
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

// ── Map Tools dialog ───────────────────────────────────────────────────────────

// ── Calendar definition dialog ─────────────────────────────────────────────────

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

    // ── Epoch name ────────────────────────────────────────────────────────────
    {
        char buf[128];
        strncpy_s(buf, sizeof(buf), cal.epoch_name.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("Epoch name", buf, sizeof(buf)))
            cal.epoch_name = buf;
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    }

    // ── Months ────────────────────────────────────────────────────────────────
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

    // ── Weekdays ──────────────────────────────────────────────────────────────
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

    // ── Eras ──────────────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::SeparatorText("Eras");
    ImGui::TextDisabled("Contiguous time periods sorted by start day.");

    // Keep sorted so the display and formatDay lookup are both consistent
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
                ImGui::TextDisabled("-\xe2\x88\x9e");  // −∞ (UTF-8)
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

    // ── Preview + close ───────────────────────────────────────────────────────
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
