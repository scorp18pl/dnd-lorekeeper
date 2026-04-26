#pragma once
#include <memory>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include "Window.h"
#include "camera/OrbitalCamera.h"
#include "command/CommandStack.h"
#include "renderer/Shader.h"
#include "renderer/CubeSphere.h"
#include "world/World.h"

enum class EditMode { Navigate, Place };

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
    void renderLabels();
    void renderHUD();

    bool tryLoadTexture(const std::string& path);
    void reloadBodyTexture();

    // Returns {lat, lon} in degrees if ray hits the sphere, else nullopt.
    std::optional<glm::vec2> castRay(float mouseX, float mouseY) const;

    // World-pos from lat/lon (degrees).
    static glm::vec3 latLonToWorld(float latDeg, float lonDeg);

    // Projects world pos to screen coords; returns off-screen sentinel if behind camera.
    glm::vec2 worldToScreen(glm::vec3 worldPos) const;

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
    int                  m_LastActiveBodyIdx = -2;
    std::string          m_SelectedEntityId;

    // ── Edit mode ─────────────────────────────────────────────────────────────
    EditMode   m_EditMode  = EditMode::Navigate;
    EntityType m_PlaceType = EntityType::City;

    // Lat/lon under cursor this frame (-1000 if not over sphere).
    float m_HoverLat = -1000.0f;
    float m_HoverLon = -1000.0f;

    // ── Dockspace ─────────────────────────────────────────────────────────────
    unsigned int m_DockId = 0;

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
