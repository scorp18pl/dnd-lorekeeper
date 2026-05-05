#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include "world/GoldbergGrid.h"
#include "renderer/GoldbergRenderer.h"
#include "world/PoliticalEntity.h"

struct CelestialBody;
class Shader;

class PoliticalMapLayer {
public:
    static constexpr float kSplitK = 8.0f;

    PoliticalMapLayer();
    ~PoliticalMapLayer();
    PoliticalMapLayer(const PoliticalMapLayer&)            = delete;
    PoliticalMapLayer& operator=(const PoliticalMapLayer&) = delete;

    // Ensure grid + renderer for paintLevel are loaded. Call when body or paintLevel changes.
    void sync(const CelestialBody& body, int paintLevel,
              const std::vector<PoliticalEntity>& entities);

    // View-dependent bake. cam_pos in normalized sphere coords (unit sphere = planet surface).
    void bake(const CelestialBody& body,
              const std::vector<PoliticalEntity>& entities,
              glm::vec3 cam_pos);

    GLuint texId()   const { return m_Tex; }
    bool   isDirty() const { return m_Dirty; }
    void   markDirty()     { m_Dirty = true; }

    bool                hasGrid(int level)  const;
    const GoldbergGrid* grid(int level)     const;
    int                 findCellNearest(int level, glm::vec3 dir) const;
    void                setHoverCell(int cellId);

    void draw(int level, Shader& shader,
              const glm::mat4& vp, const glm::mat4& model) const;

    static std::string makeEntityId(const std::vector<PoliticalEntity>& entities);
    static int         computeMaxLevel(double radius_km, double min_cell_km);

private:
    struct CacheEntry {
        int                               level    = -1;
        std::unique_ptr<GoldbergGrid>     grid;
        std::unique_ptr<GoldbergRenderer> renderer; // built lazily for paint overlay
        uint32_t                          lru      = 0;
    };
    static constexpr int kCacheCapacity = 16;
    mutable std::array<CacheEntry, kCacheCapacity> m_Cache;
    mutable uint32_t m_Tick = 0;

    CacheEntry*         findEntry(int level) const;
    CacheEntry&         evictLRU() const;
    const GoldbergGrid* ensureGrid(int level) const;
    GoldbergRenderer*   ensureRenderer(int level,
                            const std::unordered_map<int,std::string>& ownership,
                            const std::vector<PoliticalEntity>& entities) const;

    int    m_HoverCell = -1;
    int    m_PaintLevelCached = -1;

    GLuint m_Tex   = 0;
    bool   m_Dirty = true;
    void   initTex();

    // Precompute which cells at each level have finer explicit ownership.
    std::vector<std::unordered_set<int>> buildHasDescendants(
        const CelestialBody& body, int maxLevel) const;

    std::string resolveOwnership(
        const CelestialBody& body,
        const std::vector<std::unordered_set<int>>& hasDesc,
        int level, int cellId) const;
};
