#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "renderer/GoldbergRenderer.h"

class Shader;

class PoliticalMapLayer {
public:
    PoliticalMapLayer();
    ~PoliticalMapLayer();

    PoliticalMapLayer(const PoliticalMapLayer&)            = delete;
    PoliticalMapLayer& operator=(const PoliticalMapLayer&) = delete;

    // Rebuild 3 slots at finestSubdiv, finestSubdiv/2, finestSubdiv/4.
    void rebuildSlots(int finestSubdiv);

    // Mark a slot dirty so it will be rebaked next frame.
    void syncSlot(int slot,
                  const std::unordered_map<int, std::string>& ownership,
                  const std::vector<PoliticalEntity>& entities);

    // Bake equirect texture for one slot and upload to GL.
    void bakeSlot(int slot,
                  const std::unordered_map<int, std::string>& ownership,
                  const std::vector<PoliticalEntity>& entities);

    // For slot i > 0: map each coarse cell centroid to nearest fine-slot cell and inherit ownership.
    std::unordered_map<int, std::string> deriveOwnership(
        int coarseSlot, int fineSlot,
        const std::unordered_map<int, std::string>& fineOwnership) const;

    bool   slotDirty(int slot) const;
    int    slotCount()         const { return (int)m_Slots.size(); }
    GLuint texId(int slot)     const;

    void draw(int slot, Shader& shader, const glm::mat4& vp, const glm::mat4& model) const;
    void setHoverCell(int slot, int cellId);
    int  findCellNearest(int slot, glm::vec3 dir) const;

    const GoldbergGrid* grid(int slot) const;
    bool hasGrid(int slot) const;

    static std::string makeEntityId(const std::vector<PoliticalEntity>& entities);

private:
    struct Slot {
        std::unique_ptr<GoldbergGrid>     grid;
        std::unique_ptr<GoldbergRenderer> renderer;
        GLuint               tex   = 0;
        bool                 dirty = true;
        std::vector<uint8_t> bakeData;
        std::vector<int>     bakeMap;
    };

    std::vector<Slot> m_Slots;

    void initSlotTex(Slot& s);
    void bakeSlotImpl(Slot& s,
                      const std::unordered_map<int, std::string>& ownership,
                      const std::vector<PoliticalEntity>& entities);
};
