<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYMAP.md
//  Purpose: Specifies the CypherEngine map source format: `.cymap` root
//           documents (cypher.map V10) and `.cymapchunk` chunk documents
//           (cypher.map_chunk V10) - every object a level can contain, how
//           it is written, and how editors read, place, and preserve it.
//  Details: Decisions are recorded in ADR 0011; this is the field-level
//           contract, written so that a person can read any map file and
//           know what is in the level and where. Source and tests are
//           authoritative if they diverge; the example map under
//           docs/formats/examples/cymap is loaded by the tests, so the
//           examples here cannot drift from the code.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Rewritten on 2026-09-27: map-owned readable geometry, nested brush
//    entities, terrain paint, shapes, foliage, notes, selection sets,
//    modifiers, info annotations, text layout, and hand-editing rules
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Map Source Format (`.cymap`, V10)

A map is the complete source of one level. Everything a level designer
authors is in its files, in plain text, in a form a person can read: every
entity with every property and every input/output connection, every brush
face with its plane, material, and texture mapping, every mesh vertex and
face, every patch control point, every terrain height and paint weight,
every path, foliage instance, note, group, and prefab instance. Nothing
authored lives anywhere else; nothing in the files is compiled or cached.

The format is built for six things:

| Goal | How |
| --- | --- |
| **Complete** | Every authored value is written, including entity property defaults. A map opens and reads correctly without the editor, the game profile, or any cache. |
| **Readable** | Named members, degrees for angles, material paths on every face, geometry in coordinates, short records on one line, and an `info` block on every object saying where it is. |
| **Deterministic** | A fixed member order, objects sorted by ID, and one spelling per number, so saving an unchanged map reproduces the same bytes and editing one object changes only that object's lines. |
| **Mergeable** | The map is split into chunk files by layer and position, and nothing volatile lives in the root, so two people working on different areas never touch the same file. |
| **Tolerant** | Unknown members are kept; an object that cannot be read is kept verbatim; one damaged file never damages another. |
| **Live** | Files can be edited in a text editor while Mason runs; a changed chunk is reloaded on the fly (section 12). |

## 1. Files

```text
maps/facility.cymap                          root document: identity, layers, visgroups, settings
maps/facility/structure/x0_y0.cymapchunk     layer "structure", cell (0, 0)
maps/facility/structure/x0_y-1.cymapchunk    layer "structure", cell (0, -1)
maps/facility/gameplay/x0_y0.cymapchunk      layer "gameplay", cell (0, 0)
maps/facility/gameplay/global.cymapchunk     layer "gameplay", objects without a position
```

- The chunk directory is the root's file name without `.cymap`, beside it.
- A chunk lives at `<layer id>/<cell>.cymapchunk`; `<cell>` is `global` or
  `x<i>_y<j>` (`_z<k>` added when the z axis is cut).
- Chunks are found by scanning the chunk directory. The root does not list
  them, so adding chunks never touches the root. Only files at exactly
  `<layer>/<name>.cymapchunk` count; anything else in the folder (notes,
  `.bak` copies, leftover `.tmp` files, a copied subfolder) is ignored, so a
  stray copy of a chunk can never load as a duplicate. File names are
  lowercase, as the writer produces them; a renamed file with capitals is not
  seen.
- A map with no objects has no chunk directory.
- Saving writes every file as `<file>.tmp` first and renames them into place
  only when all of them were written, root last; emptied chunks are deleted
  after that. A full disk or an unwritable folder leaves the map on disk as
  it was. Emptied layer folders are left in place.
- Personal state (cameras, bookmarks, hidden objects, selection, current
  tool) belongs to the workspace ([CYWORKSPACE.md](CYWORKSPACE.md)), never to
  the map.
- Compiled output (the `.cyscene` the map compiler writes, lightmaps,
  navigation meshes) lives in the project's build folder, never beside the
  map source.

## 2. Conventions

| Topic | Rule |
| --- | --- |
| Axes | Right-handed, **+Z up**. Yaw 0 faces **+X**; +Y is 90 degrees to the left of +X seen from above. |
| Units | Map units (`units_per_meter`, default 39.37: one unit is one inch). All positions, sizes, and distances are map units. |
| Positions | `[ x, y, z ]`, f64, world space. |
| Angles | `[ pitch, yaw, roll ]` in **degrees**. Yaw turns about +Z counter-clockwise seen from above; positive pitch looks down; roll turns about the forward axis clockwise seen from behind. Applied yaw, then pitch, then roll. |
| Scale | one number or `[ x, y, z ]`, applied before rotation. |
| Bounds | `[ minx, miny, minz, maxx, maxy, maxz ]`. |
| Planes | `[ nx, ny, nz, distance ]`: unit normal pointing **out** of the solid, and points `p` on the plane satisfy `n . p = distance`. The face of a box at x = 64 is `[ 1.0, 0.0, 0.0, 64.0 ]`. |
| Colours | `"#rrggbb"` or `"#rrggbbaa"`, sRGB, lower case. |
| Asset references | canonical virtual paths: lower-case, forward slashes, relative to the mounted content, with the extension (`materials/concrete/floor01.cymat`). |
| IDs | nonzero u64 written with the `u` suffix (`101u`), unique in the map (section 10). |
| Booleans | `true` / `false`. |
| Text | UTF-8; files are written with LF line endings and end with one newline. |
| World extent | every coordinate within +/-1,048,576 units (2^20); geometry outside is refused by the geometry library and reported. |

