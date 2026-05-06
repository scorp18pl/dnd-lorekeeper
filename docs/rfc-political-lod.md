# RFC: Political Map Quadtree LOD System

**App:** Lorekeeper (C++/OpenGL/ImGui)
**Replaces:** Flat 3-slot LOD (`PoliticalMapLayer::rebuildSlots`)
**Status:** Proposed

---

## 1. Data Model

**Persistent (stored in `CelestialBody`, serialized to `world.json`)**

Replace the current `goldberg_resolution + cell_ownership` pair with:

```cpp
struct CelestialBody {
    // ... existing fields ...
    double  radius_km        = 6371.0;
    int     lod_min_cell_km  = 1;   // target finest cell diameter in km
    // Replaces: int goldberg_resolution + unordered_map<int,string> cell_ownership
    // key = level index (0..max_level); value = sparse map of {cell_id -> entity_id}
    std::vector<std::unordered_map<int, std::string>> lod_ownership;
    // lod_ownership[L][cell_id] = entity_id  (absent = not explicitly set)
};
```

`lod_ownership` is sized to `max_level + 1` lazily; levels with no painted cells are empty maps (zero cost).

**Runtime (not serialized, owned by `PoliticalMapLayer`)**

```cpp
// LRU cache of GoldbergGrid instances. Only levels that are actually painted
// (or their ancestors) need to be resident. Fine levels (high L) are only
// built when the user zooms close enough to paint there.
struct GridCache {
    struct Entry {
        int                           level = -1;
        std::unique_ptr<GoldbergGrid> grid;
        uint32_t                      lru_tick = 0;
    };
    static constexpr int kCapacity = 16;  // enough for all levels 0..13
    std::array<Entry, kCapacity> slots;
    uint32_t tick = 0;

    const GoldbergGrid* get(int level);   // builds on miss, evicts LRU
};

// Single baked texture — view-dependent, rebuilt when camera moves.
GLuint baked_tex   = 0;
bool   bake_dirty  = true;
```

Also add to `CelestialBody` data model (alongside `lod_ownership`):

```cpp
// has_descendants[L] = set of cells at level L that have at least one
// explicitly-owned descendant at any finer level. Updated whenever lod_ownership
// is written. Enables O(1) "should I split?" check during the bake traversal.
std::vector<std::unordered_set<int>> lod_has_descendants;
```

When painting cell C at level L: insert C's parent chain into `lod_has_descendants[L-1]`, `lod_has_descendants[L-2]`, … down to level 0.

---

## 2. Level Count Formula

Goldberg G(n,0) has `10n² + 2` cells on a sphere of radius R km. Level L uses `n = 2^L`.

```cpp
int computeMaxLevel(double radius_km, double min_cell_km) {
    // solve: 2*pi*R / (2^L * sqrt(10)) <= min_cell_km
    double ratio = (2.0 * M_PI * radius_km) / (min_cell_km * std::sqrt(10.0));
    return (int)std::ceil(std::log2(ratio));
}
```

Default: `radius_km=6371`, `min_cell_km=1` → `max_level=13` (G(8192), ~670M cells). Levels with no painted cells cost nothing; max_level is a theoretical cap, not a memory allocation.

| Level | n=2^L | Approx cells | Cell diameter (R=6371km) |
|-------|-------|--------------|--------------------------|
| 0     | 1     | 12           | ~8 000 km                |
| 3     | 8     | 642          | ~1 000 km                |
| 5     | 32    | 10 242       | ~250 km                  |
| 7     | 128   | 163 842      | ~62 km                   |
| 10    | 1 024 | 10 485 762   | ~7.8 km                  |
| 13    | 8 192 | ~670 M       | ~1 km                    |

---

## 3. Grid Cache

Capacity 16 — sufficient for all 14 levels (0–13). Only levels that are actually referenced during a bake are ever constructed. `GoldbergGrid(2^L)` construction is O(n²) and expensive at high L; it only runs the first time a given level is needed.

```cpp
const GoldbergGrid* GridCache::get(int level) {
    for (auto& e : slots)
        if (e.level == level) { e.lru_tick = ++tick; return e.grid.get(); }
    Entry& victim = *std::min_element(slots.begin(), slots.end(),
        [](auto& a, auto& b){ return a.lru_tick < b.lru_tick; });
    victim = { level, std::make_unique<GoldbergGrid>(1 << level), ++tick };
    return victim.grid.get();
}
```

In practice, the levels accessed during any single bake are bounded by the deepest painted level, which is determined by the user's workflow — not the theoretical `max_level`. A user who only paints at level 5 never causes levels 6–13 to be built.

---

## 4. Ownership Resolution

```cpp
// Returns entity_id for (level L, cell C), or "" if unowned.
std::string resolveOwnership(
    const CelestialBody& body, GridCache& cache,
    int level, int cell_id)
{
    for (int L = level; L >= 0; --L) {
        if (L < (int)body.lod_ownership.size()) {
            auto it = body.lod_ownership[L].find(cell_id);
            if (it != body.lod_ownership[L].end() && !it->second.empty())
                return it->second;
        }
        if (L == 0) break;
        // Walk up: find parent cell at level L-1
        const GoldbergGrid* fine   = cache.get(L);
        const GoldbergGrid* coarse = cache.get(L - 1);
        if (!fine || !coarse) break;
        cell_id = coarse->findCellNearest(fine->cells()[cell_id].centroid);
    }
    return "";
}
```

**Parent mapping cache:** Before baking level L, precompute `vector<int> parentMap` where `parentMap[c]` = parent cell id in level L-1. Done once per `(L, L-1)` pair, reused for all cells in the bake.

---

## 5. Split Criterion (Per-Cell, Not Global)

