#include "Application.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <tinyfiledialogs.h>
#include <stb_image.h>

#include "io/WorldSerializer.h"
#include "command/PlaceEntityCommand.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

Application::Application() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    initImGui();

    m_SphereShader = std::make_unique<Shader>("shaders/sphere.vert",
                                              "shaders/sphere.frag");
    m_Sphere = std::make_unique<CubeSphere>(64);
}

Application::~Application() {
    if (m_TextureId) glDeleteTextures(1, &m_TextureId);
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

    // Only orbit when in Navigate mode
    bool dragging = !io.WantCaptureMouse &&
                    m_EditMode == EditMode::Navigate &&
                    m_Window.mouseButton(GLFW_MOUSE_BUTTON_LEFT);
    float scroll = io.WantCaptureMouse ? 0.0f : m_Window.scrollDelta();
    m_Camera.update(m_Window.cursorDelta(), scroll, dragging);

    if (!io.WantCaptureKeyboard) {
        bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                    ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_CommandStack.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_CommandStack.redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S) && m_World)
            WorldSerializer::save(*m_World);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            m_EditMode = EditMode::Navigate;
    }
}

// ── 3-D scene ─────────────────────────────────────────────────────────────────

void Application::renderScene() {
    if (m_ActiveBodyIdx != m_LastActiveBodyIdx) {
        m_LastActiveBodyIdx = m_ActiveBodyIdx;
        reloadBodyTexture();
    }

    glClearColor(0.04f, 0.04f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_Window.width(), m_Window.height());

    glm::mat4 mvp = m_Camera.projectionMatrix(m_Window.aspect())
                  * m_Camera.viewMatrix()
                  * glm::mat4(1.0f);

    m_SphereShader->bind();
    m_SphereShader->setMat4("u_MVP",        mvp);
    m_SphereShader->setBool("u_HasTexture", m_HasTexture);
    m_SphereShader->setVec3("u_BaseColor",  {0.15f, 0.35f, 0.65f});

    if (m_HasTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_TextureId);
        m_SphereShader->setInt("u_Texture", 0);
    }

    m_Sphere->draw();
    m_SphereShader->unbind();
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
    float lon = glm::degrees(std::atan2(hit.z, hit.x));
    return glm::vec2(lat, lon);
}

glm::vec3 Application::latLonToWorld(float latDeg, float lonDeg) {
    float lat = glm::radians(latDeg);
    float lon = glm::radians(lonDeg);
    return { std::cos(lat) * std::cos(lon),
             std::sin(lat),
             std::cos(lat) * std::sin(lon) };
}

glm::vec2 Application::worldToScreen(glm::vec3 worldPos) const {
    glm::mat4 mvp = m_Camera.projectionMatrix(m_Window.aspect()) * m_Camera.viewMatrix();
    glm::vec4 clip = mvp * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return { -10000.0f, -10000.0f };
    clip /= clip.w;
    float sx = (clip.x * 0.5f + 0.5f) * m_Window.width();
    float sy = (1.0f - (clip.y * 0.5f + 0.5f)) * m_Window.height();
    return { sx, sy };
}

// ── ImGui UI ──────────────────────────────────────────────────────────────────

