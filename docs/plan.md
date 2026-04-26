# Lorekeeper — Project Plan

## Overview

A native desktop worldbuilding tool for creating spatially and temporally consistent fictional worlds. Supports full solar system hierarchies (star → planet → moon → continent → city → building), layered map rendering with Goldberg-polyhedron tiles (political + geographic + biome), climate simulation, local projection overlays, an animated timeline scrubber, party travel tracking with road-graph distance, and `.md` lore links.

**Stack:** C++20 · OpenGL 3.3+ · Dear ImGui (docking) · GLFW · GLM · stb_image · nlohmann/json

---

## Architecture

### Application Layers

```
┌─────────────────────────────────────────┐
│              ImGui UI Layer             │  panels, tabs, timeline, toolbars
├─────────────────────────────────────────┤
│        Command / Undo-Redo Layer        │  all mutations go through commands
├─────────────────────────────────────────┤
│           Scene / Camera Layer          │  viewport, orbital cam, zoom, raycasting
├─────────────────────────────────────────┤
│           World Data Layer              │  solar system, entities, events, climate
├─────────────────────────────────────────┤
│         Renderer / OpenGL Layer         │  cube sphere, Goldberg, labels, overlays
├─────────────────────────────────────────┤
│        Persistence / IO Layer           │  JSON load/save, asset streaming
└─────────────────────────────────────────┘
```

### View Hierarchy (drill-down navigation)

```
Solar System  →  Planet/Moon  →  Region  →  Building (interior)
```

Each level is a separate render mode. Transitions are animated (zoom-in/out).

---

## Data Model

All world state stored as human-readable JSON, git-tracked. Binary assets (heightmaps, textures) use Git LFS.

### File Layout

```
world/
  world.json                 # solar system root, calendar, political entities, world notes
  bodies/
    <id>/
      body.json              # planet/moon metadata
      tiles/
        ownership.json       # Goldberg cell-id → political entity + date ranges
        geography.json       # Goldberg cell-id → geographic region membership
        biome.json           # Goldberg cell-id → climate sim data + biome + overrides
      layers/
        cities.json          # city/poi entities
        roads.json           # road graph (nodes + edges with distance_km)
        sea_routes.json      # naval routes graph
        travel.json          # party travel records
        overlays.json        # region overlay definitions
      entities/
        <id>.json            # city/location/poi with position + timeline
  media/
    <entity-id>.md           # lore pages linked from entities
    notes/
      <id>.md                # unattached world notes
  assets/
    textures/                # (Git LFS)
    heightmaps/              # (Git LFS) tiled, see streaming strategy
```

### Key Types (conceptual)