There is no single global "active level". Instead, the bake traverses the quadtree per-pixel, and each cell independently decides whether to split into its children based on **two conditions that must both be true**:

```
split(L, cell, cam_pos) =
    lod_has_descendants[L].count(cell)          // finer ownership data exists here
    AND
    dist3d(cam_pos, cell_centroid[L]) < k / (1 << L)   // camera is close enough
```

`k` is a tunable split constant in normalized sphere units (planet radius = 1). A value of `k = 4.0` means: at level 0 (12-cell globe), split when camera is within 4 radii. At level 5, split when within 4/32 = 0.125 radii (~800km on Earth). Adjust `k` to taste; expose as a settings constant.

The first condition (data exists) ensures we never descend into empty regions. The second condition (camera close enough) is the view-dependent LOD: cells near the camera nadir get fine levels; cells at the limb or back of the planet stay coarse. **Multiple levels coexist across the sphere simultaneously.**

---

## 6. Bake Pipeline

**Texture:** Single 2048×1024 equirectangular RGBA8. One texture for the whole planet, view-dependent content. Rebaked when camera moves by more than a threshold angle/distance (hysteresis: ~5° rotation or 10% altitude change).

**Bake procedure — per pixel:**

```cpp
// cam_pos: camera position in normalized sphere coords (unit sphere)
void bakePixel(int px, int py, glm::vec3 cam_pos, ...) {
    float lon = ((px + 0.5f) / W) * 2*PI - PI;
    float lat = ((py + 0.5f) / H - 0.5f) * PI;
    glm::vec3 dir = latLonToSphere(lat, lon);

    // Quadtree descent: start at root, split while criterion holds
    int   level   = 0;
    int   cell_id = cache.get(0)->findCellNearest(dir);

    while (level < max_painted_level) {
        bool has_children = body.lod_has_descendants[level].count(cell_id);
        float dist = glm::length(cam_pos - cache.get(level)->cells()[cell_id].centroid);
        bool close_enough = dist < split_k / (float)(1 << level);
        if (!has_children || !close_enough) break;
        ++level;
        cell_id = cache.get(level)->findCellNearest(dir);
    }

    std::string owner = resolveOwnership(body, cache, level, cell_id);
    writePixelColor(px, py, owner);
}
```

The Voronoi warm-start walk (existing `bakeSlotImpl` technique) still applies within each level to avoid O(n) `findCellNearest` per pixel: seed the walk from the previous pixel's cell.

**Border pass:** same 4-neighbor scan as current code — detects cell boundaries and writes dark border pixels.

**Upload:** `glTexImage2D` + `glGenerateMipmap`. Min filter `GL_LINEAR_MIPMAP_LINEAR`.

**Invalidation triggers (mark `bake_dirty = true`):**
- Any `lod_ownership` write (paint stroke completed).
- Any `PoliticalEntity` color change.
- Camera moves beyond hysteresis threshold (view-dependent content changed).

---

## 7. Paint Interaction

**Level selector UI** (`Application_ui.cpp`, political map panel):

```cpp
int max_level = computeMaxLevel(body.radius_km, body.lod_min_cell_km);
ImGui::SliderInt("Paint Level", &m_PaintLevel, 0, max_level);
ImGui::TextDisabled("~%.0f km/cell",
    2.0 * M_PI * body.radius_km / ((1 << m_PaintLevel) * std::sqrt(10.0)));
```

**Painting:** `m_HoverCellId` is resolved against `GridCache::get(m_PaintLevel)`. On paint: `body.lod_ownership[m_PaintLevel][cell_id] = entity_id`. Dirty `LevelTex` for L=m_PaintLevel and all finer resident levels.

**Grid overlay:** Always renders the wire overlay for `m_PaintLevel` (not view level), so the user sees the cells they are painting regardless of zoom.

---

## 8. Serialization

**New format** (`world.json` version `"0.2"`):

```json
{
  "version": "0.2",
  "bodies": [{
    "lod_min_cell_km": 1,
    "lod_ownership": {
      "3": { "42": "pe_0", "117": "pe_1" },
      "5": { "1823": "pe_0" }
    }
  }]
}
```

Keys in `lod_ownership` are level indices (as strings). Absent levels are not written.

**Backward compat loader:**

```cpp
// Migrates all three legacy formats to lod_ownership
if (!bj.contains("lod_ownership")) {
    int subdiv = 32;
    std::unordered_map<int,std::string> old_ownership;

    if (bj.contains("political_levels") && !bj["political_levels"].empty()) {
        subdiv        = bj["political_levels"][0].value("subdiv", 32);
        old_ownership = loadOwnershipObj(bj["political_levels"][0]["cell_ownership"]);
    } else {
        subdiv        = bj.value("goldberg_resolution", 32);
        old_ownership = loadOwnershipObj(bj["cell_ownership"]);
    }
    int level = (int)std::round(std::log2(std::max(1, subdiv)));
    body.lod_min_cell_km = 1;
    body.lod_ownership.resize(level + 1);
    body.lod_ownership[level] = std::move(old_ownership);
}
```

---

## 9. Open Questions

1. **Bake latency:** The per-pixel quadtree descent adds `findCellNearest` calls for each level traversed. With the Voronoi warm-start, cost is O(pixels × average_depth). Average depth depends on how much of the screen is near the camera nadir. Likely 100–300ms at fine levels; async bake (thread + fence) is the safe choice if this blocks the UI noticeably.

2. **Max paint level cap:** Level 13 grids (~670M cells) are impractical to construct even once. The UI paint level slider should show an estimate of cell size in km and let the user judge. A soft warning above level 10 (~12km cells) is appropriate; no hard cap.
