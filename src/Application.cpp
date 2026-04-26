#include "Application.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/constants.hpp>
#include <tinyfiledialogs.h>
#include <stb_image.h>

#include "io/WorldSerializer.h"
#include "command/PlaceEntityCommand.h"
#include "command/DeleteEntityCommand.h"
#include "command/DeleteBodyCommand.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#ifdef _WIN32
#define NOMINMAX
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

    m_SolarCam.setDistanceLimits(2.0f, 500.0f);
    m_SolarCam.setDistance(20.0f);
    m_SolarCam.setElevation(glm::radians(30.0f));

    // 1×1 transparent texture bound to unused overlay slots
    {
        glGenTextures(1, &m_NullTex);
        glBindTexture(GL_TEXTURE_2D, m_NullTex);
        static const unsigned char zero[4] = {0, 0, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
        for (int i = 0; i < 4; ++i) {
            m_OverlayTexIds[i]  = m_NullTex;
            m_OvHeightmapIds[i] = m_NullTex;
        }
    }
}

Application::~Application() {
    if (m_TextureId)   glDeleteTextures(1, &m_TextureId);
    if (m_HeightmapId) glDeleteTextures(1, &m_HeightmapId);
    for (int i = 0; i < 4; ++i) {
        if (m_OverlayTexIds[i]  && m_OverlayTexIds[i]  != m_NullTex)
            glDeleteTextures(1, &m_OverlayTexIds[i]);
        if (m_OvHeightmapIds[i] && m_OvHeightmapIds[i] != m_NullTex)
            glDeleteTextures(1, &m_OvHeightmapIds[i]);
    }
    if (m_NullTex) glDeleteTextures(1, &m_NullTex);
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

    bool  dragging = !io.WantCaptureMouse &&
                     m_Window.mouseButton(GLFW_MOUSE_BUTTON_MIDDLE);
    float scroll   = io.WantCaptureMouse ? 0.0f : m_Window.scrollDelta();

    float vpH = (float)m_Window.height();
    if (m_ViewMode == ViewMode::SolarSystem)
        m_SolarCam.update(m_Window.cursorDelta(), scroll, dragging, vpH);
    else
        m_Camera.update(m_Window.cursorDelta(), scroll, dragging, vpH);

    if (!io.WantCaptureKeyboard) {
        bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                    ImGui::IsKeyDown(ImGuiKey_RightCtrl);
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) m_CommandStack.undo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) m_CommandStack.redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S) && m_World)
            WorldSerializer::save(*m_World);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (m_ViewMode == ViewMode::Planet)
                m_EditMode = EditMode::Navigate;
            m_DraggingEntity = false;
            m_DragEntityIdx  = -1;
        }
    }
}

// ── 3-D scene ─────────────────────────────────────────────────────────────────

void Application::renderScene() {
    if (m_ViewMode == ViewMode::SolarSystem)
        renderSolarSystem();
    else
        renderPlanet();
}