```
SolarSystem
  stars[]
  bodies[]                 → CelestialBody
  calendar                 → CalendarSystem
  political_entities[]     → PoliticalEntity
  geographic_regions[]     → GeographicRegion  (stored per-body but referenced here)
  world_notes[]            → WorldNote

CalendarSystem
  epoch_name               e.g. "Year of the First Flame"
  eras[]                   → Era { name, start_day, end_day }
  months[]                 → Month { name, days }
  week_days[]              string[]
  leap_rule                optional { every_n_years, extra_month | extra_day }
  The length of one "day" integer = the home planet's rotation_period_hours.

CelestialBody
  id, name, type           (star | planet | moon | asteroid)
  parent_id                null for top-level bodies
  radius_km
  axial_tilt_deg
  orbital_period_days      in world-days
  rotation_period_hours    defines the length of one calendar day unit
  surface_texture          asset ref
  heightmap_root           asset ref → tiled heightmap root directory
  goldberg_resolution      GP subdivision level (determines cell count)
  layers[]                 → MapLayer
  entities[]               → WorldEntity ref ids
  timeline[]               → TimelineEvent

PoliticalEntity
  id, name
  color                    hex
  type                     (empire | kingdom | duchy | city-state | tribe | ...)
  liege_id                 → PoliticalEntity id, null if sovereign
  capital_id               → WorldEntity (city) ref
  founded_day, dissolved_day
  media_ref                → .md lore page
  Note: tile ownership (which cells this entity controls) is in tiles/ownership.json
  Note: vassal chain is resolved by following liege_id recursively

GeographicRegion           (per body, stored in tiles/geography.json + world.json index)
  id, name
  type                     (forest | mountain_range | sea | ocean | desert | river |
                            plains | swamp | tundra | volcanic | ...)
  cell_ids[]               Goldberg cell indices belonging to this region (fixed for region lifetime)
  label_anchor_lat/lon     where the name label appears on globe
  label_visible            bool, true by default
  color, opacity
  born_day, died_day       optional — the whole region appears/disappears at these dates.
                           To model a forest becoming plains, use two overlapping regions
                           with complementary date ranges covering the same cells.
  media_ref

BiomeCell                  (tiles/biome.json, one entry per Goldberg cell)
  cell_id
  temperature_c            simulated value
  humidity_pct             simulated value
  wind_dir_deg             simulated value (prevailing direction)
  elevation_m              sampled from heightmap at cell centroid
  biome                    auto-calculated (tundra | boreal | temperate | mediterranean |
                            arid | tropical | ocean | alpine | ...)
  biome_override           optional user override (null = use calculated)

TileOwnership              (tiles/ownership.json)
  cell_id                  Goldberg cell index (integer)
  entries[]                → { political_entity_id, from_day, to_day }
  Each cell owned by 0 or 1 entity at any given date.
  Vassal relationships live on PoliticalEntity, not on cells.

RoadGraph                  (layers/roads.json)
  nodes[]                  → { id, entity_ref (optional — null for bare intersections), lat, lon }
  edges[]                  → { id, from_node, to_node, distance_km, type (road|path|...) }
  Distance along a route = sum of edge distances on the shortest path between nodes.
  Used for distance measurement and travel interpolation snapping.

SeaRouteGraph              (layers/sea_routes.json)
  Same structure as RoadGraph, flagged naval.
  Travel interpolation snaps to sea route edges when road_ref points to a sea route.

WorldEntity
  id, name, type           (city | ruin | poi | building | ...)
                           Building is the smallest particle — castles, dungeons, temples,
                           towers etc. are all buildings differentiated by tags/name, not type.
  position                 { lat, lon } for sphere, { x, y } for local (building interior) view
  born_day, died_day
  media_ref                → .md lore page
  timeline[]               → TimelineEvent

TravelRecord
  id, party_name, color
  waypoints[]              → { node_id (optional), lat, lon, day }
  route_type               land | sea | air
  snapped_edge_ids[]       optional ordered list of RoadGraph/SeaRouteGraph edge IDs
                           defining the snapped path; interpolation follows these edges.
  Interpolated position at a given day = lerp between waypoints,
  projected along snapped_edge_ids if set (great-circle arc otherwise).

RegionOverlay
  id, name
  center_lat, center_lon
  extent_km                N×N km square
  layers[]                 → { type: image|paint, asset_ref, blend_mode, opacity, visible }
  z_order
  media_ref

WorldNote                  (unattached to any entity)
  id, title
  created_day              optional in-world date
  file_path                → media/notes/<id>.md

Bookmark                   (persisted in app config, not world data — user preference)
  id, name
  body_id
  lat, lon, zoom
  view_type                globe | local | solar_system

TimelineEvent
  day                      integer
  label
  entity_changes[]         what changed
```

---

## Core Systems

### 1. Spherical Renderer
- **Mesh:** Cube sphere (6 cube faces, each with independent quadtree LOD). No pole pinching, uniform vertex distribution, equirectangular UVs via per-vertex lat/lon pass.
- Heightmap displacement in vertex shader (tiled streaming, see §14)
- Quadtree LOD per cube face: subdivide by screen-space error
- Atmosphere/haze pass (optional toggle)

### 2. Goldberg Polyhedron Tiles + LOD
- Generated once per body from GP subdivision level, cached as `goldberg.json`
- **Exactly 12 pentagons**, rest hexagons — near-uniform cell size everywhere on sphere
- **Three tile-layer types using the same Goldberg grid:** political ownership, geographic regions, biome data
- **Visual LOD:** data is stored at full resolution; at low zoom, cells below a minimum screen size are aggregated for display — dominant owner/biome/region wins in each aggregate cluster. Border lines are suppressed below a screen-pixel threshold.
- Paint mode per layer type: click/drag to assign cells

### 3. Solar System View
- Star at center, planets/moons as textured cube spheres with orbital ellipses
- Scale toggle: realistic vs. illustrative
- Click body → animated zoom-in to Planet View