## 3. Text layout

Map files are CYKV language 1 ([CYKV.md](CYKV.md)) written with the compact
layout ([CYKV.md](CYKV.md), writer options) at a width of 200 columns:

- Keys are written bare when they are CYKV bare keys, quoted otherwise.
- A container whose one-line form fits within the width (at its
  indentation) is written on one line: `origin = [ 128.0, -64.0, 16.0 ]`,
  `{ output = "on_trigger" target = "wave1" input = "start" parameter = "" delay = 0.5 times = 1 }`.
- A list of records shares one layout: every record on its own line when
  all fit, otherwise every record spread over lines. An ordinary brush face
  fits, so a brush reads as one line per face; a face with long diagonal
  plane numbers spreads all faces of that brush alike.
- A longer list of plain numbers is packed, as many per line as fit
  (terrain height rows, ID lists).
- Reals are written with the fewest digits that read back to the same value
  (`39.37`, not `39.369999999999997`), always with a decimal point
  (`16.0`); `-0.0` is written as `0.0`; non-finite values are never written.
- **One spelling per value.** Members whose type this format defines are
  written in that type whatever was typed: IDs (`id`, `parent`, `members`,
  `visgroups`) as u64 (`7u`); positions, angles, bounds, shape points, and
  foliage instances as reals (`[ 1.0, 2.0, 3.0 ]`); `delay` as a real; `times`
  as an integer. Entity `properties` keep the types written, because the game
  profile, not this format, defines them.
- Angles and texture rotations are rounded to 1e-9 degrees before writing,
  so converting to and from radians cannot make saves drift.
- Members follow the fixed order in each table below; members an editor does
  not know follow the known ones in the order they were read.
- Objects in every list are sorted by `id`; unreadable objects follow in
  their original order. ID lists (`visgroups`, `members`) and `tags` are
  sorted. Entity `outputs` keep their order: it is the order they fire in.
- Members equal to their documented default are omitted where a table says
  so; everything else is always written.

Comments in hand-edited files are allowed by the reader and dropped on the
next save; put lasting notes in `comment` members or `notes` objects.

## 4. Root document - `cypher.map` V10

The root holds what the whole map shares and changes rarely. It is small by
design: the level itself lives in the chunks.

```cykv
@cykv 1
@schema "cypher.map" 10

{
    map_id = "4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b"
    name = "Research Facility"
    game = "reap"
    game_version = 3u
    next_id = 20481u
    description = "Research facility overrun at night; twelve waves."
    authors = [ "Karlo" ]
    tags = [ "indoor", "waves" ]
    thumbnail = "maps/facility/thumbnail.png"
    units_per_meter = 39.37
    cell_size = [ 8192.0, 8192.0, 0.0 ]
    layers = [
        { id = "structure" name = "Structure" color = "#6fa8dc" },
        { id = "detail" name = "Detail" },
        { id = "gameplay" name = "Gameplay" },
        { id = "notes" name = "Designer notes" editor_only = true }
    ]
    visgroups = [
        { id = 12u name = "Upper deck" color = "#e69138" },
        { id = 13u name = "Lights" parent = 12u }
    ]
    selection_sets = [
        { id = 14u name = "Arena doors" members = [ 3301u, 3302u, 3303u ] }
    ]
    cordons = [
        { name = "Spawn room" bounds = [ -512.0, -512.0, -64.0, 512.0, 512.0, 384.0 ] }
    ]
    settings = {
        map = { sky = "materials/sky/dusk.cymat" gravity = 800.0 ambient_color = "#202028" fog_color = "#101820" fog_start = 2048.0 fog_end = 12000.0 }
        game = { wave_count = 12 wave_intermission = 8.0 difficulty = "normal" }
    }
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `map_id` | string | yes | nonzero UUID; the map's identity across copies and workspaces |
| `name` | string | yes | display name, 1-128 bytes |
| `game` | string | yes | game profile ID ([CYGAME.md](CYGAME.md)) |
| `game_version` | u64 | no | game profile version the map was last saved against; drives migrations |
| `next_id` | u64 | yes | greater than every ID ever issued in the map; a removed ID is never reused |
| `description` | string | no | at most 4096 bytes |
| `authors` | array of strings | no | display names, at most 64 |
| `tags` | array of strings | no | stable identifiers for map browsers and server lists, at most 64, sorted |
| `thumbnail` | string | no | asset path of a preview image |
| `units_per_meter` | f64 | no | default from the game profile, else 39.37 |
| `cell_size` | `[f64 x3]` | no | chunk cell size per axis (section 9); default `[ 8192, 8192, 0 ]` |
| `layers` | array | yes | at least one, in the editor's layer order: `id` (stable identifier, names the chunk folder), `name`, `color`, `editor_only` (never compiled: notes, reference geometry) |
| `visgroups` | array | no | shared visibility groups: `id` (map ID), `name`, `color`, `parent` (visgroup ID) |
| `selection_sets` | array | no | named object sets shown in the Selection Sets panel: `id`, `name`, `members` |
| `cordons` | array | no | named boxes compiles and views can be limited to: `name`, `bounds`; which one is active is personal |
| `settings` | object | no | `map` (engine settings, section 14) and `game` (defined by the game profile); unknown sections kept |

## 5. Chunk document - `cypher.map_chunk` V10

A chunk holds the objects of one layer inside one cell, in sections by kind.

```cykv
@cykv 1
@schema "cypher.map_chunk" 10