void Application::renderPlanet() {
    if (m_ActiveBodyIdx != m_LastActiveBodyIdx) {
        m_LastActiveBodyIdx = m_ActiveBodyIdx;
        reloadBodyTexture();
    }

    glClearColor(0.04f, 0.04f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_Window.width(), m_Window.height());

    // Dynamic clip planes: near tracks camera-to-surface gap, far stays generous.
    float surf  = m_Camera.distance() - 1.0f;
    float nearZ = std::max(0.0001f, surf * 0.1f);
    float farZ  = std::max(100.0f,  m_Camera.distance() * 100.0f);
    glm::mat4 proj = glm::perspective(m_Camera.fov(), m_Window.aspect(), nearZ, farZ);
    glm::mat4 vp    = proj * m_Camera.viewMatrix();
    glm::mat4 model(1.0f);

    m_SphereShader->bind();
    m_SphereShader->setMat4("u_VP",         vp);
    m_SphereShader->setMat4("u_Model",      model);
    m_SphereShader->setBool("u_HasTexture", m_HasTexture);
    m_SphereShader->setVec3("u_BaseColor",  {0.15f, 0.35f, 0.65f});

    if (m_HasTexture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_TextureId);
        m_SphereShader->setInt("u_Texture", 0);
    }

    float heightScale = 0.0f;
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size())
        heightScale = m_World->bodies[m_ActiveBodyIdx].height_scale;

    m_SphereShader->setBool ("u_HasHeightmap", m_HasHeightmap);
    m_SphereShader->setFloat("u_HeightScale",  heightScale);
    if (m_HasHeightmap) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_HeightmapId);
        m_SphereShader->setInt("u_Heightmap", 1);
    }

    // Overlay uniforms
    {
        int   ovCount        = 0;
        float ovCenterLat[4] = {}, ovCenterLon[4] = {};
        float ovExtentKm[4]  = {}, ovOpacity[4]   = {};
        float radiusKm       = 6371.0f;

        if (m_World && m_ActiveBodyIdx >= 0 &&
            m_ActiveBodyIdx < (int)m_World->bodies.size()) {
            const auto& b = m_World->bodies[m_ActiveBodyIdx];
            radiusKm = (float)b.radius_km;
            for (const auto& ov : b.overlays) {
                if (!ov.visible || ovCount >= 4) continue;
                ovCenterLat[ovCount] = ov.center_lat;
                ovCenterLon[ovCount] = ov.center_lon;
                ovExtentKm [ovCount] = ov.extent_km;
                ovOpacity  [ovCount] = ov.opacity;
                ++ovCount;
            }
        }

        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE2 + i);
            glBindTexture(GL_TEXTURE_2D, m_OverlayTexIds[i]);
        }
        static const int ovSamplers[4] = {2, 3, 4, 5};
        m_SphereShader->setInt1v  ("u_OvTex",        4, ovSamplers);
        m_SphereShader->setInt    ("u_OvCount",       ovCount);
        m_SphereShader->setFloat1v("u_OvCenterLat",   4, ovCenterLat);
        m_SphereShader->setFloat1v("u_OvCenterLon",   4, ovCenterLon);
        m_SphereShader->setFloat1v("u_OvExtentKm",    4, ovExtentKm);
        m_SphereShader->setFloat1v("u_OvOpacity",     4, ovOpacity);
        m_SphereShader->setFloat  ("u_PlanetRadiusKm", radiusKm);

        // Overlay heightmaps — units 6-9 (additive displacement)
        float ovHmScale[4] = {};
        if (m_World && m_ActiveBodyIdx >= 0 &&
            m_ActiveBodyIdx < (int)m_World->bodies.size()) {
            int slot = 0;
            for (const auto& ov : m_World->bodies[m_ActiveBodyIdx].overlays) {
                if (!ov.visible || slot >= 4) continue;
                ovHmScale[slot++] = ov.height_scale;
            }
        }
        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE6 + i);
            glBindTexture(GL_TEXTURE_2D, m_OvHeightmapIds[i]);
        }
        static const int ovHmSamplers[4] = {6, 7, 8, 9};
        m_SphereShader->setInt1v  ("u_OvHeightmap", 4, ovHmSamplers);
        m_SphereShader->setFloat1v("u_OvHmScale",   4, ovHmScale);
    }

    m_Sphere->draw();
    m_SphereShader->unbind();
}

void Application::renderSolarSystem() {
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, m_Window.width(), m_Window.height());

    if (!m_World || m_World->bodies.empty()) return;

    auto      infos = computeSolarPositions();
    glm::mat4 vp    = m_SolarCam.projectionMatrix(m_Window.aspect())
                    * m_SolarCam.viewMatrix();

    m_SphereShader->bind();
    m_SphereShader->setMat4("u_VP", vp);
    m_SphereShader->setBool("u_HasTexture", false);

    for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
        const auto& b   = m_World->bodies[i];
        const auto& inf = infos[i];

        glm::vec3 col;
        switch (b.type) {
            case BodyType::Star:   col = {1.0f, 0.85f, 0.30f}; break;
            case BodyType::Moon:   col = {0.55f, 0.55f, 0.55f}; break;
            default:               col = {0.20f, 0.45f, 0.80f}; break;
        }
        if (i == m_HoverBodyIdx)
            col = glm::mix(col, glm::vec3(1.0f), 0.35f);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), inf.pos)
                        * glm::scale(glm::mat4(1.0f), glm::vec3(inf.radius));

        m_SphereShader->setMat4("u_Model",    model);
        m_SphereShader->setVec3("u_BaseColor", col);
        m_Sphere->draw();
    }

    m_SphereShader->unbind();
}

