#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Window.h"
#include "camera/OrbitalCamera.h"
#include "command/CommandStack.h"
#include "command/MoveEntityCommand.h"
#include "renderer/Shader.h"
#include "renderer/QuadSphere.h"
#include "renderer/TextRenderer.h"
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
    void renderPlanet();
    void renderUI();
    void renderMenuBar();
    void renderNewWorldDialog();
    void renderAddBodyDialog();
    void renderDeleteBodyDialog();
    void renderPanels();
    void renderWorldPanel();
    void renderLabels();
    void renderHUD();

    bool   tryLoadTexture(const std::string& path);
    void   reloadBodyTexture();
    void   reloadBodyOverlays();
    // Loads an image file as an OpenGL texture.
    // Pass STBI_rgb_alpha for RGBA overlays.
    GLuint loadImageTex(const std::string& path, int stbiChannels,
                        unsigned int glFormat, unsigned int glInternalFormat);

    // Returns {lat, lon} in degrees if ray hits the planet sphere, else nullopt.
    std::optional<glm::vec2> castRay(float mouseX, float mouseY) const;

    // World-pos from lat/lon (degrees).
    glm::vec3 latLonToWorld(float latDeg, float lonDeg) const;

    // Projects world pos to screen coords; returns off-screen sentinel if behind camera.
    glm::vec2 worldToScreen(glm::vec3 worldPos) const;

    // ── Core systems ──────────────────────────────────────────────────────────
    Window        m_Window { 1400, 900, "Lorekeeper" };
    OrbitalCamera m_Camera;       // planet view camera
    CommandStack  m_CommandStack;

    std::unique_ptr<Shader>     m_PlanetShader;   // planet view (QuadSphere)
    std::unique_ptr<QuadSphere>  m_QuadSphere;
    TextRenderer                 m_TextRenderer;

    GLuint m_TextureId   = 0;
    bool   m_HasTexture  = false;
    GLuint m_NullTex              = 0;
    GLuint m_OverlayTexIds[4]    = {};

    // ── World state ───────────────────────────────────────────────────────────
    std::optional<World> m_World;
    int                  m_ActiveBodyIdx     = -1;
    int                  m_LastActiveBodyIdx = -2;
    std::string          m_SelectedEntityId;
    std::string          m_SelectedOverlayId;

    // ── Edit mode ─────────────────────────────────────────────────────────────
    EditMode   m_EditMode   = EditMode::Navigate;
    EntityType m_PlaceType  = EntityType::City;

    // Lat/lon under cursor this frame in planet view (-1000 if not over sphere).
    float m_HoverLat = -1000.0f;
    float m_HoverLon = -1000.0f;

    // ── Dockspace ─────────────────────────────────────────────────────────────
    unsigned int m_DockId = 0;

    // ── Entity drag state ─────────────────────────────────────────────────────
    bool        m_DraggingEntity = false;
    int         m_DragEntityIdx  = -1;   // index in active body's entities
    float       m_DragOrigLat    = 0.0f;
    float       m_DragOrigLon    = 0.0f;

    // ── Dialog / panel flags ──────────────────────────────────────────────────
    bool m_OpenNewWorldDialog = false;
    bool m_OpenAddBodyDialog  = false;
    bool m_ResetDockLayout    = false;
    char m_NewWorldName[256]  = "My World";
    char m_NewWorldPath[1024] = {};
    char m_StatusMsg[512]     = {};

    // ── Add Body dialog state ─────────────────────────────────────────────────
    char  m_NewBodyName[256]  = "New Body";

    // ── Delete Body dialog state ──────────────────────────────────────────────
    bool m_OpenDeleteBodyDialog    = false;
    int  m_DeleteBodyIdx           = -1;
    char m_DeleteBodyConfirm[256]  = {};

    // ── Recent projects ───────────────────────────────────────────────────────
    void loadRecentProjects();
    void saveRecentProjects();
    void addRecentProject(const std::string& path);
    bool openWorld(const std::string& path, bool silent = false);

    std::vector<std::string> m_RecentProjects;  // most-recent first, max 10

    // ── Map Tools dialog ──────────────────────────────────────────────────────
    void renderMapToolsDialog();

    bool m_ShowMapTools       = false;
    int  m_MapToolsTab        = 0;      // 0=Color→Gray 1=Project 2=Unproject 3=Hillshade
    int  m_MapToolsProj       = 0;      // 0=AEQD 1=Ortho 2=Gnomonic
    char m_MtInPath[1024]     = {};
    char m_MtOutPath[1024]    = {};
    float m_MtLat0            = 0.0f;
    float m_MtLon0            = 0.0f;
    float m_MtSizeKm          = 1000.0f;
    int   m_MtResolution      = 1024;
    int   m_MtOutWidth        = 4096;
    int   m_MtOutHeight       = 2048;
    float m_MtAzimuth         = 315.0f;
    float m_MtAltitude        = 45.0f;
    float m_MtZScale          = 5.0f;
    char  m_MtStatus[512]     = {};
};