void Application::renderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();

    // ── Hover ray cast ────────────────────────────────────────────────────────
    if (!io.WantCaptureMouse) {
        ImVec2 pos = ImGui::GetMousePos();
        if (auto hit = castRay(pos.x, pos.y)) {
            m_HoverLat = hit->x;
            m_HoverLon = hit->y;
        } else {
            m_HoverLat = m_HoverLon = -1000.0f;
        }
    } else {
        m_HoverLat = m_HoverLon = -1000.0f;
    }

    // ── Globe click (place / select) ──────────────────────────────────────────
    if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
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
            // Select nearest visible entity within pixel threshold.
            // worldToScreen returns screen-space, GetMousePos returns screen-space.
            const auto& body   = m_World->bodies[m_ActiveBodyIdx];
            glm::vec3   camDir = glm::normalize(m_Camera.position());
            ImVec2      mpos   = ImGui::GetMousePos();
            float       best   = 14.0f;
            std::string bestId;
            for (const auto& e : body.entities) {
                glm::vec3 wp = latLonToWorld(e.lat_deg, e.lon_deg);
                if (glm::dot(wp, camDir) < 0.05f) continue;
                glm::vec2 sp = worldToScreen(wp);
                float     d  = glm::length(sp - glm::vec2(mpos.x, mpos.y));
                if (d < best) { best = d; bestId = e.id; }
            }
            m_SelectedEntityId = bestId;
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

    renderMenuBar();
    ImGui::End();

    renderNewWorldDialog();
    renderAddBodyDialog();
    renderPanels();
    renderLabels();
    renderHUD();

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
            if (picked) {
                World w;
                if (WorldSerializer::load(picked, w)) {
                    m_World             = w;
                    m_ActiveBodyIdx     = w.bodies.empty() ? -1 : 0;
                    m_LastActiveBodyIdx = -2;
                    m_FocusWorldPanel   = true;
                    m_SelectedEntityId.clear();
                    m_CommandStack.clear();
                    std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                                  "Opened: %s", w.name.c_str());
                } else {
                    std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                                  "No world.json found in selected folder.");
                }
            }
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
            m_FocusWorldPanel   = true;
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

// ── Add Body dialog ───────────────────────────────────────────────────────────