// ── Solar system positions ────────────────────────────────────────────────────

std::vector<SolarBodyInfo> Application::computeSolarPositions() const {
    if (!m_World) return {};
    const auto& bodies = m_World->bodies;
    const int   n      = (int)bodies.size();

    std::vector<SolarBodyInfo> result(n);

    std::unordered_map<std::string, int> idxOf;
    for (int i = 0; i < n; ++i)
        if (!bodies[i].id.empty()) idxOf[bodies[i].id] = i;

    // Process in dependency order: Stars (pass 0), Planets (pass 1), Moons (pass 2).
    auto passOf = [](BodyType t) -> int {
        switch (t) {
            case BodyType::Star: return 0;
            case BodyType::Moon: return 2;
            default:             return 1;
        }
    };

    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < n; ++i) {
            if (passOf(bodies[i].type) != pass) continue;
            const auto& b = bodies[i];

            glm::vec3 parentPos(0.0f);
            int       parentIdx = -1;
            if (!b.parent_id.empty()) {
                auto it = idxOf.find(b.parent_id);
                if (it != idxOf.end()) {
                    int       candidate   = it->second;
                    BodyType  parentType  = bodies[candidate].type;
                    // Enforce valid hierarchy: planet→star, moon→planet.
                    bool valid = (b.type == BodyType::Planet && parentType == BodyType::Star)
                              || (b.type == BodyType::Moon   && parentType == BodyType::Planet);
                    if (valid) {
                        parentIdx = candidate;
                        parentPos = result[parentIdx].pos;
                    }
                }
            }
            // Fallbacks for missing/invalid parents.
            if (parentIdx < 0 && b.type == BodyType::Planet) {
                for (int j = 0; j < n; ++j) {
                    if (bodies[j].type == BodyType::Star) { parentPos = result[j].pos; break; }
                }
            }
            if (parentIdx < 0 && b.type == BodyType::Moon) {
                for (int j = 0; j < n; ++j) {
                    if (bodies[j].type == BodyType::Planet) { parentPos = result[j].pos; break; }
                }
            }

            // Count siblings that share the same parent_id and same pass.
            int siblingIdx  = 0;
            int numSiblings = 0;
            for (int j = 0; j < n; ++j) {
                if (bodies[j].parent_id == b.parent_id && passOf(bodies[j].type) == pass) {
                    if (j < i) ++siblingIdx;
                    ++numSiblings;
                }
            }

            float orbitR, radius;

            if (m_RealisticScale) {
                radius = std::clamp((float)(b.radius_km / 6371.0) * 0.07f, 0.02f, 0.8f);
                orbitR = (float)(b.orbital_radius_au * 10.0);
            } else {
                switch (b.type) {
                    case BodyType::Star:
                        radius = 0.25f;
                        orbitR = (numSiblings > 1) ? (float)siblingIdx * 5.0f : 0.0f;
                        break;
                    case BodyType::Moon:
                        radius = 0.03f;
                        orbitR = 0.18f + (float)siblingIdx * 0.12f;
                        break;
                    default:
                        radius = 0.07f;
                        orbitR = 3.0f + (float)siblingIdx * 2.5f;
                        break;
                }
            }

            float angle = (numSiblings > 1)
                ? (float)siblingIdx * glm::two_pi<float>() / (float)numSiblings
                : 0.0f;

            glm::vec3 pos = (b.type == BodyType::Star && orbitR < 0.0001f)
                ? parentPos
                : parentPos + glm::vec3(orbitR * std::cos(angle), 0.0f,
                                        orbitR * std::sin(angle));

            result[i] = { pos, radius, parentPos, orbitR };
        }
    }

    return result;
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
    float lon = glm::degrees(std::atan2(-hit.z, hit.x));
    return glm::vec2(lat, lon);
}

