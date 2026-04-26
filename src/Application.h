#pragma once
#include <memory>
#include <optional>
#include "Window.h"
#include "camera/OrbitalCamera.h"
#include "command/CommandStack.h"
#include "renderer/Shader.h"
#include "renderer/CubeSphere.h"
#include "world/World.h"

class Application {
public:
    Application();
    ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    void initImGui();
    void shutdownImGui();

    void processInput();
    void renderScene();
    void renderUI();
    void renderMenuBar();
    void renderNewWorldDialog();
    void renderAddBodyDialog();
    void renderPanels();
    void renderWorldPanel();

    bool tryLoadTexture(const std::string& path);
    void reloadBodyTexture();

    // ── Core systems ──────────────────────────────────────────────────────────
    Window        m_Window { 1400, 900, "Lorekeeper" };
    OrbitalCamera m_Camera;
    CommandStack  m_CommandStack;

    std::unique_ptr<Shader>     m_SphereShader;
    std::unique_ptr<CubeSphere> m_Sphere;

    GLuint m_TextureId  = 0;
    bool   m_HasTexture = false;

    // ── World state ───────────────────────────────────────────────────────────
    std::optional<World> m_World;
    int                  m_ActiveBodyIdx     = -1;
    int                  m_LastActiveBodyIdx = -2;  // sentinel — forces first load

    // ── Dialog / panel flags ──────────────────────────────────────────────────
    bool m_OpenNewWorldDialog = false;
    bool m_OpenAddBodyDialog  = false;
    bool m_FocusWorldPanel    = false;
    bool m_ResetDockLayout    = false;
    char m_NewWorldName[256]  = "My World";
    char m_NewWorldPath[1024] = {};
    char m_StatusMsg[512]     = {};

    // ── Add Body dialog state ─────────────────────────────────────────────────
    char m_NewBodyName[256] = "New Body";
    int  m_NewBodyType      = 1;   // 0=Star 1=Planet 2=Moon
};