### 4. Layer System
- Each layer: toggle + opacity slider
- Render order (adjustable):
  1. Terrain / heightmap
  2. Biome overlay (cell colors)
  3. Geographic regions (cell tint + border + label)
  4. Political overlay (cell colors + borders)
  5. Region overlays / map decals
  6. Roads + sea routes
  7. Cities / POIs (billboard markers)
  8. Travel record trails + party markers
  9. Custom user layers

### 5. Custom Calendar System
- Defined in `world.json`: eras, months, weekdays, leap rule, rotation period
- All dates stored as integer days since epoch 0 internally
- Calendar formatter converts day-int → display string ("14th of Harvestmoon, Age of Ash, Yr 312")
- Timeline scrubber operates on raw integers; display updates live

### 6. Timeline System
- ImGui scrubber bar at bottom of viewport; named event markers on bar
- Scrubbing drives:
  - Entity visibility (`born_day` / `died_day`)
  - Tile ownership replay (political layer)
  - Geographic region visibility (born/died)
  - Party travel position interpolation
- Events panel: list of events within ±N days of current position

### 7. Political Entity & Vassal Hierarchy
- Political entities defined globally in `world.json`
- Each entity has an optional `liege_id` → parent entity (recursive — full feudal trees supported)
- Tile ownership is flat per-cell (0 or 1 entity), vassal relationship is on the entity, not on cells
- Political inspector panel shows: entity info, full liege chain upward, all direct vassals, cell count, timeline
- Visual option: shade vassal territories slightly darker than liege territory color

### 8. Geographic Regions
- Named zones (forests, mountain ranges, seas, etc.) defined by sets of Goldberg cells
- Labels rendered on the globe by default (no toggle required for visibility), anchored to `label_anchor_lat/lon`
- Layer can be hidden but labels default to on
- Separate from political tiles — a forest remains a forest regardless of who controls it

### 9. Climate Simulation & Biome Layer
- Runs as a one-shot simulation pass over all Goldberg cells on demand ("Recalculate Climate")
- Inputs per cell: latitude (from cell centroid), elevation (sampled from heightmap), distance to ocean (derived from geographic region ocean cells), wind_dir from neighboring cells
- Simulated outputs: `temperature_c`, `humidity_pct`, `wind_dir_deg`
- Biome classification: Whittaker-style (temperature × humidity → biome category)
- User can override any cell's biome manually (`biome_override`)
- Biome, temperature, humidity, and wind direction each visualizable as a separate sub-layer

### 10. Road Graph & Distance Measurement
- Roads stored as a graph: nodes (POIs/intersections at lat/lon), edges (segments with precalculated `distance_km`)
- Sea routes same structure, separate graph, flagged naval
- **Distance tool:** click two points → shortest path computed (Dijkstra on road/sea graph), displays total distance in km + segment breakdown
- For points not on the graph, great-circle distance is used as fallback
- Road edges visible as great-circle arc strips on globe

### 11. Party Travel & Path Interpolation
- TravelRecord: ordered waypoints `(lat/lon or node_id, day)`
- Interpolated position at scrubber day = lerp between waypoints; if `road_ref` is set, snaps to road graph path
- Multiple simultaneous parties, distinct colors, fading trail (configurable window)
- Waypoints editable in travel editor (click globe to place, drag to move)

### 12. Local Projection Overlays (Map Decals)
- RegionOverlay: center lat/lon + N×N km extent, anchored to globe
- Internal layer stack: imported images, painted layers, vector annotations; composited top-to-bottom with blend modes
- Rendered into offscreen framebuffer, projected (azimuthal equidistant, accurate to ~400 km) and blended onto sphere after terrain, before labels
- Curvature warning shown for extents > 400 km
- Export: selected overlay or composite → flat PNG at user-defined resolution

### 13. Entity Inspector (ImGui panel)
- Click any entity, cell, region, or overlay to inspect
- Shows: metadata, timeline events, political/geographic membership, vassal chain for political entities
- "Open Lore" → system editor for linked `.md`

### 14. Heightmap Streaming
- Heightmaps stored as a quadtree tile pyramid (pre-processed at import time into tile images)
- Tiles loaded asynchronously on demand; evicted from GPU memory by LRU when budget exceeded
- Only tiles visible at current LOD level are resident
- Import pipeline (C++ MapImporter) pre-tiles heightmaps from Gaea exports at import time