int Application::castRaySolarSystem(float mouseX, float mouseY,
                                    const std::vector<SolarBodyInfo>& infos) const {
    float ndcX =  (mouseX / m_Window.width())  * 2.0f - 1.0f;
    float ndcY = 1.0f - (mouseY / m_Window.height()) * 2.0f;

    glm::mat4 invVP = glm::inverse(
        m_SolarCam.projectionMatrix(m_Window.aspect()) * m_SolarCam.viewMatrix());

    glm::vec4 near4 = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    near4 /= near4.w;
    glm::vec4 far4  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    far4  /= far4.w;

    glm::vec3 ro = m_SolarCam.position();
    glm::vec3 rd = glm::normalize(glm::vec3(far4) - glm::vec3(near4));

    int   bestIdx = -1;
    float bestT   = 1e30f;

    for (int i = 0; i < (int)infos.size(); ++i) {
        glm::vec3 oc  = ro - infos[i].pos;
        float     r   = infos[i].radius;
        float     a   = glm::dot(rd, rd);
        float     bv  = 2.0f * glm::dot(oc, rd);
        float     cv  = glm::dot(oc, oc) - r * r;
        float     dis = bv * bv - 4.0f * a * cv;
        if (dis < 0.0f) continue;
        float t = (-bv - std::sqrt(dis)) / (2.0f * a);
        if (t < 0.0f) continue;
        if (t < bestT) { bestT = t; bestIdx = i; }
    }
    return bestIdx;
}

glm::vec3 Application::latLonToWorld(float latDeg, float lonDeg) {
    float lat = glm::radians(latDeg);
    float lon = glm::radians(lonDeg);
    return { std::cos(lat) * std::cos(lon),
             std::sin(lat),
            -std::cos(lat) * std::sin(lon) };
}

glm::vec2 Application::worldToScreen(glm::vec3 worldPos) const {
    glm::mat4 vp   = m_Camera.projectionMatrix(m_Window.aspect()) * m_Camera.viewMatrix();
    glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return { -10000.0f, -10000.0f };
    clip /= clip.w;
    return { (clip.x * 0.5f + 0.5f) * m_Window.width(),
             (1.0f - (clip.y * 0.5f + 0.5f)) * m_Window.height() };
}

