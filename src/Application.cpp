#include "Application.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <tinyfiledialogs.h>
#include <stb_image.h>

#include "io/WorldSerializer.h"

#include <iostream>
#include <filesystem>

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

    bool dragging = !io.WantCaptureMouse &&
                    m_Window.mouseButton(GLFW_MOUSE_BUTTON_LEFT);
    float scroll  = io.WantCaptureMouse ? 0.0f : m_Window.scrollDelta();
    m_Camera.update(m_Window.cursorDelta(), scroll, dragging);

    if (!io.WantCaptureKeyboard) {
        bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                    ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_CommandStack.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_CommandStack.redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S) && m_World)
            WorldSerializer::save(*m_World);
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

// ── ImGui UI ──────────────────────────────────────────────────────────────────

void Application::renderUI() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Full-window dockspace host
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

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup);
    }
}

// ── Menu bar ──────────────────────────────────────────────────────────────────

void Application::renderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {

        if (ImGui::MenuItem("New World...")) {
            m_OpenNewWorldDialog = true;
        }

        if (ImGui::MenuItem("Open World...")) {
            const char* picked = tinyfd_selectFolderDialog(
                "Select world folder", nullptr);
            if (picked) {
                World w;
                if (WorldSerializer::load(picked, w)) {
                    m_World = w;
                    m_ActiveBodyIdx     = w.bodies.empty() ? -1 : 0;
                    m_LastActiveBodyIdx = -2;
                    m_FocusWorldPanel   = true;
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

    // Right-aligned status message
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
    if (ImGui::Button("Cancel", {120, 0}))
        ImGui::CloseCurrentPopup();

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

    static const char* typeLabels[] = { "Star", "Planet", "Moon" };
    ImGui::Combo("Type", &m_NewBodyType, typeLabels, 3);

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
        WorldSerializer::save(*m_World);
        std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                      "Added body: %s", b.name.c_str());
        ImGui::CloseCurrentPopup();
    }

    if (!canAdd) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", {120, 0}))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ── Side panels ───────────────────────────────────────────────────────────────

void Application::renderWorldPanel() {
    if (!m_World) {
        ImGui::TextDisabled("No world loaded.");
        ImGui::TextDisabled("File > New World  or  Open World...");
        return;
    }

    ImGui::Text("%s", m_World->name.c_str());
    ImGui::TextDisabled("%s", m_World->rootPath.string().c_str());
    ImGui::Separator();

    ImGui::TextUnformatted("Bodies");
    ImGui::SameLine();
    if (ImGui::SmallButton("+##body"))
        m_OpenAddBodyDialog = true;

    ImGui::Spacing();

    static const char* typeIcon[] = { "[*]", "[o]", "[.]" };

    if (m_World->bodies.empty()) {
        ImGui::TextDisabled("  No bodies yet. Use + to add one.");
    } else {
        for (int i = 0; i < static_cast<int>(m_World->bodies.size()); ++i) {
            const auto& b = m_World->bodies[i];
            int typeIdx = static_cast<int>(b.type);
            char label[320];
            std::snprintf(label, sizeof(label), "%s %s",
                          typeIcon[typeIdx], b.name.c_str());
            bool selected = (m_ActiveBodyIdx == i);
            if (ImGui::Selectable(label, selected))
                m_ActiveBodyIdx = i;
        }
    }
}

void Application::renderPanels() {
    if (m_FocusWorldPanel) {
        ImGui::SetNextWindowFocus();
        m_FocusWorldPanel = false;
    }
    ImGui::Begin("World");
    renderWorldPanel();
    ImGui::End();

    ImGui::Begin("Inspector");
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < static_cast<int>(m_World->bodies.size())) {
        const auto& b = m_World->bodies[m_ActiveBodyIdx];
        ImGui::Text("%s", b.name.c_str());
        ImGui::Separator();
        static const char* typeLabels[] = { "Star", "Planet", "Moon" };
        ImGui::LabelText("Type",           "%s", typeLabels[static_cast<int>(b.type)]);
        ImGui::LabelText("Radius",         "%.0f km", b.radius_km);
        ImGui::LabelText("Axial tilt",     "%.1f deg", b.axial_tilt_deg);
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

// ── ImGui lifecycle ───────────────────────────────────────────────────────────

void Application::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename  = "lorekeeper.ini";

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

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

    if (m_TextureId) {
        glDeleteTextures(1, &m_TextureId);
        m_TextureId = 0;
    }

    glGenTextures(1, &m_TextureId);
    glBindTexture(GL_TEXTURE_2D, m_TextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,       GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,       GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,   GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,   GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    m_HasTexture = true;
    std::cout << "Loaded surface texture: " << path << " (" << w << "x" << h << ")\n";
    return true;
}

void Application::reloadBodyTexture() {
    // Update window title.
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < static_cast<int>(m_World->bodies.size())) {
        const auto& b = m_World->bodies[m_ActiveBodyIdx];
        std::string title = "Lorekeeper  —  " + m_World->name + "  >  " + b.name;
        glfwSetWindowTitle(m_Window.handle(), title.c_str());
    } else if (m_World) {
        glfwSetWindowTitle(m_Window.handle(),
                           ("Lorekeeper  —  " + m_World->name).c_str());
    } else {
        glfwSetWindowTitle(m_Window.handle(), "Lorekeeper");
    }

    // Release existing texture so we can cleanly fall through to defaults.
    if (m_TextureId) {
        glDeleteTextures(1, &m_TextureId);
        m_TextureId  = 0;
        m_HasTexture = false;
    }

    // Try body-specific texture first.
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < static_cast<int>(m_World->bodies.size())) {

        const auto& b   = m_World->bodies[m_ActiveBodyIdx];
        auto        base = m_World->rootPath / "assets" / "textures" / b.id;

        if (tryLoadTexture((base.string() + ".jpg")) ||
            tryLoadTexture((base.string() + ".png")))
            return;
    }

    // Fall back to the global surface texture next to the executable.
    if (!tryLoadTexture("assets/surface.jpg"))
        tryLoadTexture("assets/surface.png");
}