### 15. Ray Casting & Entity Selection
- Mouse ray cast against: cube sphere surface → lat/lon hit point
- Hit point used to find: nearest entity (city/POI within N screen pixels), Goldberg cell (for tile paint/inspect), overlap with region overlays
- Priority order: entities > overlay bounds > Goldberg cell > bare terrain

### 16. Label Rendering (3D Globe)
- Geographic region names and city/POI labels rendered in the 3D pass using SDF (Signed Distance Field) font atlas
- Labels are billboarded, scale with zoom, occlude behind the horizon
- City label size scales with entity importance (capital > city > town > POI)
- Geographic labels anchored to user-set lat/lon point, visible by default

### 17. Fuzzy Finder
- Global search (Ctrl+F): fuzzy matches against entity names, region names, political entities, world notes, lore file titles
- Results ranked by match score; selecting one flies the camera to the entity or opens the lore tab
- Searches across all bodies in the solar system

### 18. Content Tabs (Browser-Style Navigation)
- Viewport and inspector areas support named tabs with back/forward navigation history
- Tab types: Globe view (per body), Building interior (per building entity), Lore panel (per .md file), Note
- Bookmarks saved in app config; reopening a bookmark opens or focuses its tab

### 19. Undo / Redo
- All world mutations go through a Command interface (place entity, paint cell, add waypoint, etc.)
- Command stack stored in memory (not persisted); session-length undo
- Commands produce minimal JSON diffs for efficiency
- Ctrl+Z / Ctrl+Y

### 20. Coordinate & Scale Display
- HUD overlay (ImGui, bottom-right of viewport): cursor lat/lon in degrees, altitude above surface
- Scale bar: screen-space ruler calibrated to current zoom ("≈ 250 km")
- Both update continuously as mouse moves / camera orbits

### 21. Map Import (C++ port of Python utility)
- Original Python script left untouched in `.ignored/python_script/`
- C++ port in `src/import/MapImporter`: projection conversion + sector crop + heightmap tiling
- Called at import time to convert Gaea exports into the tiled asset format

---

## UI Layout (ImGui)

```
┌────────────────────────────────────────────────────────────────┐
│  [Navigate] [Paint▾] [Place▾] [Travel] [Measure]  [⌕ Search] │  toolbar
├──────────────┬──────────────────────────────┬──────────────────┤
│  World Tree  │  [Aldoria ×] [The Keep ×] [+]│  Inspector       │
│  (hierarchy) ├──────────────────────────────┤  (entity / layer │
│              │        3D Viewport           │   / political    │
│  Bookmarks   │                              │   entity props)  │
│              │                              │                  │
├──────────────┼──────────────────────────────┴──────────────────┤
│  Layers      │  Timeline ──●───────────────────────────────── │
│  (toggles)   │             14 Harvestmoon, Age of Ash, Yr 312  │
└──────────────┴────────────────────────────────────────────────┘
                                            [lat 34.2° lon -12.7°  ≈250km ───]
```

Paint submenu: Political | Geographic | Biome override
Place submenu: City | POI | Road node | Waypoint | Overlay | Note

---

## Persistence & Git Strategy

- World data: JSON, committed normally — fully diffable
- Binary assets: **Git LFS** (`*.png`, `*.raw`, `*.exr`, `*.jpg`)
- Recommended commit discipline: one commit per meaningful world event — commit message becomes in-world changelog
- Branching for alternate timelines / "what-if" campaigns
- `.gitignore`: build artifacts, IDE files, `*.o`, `*.exe`, `build/`

---

## Milestones

> **Rule (applies to every iteration):** every world mutation must go through a Command and wire undo/redo at the same time as the feature — never deferred.

---

### MVP — "It Moves"
**Goal:** a textured globe you can orbit, place named locations on, and link lore to. Enough to genuinely start mapping a world on day one.  
**Not yet:** LOD, Goldberg tiles, timeline, roads, solar system, overlays.

- [x] CMake, GLFW, GLM, ImGui docking, stb_image, nlohmann/json
- [x] Window + OpenGL 3.3, ImGui docking layout
- [x] Arcball orbital camera
- [x] Cube sphere, single resolution, equirectangular texture (no LOD yet)
- [x] Command / undo-redo framework (Ctrl+Z / Ctrl+Y)
- [x] Place cities / POIs on globe by clicking
- [ ] Drag entity to reposition
- [x] Screen-projected ImGui labels (placeholder — replaced by SDF in Iter 3)
- [x] Ray casting: sphere hit → nearest entity selection
- [x] Entity inspector: name, type, position
- [ ] Entity born/died dates (deferred to Iter 5 — Timeline)
- [x] `.md` lore link per entity → open in system default editor
- [x] Save / load world JSON (entities + body metadata)
- [x] Coordinate readout (lat/lon) + scale bar HUD