glm::vec2 Application::worldToScreenSolar(glm::vec3 worldPos) const {
    glm::mat4 vp   = m_SolarCam.projectionMatrix(m_Window.aspect())
                   * m_SolarCam.viewMatrix();
    glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return { -10000.0f, -10000.0f };
    clip /= clip.w;
    return { (clip.x * 0.5f + 0.5f) * m_Window.width(),
             (1.0f - (clip.y * 0.5f + 0.5f)) * m_Window.height() };
}

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
        if (!io.WantCaptureMouse) {
            ImVec2 pos = ImGui::GetMousePos();
            if (auto hit = castRay(pos.x, pos.y)) {
                m_HoverLat = hit->x;
                m_HoverLon = hit->y;
            }
        }

        // ── Globe click (place / select / start drag) ────────────────────────
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
                } else {
                    m_SelectedEntityId.clear();
                    m_SelectedOverlayId.clear();
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
    renderPanels();

    if (m_ViewMode == ViewMode::SolarSystem && m_World)
        renderSolarSystemOverlay();
    else
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
                    m_SelectedOverlayId.clear();
                    m_CommandStack.clear();
                    m_ViewMode          = ViewMode::Planet;
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
        ImGui::Separator();
        bool inSS = (m_ViewMode == ViewMode::SolarSystem);
        if (ImGui::MenuItem("Solar System", nullptr, inSS, m_World.has_value()))
            m_ViewMode = inSS ? ViewMode::Planet : ViewMode::SolarSystem;
        ImGui::Separator();
        if (ImGui::MenuItem("Realistic Scale", nullptr, m_RealisticScale))
            m_RealisticScale = !m_RealisticScale;
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
        // Default parent: first planet for moons, none for everything else.
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
        // Reset parent when type changes so it stays valid.
        m_NewBodyParentIdx = -1;
        if (m_NewBodyType == 2 && m_World) { // Moon → default to first planet
            for (int i = 0; i < (int)m_World->bodies.size(); ++i)
                if (m_World->bodies[i].type == BodyType::Planet) { m_NewBodyParentIdx = i; break; }
        }
    }

    // Parent selection — only valid parents for the chosen type are shown.
    // Stars: no parent.  Planets: parent must be a Star.  Moons: parent must be a Planet.
    BodyType requiredParentType = (m_NewBodyType == 1) ? BodyType::Star : BodyType::Planet;
    bool needsParent = (m_NewBodyType != 0); // stars have no parent

    if (needsParent && m_World) {
        // Validate current selection.
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
        // Clamp active index in case we deleted the last body.
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

    // View toggle button
    bool inSS = (m_ViewMode == ViewMode::SolarSystem);
    if (ImGui::SmallButton(inSS ? "Planet View" : "Solar System"))
        m_ViewMode = inSS ? ViewMode::Planet : ViewMode::SolarSystem;
    ImGui::SameLine();
    ImGui::Text("%s", m_World->name.c_str());
    ImGui::TextDisabled("%s", m_World->rootPath.string().c_str());
    ImGui::Separator();

    // Bodies list
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
            // Compute hierarchy depth for indentation
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

    // Place buttons — only in planet view
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

    // Entity list — only in planet view
    if (m_ViewMode == ViewMode::Planet &&
        m_ActiveBodyIdx >= 0 && m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& body = m_World->bodies[m_ActiveBodyIdx];
        if (!body.entities.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted("Entities");
            static const char* eIcon[] = { "[C]", "[T]", "[P]" };
            for (const auto& e : body.entities) {
                char label[320];
                std::snprintf(label, sizeof(label), "%s %s",
                              eIcon[(int)e.type], e.name.c_str());
                if (ImGui::Selectable(label, e.id == m_SelectedEntityId)) {
                    m_SelectedEntityId  = e.id;
                    m_SelectedOverlayId.clear();
                }
            }
        }
    }

    // Overlay list — only in planet view
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
    if (m_FocusWorldPanel) {
        ImGui::SetNextWindowFocus();
        m_FocusWorldPanel = false;
    }
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

// ── Solar system overlay (orbital lines + labels) ─────────────────────────────

void Application::renderSolarSystemOverlay() {
    if (!m_World || m_World->bodies.empty()) return;

    auto      infos = computeSolarPositions();
    glm::mat4 vp    = m_SolarCam.projectionMatrix(m_Window.aspect())
                    * m_SolarCam.viewMatrix();

    ImDrawList* dl       = ImGui::GetBackgroundDrawList();
    ImU32       orbitCol = IM_COL32(70, 80, 130, 150);
    ImU32       labelCol = IM_COL32(220, 220, 220, 210);

    constexpr int N = 64;

    // Orbital ellipses
    for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
        const auto& inf = infos[i];
        if (inf.orbitRadius < 0.001f) continue;

        std::vector<ImVec2> pts;
        pts.reserve(N + 1);

        for (int k = 0; k <= N; ++k) {
            float     angle = (float)k * glm::two_pi<float>() / (float)N;
            glm::vec3 p     = inf.orbitCenter
                            + glm::vec3(inf.orbitRadius * std::cos(angle), 0.0f,
                                        inf.orbitRadius * std::sin(angle));
            glm::vec4 clip  = vp * glm::vec4(p, 1.0f);
            if (clip.w <= 0.01f) {
                if (pts.size() >= 2)
                    dl->AddPolyline(pts.data(), (int)pts.size(), orbitCol, 0, 1.0f);
                pts.clear();
                continue;
            }
            clip /= clip.w;
            pts.push_back({
                (clip.x * 0.5f + 0.5f) * (float)m_Window.width(),
                (1.0f - (clip.y * 0.5f + 0.5f)) * (float)m_Window.height()
            });
        }
        if (pts.size() >= 2)
            dl->AddPolyline(pts.data(), (int)pts.size(), orbitCol, 0, 1.0f);
    }

    // Body labels + hover ring
    float halfH = (float)m_Window.height() * 0.5f;
    float tanHalfFov = std::tan(glm::radians(22.5f));

    for (int i = 0; i < (int)m_World->bodies.size(); ++i) {
        const auto& inf  = infos[i];
        glm::vec4   clip = vp * glm::vec4(inf.pos, 1.0f);
        if (clip.w <= 0.0f) continue;
        clip /= clip.w;
        float sx = (clip.x * 0.5f + 0.5f) * (float)m_Window.width();
        float sy = (1.0f - (clip.y * 0.5f + 0.5f)) * (float)m_Window.height();

        float screenR = std::max(inf.radius / (m_SolarCam.distance() * tanHalfFov) * halfH,
                                 4.0f);

        if (i == m_HoverBodyIdx)
            dl->AddCircle({sx, sy}, screenR + 4.0f, IM_COL32(255, 220, 80, 200), 0, 2.0f);

        dl->AddText({sx + screenR + 5.0f, sy - 7.0f}, labelCol,
                    m_World->bodies[i].name.c_str());
    }
}