void Application::renderAddBodyDialog() {
    if (m_OpenAddBodyDialog) {
        ImGui::OpenPopup("Add Body");
        m_OpenAddBodyDialog = false;
    }

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({380.0f, 0.0f}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Add Body", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("Name", m_NewBodyName, sizeof(m_NewBodyName));
    static const char* bodyTypeLabels[] = { "Star", "Planet", "Moon" };
    ImGui::Combo("Type", &m_NewBodyType, bodyTypeLabels, 3);
    ImGui::Separator();

    bool canAdd = m_NewBodyName[0] != '\0';
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("Add", {120, 0})) {
        CelestialBody b;
        b.id   = std::to_string(m_World->bodies.size() + 1);
        b.name = m_NewBodyName;
        b.type = static_cast<BodyType>(m_NewBodyType);
        m_World->bodies.push_back(b);
        m_ActiveBodyIdx = static_cast<int>(m_World->bodies.size()) - 1;
        m_SelectedEntityId.clear();
        WorldSerializer::save(*m_World);
        std::snprintf(m_StatusMsg, sizeof(m_StatusMsg), "Added body: %s", b.name.c_str());
        ImGui::CloseCurrentPopup();
    }
    if (!canAdd) ImGui::EndDisabled();

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

    // Bodies
    ImGui::TextUnformatted("Bodies");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##body")) m_OpenAddBodyDialog = true;

    static const char* bodyIcon[] = { "[*]", "[o]", "[.]" };
    if (m_World->bodies.empty()) {
        ImGui::TextDisabled("  No bodies. Use + to add one.");
    } else {
        for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
            const auto& b = m_World->bodies[i];
            char label[320];
            std::snprintf(label, sizeof(label), "%s %s",
                          bodyIcon[(int)b.type], b.name.c_str());
            if (ImGui::Selectable(label, m_ActiveBodyIdx == i)) {
                m_ActiveBodyIdx = i;
                m_SelectedEntityId.clear();
            }
        }
    }

    // Place mode
    if (m_ActiveBodyIdx >= 0) {
        ImGui::Separator();
        ImGui::TextUnformatted("Place");
        ImGui::SameLine();

        auto placeBtn = [&](const char* lbl, EntityType type) {
            bool active = m_EditMode == EditMode::Place && m_PlaceType == type;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            bool clicked = ImGui::SmallButton(lbl);
            if (active) ImGui::PopStyleColor();
            if (clicked) { m_PlaceType = type; m_EditMode = EditMode::Place; }
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

    // Entity list for active body
    if (m_ActiveBodyIdx >= 0 && m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& body = m_World->bodies[m_ActiveBodyIdx];
        if (!body.entities.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Entities");
            static const char* eIcon[] = { "[C]", "[T]", "[P]" };
            for (const auto& e : body.entities) {
                char label[320];
                std::snprintf(label, sizeof(label), "%s %s",
                              eIcon[(int)e.type], e.name.c_str());
                if (ImGui::Selectable(label, e.id == m_SelectedEntityId))
                    m_SelectedEntityId = e.id;
            }
        }
    }
}

// ── Panels ────────────────────────────────────────────────────────────────────

void Application::renderPanels() {
    if (m_FocusWorldPanel) {
        ImGui::SetNextWindowFocus();
        m_FocusWorldPanel = false;
    }
    ImGui::Begin("World");
    renderWorldPanel();
    ImGui::End();

    // ── Inspector ─────────────────────────────────────────────────────────────
    ImGui::Begin("Inspector");

    // Find selected entity
    WorldEntity* ent = nullptr;
    if (m_World && m_ActiveBodyIdx >= 0 && !m_SelectedEntityId.empty()) {
        auto& ents = m_World->bodies[m_ActiveBodyIdx].entities;
        auto  it   = std::find_if(ents.begin(), ents.end(),
                        [&](const WorldEntity& e) { return e.id == m_SelectedEntityId; });
        if (it != ents.end()) ent = &(*it);
    }

    if (ent) {
        // Sync edit buffers when selection changes
        static char      nameEdit[256]  = {};
        static char      mediaEdit[512] = {};
        static std::string lastId;
        if (lastId != m_SelectedEntityId) {
            lastId = m_SelectedEntityId;
            strncpy_s(nameEdit,  sizeof(nameEdit),  ent->name.c_str(),      _TRUNCATE);
            strncpy_s(mediaEdit, sizeof(mediaEdit),  ent->media_ref.c_str(), _TRUNCATE);
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

        bool hasMedia = ent->media_ref.length() > 0;

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
            // Default path: world/media/<entity-id>.md
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

    } else if (m_World && m_ActiveBodyIdx >= 0 &&
               m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        // Body inspector
        const auto& b = m_World->bodies[m_ActiveBodyIdx];
        ImGui::Text("%s", b.name.c_str());
        ImGui::Separator();
        static const char* bodyTypeLabels[] = { "Star", "Planet", "Moon" };
        ImGui::LabelText("Type",           "%s", bodyTypeLabels[(int)b.type]);
        ImGui::LabelText("Radius",         "%.0f km", b.radius_km);
        ImGui::LabelText("Axial tilt",     "%.1f\xc2\xb0", b.axial_tilt_deg);
        ImGui::LabelText("Rotation",       "%.2f h", b.rotation_h);
        ImGui::LabelText("Orbital period", "%.2f days", b.orbital_period_d);
    } else {
        ImGui::TextDisabled("Nothing selected.");
    }

    ImGui::End();

    ImGui::Begin("Layers");
    ImGui::TextDisabled("No layers.");
    ImGui::End();

    ImGui::Begin("Timeline");
    ImGui::TextDisabled("No calendar defined.");
    ImGui::End();
}

// ── Labels (screen-projected entity markers) ──────────────────────────────────

void Application::renderLabels() {
    if (!m_World || m_ActiveBodyIdx < 0 ||
        m_ActiveBodyIdx >= (int)m_World->bodies.size()) return;

    const auto& body   = m_World->bodies[m_ActiveBodyIdx];
    glm::vec3   camDir = glm::normalize(m_Camera.position());
    ImDrawList* dl     = ImGui::GetBackgroundDrawList();

    static const ImU32 fillColors[] = {
        IM_COL32(255, 200,  60, 255), // City  – gold
        IM_COL32(140, 200, 255, 255), // Town  – sky blue
        IM_COL32(140, 255, 160, 255), // POI   – mint
    };
    static const float radii[] = { 6.0f, 4.5f, 3.5f };

    for (const auto& e : body.entities) {
        glm::vec3 wp = latLonToWorld(e.lat_deg, e.lon_deg);
        if (glm::dot(wp, camDir) < 0.05f) continue; // behind the limb

        glm::vec2 sp  = worldToScreen(wp);
        int       idx = (int)e.type;
        float     r   = radii[idx];

        if (e.id == m_SelectedEntityId)
            dl->AddCircle({sp.x, sp.y}, r + 3.0f,
                          IM_COL32(255, 255, 255, 220), 0, 2.0f);

        dl->AddCircleFilled({sp.x, sp.y}, r, fillColors[idx]);
        dl->AddText({sp.x + r + 4.0f, sp.y - 7.0f},
                    IM_COL32(255, 255, 255, 210), e.name.c_str());
    }
}

// ── HUD (lat/lon + scale bar) ─────────────────────────────────────────────────

void Application::renderHUD() {
    float W = (float)m_Window.width();
    float H = (float)m_Window.height();

    float radius_km = 6371.0f;
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size())
        radius_km = (float)m_World->bodies[m_ActiveBodyIdx].radius_km;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImU32 col = IM_COL32(210, 210, 210, 200);

    // ── Coordinate readout (bottom-right) ─────────────────────────────────────
    char coord[80];
    if (m_HoverLat > -999.0f)
        std::snprintf(coord, sizeof(coord),
                      "%.2f\xc2\xb0 %c   %.2f\xc2\xb0 %c",
                      std::abs(m_HoverLat), m_HoverLat >= 0 ? 'N' : 'S',
                      std::abs(m_HoverLon), m_HoverLon >= 0 ? 'E' : 'W');
    else
        std::snprintf(coord, sizeof(coord), "-- --");

    ImVec2 csz = ImGui::CalcTextSize(coord);
    dl->AddText({W - csz.x - 12.0f, H - 24.0f}, col, coord);

    // ── Scale bar (bottom-left) ────────────────────────────────────────────────
    float km_per_px = (2.0f * radius_km *
                       std::tan(glm::radians(22.5f)) *
                       m_Camera.distance()) / H;

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

    float barY  = H - 14.0f;
    float barX1 = 12.0f;
    float barX2 = barX1 + barPx;
    dl->AddLine({barX1, barY},     {barX2, barY},     col, 2.0f);
    dl->AddLine({barX1, barY - 4}, {barX1, barY + 4}, col, 2.0f);
    dl->AddLine({barX2, barY - 4}, {barX2, barY + 4}, col, 2.0f);

    char scaleLabel[32];
    if (barKm >= 1000.0f)
        std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f,000 km", barKm / 1000.0f);
    else
        std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f km", barKm);

    ImVec2 lsz = ImGui::CalcTextSize(scaleLabel);
    dl->AddText({barX1 + (barPx - lsz.x) * 0.5f, barY - 17.0f}, col, scaleLabel);
}

// ── ImGui lifecycle ───────────────────────────────────────────────────────────

void Application::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename  = "lorekeeper.ini";

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(m_Window.handle(), true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

// ── Texture loading ───────────────────────────────────────────────────────────

bool Application::tryLoadTexture(const std::string& path) {
    if (!std::filesystem::exists(path)) return false;

    stbi_set_flip_vertically_on_load(false);
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb);
    if (!data) return false;

    if (m_TextureId) { glDeleteTextures(1, &m_TextureId); m_TextureId = 0; }

    glGenTextures(1, &m_TextureId);
    glBindTexture(GL_TEXTURE_2D, m_TextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    m_HasTexture = true;
    std::cout << "Loaded texture: " << path << " (" << w << "x" << h << ")\n";
    return true;
}

void Application::reloadBodyTexture() {
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& b = m_World->bodies[m_ActiveBodyIdx];
        glfwSetWindowTitle(m_Window.handle(),
            ("Lorekeeper  \xe2\x80\x94  " + m_World->name + "  >  " + b.name).c_str());
    } else if (m_World) {
        glfwSetWindowTitle(m_Window.handle(),
            ("Lorekeeper  \xe2\x80\x94  " + m_World->name).c_str());
    } else {
        glfwSetWindowTitle(m_Window.handle(), "Lorekeeper");
    }

    if (m_TextureId) { glDeleteTextures(1, &m_TextureId); m_TextureId = 0; m_HasTexture = false; }

    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& b    = m_World->bodies[m_ActiveBodyIdx];
        auto        base = m_World->rootPath / "assets" / "textures" / b.id;
        if (tryLoadTexture(base.string() + ".jpg") ||
            tryLoadTexture(base.string() + ".png")) return;
    }

    if (!tryLoadTexture("assets/surface.jpg"))
        tryLoadTexture("assets/surface.png");
}