---

### Iteration 1 — Solar System
**Goal:** multiple bodies; navigate the solar system.

- [x] Parse solar system from `world.json`; render star + planets/moons as textured spheres
- [x] Orbital ellipse lines, scale toggle (realistic vs. illustrative)
- [x] Click body → navigate to planet view; Solar System button returns to system view
- [ ] Animated zoom-in / zoom-out transition between views

---

### Iteration 2 — Terrain & Import
**Goal:** real heightmap terrain with LOD; import pipeline from Gaea.

- [ ] Quadtree LOD per cube face (screen-space error metric)
- [ ] Heightmap displacement vertex shader
- [ ] Tiled heightmap pyramid format; async streaming + LRU GPU eviction
- [ ] C++ MapImporter: projection conversion, sector crop, heightmap tiling (Python script ported; original untouched)
- [ ] `rotation_period_hours` on CelestialBody

---

### Iteration 3 — Political Map
**Goal:** paint political territories; vassal/liege hierarchy; borders auto-derived.

- [ ] Goldberg polyhedron generator (icosahedron subdivide → project to sphere)
- [ ] Cell ID scheme (integer index) + adjacency graph
- [ ] Visual LOD: cell aggregation at low zoom; border suppression below pixel threshold
- [ ] SDF font atlas — upgrades MVP's ImGui projected labels to proper 3D-pass SDF rendering
- [ ] Full ray casting: expand MVP's entity hit to also cover Goldberg cell + overlay bounds; priority: entity > overlay > cell > terrain
- [ ] PoliticalEntity data model (liege/vassal hierarchy, color, type, capital) + JSON persistence
- [ ] Political entity editor panel (create/edit entities, assign liege)
- [ ] Paint mode: cells → political entity by date
- [ ] Border derivation from ownership diff; Laplacian smoothing
- [ ] Political color overlay + liege/vassal shading
- [ ] Date-aware ownership query (given day → owning entity per cell); scrubber integration deferred to Iter 5

---

### Iteration 4 — Geography & Biome
**Goal:** named geographic zones (forests, seas, mountains) with globe labels; simulated climate + biome.

- [ ] GeographicRegion data model + JSON persistence
- [ ] Paint mode: cells → geographic regions
- [ ] SDF region name labels billboarded on globe, visible by default; date-aware (born/died)
- [ ] Geographic region inspector (cell list, lore link)
- [ ] BiomeCell data model + JSON
- [ ] Climate simulation pass (latitude, elevation, ocean proximity from geography, wind propagation)
- [ ] Whittaker-style biome classification
- [ ] Biome override per cell
- [ ] Sub-layer visualizations: biome, temperature, humidity, wind direction
- [ ] Scrubber integration for region visibility deferred to Iter 5

---

### Iteration 5 — Timeline
**Goal:** scrub through world history; all layers respond.

- [ ] Calendar definition UI (eras, months, weekdays, leap rule)
- [ ] Day-integer ↔ calendar string conversion
- [ ] Timeline scrubber bar + named event markers
- [ ] Wire entity visibility (born_day / died_day) — entities from MVP
- [ ] Wire political ownership replay — from Iter 3
- [ ] Wire geographic region visibility — from Iter 4
- [ ] Events panel (±N days of current scrubber position)

---

### Iteration 6 — Roads & Distance
**Goal:** road network with real distances; measurement tool.

- [ ] Road graph data model + JSON (nodes with optional entity_ref, edges with distance_km, type)
- [ ] Sea route graph (same structure, naval flag)
- [ ] Road / sea route rendering (great-circle arc strips)
- [ ] Distance tool: click two points → Dijkstra shortest path → km + segment breakdown; great-circle fallback off-graph
- [ ] Layer toggle + opacity UI for all layers (roads, sea routes, political, geographic, biome, entities, travel, overlays)

---

### Iteration 7 — Party Travel
**Goal:** animate party movement through the world on the timeline scrubber.  
*Depends on Iter 5 (scrubber) + Iter 6 (road graph for snapping).*