// ── Labels (screen-projected entity markers) ──────────────────────────────────

void Application::renderLabels() {
    if (!m_World || m_ActiveBodyIdx < 0 ||
        m_ActiveBodyIdx >= (int)m_World->bodies.size()) return;

    const auto& body   = m_World->bodies[m_ActiveBodyIdx];
    glm::vec3   camDir = glm::normalize(m_Camera.position());
    ImDrawList* dl     = ImGui::GetBackgroundDrawList();

    static const ImU32 fillColors[] = {
        IM_COL32(255, 200,  60, 255),
        IM_COL32(140, 200, 255, 255),
        IM_COL32(140, 255, 160, 255),
    };
    static const float radii[] = { 6.0f, 4.5f, 3.5f };

    for (const auto& e : body.entities) {
        glm::vec3 wp = latLonToWorld(e.lat_deg, e.lon_deg);
        if (glm::dot(wp, camDir) < 0.05f) continue;

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
    ImGuiDockNode* central = (m_DockId != 0)
        ? ImGui::DockBuilderGetCentralNode(m_DockId) : nullptr;

    float cx2 = central ? central->Pos.x + central->Size.x : (float)m_Window.width();
    float cy2 = central ? central->Pos.y + central->Size.y : (float)m_Window.height();
    float cH  = central ? central->Size.y                  : (float)m_Window.height();

    ImDrawList* dl  = ImGui::GetForegroundDrawList();
    ImU32       col = IM_COL32(210, 210, 210, 200);

    if (m_ViewMode == ViewMode::SolarSystem) {
        const char* scaleMode = m_RealisticScale ? "Scale: Realistic" : "Scale: Illustrative";
        ImVec2 sz = ImGui::CalcTextSize(scaleMode);
        dl->AddText({cx2 - sz.x - 10.0f, cy2 - sz.y - 10.0f},
                    IM_COL32(180, 180, 180, 180), scaleMode);
        return;
    }

    float radius_km = 6371.0f;
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size())
        radius_km = (float)m_World->bodies[m_ActiveBodyIdx].radius_km;

    float km_per_px = (2.0f * radius_km *
                       std::tan(m_Camera.fov() * 0.5f) *
                       (m_Camera.distance() - 1.0f)) / cH;

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

    const float margin = 10.0f;
    const float textH  = ImGui::GetTextLineHeight();
    const float tickH  = 4.0f;
    const float rowGap = 8.0f;

    char coord[80];
    if (m_HoverLat > -999.0f)
        std::snprintf(coord, sizeof(coord),
                      "%.2f\xc2\xb0 %c   %.2f\xc2\xb0 %c",
                      std::abs(m_HoverLat), m_HoverLat >= 0 ? 'N' : 'S',
                      std::abs(m_HoverLon), m_HoverLon >= 0 ? 'E' : 'W');
    else
        std::snprintf(coord, sizeof(coord), "-- --");

    ImVec2 csz    = ImGui::CalcTextSize(coord);
    float  coordY = cy2 - margin - textH;
    dl->AddText({cx2 - csz.x - margin, coordY}, col, coord);

    float barY  = coordY - rowGap - tickH;
    float barX2 = cx2 - margin;
    float barX1 = barX2 - barPx;
    dl->AddLine({barX1, barY},          {barX2, barY},          col, 2.0f);
    dl->AddLine({barX1, barY - tickH},  {barX1, barY + tickH},  col, 2.0f);
    dl->AddLine({barX2, barY - tickH},  {barX2, barY + tickH},  col, 2.0f);

    char scaleLabel[32];
    if (barKm >= 1000.0f)
        std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f,000 km", barKm / 1000.0f);
    else
        std::snprintf(scaleLabel, sizeof(scaleLabel), "%.0f km", barKm);

    ImVec2 lsz    = ImGui::CalcTextSize(scaleLabel);
    float  labelY = barY - tickH - rowGap - textH;
    dl->AddText({barX1 + (barPx - lsz.x) * 0.5f, labelY}, col, scaleLabel);
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

    bool texLoaded = false;
    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& b = m_World->bodies[m_ActiveBodyIdx];
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

    reloadBodyHeightmap();
    reloadBodyOverlays();
}

