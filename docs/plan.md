# Lorekeeper — Project Plan

## Stack

C++20 · OpenGL 3.3+ · Dear ImGui (docking) · GLFW · GLM · stb_image · nlohmann/json  
Obsidian plugin: TypeScript · Obsidian Plugin API

---

## Architecture

```
┌─────────────────────────────────────────┐
│              ImGui UI Layer             │
├─────────────────────────────────────────┤
│        Command / Undo-Redo Layer        │
├─────────────────────────────────────────┤
│           Scene / Camera Layer          │
├─────────────────────────────────────────┤
│           World Data Layer              │
├─────────────────────────────────────────┤
│         Renderer / OpenGL Layer         │
├─────────────────────────────────────────┤
│        Persistence / IO Layer           │
└─────────────────────────────────────────┘
```

The app exposes a local WebSocket server. The Obsidian plugin connects to it for bidirectional communication (note opened/modified → highlight POI; app action → open note in Obsidian).

---

## Data Model

```
world.json
  name
  calendar                 → CalendarSystem
  obsidian_vault_path      absolute path to Obsidian vault root
  obsidian_ws_port         WebSocket port (default 7331)
  bodies[]                 → CelestialBody

CelestialBody
  id, name
  radius_km
  surface_texture          asset ref
  entities[]               → WorldEntity
  overlays[]               → RegionOverlay
  roads                    → RoadGraph
  sea_routes               → RoadGraph
  travel_records[]         → TravelRecord

WorldEntity  (POI)
  id, name, type           city | town | poi
  lat_deg, lon_deg
  born_day, died_day
  obsidian_note            vault-relative path to linked .md
  media_ref                fallback path if no Obsidian vault

CalendarSystem
  epoch_name
  eras[]                   { name, start_day, end_day }
  months[]                 { name, days }
  week_days[]
  leap_rule                optional { every_n_years, extra_days }

RegionOverlay
  id, name
  center_lat, center_lon
  extent_km
  opacity, visible
  image_path
  height_scale

RoadGraph
  nodes[]                  { id, entity_ref?, lat, lon }
  edges[]                  { id, from, to, distance_km, type }

TravelRecord
  id, party_name, color
  waypoints[]              { lat, lon, day, node_id? }
  snapped_edge_ids[]
```

All dates are integer days since epoch 0. Binary assets use Git LFS.

---

## UI Layout

```
┌──────────────────────────────────────────────────────────────┐
│  [Navigate] [Place▾] [Travel] [Measure]     [Body: Aldoria▾] │  toolbar
├─────────────┬────────────────────────────┬───────────────────┤
│  World Tree │                            │  Inspector        │
│             │       3D Viewport          │                   │
│  Layers     │                            │                   │
│  (toggles)  │                            │                   │
├─────────────┴────────────────────────────┴───────────────────┤
│  Timeline ──●───────────────────────────────────────────────  │
│             14 Harvestmoon, Age of Ash, Yr 312                │
└───────────────────────────────────────────────────────────────┘
                                 [lat 34.2°  lon -12.7°  ≈250 km]
```

---

## Milestones

> Every world mutation goes through a Command with undo/redo wired at the same time — never deferred.

---

### Cleanup — strip codebase to match current scope

- [ ] Remove solar system view, SolarBodyInfo, orbital rendering, solar camera
- [ ] Remove heightmap loading, CPU heightmap, heightmap displacement shader uniform
- [ ] Body switching via toolbar dropdown instead of solar system navigation
- [ ] Remove dead `m_GoldbergShader`, `m_PlanetShader` uniforms that referenced political/heightmap features
- [ ] Simplify `renderPlanet` now that heightmap and solar system are gone
- [ ] Update `world.json` schema: drop `bodies[].heightmap_path`, `bodies[].height_scale`, `orbital_*` fields

---

### Iteration 1 — Timeline

**Goal:** scrub through world history; entities appear and disappear on their born/died dates.

