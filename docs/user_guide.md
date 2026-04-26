# Lorekeeper — User Guide

## Overview

Lorekeeper is a desktop worldbuilding tool for creating spatially consistent
fictional worlds. You can orbit a textured globe, place named locations on it,
link Markdown lore files to each location, and navigate a solar system of
multiple planets and moons — all saved as plain JSON your git repository can track.

---

## Getting Started

### Create a new world

1. **File → New World…**
2. Enter a **Name** for the world.
3. Click **Browse…** to choose a parent folder.  
   The world folder is created at `<parent>/<name>/`.
4. Click **Create**.

### Open an existing world

1. **File → Open World…**
2. Select the world's root folder (the one containing `world.json`).

### Save

- **Ctrl+S** or **File → Save** at any time.  
  Most edits (renaming an entity, assigning a texture) save automatically.

---

## Navigation

### Planet view

| Action | Control |
|---|---|
| Orbit the globe | Hold **middle mouse** and drag |
| Zoom in / out | **Scroll wheel** |
| Cancel place mode | **Escape** |
| Undo | **Ctrl+Z** |
| Redo | **Ctrl+Y** |

The camera always orbits the centre of the active body.

### Solar system view

Switch with the **Solar System** button at the top of the World panel, or
**View → Solar System**.

| Action | Control |
|---|---|
| Orbit the system | Hold **middle mouse** and drag |
| Zoom in / out | **Scroll wheel** |
| Navigate to a planet | **Left-click** its sphere |
| Return to planet view | **Planet View** button in World panel |

Bodies are highlighted on hover. Orbital paths are shown as faint ellipses
in the ecliptic plane.

**Scale modes** (View → Realistic Scale):

| Mode | Body sizes | Orbital spacing |
|---|---|---|
| Illustrative *(default)* | Fixed — stars large, planets medium, moons small | Evenly spaced by index |
| Realistic | Proportional to `radius_km` | Proportional to `orbital_radius_au` |

---

## Bodies (Planets, Stars, Moons)

### Add a body

In the World panel click **+** next to *Bodies*, then fill in:

| Field | Description |
|---|---|
| Name | Display name |
| Type | Star / Planet / Moon |
| Parent | Which body this orbits (leave blank for top-level stars) |
| Orbital Radius (AU) | Distance from parent in astronomical units (shown when a parent is chosen) |

Click **Add**. The new body becomes the active body.

### Select a body

Click its name in the World panel list. Moons are indented under their parent
planet. Selecting a body in solar system view also switches to planet view.

### Assign a texture

With a body selected and no entity selected, the Inspector panel shows a
**Browse…** button under *Texture*. Choose any `.jpg` or `.png` equirectangular
map. Click **Clear** to remove it.

The app also auto-discovers a texture at  
`assets/textures/<body-id>.jpg` (or `.png`) inside the world folder, and falls
back to `assets/surface.jpg` next to the executable.

---

## Entities (Cities, Towns, POIs)

### Place an entity

1. Select a body in the World panel.
2. Click **City**, **Town**, or **POI** in the Place row.
3. Left-click anywhere on the globe. The entity is placed at that lat/lon.
   Place mode exits automatically; press **Escape** or **Cancel** to abort.

### Select an entity

Left-click its dot on the globe, or click its name in the entity list at the
bottom of the World panel.  
A white ring appears around the selected entity's dot.

### Reposition an entity

Left-click and **drag** an entity's dot to move it to a new position on the
globe. Release to confirm. The move is undoable with **Ctrl+Z**.

### Edit an entity

With an entity selected the Inspector panel shows:

| Field | Notes |
|---|---|
| Name | Editable; saves on focus loss |
| Type | City / Town / POI dropdown |
| Lat / Lon | Read-only; position on the globe |

### Lore file

Each entity can be linked to a Markdown file:

- **Create** — creates `media/<entity-id>.md` inside the world folder and opens
  it in the system default editor (e.g. VS Code, Obsidian).
- **Open** — opens the linked file in the system default editor.
- You can also type or paste an absolute path directly into the lore file field.

### Delete an entity

Select the entity, scroll to the bottom of the Inspector panel, and click the
red **Delete** button. This is undoable with **Ctrl+Z**.

---

## World panel reference

```
[Solar System]  My World
path/to/world/
──────────────────────────────
Bodies                        +
  [*] Sol
    [o] Aldoria
      [.] Luna
  [o] Pyraxis
──────────────────────────────
Place  City  Town  POI
──────────────────────────────
Entities
  [C] Ironhold
  [T] Millhaven
  [P] The Shrine
```

Icons: `[*]` star · `[o]` planet · `[.]` moon · `[C]` city · `[T]` town · `[P]` POI

---

## HUD

The bottom-right corner of the viewport shows:

- **Planet view** — lat/lon under the cursor and a calibrated scale bar.
- **Solar system view** — current scale mode (Illustrative / Realistic).

---

## World folder layout

```
<world-name>/
  world.json          ← all body and entity data
  media/
    <entity-id>.md    ← lore files created by Lorekeeper
    notes/            ← unattached world notes (future)
  assets/
    textures/         ← body textures (place them here for auto-discovery)
    heightmaps/       ← future: heightmap tiles
```

---

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Ctrl+S | Save world |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Escape | Exit place mode / deselect |
| Middle drag | Orbit camera |
| Scroll | Zoom |