{
    map_id = "4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b"
    layer = "gameplay"
    cell = [ 0, 0 ]
    info = { entities = 3 brushes = 1 bounds = [ -64.0, -128.0, 0.0, 1400.0, 640.0, 280.0 ] }
    entities = [ ... ]
    brushes = [ ... ]
    meshes = [ ... ]
    patches = [ ... ]
    terrains = [ ... ]
    shapes = [ ... ]
    foliage = [ ... ]
    notes = [ ... ]
    groups = [ ... ]
    prefabs = [ ... ]
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `map_id` | string | yes | equals the root's `map_id`; a chunk from another map is never read or written |
| `layer` | string | yes | a layer ID; one the root does not declare is adopted and reported |
| `cell` | `"global"`, `[i, j]`, or `[i, j, k]` | yes | the cell (section 9) |
| `info` | object | no | summary for readers (section 8); ignored on read |
| `entities` | array | no | section 6.2 |
| `brushes` | array | no | world brushes (brushes owned by an entity live inside it), section 6.3 |
| `meshes` | array | no | world meshes, section 6.4 |
| `patches` | array | no | world patches, section 6.5 |
| `terrains` | array | no | section 6.6 |
| `shapes` | array | no | section 6.7 |
| `foliage` | array | no | section 6.8 |
| `notes` | array | no | section 6.9 |
| `groups` | array | no | section 6.10 |
| `prefabs` | array | no | prefab instances, section 6.11 |

Empty sections are not written. Members in the order of the table.

## 6. Objects

### 6.1 Common members

Every object except foliage instances has an `id`. Objects that can be named
in the editor also take these, written after the kind's identifying members:

| Member | Type | Rules |
| --- | --- | --- |
| `name` | string | display name (for entities, the target name used by I/O) |
| `comment` | string | a designer's note about this object, at most 4096 bytes |
| `visgroups` | array of IDs | visgroups the object belongs to, sorted |
| `info` | object | reader summary (section 8); ignored on read |

### 6.2 Entities

Lights, spawn points, triggers, doors, props, sounds, logic: anything with a
class from the game profile.

```cykv
{
    id = 102u
    class = "trigger_once"
    name = "wave1_trigger"
    origin = [ 0.0, 0.0, 64.0 ]
    visgroups = [ 12u ]
    info = { bounds = [ -64.0, -64.0, 0.0, 64.0, 64.0, 128.0 ] }
    properties = { start_disabled = false filter = "players" }
    outputs = [
        { output = "on_trigger" target = "wave1" input = "start" parameter = "" delay = 0.5 times = 1 }
    ]
    brushes = [
        {
            id = 2201u
            info = { bounds = [ -64.0, -64.0, 0.0, 64.0, 64.0, 128.0 ] }
            faces = [
                { id = 2202u plane = [ 1.0, 0.0, 0.0, 64.0 ] material = "materials/editor/trigger.cymat" uv_u = [ 0.0, 1.0, 0.0 ] uv_v = [ 0.0, 0.0, -1.0 ] },
                { id = 2203u plane = [ -1.0, 0.0, 0.0, 64.0 ] material = "materials/editor/trigger.cymat" uv_u = [ 0.0, -1.0, 0.0 ] uv_v = [ 0.0, 0.0, -1.0 ] },
                { id = 2204u plane = [ 0.0, 1.0, 0.0, 64.0 ] material = "materials/editor/trigger.cymat" uv_u = [ -1.0, 0.0, 0.0 ] uv_v = [ 0.0, 0.0, -1.0 ] },
                { id = 2205u plane = [ 0.0, -1.0, 0.0, 64.0 ] material = "materials/editor/trigger.cymat" uv_u = [ 1.0, 0.0, 0.0 ] uv_v = [ 0.0, 0.0, -1.0 ] },
                { id = 2206u plane = [ 0.0, 0.0, 1.0, 128.0 ] material = "materials/editor/trigger.cymat" uv_u = [ 1.0, 0.0, 0.0 ] uv_v = [ 0.0, -1.0, 0.0 ] },
                { id = 2207u plane = [ 0.0, 0.0, -1.0, 0.0 ] material = "materials/editor/trigger.cymat" uv_u = [ 1.0, 0.0, 0.0 ] uv_v = [ 0.0, 1.0, 0.0 ] }
            ]
        }
    ]
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID |
| `class` | string | yes | class name from the game profile; an unknown class is kept and reported |
| `name` | string | no | target name for entity I/O; not unique by design (several entities may answer one name) |
| `origin` | `[f64 x3]` | no | entity position; absent for logic entities without a place in the world |
| `angles` | `[f64 x3]` | no | degrees; omitted when zero |
| `scale` | f64 or `[f64 x3]` | no | omitted when 1 |
| `parent` | u64 | no | entity this one is attached to (moves with it) |
| `comment`, `visgroups`, `info` | | no | section 6.1 |
| `properties` | object | no | every property of the class, defaults included (the game profile decides the value types); keys not in the class are kept |
| `outputs` | array | no | connections, below |
| `brushes`, `meshes`, `patches` | arrays | no | geometry the entity owns (brush entities: triggers, doors, detail); same members as world geometry. Owned geometry lives in the entity's chunk |
| `editor` | object | no | shared editor data: `logic_position` `[x, y]` (the entity's place in the logic graph view), `color` (overrides the class colour in views) |

**Outputs** fire an input on every entity whose name matches `target` when
the output happens:

| Member | Type | Rules |
| --- | --- | --- |
| `output` | string | output name from the entity's class |
| `target` | string | a target name, compared case-sensitively; `name*` matches every name starting with `name`; `!self`, `!activator`, `!caller`, `!player` are the special targets |
| `input` | string | input name on the target's class |
| `parameter` | string | value passed to the input; empty passes the output's own value, if any |
| `delay` | f64 | seconds after the output fires; default 0 |
| `times` | integer | how many times the connection may fire; -1 is unlimited (default) |

All six members are always written. Outputs keep their authored order (the
order they fire in), not a sorted order.

### 6.3 Brushes

A brush is a convex solid bounded by planes: Quake/Hammer-style world
construction. Each face carries its material and texture mapping.

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID of the brush |
| `name`, `comment`, `visgroups`, `info` | | no | section 6.1 |
| `faces` | array | yes | 4-256 faces, below |

| Face member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID of the face (overlays and decals refer to faces by ID) |
| `plane` | `[f64 x4]` | yes | section 2 |
| `material` | string | no | material asset path; absent means none (drawn with the missing-material pattern) |
| `uv_u`, `uv_v` | `[f64 x3]` | yes | world directions of increasing U and V |
| `uv_origin` | `[f64 x3]` | no | world point at UV (0, 0) before offset; default `[ 0, 0, 0 ]` |
| `uv_normal` | `[f64 x3]` | no | projection normal; default: the face normal |
| `uv_size` | `[f64 x2]` | no | world size of one texture repeat along U and V; default `[ 1, 1 ]` |
| `uv_rotation` | f64 | no | degrees about the projection normal; default 0 |
| `uv_offset` | `[f64 x2]` | no | UV shift after projection; default `[ 0, 0 ]` |
| `lightmap_scale` | f64 | no | world units per lightmap texel on this face; default `settings.map.lightmap_scale` |
| `smoothing` | u64 | no | smoothing-group bits for lit shading across faces; default 0 (flat) |

The solid is the intersection of the faces' inner half-spaces; faces that
do not touch it are invalid and reported. Texture lock, alignment, and
fitting are editing operations; the file stores the resulting mapping.
`lightmap_scale` and `smoothing` belong to the map, not to the geometry
library; they are kept per face by its `id`. Members a face carries that this
build does not know are kept the same way and written back after the known
ones; the same holds for mesh faces.

### 6.4 Meshes

A mesh is a polygon mesh with arbitrary topology: Hammer 5 / Blender-style
modelling inside the map. From the example map:

```cykv
{
    id = 1200u
    name = "loading ramp"
    info = { bounds = [ 256.0, 256.0, 0.0, 384.0, 384.0, 48.0 ] vertices = 4 faces = 1 }
    vertices = [ [ 256.0, 256.0, 0.0 ], [ 384.0, 256.0, 0.0 ], [ 384.0, 384.0, 48.0 ], [ 256.0, 384.0, 48.0 ] ]
    vertex_ids = [ 1201u, 1202u, 1203u, 1204u ]
    faces = [ { id = 1205u vertices = [ 0, 1, 2, 3 ] material = "materials/metal/grate01.cymat" uv = [ [ 0.0, 0.0 ], [ 1.0, 0.0 ], [ 1.0, 1.0 ], [ 0.0, 1.0 ] ] } ]
    edges = [ { vertices = [ 0, 1 ] hard = true } ]
    modifiers = [ { type = "mirror" axis = "x" offset = 256.0 weld = true } ]
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID of the mesh |
| `name`, `comment`, `visgroups`, `info` | | no | section 6.1 |
| `vertices` | array of `[f64 x3]` | yes | positions |
| `vertex_ids` | array of u64 | yes | map ID of each vertex, parallel to `vertices` |
| `faces` | array | yes | polygons, below |
| `edges` | array | no | only edges with non-default data: `vertices` `[a, b]` (indices, a < b), `hard` (split normals), `seam` (UV island boundary), `crease` (0-1, subdivision sharpness) |
| `modifiers` | array | no | non-destructive operations evaluated over the mesh (below); the mesh itself stays the authored cage |

| Face member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID of the face |
| `vertices` | array of integers | yes | 3-256 vertex indices, counter-clockwise seen from outside |
| `material` | string | no | material asset path |
| `smoothing` | u64 | no | smoothing group bits; faces sharing a bit shade smoothly across non-hard edges; default 1, 0 is flat |
| `uv` | array of `[f64 x2]` | no | per-corner UV set 0; default all zero |
| `uv2` | array of `[f64 x2]` | no | per-corner UV set 1 (lightmaps, detail); default all zero |
| `colors` | array of colours | no | per-corner vertex colours; default all `#ffffffff` |

**Modifiers** run in order: `mirror` (`axis` `x`/`y`/`z`, `offset`, `weld`,
`weld_distance`), `linear_array` (`copies`, `step` `[x, y, z]`, `weld`),
`radial_array` (`copies`, `origin`, `direction`, `angle` degrees, `weld`),
`bend` (`axis`, `along`, `origin`, `angle` degrees), `taper` (`along`,
`factor`), `subdivide` (`levels`, `scheme` `catmull_clark` / `loop`,
`boundary` `smooth` / `sharp`). Every modifier takes `enabled` (default
true). Unknown modifier types are kept and skipped when evaluating.

### 6.5 Patches

A patch is a curved Bezier surface (Radiant-style arches, pipes, terrain
caps) defined by a grid of control points, one row of the grid per line:

```cykv
{
    id = 1300u
    name = "doorway arch"
    info = { bounds = [ -64.0, 496.0, 128.0, 64.0, 496.0, 192.0 ] }
    basis = "quadratic"
    columns = 3
    rows = 3
    material = "materials/concrete/arch.cymat"
    controls = [
        [ [ -64.0, 496.0, 128.0, 0.0, 0.0 ], [ 0.0, 496.0, 128.0, 0.5, 0.0 ], [ 64.0, 496.0, 128.0, 1.0, 0.0 ] ],
        [ [ -64.0, 496.0, 160.0, 0.0, 0.5 ], [ 0.0, 496.0, 192.0, 0.5, 0.5 ], [ 64.0, 496.0, 160.0, 1.0, 0.5 ] ],
        [ [ -64.0, 496.0, 192.0, 0.0, 1.0 ], [ 0.0, 496.0, 192.0, 0.5, 1.0 ], [ 64.0, 496.0, 192.0, 1.0, 1.0 ] ]
    ]
    control_ids = [ 1301u, 1302u, 1303u, 1304u, 1305u, 1306u, 1307u, 1308u, 1309u ]
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID of the patch |
| `name`, `comment`, `visgroups`, `info` | | no | section 6.1 |
| `basis` | string | yes | `quadratic` (2k + 1 controls per axis) or `cubic` (3k + 1) |
| `columns`, `rows` | integers | yes | control grid size |
| `material` | string | no | material asset path |
| `controls` | array | yes | `rows` rows of `columns` controls; each control `[ x, y, z, u, v ]` |
| `control_ids` | array of u64 | yes | map ID of each control, row by row |

### 6.6 Terrains

A terrain is a height grid with holes, split into tiles, painted with
material layers. Heights and paint weights are written one grid row per
line, so the shape of the ground is visible in the file. From the example
map (a small hill ringed with grass):

```cykv
{
    id = 1100u
    name = "yard"
    info = { bounds = [ 600.0, -128.0, 0.0, 856.0, 128.0, 40.0 ] }
    origin = [ 600.0, -128.0, 0.0 ]
    cell_size = 32.0
    cells = [ 8, 8 ]
    tile_cells = 4
    heights = [
        [ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 ],
        [ 0.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 0.0 ],
        [ 0.0, 4.0, 12.0, 12.0, 12.0, 12.0, 12.0, 4.0, 0.0 ],
        [ 0.0, 4.0, 12.0, 24.0, 24.0, 24.0, 12.0, 4.0, 0.0 ],
        [ 0.0, 4.0, 12.0, 24.0, 40.0, 24.0, 12.0, 4.0, 0.0 ],
        [ 0.0, 4.0, 12.0, 24.0, 24.0, 24.0, 12.0, 4.0, 0.0 ],
        [ 0.0, 4.0, 12.0, 12.0, 12.0, 12.0, 12.0, 4.0, 0.0 ],
        [ 0.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 4.0, 0.0 ],
        [ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 ]
    ]
    holes = [ [ 7, 0 ] ]
    tile_ids = [ 1358u, 1359u, 1360u, 1361u ]
    paint = {
        base = "materials/terrain/dirt.cymat"
        layers = [
            {
                material = "materials/terrain/grass.cymat"
                weights = [
                    [ 255, 255, 255, 255, 255, 255, 255, 255, 255 ],
                    [ 255, 200, 200, 200, 200, 200, 200, 200, 255 ],
                    [ 255, 200, 90, 90, 90, 90, 90, 200, 255 ],
                    [ 255, 200, 90, 0, 0, 0, 90, 200, 255 ],
                    [ 255, 200, 90, 0, 0, 0, 90, 200, 255 ],
                    [ 255, 200, 90, 0, 0, 0, 90, 200, 255 ],
                    [ 255, 200, 90, 90, 90, 90, 90, 200, 255 ],
                    [ 255, 200, 200, 200, 200, 200, 200, 200, 255 ],
                    [ 255, 255, 255, 255, 255, 255, 255, 255, 255 ]
                ]
            }
        ]
    }
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID of the terrain |
| `name`, `comment`, `visgroups`, `info` | | no | section 6.1 |
| `origin` | `[f64 x3]` | yes | world position of sample (0, 0) at height 0 |
| `cell_size` | f64 | yes | spacing between samples |
| `cells` | `[integer x2]` | yes | cells along X and Y; multiples of `tile_cells` |
| `tile_cells` | integer | yes | cells per tile edge; a power of two |
| `heights` | array of rows | yes | `cells[1] + 1` rows of `cells[0] + 1` heights above `origin.z`, row 0 at the lowest Y |
| `holes` | array of `[x, y]` | no | cells cut out of the terrain, sorted |
| `tile_ids` | array of u64 | yes | map ID of each tile, row by row |
| `paint` | object | no | `base` material and `layers` painted over it in order; each layer's `weights` has one row per height row, 0-255 per sample (255 fully covers the layers below) |

### 6.7 Shapes

A shape is a named line through the world: patrol routes, camera rails,
rivers, roads, spline-based props. Entities refer to shapes through
properties of type `shape` ([CYGAME.md](CYGAME.md)).

```cykv
{ id = 1200u name = "patrol_route_a" type = "catmull_rom" closed = true points = [ [ 0.0, 0.0, 16.0 ], [ 512.0, 0.0, 16.0 ], [ 512.0, 512.0, 16.0 ] ] }
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | u64 | yes | map ID |
| `name`, `comment`, `visgroups`, `info` | | no | section 6.1 |
| `type` | string | yes | `polyline`, `catmull_rom`, or `bezier` |
| `closed` | bool | no | default false |
| `points` | array of `[f64 x3]` | yes | at least 2 |
| `handles` | array of `[f64 x6]` | for `bezier` | per point: incoming then outgoing handle, as offsets from the point |
| `widths` | array of f64 | no | per point width (roads, rivers) |

### 6.8 Foliage

Foliage is many copies of a model scattered over the world - grass, trees,
rocks, debris - stored compactly: one record per model per chunk.

```cykv
{
    model = "models/foliage/pine01.cymodel"
    instances = [
        [ 812.0, -1210.0, 44.5, 0.0, 132.5, 0.0, 1.1 ],
        [ 890.25, -1180.0, 46.0, 0.0, 17.0, 0.0, 0.95 ]
    ]
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `model` | string | yes | model asset path; unique within the chunk's `foliage` list |
| `instances` | array | yes | one `[ x, y, z, pitch, yaw, roll, scale ]` per instance, sorted by x, then y, then z |
| `properties` | object | no | per-map settings for this model's instances (`cast_shadows`, `fade_distance`, ...); kept as written |

Instances have no IDs: foliage tools select them by painting and area. Each
instance is written to the chunk of its own position, so dense forests
spread over many files.

### 6.9 Notes

A note pins text into the level for other designers - "needs cover here",
"placeholder art". Notes are never compiled.

```cykv
{ id = 1500u position = [ 256.0, 128.0, 96.0 ] text = "Needs cover for wave 4." color = "#ffd84a" }
```

`id`, `position` (required), `text` (required, at most 4096 bytes), `color`
(default `#ffd84a`), `size` (label scale, default 1), `visgroups`.

### 6.10 Groups

A group ties objects together so they select and move as one.

`id`, `name`, `members` (object IDs in any chunk and layer, sorted),
`comment`, `visgroups`. Nested groups are members of their parent group.

### 6.11 Prefab instances

A placed copy of a prefab file (`.cyprefab`: the same object sections as a
chunk, in the prefab's own ID space).

```cykv
{ id = 400u name = "arena_door_west" source = "prefabs/security_door.cyprefab" origin = [ 512.0, 0.0, 0.0 ] angles = [ 0.0, 180.0, 0.0 ] overrides = { "door.name" = "arena_door" "door.properties.speed" = 150.0 } }
```

`id`, `name`, `comment`, `visgroups`, `info` (nothing yet: the prefab file is
not loaded to write the chunk), `source` (required), `origin` (required),
`angles`, `scale`, `overrides` (`<object name or prefab ID>.<member path>` to
value; changes applied to this instance only).

### 6.12 Prefab files (`.cyprefab`, `cypher.prefab` V1)

A prefab is a reusable piece of a level - a security door with its frame,
lights, trigger, and wiring - stored once and placed many times. Its file is
one document with the object sections of a chunk, in the prefab's own ID
space:

```cykv
@cykv 1
@schema "cypher.prefab" 1

{
    prefab_id = "9a3e6f0c-51d2-4c8b-8e17-2f4b6d0a9c31"
    name = "Security door"
    game = "reap"
    next_id = 40u
    description = "Sliding door with frame, light, and open trigger."
    parameters = [
        { key = "door.name" type = "string" default = "security_door" label = "Door name" },
        { key = "door.properties.speed" type = "real" default = 200.0 label = "Speed" }
    ]
    entities = [ ... ]
    brushes = [ ... ]
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `prefab_id` | string | yes | nonzero UUID |
| `name` | string | yes | display name, 1-128 bytes |
| `game` | string | yes | game profile ID |
| `next_id` | u64 | yes | as in a map root, for the prefab's own IDs |
| `description` | string | no | at most 4096 bytes |
| `parameters` | array | no | members an instance may override: `key` (`<object name or ID>.<member path>`), `type` and `default` (game-profile property types), `label` |
| sections | arrays | no | `entities`, `brushes`, `meshes`, `patches`, `terrains`, `shapes`, `foliage`, `notes`, `groups`, as in a chunk, positioned relative to the prefab's origin |

Instances (section 6.11) keep the prefab's objects out of the map: the map
stores only the placement and the overrides, and compiling or "Collapse
Prefab" copies the objects in with fresh map IDs.

## 7. Material references

Every face, patch, and paint layer names its material by path, so a chunk is
self-contained and a text search for a material path finds every use. The
editor keeps an in-memory table of the paths a map uses; it is never written.

## 8. `info` annotations

`info` members exist so that a person reading a file sees where things are
and how big they are without running the editor. They are **written for
readers only**: recomputed from the object on every save, ignored on every
read. Editing one by hand has no effect and is overwritten.

| Where | Members |
| --- | --- |
| chunk | counts per section (`entities`, `brushes`, `meshes`, `patches`, `terrains`, `shapes`, `foliage` (instances), `notes`, `groups`, `prefabs`), `bounds` of everything positioned |
| brush, mesh, patch, terrain, shape | `bounds`; meshes also `vertices`, `faces` |
| entity with owned geometry | `bounds` of the owned geometry |
| prefab instance | nothing yet (the prefab file is not loaded to write the chunk) |

The root carries no `info`: it would change with every edit anywhere and
make the shared file conflict constantly.

## 9. Chunk assignment

Every object belongs to exactly one chunk, decided by its layer and position,
so every editor writes the same files for the same map.

- An entity's position is its `origin`; a brush's, mesh's, patch's, or
  shape's is the centre of its bounds; a terrain's is the centre of its
  footprint (heights do not move a terrain between cells as it is
  sculpted); a note's is its `position`; a prefab instance's is its `origin`;
  each foliage instance's is its own position.
- Geometry an entity owns is written inside the entity.
- Group records go to the `global` chunk of their first member's layer.
- Entities without an origin go to `global`.
- An object whose position cannot be read, and any unreadable object, stays
  in the chunk it was read from.
- With cell size `s > 0` on an axis, the cell index is `floor(p / s)` for the
  position component `p`. Axes with `s = 0` are not cut. `cell_size` cuts x
  and y together; z is optional.
- Empty chunks are not written; a chunk that becomes empty is deleted when
  the map is saved.

## 10. Identity

- Every object and every addressable part of one - brush faces, mesh
  vertices and faces, patch controls, terrain tiles - has a map ID: a nonzero
  u64 unique within the map. Parts share the ID space with objects, so any
  ID names exactly one thing, and references (outputs by name aside) are by
  ID.
- `next_id` in the root is higher than every ID ever issued. Removing an
  object never frees its ID.
- **Merge rule:** when two branches both change `next_id`, keep the larger.
  Two branches that both issued the same new IDs produce duplicates on
  merge; the editor reports them (below).

**Adding objects by hand.** Leave out `id` (or write `0u`) on a new object,
face, vertex, control, or tile, and Mason assigns a fresh ID when it reads
the file, reports it, and writes it on the next save. A mesh whose
`vertex_ids` is shorter than `vertices` gets IDs for the rest the same way.

**Duplicate IDs** (a copy-pasted block with its IDs, or a bad merge) open
the map read-only with every location listed. The editor's "Reassign
Duplicate IDs" command gives the later copies fresh IDs; the map is then
editable and saving writes the new IDs.

## 11. Game profile

The map refers to its game profile by `game` and records `game_version`.
Entity classes, property types, inputs, and outputs come from the profile;
the map stores values, not definitions. An entity of a class the profile
does not define is kept, shown as an unknown class, and saved unchanged.
When `game_version` is older than the profile's `version`, the profile's
migrations apply on load and are reported ([CYGAME.md](CYGAME.md)).

## 12. Hand editing and live reload

The files are meant to be edited by hand as well as by Mason:

- Mason watches the chunk directory and the root. When a file changes on
  disk and Mason has no unsaved change in it, Mason reloads just that file
  and updates the views. When both changed, Mason keeps its version, reports
  the conflict, and offers to reload or to keep editing.
- A chunk that fails to read after a hand edit is reported with the line and
  column of the first error and left untouched on disk; the rest of the map
  stays open, and saving never overwrites the broken file.
- New objects without IDs get IDs (section 10). Moving an object by editing
  its coordinates is fine: it moves to the right chunk on the next save.
- Members written by hand in a different order, or with quoted keys or
  comments, are read normally and rewritten in the canonical layout on the
  next save of that chunk.

## 13. Validation and damage

| Problem | Outcome |
| --- | --- |
| Root not CYKV, wrong schema, or version other than 10 | map not opened |
| Required root member invalid (`map_id`, `name`, `game`, `next_id`, `layers`) | map not opened; the diagnostic names the member |
| Optional root member invalid (`cell_size`, `units_per_meter`, ...) | default used, reported |
| Chunk from another map (`map_id` mismatch) | file kept untouched and reported; a save that would write that path is refused until the file is resolved |
| Chunk not CYKV, wrong schema, unreadable `layer`, or a section that is not a list | as above |
| Chunk `cell` unreadable | objects placed by position on save, reported |
| Chunk at a path other than its layer and cell | read normally; rewritten at the right path and the old file removed on save |
| One unreadable object (bad face plane, fewer than 4 faces, mesh index out of range, ...) | object kept verbatim in its chunk, reported, written back unchanged |
| Object or part without an ID | fresh ID assigned, reported |
| Duplicate ID | map opens read-only; "Reassign Duplicate IDs" fixes it |
| ID at or above `next_id` | `next_id` raised past it, reported |
| Two files for the same layer and cell | objects merge on save and the extra file is removed; differing unknown chunk members under one name refuse the save |
| Unknown entity class or property | kept; reported by the entity validator |

## 14. Engine map settings (`settings.map`)

Read tolerantly with setting descriptors: an invalid value falls back to its
default and is reported. Game-specific settings live in `settings.game`,
defined by the game profile.

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `sky` | asset `.cymat` | none | sky material |
| `gravity` | real | 800 | units per second squared, down the z axis |
| `ambient_color` | colour | `#202020` | base ambient light |
| `fog_color` | colour | `#808080` | distance fog colour |
| `fog_start`, `fog_end` | real | 0, 0 | fog range; equal values disable fog |
| `fog_density` | real | 0 | exponential height fog density; 0 disables |
| `music` | asset | none | background music |
| `ambience` | asset | none | looping ambient sound |
| `reverb` | string | `none` | default reverb preset outside reverb volumes |
| `max_view_distance` | real | 0 | far clip override; 0 uses the engine default |
| `postfx` | asset `.cypostfx` | none | post-processing profile |
| `lightmap_scale` | real | 16 | default world units per lightmap texel |
| `navigation_agents` | array | engine default | agent sizes for navigation builds: `{ name, radius, height, step, slope }` |

## 15. Scale

A map has no total size limit: every chunk is its own file, loaded in
parallel, and a layer spreads over as many cells as its content covers. The
per-chunk limits bind instead.

Measured on 2026-09-29 with the compact layout (the `[.scale]` test in the
map tests prints them):

| Object | Text |
| --- | --- |
| box brush, 6 faces, a material on each | 1,269 bytes, 127 values |
| point entity with 5 properties and 1 output | 454 bytes |
| foliage instance | about 60 bytes |
| terrain | 8.3 bytes per height sample, plus about 4 per painted sample per layer |

Meshes cost about 40 bytes per vertex plus 60-220 bytes per face, depending
on which UV and colour channels are present (from the example map). A
50,000-brush map is on the order of 65 MB across its chunks.

For brushes the value budget binds before the text size: one chunk holds
about 7,900 box brushes (1,000,000 values) against about 13,000 by text
(16 MiB). The default 8192-unit cells keep ordinary levels far below either.

A cell that outgrows its chunk is refused on save (`LIMIT_EXCEEDED`) rather
than written unreadable; the fix is a smaller `cell_size` for the map, or
moving dense detail onto its own layer.

## 16. Limits

| Item | Limit |
| --- | --- |
| Root document | 1 MiB |
| One chunk | 16 MiB text and 1,000,000 values; the writer refuses a chunk over either, so it never writes one the reader rejects |
| Layers | 256 |
| Visgroups, selection sets | 4,096 each |
| Cordons | 256 |
| Objects per chunk | 65,536 (foliage instances not counted) |
| Faces per brush | 256 |
| Vertices per mesh face | 256 |
| Entity properties / outputs | 512 / 256 per entity |
| Foliage instances per chunk | 1,000,000 |
| World extent | +/-1,048,576 units per axis |

## 17. Versions

`cypher.map` V1-V3 are the legacy tile-map lineage (now `.cytilemap`) and
V4-V9 are never used ([ADR 0009](../adr/0009-editor-and-map-file-identities.md)).
V10 is the first full-map version. Game-side changes (renamed classes or
properties) migrate through `game_version` without changing the map schema
version; a future map schema change bumps to V11 with a documented upgrade
applied on load.

## 18. Complete example

[`docs/formats/examples/cymap/`](examples/cymap/) holds a small complete
map written by Mason's own writer: `facility.cymap` and 13 chunk files over
four layers - a room of six brushes with materials, a terrain with a hole
and a paint layer, a mesh ramp with a modifier, a patch arch, foliage spread
over four cells, two player starts, a wave start with outputs, two enemy
spawns, a light in a visgroup, a trigger and a door that own their brushes, a
prop, a logic relay without a position (in `global`), a patrol shape, a
group, a prefab instance, and a designer note. The root adds visgroups, a
selection set, a cordon, and map and game settings.

The tests load it and require that saving it reproduces every file byte for
byte, so it is always a true example of the format. It is generated from the
hand-written form in the map tests (`[.write-example]`).