- [ ] TravelRecord data model + JSON (waypoints with node_id, lat/lon, day; snapped_edge_ids path)
- [ ] Travel editor: place/move waypoints on globe; snap to road graph nodes
- [ ] Interpolated party position on scrub (great-circle lerp; road-edge-snapped variant)
- [ ] Fading trail rendering; multiple simultaneous parties with distinct colors
- [ ] Wire party positions to timeline scrubber

---

### Iteration 8 — Navigation UX
**Goal:** fast navigation across a large world.

- [ ] Fuzzy finder (Ctrl+F): entities, regions, political entities, lore titles, notes — across all bodies; camera flies to selection
- [ ] Content tabs with back/forward history (Globe, Building interior, Lore, Note tab types)
- [ ] Bookmarks (app config, not world data): save/load named camera positions

---

### Iteration 9 — Overlays
**Goal:** register external detailed maps (Gaea, Inkarnate, hand-drawn) on the globe and composite them.

- [ ] RegionOverlay data model + JSON
- [ ] Azimuthal equidistant projection → offscreen framebuffer → sphere blend pass
- [ ] Overlay layer stack UI (import image, reorder, blend mode, opacity)
- [ ] Curvature warning for extents > 400 km
- [ ] Export selected overlay or composite as flat PNG

---

### Iteration 10 — Full Lore & Notes
**Goal:** unattached world notes; inline markdown viewer.

- [ ] WorldNote (unattached): create, list, open in tab or system editor
- [ ] Built-in read-only markdown viewer panel (ImGui)
- [ ] Lore links on any entity, region, overlay (previously MVP opened in system editor only)

---

### Iteration 11 — Building Interiors
**Goal:** drill down into buildings; edit multi-floor floor plans.  
*Buildings are the smallest particle — dungeons, temples, towers are all buildings.*

- [ ] Building entity on globe has lat/lon anchor + interior canvas reference
- [ ] Zoom-in transition from globe into building interior view
- [ ] 2D top-down editor: rooms, walls, doors, stairs, POIs as local entities with lore links
- [ ] Multi-floor: named 2D canvases per floor; stair/ladder links between floors
- [ ] Local entities support born_day / died_day

---

### Iteration 12 — Polish
**Goal:** export, onboarding, atmosphere, performance pass.

- [ ] Export: render globe view to PNG at specified date + layer config
- [ ] Atmosphere / haze shader on planets
- [ ] "New World" wizard (calendar, first planet, Goldberg resolution choice)
- [ ] Performance profiling pass (LOD, Goldberg aggregation, heightmap streaming, overlay compositing)

---

## Decided

| Topic | Decision |
|---|---|
| Calendar | Fully custom — eras, months, weekdays, leap rules; integer days internally |
| Sphere mesh | Cube sphere (6-face quadtree) — uniform distribution, no pole pinching |
| Political tiles | Goldberg polyhedron cells (0 or 1 entity per cell); vassal hierarchy on PoliticalEntity, not on cells |
| Geographic regions | Goldberg cell sets, labelled by default with SDF text on globe |
| Biome layer | Per Goldberg cell; climate simulated (temp/humidity/wind), Whittaker classification, user override |
| Goldberg LOD | Visual aggregation at low zoom; data always at full resolution |
| Region overlays | N×N km azimuthal equidistant decals; layered, composited, exportable |
| Road system | Graph (nodes + edges with distance_km); Dijkstra for distance/routing |
| Sea routes | Separate naval graph, same structure as road graph |
| Distance tool | Road-graph shortest path; great-circle fallback off-graph |
| Labels | SDF font atlas rendered in 3D pass; geographic labels on by default |
| Python script | Port to C++ `src/import/MapImporter`; original untouched |
| Travel records | Interpolated waypoints; road-snapped; fading trail |
| Undo/redo | Command pattern, session-length, JSON diff per command |
| Persistence | JSON + Git LFS for binaries; tiled heightmap pyramid |
| Platform | Native desktop, C++20, OpenGL, ImGui |
| Multiplayer | Offline only |

---

## Future Upgrades (post-v1)

- **Entity relationship graph** — model alliances, wars, trade agreements between political entities as typed, date-ranged edges (entity A → "at war with" → entity B, from_day to_day). Could drive map highlighting (show all enemies of a kingdom) and future simulation hooks.
- **Multiple solar systems / universe level** — not in scope per project; one solar system per world file.
