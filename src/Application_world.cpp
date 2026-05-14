#include "Application.h"

#include <imgui_impl_opengl3.h>
#include <imgui_impl_glfw.h>
#include <imgui.h>
#include <stb_image.h>
#include <nlohmann/json.hpp>

#include "io/WorldSerializer.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

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

    stbi_set_flip_vertically_on_load(true);
    int            w, h, ch;
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
    if (m_World) {
        const auto& b = m_World->body;
        glfwSetWindowTitle(m_Window.handle(),
            ("Lorekeeper  \xe2\x80\x94  " + m_World->name + "  >  " + b.name).c_str());
    } else {
        glfwSetWindowTitle(m_Window.handle(), "Lorekeeper");
    }

    if (m_TextureId) { glDeleteTextures(1, &m_TextureId); m_TextureId = 0; m_HasTexture = false; }

    bool texLoaded = false;
    if (m_World) {
        const auto& b = m_World->body;
        if (!b.texture_path.empty())
            texLoaded = tryLoadTexture(b.texture_path);
        if (!texLoaded) {
            auto base = m_World->rootPath / "assets" / "textures" / b.id;
            texLoaded = tryLoadTexture(base.string() + ".jpg") ||
                        tryLoadTexture(base.string() + ".png");
        }
    }
    if (!texLoaded) {
        if (!tryLoadTexture("assets/surface.jpg"))
            tryLoadTexture("assets/surface.png");
    }

    reloadBodyOverlays();
}

void Application::reloadBodyOverlays() {
    for (int i = 0; i < 4; ++i) {
        if (m_OverlayTexIds[i] && m_OverlayTexIds[i] != m_NullTex)
            glDeleteTextures(1, &m_OverlayTexIds[i]);
        m_OverlayTexIds[i] = m_NullTex;
    }

    if (!m_World) return;

    const auto& b = m_World->body;
    int slot = 0;
    for (const auto& ov : b.overlays) {
        if (!ov.visible || slot >= 4) continue;
        if (!ov.image_path.empty()) {
            GLuint id = loadImageTex(ov.image_path, STBI_rgb_alpha, GL_RGBA, GL_RGBA);
            if (id) m_OverlayTexIds[slot] = id;
        }
        ++slot;
    }
}

GLuint Application::loadImageTex(const std::string& path, int stbiChannels,
                                  unsigned int glFormat, unsigned int glInternalFormat) {
    if (!std::filesystem::exists(path)) return 0;

    stbi_set_flip_vertically_on_load(true);
    int w, h, ch;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, stbiChannels);
    if (!pixels) return 0;

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)glInternalFormat,
                 w, h, 0, glFormat, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    return id;
}

// ── Recent projects ────────────────────────────────────────────────────────────

static std::filesystem::path recentFilePath() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path() / L"lorekeeper_recent.json";
#else
    return std::filesystem::path("lorekeeper_recent.json");
#endif
}

void Application::loadRecentProjects() {
    std::ifstream f(recentFilePath());
    if (!f) return;
    try {
        auto j = nlohmann::json::parse(f);
        for (auto& p : j.value("recent", nlohmann::json::array()))
            m_RecentProjects.push_back(p.get<std::string>());
    } catch (...) {}
}

void Application::saveRecentProjects() {
    nlohmann::json j;
    j["recent"] = m_RecentProjects;
    std::ofstream(recentFilePath()) << j.dump(2);
}

void Application::addRecentProject(const std::string& path) {
    auto it = std::find(m_RecentProjects.begin(), m_RecentProjects.end(), path);
    if (it != m_RecentProjects.end()) m_RecentProjects.erase(it);
    m_RecentProjects.insert(m_RecentProjects.begin(), path);
    if (m_RecentProjects.size() > 10) m_RecentProjects.resize(10);
    saveRecentProjects();
}

bool Application::openWorld(const std::string& path, bool silent) {
    World w;
    if (!WorldSerializer::load(path, w)) {
        if (!silent)
            std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                          "No world.json found in: %s", path.c_str());
        return false;
    }
    m_World              = std::move(w);
    m_NeedsTextureReload = true;
    m_SelectedNodeId.clear();
    m_SelectedEdgeId.clear();
    m_SelectedOverlayId.clear();
    m_CommandStack.clear();
    addRecentProject(path);
    if (!silent)
        std::snprintf(m_StatusMsg, sizeof(m_StatusMsg),
                      "Opened: %s", m_World->name.c_str());
    return true;
}