bool Application::tryLoadHeightmap(const std::string& path) {
    if (!std::filesystem::exists(path)) return false;

    stbi_set_flip_vertically_on_load(true);
    int            w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, STBI_grey);
    if (!data) return false;

    if (m_HeightmapId) { glDeleteTextures(1, &m_HeightmapId); m_HeightmapId = 0; }

    glGenTextures(1, &m_HeightmapId);
    glBindTexture(GL_TEXTURE_2D, m_HeightmapId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    m_HasHeightmap = true;
    return true;
}

void Application::reloadBodyHeightmap() {
    if (m_HeightmapId) { glDeleteTextures(1, &m_HeightmapId); m_HeightmapId = 0; }
    m_HasHeightmap = false;

    if (m_World && m_ActiveBodyIdx >= 0 &&
        m_ActiveBodyIdx < (int)m_World->bodies.size()) {
        const auto& b = m_World->bodies[m_ActiveBodyIdx];
        if (!b.heightmap_path.empty())
            tryLoadHeightmap(b.heightmap_path);
    }
}

void Application::reloadBodyOverlays() {
    for (int i = 0; i < 4; ++i) {
        if (m_OverlayTexIds[i]  && m_OverlayTexIds[i]  != m_NullTex)
            glDeleteTextures(1, &m_OverlayTexIds[i]);
        if (m_OvHeightmapIds[i] && m_OvHeightmapIds[i] != m_NullTex)
            glDeleteTextures(1, &m_OvHeightmapIds[i]);
        m_OverlayTexIds[i]  = m_NullTex;
        m_OvHeightmapIds[i] = m_NullTex;
    }

    if (!m_World || m_ActiveBodyIdx < 0 ||
        m_ActiveBodyIdx >= (int)m_World->bodies.size()) return;

    const auto& b = m_World->bodies[m_ActiveBodyIdx];
    int slot = 0;
    for (const auto& ov : b.overlays) {
        if (!ov.visible || slot >= 4) continue;
        if (!ov.image_path.empty()) {
            GLuint id = loadOverlayTex(ov.image_path);
            if (id) m_OverlayTexIds[slot] = id;
        }
        if (!ov.heightmap_path.empty()) {
            GLuint id = loadOverlayHeightmapTex(ov.heightmap_path);
            if (id) m_OvHeightmapIds[slot] = id;
        }
        ++slot;
    }
}

GLuint Application::loadOverlayHeightmapTex(const std::string& path) {
    if (!std::filesystem::exists(path)) return 0;

    stbi_set_flip_vertically_on_load(true);
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, STBI_grey);
    if (!data) return 0;

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return id;
}

GLuint Application::loadOverlayTex(const std::string& path) {
    if (!std::filesystem::exists(path)) return 0;

    stbi_set_flip_vertically_on_load(true);
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!data) return 0;

    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return id;
}
