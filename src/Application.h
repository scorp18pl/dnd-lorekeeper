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

enum class EditMode       { Navigate, Place, NetworkEdit, Measure };
enum class NetworkSubMode { PlaceNode, Connect };

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
    void renderCalendarDialog();
    void renderPanels();
    void renderRoads();
    void renderWorldPanel();
    void renderLabels();
    void renderHUD();

    bool   tryLoadTexture(const std::string& path);
    void   reloadBodyTexture();
    void   reloadBodyOverlays();
    GLuint loadImageTex(const std::string& path, int stbiChannels,
                        unsigned int glFormat, unsigned int glInternalFormat);

    std::optional<glm::vec2> castRay(float mouseX, float mouseY) const;
    glm::vec3 latLonToWorld(float latDeg, float lonDeg) const;
    glm::vec2 worldToScreen(glm::vec3 worldPos) const;

    // ── Core systems ──────────────────────────────────────────────────────────
    Window        m_Window { 1400, 900, "Lorekeeper" };
    OrbitalCamera m_Camera;
    CommandStack  m_CommandStack;

    std::unique_ptr<Shader>     m_PlanetShader;
    std::unique_ptr<QuadSphere>  m_QuadSphere;
    TextRenderer                 m_TextRenderer;

    GLuint m_TextureId   = 0;
    bool   m_HasTexture  = false;
    GLuint m_NullTex              = 0;
    GLuint m_OverlayTexIds[4]    = {};

    // ── World state ───────────────────────────────────────────────────────────
    std::optional<World> m_World;
    bool                 m_NeedsTextureReload = true;
    std::string          m_SelectedNodeId;     // unified: entity or network node
    std::string          m_SelectedEdgeId;
    std::string          m_SelectedOverlayId;

    // ── Timeline ──────────────────────────────────────────────────────────────
    int  m_CurrentDay         = 0;
    bool m_ShowCalendarDialog = false;

    // ── Edit mode ─────────────────────────────────────────────────────────────
    EditMode    m_EditMode  = EditMode::Navigate;
    EntityType  m_PlaceType = EntityType::City;  // for Place mode

    // ── Network editing ───────────────────────────────────────────────────────
    NetworkSubMode m_NetworkSubMode    = NetworkSubMode::PlaceNode;
    std::string    m_NetworkConnectFrom;
    RouteType      m_RouteType         = RouteType::Road;

    // ── Layer visibility ──────────────────────────────────────────────────────
    bool m_ShowRoads = true;
    bool m_ShowSea   = true;

    // ── Route tool ────────────────────────────────────────────────────────────
    bool                     m_RouteMode       = false;
    std::string              m_RouteFrom;
    std::string              m_RouteTo;
    std::vector<std::string> m_RouteEdgeIds;
    float                    m_RouteKm         = -1.f;
    int                      m_RouteTypeFilter = 0;  // 0=Any 1=Road 2=Sea

    // ── Measure tool ──────────────────────────────────────────────────────────
    std::vector<glm::vec2> m_MeasurePath;        // lat/lon of each waypoint
    float                  m_MeasureTotalKm = 0.f;
    std::string            m_MeasureResult;
    int                    m_MeasureDragIdx  = -1;   // index of waypoint being dragged

    // ── Party travel ──────────────────────────────────────────────────────────
    std::string              m_SelectedPartyId;
    bool                     m_PartyPlaceMode  = false;
    bool                     m_PartyTravelMode = false;
    std::string              m_PartyTravelDest;
    std::vector<std::string> m_PartyRouteEdgeIds;
    float                    m_PartyRouteKm    = -1.f;

    // ── Hover state ───────────────────────────────────────────────────────────
    float       m_HoverLat    = -1000.0f;
    float       m_HoverLon    = -1000.0f;
    std::string m_HoverNodeId;
    std::string m_HoverEdgeId;

    // ── Dockspace ─────────────────────────────────────────────────────────────
    unsigned int m_DockId = 0;

    // ── Node drag / relocate state ────────────────────────────────────────────
    bool        m_RelocateMode = false;  // enabled via Inspector "Move" button
    bool        m_DraggingNode = false;
    std::string m_DragNodeId;
    float       m_DragOrigLat = 0.0f;
    float       m_DragOrigLon = 0.0f;

    // ── Dialog / panel flags ──────────────────────────────────────────────────
    bool m_OpenNewWorldDialog = false;
    bool m_ResetDockLayout    = false;
    char m_NewWorldName[256]  = "My World";
    char m_NewWorldPath[1024] = {};
    char m_StatusMsg[512]     = {};

    // ── Recent projects ───────────────────────────────────────────────────────
    void loadRecentProjects();
    void saveRecentProjects();
    void addRecentProject(const std::string& path);
    bool openWorld(const std::string& path, bool silent = false);

    std::vector<std::string> m_RecentProjects;

    // ── Map Tools dialog ──────────────────────────────────────────────────────
    void renderMapToolsDialog();

    bool  m_ShowMapTools   = false;
    int   m_MapToolsTab    = 0;
    int   m_MapToolsProj   = 0;
    char  m_MtInPath[1024] = {};
    char  m_MtOutPath[1024]= {};
    float m_MtLat0         = 0.0f;
    float m_MtLon0         = 0.0f;
    float m_MtSizeKm       = 1000.0f;
    int   m_MtResolution   = 1024;
    int   m_MtOutWidth     = 4096;
    int   m_MtOutHeight    = 2048;
    float m_MtAzimuth      = 315.0f;
    float m_MtAltitude     = 45.0f;
    float m_MtZScale       = 5.0f;
    char  m_MtStatus[512]  = {};
};
