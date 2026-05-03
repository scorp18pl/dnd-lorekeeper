#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "renderer/GoldbergRenderer.h"

class Shader;

// Owns the Goldberg grid + renderer, the baked equirect texture, and bake buffers.
// Application delegates all political-map GL state to this class.
class PoliticalMapLayer {
public:
    PoliticalMapLayer();
    ~PoliticalMapLayer();

    PoliticalMapLayer(const PoliticalMapLayer&)            = delete;
    PoliticalMapLayer& operator=(const PoliticalMapLayer&) = delete;

    // Rebuild the grid if subdiv changed. Returns true if a new grid was built.
    bool rebuildGrid(int subdiv);

    // Sync renderer colors from current ownership + entity palette; marks texture dirty.
    void sync(const std::unordered_map<int, std::string>& ownership,
              const std::vector<PoliticalEntity>& entities);

    // Force next bakeAndUpload to re-run even if sync wasn't called.
    void markDirty();

    // Bake the 2048×1024 equirect texture from ownership data and upload to GL.
    // Pass empty maps when no body is active (uploads a transparent texture).
    void bakeAndUpload(const std::unordered_map<int, std::string>& ownership,
                       const std::vector<PoliticalEntity>& entities);

    bool   isDirty() const { return m_Dirty; }
    GLuint texId()   const { return m_Tex;   }

    // Draw Goldberg cell fills (used for hover-highlight in paint mode).
    void draw(Shader& shader, const glm::mat4& vp, const glm::mat4& model) const;

    // Set which cell to highlight (-1 = none).
    void setHoverCell(int cellId);

    // Find the cell whose centroid is closest to dir. Returns -1 if no grid.
    int findCellNearest(glm::vec3 dir) const;

    const GoldbergGrid* grid() const { return m_Grid.get(); }
    bool hasGrid() const             { return m_Grid != nullptr; }

    // Generate a unique "pe_N" ID not already used in entities.
    static std::string makeEntityId(const std::vector<PoliticalEntity>& entities);

private:
    std::unique_ptr<GoldbergGrid>     m_Grid;
    std::unique_ptr<GoldbergRenderer> m_Renderer;
    GLuint               m_Tex   = 0;
    bool                 m_Dirty = true;
    std::vector<uint8_t> m_BakeData;
    std::vector<int>     m_BakeCellMap;
};