- [ ] `CalendarSystem` data model + JSON persistence
- [ ] Calendar definition UI (eras, months, weekdays, leap rule)
- [ ] Day-integer ↔ calendar string formatter
- [ ] Timeline scrubber bar at the bottom of the viewport
- [ ] Named event markers on the scrubber bar
- [ ] Wire entity visibility to born_day / died_day
- [ ] Events panel: list events within ±N days of current scrubber position
- [ ] `born_day` / `died_day` fields on WorldEntity + editor UI
- [ ] Undo/redo for scrubber-driven mutations

---

### Iteration 2 — Roads & Distance

**Goal:** road network with real distances; click-to-measure tool.

- [ ] `RoadGraph` + `SeaRouteGraph` data model + JSON persistence
- [ ] Road node placement: click globe in road-edit mode to place nodes, click existing entity to attach
- [ ] Edge creation: click two nodes to connect; distance_km auto-computed (great-circle)
- [ ] Road rendering: great-circle arc strips on globe
- [ ] Sea route rendering (same, dashed or distinct color)
- [ ] Distance tool: click two points → Dijkstra shortest path → km + segment list; great-circle fallback
- [ ] Layer toggle + opacity for roads and sea routes
- [ ] Undo/redo for all road edits

---

### Iteration 3 — Party Travel

**Goal:** animate party movement on the timeline scrubber.

- [ ] `TravelRecord` data model + JSON persistence
- [ ] Travel editor: place/move waypoints on globe; snap to road graph nodes
- [ ] Interpolated party position driven by timeline scrubber (great-circle lerp; road-snapped variant)
- [ ] Fading trail rendering; configurable trail window in days
- [ ] Multiple simultaneous parties with distinct colors
- [ ] Travel record panel: list parties, toggle visibility, edit name/color
- [ ] Undo/redo for waypoint edits

---

### Iteration 4 — Obsidian Integration

**Goal:** full bidirectional sync between app POIs and Obsidian vault notes.

#### App side
- [ ] Vault path + WebSocket port configurable per world (stored in `world.json`)
- [ ] Local WebSocket server inside the app (starts when a world is open, stops on close)
- [ ] File watcher on vault directory: detect note create/modify/delete
- [ ] On note change: parse YAML frontmatter for `lorekeeper_id`, `lat`, `lon`; update matching POI in world data
- [ ] Per-POI "Open in Obsidian" action → `obsidian://open?vault=...&file=...` URI
- [ ] Per-POI "Create note" action → create `.md` in vault with frontmatter pre-filled (`lorekeeper_id`, `lat`, `lon`, `name`)
- [ ] Inspector shows linked note title + last-modified time; highlights POI when its note is active in Obsidian
- [ ] Vault browser panel: list all vault notes, filter by linked/unlinked, click to fly camera to linked POI

#### Obsidian plugin
- [ ] Plugin connects to app WebSocket on load; auto-reconnects
- [ ] On active note change → send `{ event: "note_opened", path }` to app
- [ ] On note save → send `{ event: "note_saved", path, frontmatter }` to app
- [ ] Receives `{ event: "open_note", path }` from app → opens the note
- [ ] Receives `{ event: "highlight_poi", id }` from app → shows a status-bar indicator with POI name
- [ ] Ribbon icon: connect/disconnect toggle with connection status
- [ ] Settings tab: server host + port (default `localhost:7331`)

---

### Iteration 5 — Polish

**Goal:** export, UX cleanup, performance pass.

- [ ] Export current globe view to PNG at user-defined resolution and date
- [ ] Scale bar calibrated to current zoom (HUD, bottom-right)
- [ ] "New World" wizard: name, first body, calendar presets
- [ ] Overlay export: selected overlay or composite → flat PNG
- [ ] Performance profiling pass (render loop, file watcher, WebSocket overhead)
- [ ] Keyboard shortcuts reference panel
