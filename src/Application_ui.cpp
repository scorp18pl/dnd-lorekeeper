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
#include "command/DeleteBodyCommand.h"

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

    if (m_ViewMode == ViewMode::SolarSystem) {
        // ── Solar system hover / click ────────────────────────────────────────
        m_HoverBodyIdx = -1;
        if (!io.WantCaptureMouse && m_World) {
            auto   infos = computeSolarPositions();
            ImVec2 mpos  = ImGui::GetMousePos();
            m_HoverBodyIdx = castRaySolarSystem(mpos.x, mpos.y, infos);

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_HoverBodyIdx >= 0) {
                m_ActiveBodyIdx     = m_HoverBodyIdx;
                m_ViewMode          = ViewMode::Planet;
                m_LastActiveBodyIdx = -2;
                m_SelectedEntityId.clear();
                m_SelectedOverlayId.clear();
            }
        }
    } else {
        // ── Planet hover ray cast ─────────────────────────────────────────────
        m_HoverLat = m_HoverLon = -1000.0f;
        m_HoverCellId = -1;
        if (!io.WantCaptureMouse) {
            ImVec2 pos = ImGui::GetMousePos();
            if (auto hit = castRay(pos.x, pos.y)) {
                m_HoverLat = hit->x;
                m_HoverLon = hit->y;
                if (m_ShowPoliticalMap && m_PolMap.hasGrid(0))
                    m_HoverCellId = m_PolMap.findCellNearest(0,
                        latLonToWorld(m_HoverLat, m_HoverLon));
            }
        }
        m_PolMap.setHoverCell(0, m_PoliticalPaintMode ? m_HoverCellId : -1);

        // ── Paint mode: drag-paint cells ──────────────────────────────────────
        if (m_PoliticalPaintMode && !io.WantCaptureMouse &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            m_HoverCellId >= 0 && m_World && m_ActiveBodyIdx >= 0) {

            auto& ownership = m_World->bodies[m_ActiveBodyIdx].cell_ownership;
            const std::string prev = [&]() -> std::string {
                auto it = ownership.find(m_HoverCellId);
                return it != ownership.end() ? it->second : "";
            }();
            const std::string next = (m_PoliticalEraseMode || m_ActivePolEntityId.empty())
                                     ? "" : m_ActivePolEntityId;

            if (prev != next) {
                if (next.empty())
                    ownership.erase(m_HoverCellId);
                else
                    ownership[m_HoverCellId] = next;
                syncPoliticalRenderer();
                m_PoliticalDirty = true;
            }
        }
        // Save once per stroke on mouse release
        if (m_PoliticalDirty && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && m_World)
            { WorldSerializer::save(*m_World); m_PoliticalDirty = false; }

        // ── Globe click (place / select / start drag) ────────────────────────
        if (!m_PoliticalPaintMode &&
            !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            m_HoverLat > -999.0f) {

            if (m_EditMode == EditMode::Place && m_World && m_ActiveBodyIdx >= 0) {
                auto& bodyEnts = m_World->bodies[m_ActiveBodyIdx].entities;
                WorldEntity e;
                e.id      = m_World->bodies[m_ActiveBodyIdx].id + "_e" +
                            std::to_string(bodyEnts.size() + 1);
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

            } else if (m_EditMode == EditMode::Navigate && m_World && m_ActiveBodyIdx >= 0) {
                auto&     body   = m_World->bodies[m_ActiveBodyIdx];
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
                    m_SelectedCellId   = -1;
                } else if (m_ShowPoliticalMap && m_HoverCellId >= 0) {
                    m_SelectedCellId   = m_HoverCellId;
                    m_SelectedEntityId.clear();
                    m_SelectedOverlayId.clear();
                } else {
                    m_SelectedEntityId.clear();
                    m_SelectedOverlayId.clear();
                    m_SelectedCellId   = -1;
                }
            }
        }

        // ── Live entity drag ──────────────────────────────────────────────────
        if (!io.WantCaptureMouse && m_EditMode == EditMode::Navigate &&
            m_DragEntityIdx >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f) &&
            m_HoverLat > -999.0f && m_World && m_ActiveBodyIdx >= 0 &&
            m_DragEntityIdx < (int)m_World->bodies[m_ActiveBodyIdx].entities.size()) {
            auto& e = m_World->bodies[m_ActiveBodyIdx].entities[m_DragEntityIdx];
            e.lat_deg      = m_HoverLat;
            e.lon_deg      = m_HoverLon;
            m_DraggingEntity = true;
        }

        // ── Commit drag on mouse release ──────────────────────────────────────
        if (!io.WantCaptureMouse && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            m_DraggingEntity) {
            if (m_World && m_ActiveBodyIdx >= 0 &&
                m_DragEntityIdx < (int)m_World->bodies[m_ActiveBodyIdx].entities.size()) {
                auto& ents   = m_World->bodies[m_ActiveBodyIdx].entities;
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
    renderAddBodyDialog();
    renderDeleteBodyDialog();
    renderMapToolsDialog();
    renderPanels();

    if (m_ViewMode == ViewMode::SolarSystem && m_World)
        renderSolarSystemOverlay();
    else
        renderLabels();

    renderHUD();

    m_TextRenderer.beginFrame(m_Window.width(), m_Window.height());
    // (text quads were batched inside renderLabels / renderSolarSystemOverlay)
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
        ImGui::Separator();
        bool inSS = (m_ViewMode == ViewMode::SolarSystem);
        if (ImGui::MenuItem("Solar System", nullptr, inSS, m_World.has_value()))
            m_ViewMode = inSS ? ViewMode::Planet : ViewMode::SolarSystem;
        ImGui::Separator();
        if (ImGui::MenuItem("Realistic Scale", nullptr, m_RealisticScale))
            m_RealisticScale = !m_RealisticScale;
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
            m_World             = w;
            m_ActiveBodyIdx     = -1;
            m_LastActiveBodyIdx = -2;

            m_SelectedEntityId.clear();
            m_CommandStack.clear();
            m_ViewMode          = ViewMode::Planet;
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

// ── Add Body dialog ───────────────────────────────────────────────────────────

void Application::renderAddBodyDialog() {
    if (m_OpenAddBodyDialog) {
        ImGui::OpenPopup("Add Body");
        m_OpenAddBodyDialog    = false;
        m_NewBodyOrbitalRadius = 1.0f;
        m_NewBodyParentIdx = -1;
        if (m_NewBodyType == 2 && m_World) { // Moon
            for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
                if (m_World->bodies[i].type == BodyType::Planet) {
                    m_NewBodyParentIdx = i;
                    break;
                }
            }
        }
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({420.0f, 0.0f}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Add Body", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("Name", m_NewBodyName, sizeof(m_NewBodyName));

    static const char* bodyTypeLabels[] = { "Star", "Planet", "Moon" };
    int prevType = m_NewBodyType;
    ImGui::Combo("Type", &m_NewBodyType, bodyTypeLabels, 3);
    if (m_NewBodyType != prevType) {
        m_NewBodyParentIdx = -1;
        if (m_NewBodyType == 2 && m_World) {
            for (int i = 0; i < (int)m_World->bodies.size(); ++i)
                if (m_World->bodies[i].type == BodyType::Planet) { m_NewBodyParentIdx = i; break; }
        }
    }

    BodyType requiredParentType = (m_NewBodyType == 1) ? BodyType::Star : BodyType::Planet;
    bool needsParent = (m_NewBodyType != 0);

    if (needsParent && m_World) {
        if (m_NewBodyParentIdx >= 0 &&
            m_World->bodies[m_NewBodyParentIdx].type != requiredParentType)
            m_NewBodyParentIdx = -1;

        const char* parentLabel = (m_NewBodyParentIdx < 0)
            ? "-- select --"
            : m_World->bodies[m_NewBodyParentIdx].name.c_str();
        ImGui::Text("Parent");
        ImGui::SameLine();
        if (ImGui::BeginCombo("##parent", parentLabel)) {
            for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
                if (m_World->bodies[i].type != requiredParentType) continue;
                bool sel = (i == m_NewBodyParentIdx);
                if (ImGui::Selectable(m_World->bodies[i].name.c_str(), sel))
                    m_NewBodyParentIdx = i;
            }
            ImGui::EndCombo();
        }

        if (m_NewBodyParentIdx >= 0)
            ImGui::SliderFloat("Orbital Radius (AU)", &m_NewBodyOrbitalRadius,
                               0.01f, 50.0f, "%.3f AU");
    }

    ImGui::Separator();

    bool canAdd = m_NewBodyName[0] != '\0' &&
                  (!needsParent || m_NewBodyParentIdx >= 0);
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add", {120, 0})) {
        CelestialBody b;
        b.id   = std::to_string(m_World->bodies.size() + 1);
        b.name = m_NewBodyName;
        b.type = static_cast<BodyType>(m_NewBodyType);
        if (m_NewBodyParentIdx >= 0) {
            b.parent_id         = m_World->bodies[m_NewBodyParentIdx].id;
            b.orbital_radius_au = (double)m_NewBodyOrbitalRadius;
        }
        m_World->bodies.push_back(b);
        m_ActiveBodyIdx = static_cast<int>(m_World->bodies.size()) - 1;
        m_SelectedEntityId.clear();
        m_SelectedOverlayId.clear();
        WorldSerializer::save(*m_World);
        std::snprintf(m_StatusMsg, sizeof(m_StatusMsg), "Added body: %s", b.name.c_str());
        ImGui::CloseCurrentPopup();
    }
    if (!canAdd) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", {120, 0})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ── Delete Body dialog ────────────────────────────────────────────────────────

void Application::renderDeleteBodyDialog() {
    if (m_OpenDeleteBodyDialog) {
        ImGui::OpenPopup("Delete Body");
        m_OpenDeleteBodyDialog = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380.0f, 0.0f}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Delete Body", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    if (!m_World || m_DeleteBodyIdx < 0 ||
        m_DeleteBodyIdx >= (int)m_World->bodies.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const auto& b = m_World->bodies[m_DeleteBodyIdx];
    ImGui::TextWrapped("This will permanently delete \"%s\" and all its entities.",
                       b.name.c_str());
    ImGui::TextWrapped("Type the body name to confirm:");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##confirm", m_DeleteBodyConfirm, sizeof(m_DeleteBodyConfirm));

    ImGui::Spacing();
    ImGui::Separator();

    bool nameMatches = (b.name == m_DeleteBodyConfirm);
    if (!nameMatches) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
    if (ImGui::Button("Delete", {120, 0})) {
        m_CommandStack.execute(
            std::make_unique<DeleteBodyCommand>(m_World->bodies, m_DeleteBodyIdx));
        if (m_ActiveBodyIdx >= (int)m_World->bodies.size())
            m_ActiveBodyIdx = (int)m_World->bodies.size() - 1;
        m_SelectedEntityId.clear();
        m_SelectedOverlayId.clear();
        m_LastActiveBodyIdx = -2;
        WorldSerializer::save(*m_World);
        std::snprintf(m_StatusMsg, sizeof(m_StatusMsg), "Deleted body: %s", b.name.c_str());
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    if (!nameMatches) ImGui::EndDisabled();

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

    bool inSS = (m_ViewMode == ViewMode::SolarSystem);
    if (ImGui::SmallButton(inSS ? "Planet View" : "Solar System"))
        m_ViewMode = inSS ? ViewMode::Planet : ViewMode::SolarSystem;
    ImGui::SameLine();
    ImGui::Text("%s", m_World->name.c_str());
    ImGui::TextDisabled("%s", m_World->rootPath.string().c_str());
    ImGui::Separator();

    ImGui::TextUnformatted("Bodies");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##body")) m_OpenAddBodyDialog = true;

    static const char* bodyIcon[] = { "[*]", "[o]", "[.]" };

    if (m_World->bodies.empty()) {
        ImGui::TextDisabled("  No bodies. Use + to add one.");
    } else {
        std::unordered_map<std::string, int> idxOf;
        for (int i = 0; i < (int)m_World->bodies.size(); ++i)
            idxOf[m_World->bodies[i].id] = i;

        for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
            const auto& b = m_World->bodies[i];
            int         depth = 0;
            std::string pid   = b.parent_id;
            while (!pid.empty() && depth < 5) {
                auto it = idxOf.find(pid);
                if (it == idxOf.end()) break;
                pid = m_World->bodies[it->second].parent_id;
                ++depth;
            }
            if (depth > 0) ImGui::Indent((float)depth * 12.0f);

            char label[320];
            std::snprintf(label, sizeof(label), "%s %s##body%d",
                          bodyIcon[(int)b.type], b.name.c_str(), i);

            if (ImGui::Selectable(label, m_ActiveBodyIdx == i)) {
                m_ActiveBodyIdx = i;
                m_SelectedEntityId.clear();
                m_SelectedOverlayId.clear();
                if (m_ViewMode == ViewMode::SolarSystem) {
                    m_ViewMode          = ViewMode::Planet;
                    m_LastActiveBodyIdx = -2;
                }
            }

            if (depth > 0) ImGui::Unindent((float)depth * 12.0f);
        }
    }

    if (m_ViewMode == ViewMode::Planet && m_ActiveBodyIdx >= 0) {
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
    }

    if (m_ViewMode == ViewMode::Planet &&
        m_ActiveBodyIdx >= 0 && m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& body = m_World->bodies[m_ActiveBodyIdx];
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
    }

    if (m_ViewMode == ViewMode::Planet &&
        m_ActiveBodyIdx >= 0 && m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        auto& body = m_World->bodies[m_ActiveBodyIdx];
        ImGui::Separator();
        ImGui::TextUnformatted("Overlays");
        ImGui::SameLine();
        if (ImGui::SmallButton("+##ov")) {
            RegionOverlay ov;
            ov.id   = body.id + "_ov" + std::to_string(body.overlays.size() + 1);
            ov.name = "New Overlay";
            body.overlays.push_back(ov);
            m_SelectedOverlayId = ov.id;
            m_SelectedEntityId.clear();
            WorldSerializer::save(*m_World);
            reloadBodyOverlays();
        }
        int swapA = -1, swapB = -1;
        for (int i = 0; i < (int)body.overlays.size(); ++i) {
            auto& ov = body.overlays[i];
            ImGui::PushID(i);

            bool vis = ov.visible;
            if (ImGui::Checkbox("##ovis", &vis)) {
                ov.visible = vis;
                WorldSerializer::save(*m_World);
                reloadBodyOverlays();
            }
            ImGui::SameLine();

            bool canUp   = i > 0;
            bool canDown = i < (int)body.overlays.size() - 1;
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
            std::swap(body.overlays[swapA], body.overlays[swapB]);
            WorldSerializer::save(*m_World);
            reloadBodyOverlays();
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

    WorldEntity* ent = nullptr;
    if (m_World && m_ActiveBodyIdx >= 0 && !m_SelectedEntityId.empty()) {
        auto& ents = m_World->bodies[m_ActiveBodyIdx].entities;
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
                    m_World->bodies[m_ActiveBodyIdx].entities, idToDelete));
            m_SelectedEntityId.clear();
            lastId.clear();
            WorldSerializer::save(*m_World);
        }
        ImGui::PopStyleColor(3);

    } else if (m_World && m_ActiveBodyIdx >= 0 &&
               m_ActiveBodyIdx < (int)m_World->bodies.size() &&
               !m_SelectedOverlayId.empty()) {
        auto& body = m_World->bodies[m_ActiveBodyIdx];
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
            ImGui::TextUnformatted("Heightmap");
            std::string hmOvDisplay = ov.heightmap_path.empty()
                ? "(none)" : std::filesystem::path(ov.heightmap_path).filename().string();
            ImGui::TextDisabled("%s", hmOvDisplay.c_str());

            if (ImGui::Button("Browse...##ovhm")) {
                static const char* hmOvFilters[] = {"*.jpg", "*.jpeg", "*.png"};
                const char* picked = tinyfd_openFileDialog(
                    "Select overlay heightmap", nullptr, 3, hmOvFilters, "Image files", 0);
                if (picked) {
                    ov.heightmap_path = picked;
                    WorldSerializer::save(*m_World);
                    reloadBodyOverlays();
                }
            }
            if (!ov.heightmap_path.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Clear##ovhm")) {
                    ov.heightmap_path.clear();
                    WorldSerializer::save(*m_World);
                    reloadBodyOverlays();
                }
                if (ImGui::SliderFloat("Scale##ovhm", &ov.height_scale, 0.0f, 0.2f, "%.3f"))
                    WorldSerializer::save(*m_World);
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

    } else if (m_World && m_ActiveBodyIdx >= 0 &&
               m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        auto& b = m_World->bodies[m_ActiveBodyIdx];
        ImGui::Text("%s", b.name.c_str());
        ImGui::Separator();
        static const char* bodyTypeLabels[] = { "Star", "Planet", "Moon" };
        ImGui::LabelText("Type",           "%s", bodyTypeLabels[(int)b.type]);
        ImGui::LabelText("Radius",         "%.0f km", b.radius_km);
        ImGui::LabelText("Axial tilt",     "%.1f\xc2\xb0", b.axial_tilt_deg);
        ImGui::LabelText("Rotation",       "%.2f h", b.rotation_h);
        ImGui::LabelText("Orbital period", "%.2f days", b.orbital_period_d);
        if (!b.parent_id.empty())
            ImGui::LabelText("Orbital radius", "%.3f AU", b.orbital_radius_au);

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
                m_LastActiveBodyIdx = -2;
            }
        }
        if (!b.texture_path.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Clear##tex")) {
                b.texture_path.clear();
                WorldSerializer::save(*m_World);
                m_LastActiveBodyIdx = -2;
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Heightmap");
        std::string hmDisplay = b.heightmap_path.empty()
            ? "(none)" : std::filesystem::path(b.heightmap_path).filename().string();
        ImGui::TextDisabled("%s", hmDisplay.c_str());

        if (ImGui::Button("Browse...##hm")) {
            static const char* hmFilters[] = { "*.jpg", "*.jpeg", "*.png" };
            const char* picked = tinyfd_openFileDialog(
                "Select heightmap", nullptr, 3, hmFilters, "Image files", 0);
            if (picked) {
                b.heightmap_path = picked;
                WorldSerializer::save(*m_World);
                m_LastActiveBodyIdx = -2;
            }
        }
        if (!b.heightmap_path.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Clear##hm")) {
                b.heightmap_path.clear();
                WorldSerializer::save(*m_World);
                m_LastActiveBodyIdx = -2;
            }
            float hs = b.height_scale;
            if (ImGui::SliderFloat("Scale##hm", &hs, 0.0f, 0.2f, "%.3f")) {
                b.height_scale = hs;
                WorldSerializer::save(*m_World);
            }
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.5f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("Delete Body...", {-1, 0})) {
            m_DeleteBodyIdx        = m_ActiveBodyIdx;
            m_DeleteBodyConfirm[0] = '\0';
            m_OpenDeleteBodyDialog = true;
        }
        ImGui::PopStyleColor(3);

    } else if (m_SelectedCellId >= 0 && m_ShowPoliticalMap && m_PolMap.hasGrid(0) &&
               m_World && m_ActiveBodyIdx >= 0) {
        // ── Cell inspector ────────────────────────────────────────────────────
        const auto& cell = m_PolMap.grid(0)->cells()[m_SelectedCellId];
        ImGui::Text("Cell  #%d", m_SelectedCellId);
        ImGui::TextDisabled("%s  (%d sides)",
            cell.num_sides == 5 ? "Pentagon" : "Hexagon", cell.num_sides);
        ImGui::Separator();
        ImGui::LabelText("Lat", "%.2f\xc2\xb0", cell.lat);
        ImGui::LabelText("Lon", "%.2f\xc2\xb0", cell.lon);
        ImGui::Separator();

        const auto& own = m_World->bodies[m_ActiveBodyIdx].cell_ownership;
        auto ownerIt = own.find(m_SelectedCellId);
        if (ownerIt != own.end() && !ownerIt->second.empty()) {
            const PoliticalEntity* pe = nullptr;
            for (const auto& p : m_World->political_entities)
                if (p.id == ownerIt->second) { pe = &p; break; }
            if (pe) {
                ImGui::ColorButton("##cellowner",
                    ImVec4(pe->color.r, pe->color.g, pe->color.b, 1.0f),
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                    ImVec2(14, 14));
                ImGui::SameLine();
                ImGui::TextUnformatted(pe->name.c_str());
                if (!pe->type.empty())
                    ImGui::TextDisabled("%s", pe->type.c_str());
            } else {
                ImGui::TextDisabled("Unknown entity");
            }
        } else {
            ImGui::TextDisabled("Unowned");
        }
    } else {
        ImGui::TextDisabled("Nothing selected.");
    }

    ImGui::End();

    ImGui::Begin("Layers");
    if (m_ViewMode == ViewMode::Planet) {
        if (ImGui::CollapsingHeader("Political Map", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show##polmap", &m_ShowPoliticalMap);
            ImGui::SameLine();
            if (ImGui::Checkbox("Paint", &m_PoliticalPaintMode) && !m_PoliticalPaintMode)
                m_PolMap.setHoverCell(0, -1);

            ImGui::Separator();

            bool hasWorld = m_World.has_value() && m_ActiveBodyIdx >= 0;

            if (hasWorld) {
                if (ImGui::Button("+ New")) {
                    PoliticalEntity pe;
                    pe.id    = PoliticalMapLayer::makeEntityId(m_World->political_entities);
                    pe.color = { 0.8f, 0.3f, 0.3f, 0.7f };
                    std::string base = "New Entity";
                    pe.name = base;
                    for (int n = 2; ; ++n) {
                        bool clash = false;
                        for (const auto& x : m_World->political_entities)
                            if (x.name == pe.name) { clash = true; break; }
                        if (!clash) break;
                        pe.name = base + " " + std::to_string(n);
                    }
                    m_World->political_entities.push_back(pe);
                    m_ActivePolEntityId = pe.id;
                    WorldSerializer::save(*m_World);
                }
                ImGui::SameLine();
                bool canDelete = !m_ActivePolEntityId.empty();
                if (!canDelete) ImGui::BeginDisabled();
                if (ImGui::Button("- Delete")) {
                    auto& ownership = m_World->bodies[m_ActiveBodyIdx].cell_ownership;
                    for (auto it = ownership.begin(); it != ownership.end(); ) {
                        if (it->second == m_ActivePolEntityId) it = ownership.erase(it);
                        else ++it;
                    }
                    auto& ents = m_World->political_entities;
                    ents.erase(std::remove_if(ents.begin(), ents.end(),
                        [&](const PoliticalEntity& pe){ return pe.id == m_ActivePolEntityId; }),
                        ents.end());
                    m_ActivePolEntityId.clear();
                    syncPoliticalRenderer();
                    WorldSerializer::save(*m_World);
                }
                if (!canDelete) ImGui::EndDisabled();

                ImGui::BeginChild("##entlist", ImVec2(0, 130), true);
                for (auto& pe : m_World->political_entities) {
                    bool selected = (pe.id == m_ActivePolEntityId);
                    ImVec4 swatchCol { pe.color.r, pe.color.g, pe.color.b, 1.0f };
                    ImGui::ColorButton(("##sw" + pe.id).c_str(), swatchCol,
                                       ImGuiColorEditFlags_NoTooltip |
                                       ImGuiColorEditFlags_NoBorder, ImVec2(14, 14));
                    ImGui::SameLine();
                    if (ImGui::Selectable((pe.name + "##" + pe.id).c_str(), selected))
                        m_ActivePolEntityId = pe.id;
                }
                ImGui::EndChild();

                PoliticalEntity* activePe = nullptr;
                for (auto& pe : m_World->political_entities)
                    if (pe.id == m_ActivePolEntityId) { activePe = &pe; break; }

                if (activePe) {
                    ImGui::Separator();

                    static char        nameBuf[256]   = {};
                    static std::string nameLastId;
                    static std::string nameBeforeEdit;
                    if (nameLastId != activePe->id) {
                        nameLastId = activePe->id;
                        strncpy_s(nameBuf, activePe->name.c_str(), sizeof(nameBuf) - 1);
                    }
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##pename", nameBuf, sizeof(nameBuf)))
                        activePe->name = nameBuf;
                    if (ImGui::IsItemActivated())
                        nameBeforeEdit = activePe->name;
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        bool clash = false;
                        for (const auto& pe : m_World->political_entities)
                            if (pe.id != activePe->id && pe.name == activePe->name)
                                { clash = true; break; }
                        if (clash) {
                            activePe->name = nameBeforeEdit;
                            strncpy_s(nameBuf, nameBeforeEdit.c_str(), sizeof(nameBuf) - 1);
                        } else {
                            WorldSerializer::save(*m_World);
                        }
                    }

                    static char typeBuf[128]   = {};
                    static std::string typeLastId;
                    if (typeLastId != activePe->id) {
                        typeLastId = activePe->id;
                        strncpy_s(typeBuf, activePe->type.c_str(), sizeof(typeBuf) - 1);
                    }
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("Type##petype", typeBuf, sizeof(typeBuf)))
                        activePe->type = typeBuf;
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        WorldSerializer::save(*m_World);

                    if (ImGui::ColorEdit4("Color##pecol", &activePe->color.x,
                                          ImGuiColorEditFlags_NoInputs |
                                          ImGuiColorEditFlags_AlphaBar)) {
                        syncPoliticalRenderer();
                        WorldSerializer::save(*m_World);
                    }

                    std::string liegeLabel = "None";
                    for (const auto& pe : m_World->political_entities)
                        if (pe.id == activePe->liege_id) { liegeLabel = pe.name; break; }
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("Liege##peliege", liegeLabel.c_str())) {
                        if (ImGui::Selectable("None##liegenone", activePe->liege_id.empty())) {
                            activePe->liege_id.clear();
                            syncPoliticalRenderer();
                            WorldSerializer::save(*m_World);
                        }
                        for (const auto& pe : m_World->political_entities) {
                            if (pe.id == activePe->id) continue;
                            bool isSel = (pe.id == activePe->liege_id);
                            if (ImGui::Selectable((pe.name + "##lie" + pe.id).c_str(), isSel)) {
                                activePe->liege_id = pe.id;
                                syncPoliticalRenderer();
                                WorldSerializer::save(*m_World);
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                if (m_PoliticalPaintMode) {
                    ImGui::Separator();
                    ImGui::Checkbox("Erase Mode", &m_PoliticalEraseMode);
                    if (m_HoverCellId >= 0) {
                        auto& own = m_World->bodies[m_ActiveBodyIdx].cell_ownership;
                        auto it = own.find(m_HoverCellId);
                        if (it != own.end() && !it->second.empty()) {
                            std::string ownerName = it->second;
                            for (const auto& pe : m_World->political_entities)
                                if (pe.id == it->second) { ownerName = pe.name; break; }
                            ImGui::TextDisabled("Cell %d — %s", m_HoverCellId, ownerName.c_str());
                        } else {
                            ImGui::TextDisabled("Cell %d — unowned", m_HoverCellId);
                        }
                    }
                    if (ImGui::Button("Clear All")) {
                        m_World->bodies[m_ActiveBodyIdx].cell_ownership.clear();
                        syncPoliticalRenderer();
                        WorldSerializer::save(*m_World);
                    }
                }
            } else {
                ImGui::TextDisabled("Open a world to edit political map.");
            }

            if (m_PolMap.hasGrid(0))
                ImGui::TextDisabled("%d cells  (subdiv %d, LOD slot %d/%d)",
                                    (int)m_PolMap.grid(0)->cells().size(),
                                    m_PolMap.grid(0)->subdiv(),
                                    m_PolLODSlot, m_PolMap.slotCount());
        }
    } else {
        ImGui::TextDisabled("Switch to planet view.");
    }
    ImGui::End();

    ImGui::Begin("Timeline");
    ImGui::TextDisabled("No calendar defined.");
    ImGui::End();
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
