# Mason toolset: what is wired, what is not

Reviewed 2026-10-04. This matrix covers what a mapper can actually do in Mason today: a command that only switches modes counts as missing.

It is compared against Hammer 5 (Source 2), TrenchBroom and NetRadiant-custom. Keybindings are tracked separately in [mason_geometry_workflow_research.md](mason_geometry_workflow_research.md) and [mason_geometry_binding_inventory.md](mason_geometry_binding_inventory.md).

Status words:

- **Wired**: it runs through a validated document edit, takes one undo step, and has contract tests.
- **Placeholder**: the command is registered and disabled (`MapWorkspace_IsPlannedCommand` returns true).
- **Mode only**: the tool button switches tools, but no viewport gesture does anything yet.

## 1. Operations wired on 2026-10-04 (`CypherMapGui_GeometryCommands`)

Implementation:

- Core adapters live in `CypherMap_TopologyEdit` and `CypherMap_TextureEdit`.
- Mesh component edits go through `MapMeshEdit_Apply`, Codex's transaction, fresh-ID and lineage path, generalised.
- The commands replace the workspace's placeholders under the same IDs, so menus, toolbars, Tool Properties and keymaps pick them up unchanged.

| Mode | Operation | Command | Geometry backend |
| --- | --- | --- | --- |
| Vertices | Merge to average | `map.mesh.merge` | `MeshSourceEdit_TryMergeVertices` (CENTER) |
| Vertices | Collapse | `map.mesh.collapse` | same |
| Vertices | Connect (cut a face between two vertices) | `map.mesh.connect_edges` | `MeshSourceEdit_TrySplitFace` |
| Vertices | Snap to grid | `map.brush.snap_to_grid` | `MeshSourceEdit_TryMoveVertex` |
| Vertices / edges | Radial Align | `map.mesh.radial_align` | `MeshSourceEdit_TryMoveVertex` |
| Vertices / edges | Weld UVs | `map.texture.weld_uvs` | `MeshSourceEdit_TrySetCornerAttributes` |
| Vertices / edges | Grow / Shrink selection | `map.select.grow`, `map.select.shrink` | `MeshSelection_TryGrow/Shrink` |
| Edges | Merge (sew two open edges) | `map.mesh.merge` | `MeshSourceEdit_TrySewEdges` |
| Edges | Collapse each connected group | `map.mesh.collapse` | `MeshSourceEdit_TryMergeVertices` |
| Edges | Dissolve | `map.mesh.dissolve` | `MeshSourceEdit_TryDissolveEdge` |
| Edges | Split at midpoint | `map.mesh.split_edges` | `MeshSourceEdit_TrySplitEdge` |
| Edges | Connect midpoints across shared faces | `map.mesh.connect_edges` | split edge, then split face (two transactions, one undo step) |
| Edges | Fill Hole | `map.mesh.fill_hole` | `MeshSourceEdit_TryFillHole` |
| Edges | Bridge two open loops | `map.mesh.bridge` | `MeshSourceEdit_TryBridge` |
| Edges | Bevel (width and segments are settings) | `map.mesh.bevel` | `MeshSourceEdit_TryBevelEdges` |
| Edges | Extrude off the surface / Extend in plane | `map.mesh.extrude_edges`, `map.mesh.extend_edges` | `MeshSourceEdit_TryExtrudeEdges` |
| Edges | Hard / Soft / Default normals | `map.mesh.normals_*` | edge HARD flag and face smoothing groups |
| Faces | Collapse | `map.mesh.collapse` | `MeshSourceEdit_TryMergeVertices` |
| Faces | Split (detach within the mesh) | `map.mesh.split` | `MeshSourceEdit_TryDetachFaces` |
| Faces / objects | Align to World / Face, Fit, Justify (L/R/T/B/centre) | `map.texture.*` | `UvAlign_*` on brush sides, `MeshUv_*` on mesh faces |
| Faces / objects | Rotate / Scale / Shift material (console arguments) | `map.texture.rotate/scale/shift` | `UvTools_*`, `MeshUv_TryTransform` |
| Meshes | Merge selected meshes into one | `map.mesh.merge` | `MeshSource_TryJoinExact` |
| Meshes | Separate disconnected pieces | `map.mesh.split` | `GeometryMeshObjectCommand_TrySeparateByShell` |
| Meshes | Smooth | `map.mesh.smooth` | `MeshSourceEdit_TrySmooth` |
| Meshes | Solidify open meshes | `map.mesh.solidify` | `MeshSolidify_TryBuild` |
| Meshes | Subdivide (one Catmull-Clark level; hard edges stay sharp) | `map.mesh.subdivide` | `Subdivision_TryCollapse` |
| Objects | Carve | `map.brush.carve` | `MapBrushEdit_Subtract`, chained per cutter |
| Objects | Snap to grid (minimum corner) | `map.brush.snap_to_grid` | `MapEdit_Translate` |
| Objects | Flip horizontally / vertically | `map.transform.flip_*` | `BrushAuthoring_TryMirror`, `MeshSourceEdit_TryMirror`, `Patch_TryTransform` |
| Objects | Rotate 90 clockwise / counter-clockwise | `map.transform.rotate_*` | `MapWorkspace_RotateSelection` |
| Objects | Align left / right / top / bottom | `map.align.*` | `MapEdit_Translate` |
| Objects | Select Touching, Select Same Material | `map.select.touching`, `map.select.same_material` | wireframe bounds and face materials |
| Settings | Texture Scale Lock | `map.texture.scale_lock` | `editor.map.texture_scale_lock` |

**Brushes in component modes (Hammer 5 workflow).** Vertex and edge tools address mesh components, and Mason's Block tool makes Quake-style brushes by default. Clicking a brush in Vertices or Edges mode converts it to a mesh, as one undo step, and picks the vertex or edge under the cursor. This works in every pane (`MapWorkspace_ConvertBrushForComponents`).

Two settings control this:

- `editor.map.components_convert_brushes` turns the click conversion off.
- `editor.map.block_output` set to `mesh` makes the Block tool create meshes directly, as Hammer 5 does. Its default stays `brush`, so Clip, Hollow, Carve and brush Merge keep working on new blocks.

Directions follow the last hovered 2D view. Before any 2D view is hovered, they follow Top.

New settings on the Map Editing page:

- `editor.map.mesh_bevel_width` and `editor.map.mesh_bevel_segments`
- `editor.map.mesh_edge_extrude_distance`
- `editor.map.mesh_solidify_thickness`
- `editor.map.mesh_smooth_strength`

## 2. Already wired before 2026-10-04 (Codex)

- **Creation and transforms**:
  - Block primitives: box, wedge, cylinder, spike, sphere, quad.
  - Staged block resize.
  - Translate, rotate, scale and resize, with gizmos, handles, snapping and texture lock.
  - Nudge, delete, duplicate, copy, cut, paste and paste in place.
- **Brush editing**:
  - Clip (Back / Front / Both, with an exact plane).
  - Hollow; Merge (convex union); Subtract.
  - Brush face push/pull in 2D and 3D; Apply Material.
- **Mesh editing**:
  - Brush→mesh conversion; flip normals; triangulate.
  - Mesh face Extrude, Inset and Quad Slice.
  - Vertex, edge and face picking; Select Loop and Select Ring.
- **Scene management**:
  - Entities: placement and properties (with undo).
  - Hide and show; Auto Vis Groups and presets; cordon.
  - Select All / Invert / Same Class.
  - Check for Problems.

## 3. Wired on 2026-10-05

### 3.1 Tools (`CypherMapGui_ToolGestures`)

Views forward mouse, Confirm, Cancel and Backspace input to these tools through one hook, and the tools paint their overlay last. Core adapters: `CypherMap_ObjectCreate`, `CypherMap_TopologyEdit`.

| Tool | What it does |
| --- | --- |
| Entity | Click in 2D (on the grid) or on a 3D surface to place the default point class. |
| Path | Clicks place `path_corner` entities, each targeting the next. |
| Decal / Overlay | Click a surface to place `infodecal` / `info_overlay` with the active material. |
| Polygon | Click points in 2D, or on a surface or the workplane in 3D; close or Confirm to extrude a mesh by the block depth. Concave shapes work. |
| Patch | Drag a box; creates a plane, cylinder, cone, sphere, disc, bevel or torus patch (`editor.map.patch_shape`). |
| Curve | Click points, then Confirm to sweep a strip mesh along them. |
| Measure | Drag to read length and per-axis distances. |
| Eyedropper | Click a face to make its material the active material. |
| Mirror | Drag a line in 2D to mirror the selection; Shift mirrors a copy. |
| Paint | Drag over a mesh to paint vertex colours down in a channel; Ctrl paints them back. |
| Knife | Click points across a mesh (corners, edges and face interiors snap), then Confirm. |
| Loop Cut | Click a mesh edge to cut a loop across its ring. |
| Vertex | Drag corners of selected brushes (convex rebuild) and meshes (moves the component selection). |
| Pivot | Click to set the point Rotate 90 and the flips use; Clear Pivot removes it. |
| Workplane | Click a face to set the plane the Polygon tool draws on in 3D. |
| Clip on meshes | Folded into the one Clip tool on 2026-10-07 (see 3.6). |
| Edge Cut | Click points on mesh edges or corners (Ctrl snaps to the middle); Confirm cuts across the faces between them. |
| Edge Arc | With an open chain of edges selected, drag to bend it into a circular arc between its ends; the height snaps to the grid. |
| Component gizmo | Translate, Rotate and Scale in Vertices, Edges and Faces modes: axis handles in 2D and 3D, grid, angle and scale snapping, Shift+drag extrudes a face or extends open edges. Brush faces translate. The numeric Move/Rotate/Scale panel applies the same transforms. |

### 3.2 Commands

- **Convert to Brush:** closed convex meshes become brushes again, with UVs fitted.
- **Union, Subtract, Intersect:** run through brush CSG on convex meshes.
- **Replace Materials:** a dialog, or `map.texture.replace <from> <to>` in the console.
- **UV Islands:** charts by facing direction, packed into one repeat.
- **Group and Ungroup:** CYMAP 6.10 records. Groups mode selects a group's members together, and groups can nest.
- **New Layer:** adds a layer and moves the selected geometry into it.
- **New Visgroup from Selection:** CYMAP root visgroups. The visgroup gets a show/hide row in Auto Vis Groups.
- **Create Prefab:** writes a `.cyprefab` per CYMAP 6.12.
- **Insert Prefab:** places a copy with fresh IDs. Linked instances are not created, because the views do not load prefab files yet.

### 3.3 Selection queries, cleanup and the patch matrix

- **Component selection:**
  - Select Connected, Invert Component Selection and Select Path (shortest path between two vertices).
  - Select Ribs (edges crossing the selected edges at their vertices).
  - Select Open Edges and Select Sharp Edges (`editor.map.sharp_edge_angle`).
  - Convert to Edges / Convert to Vertices, which switch mode.
- **Component edits:**
  - Offset along normals (`editor.map.mesh_offset_distance`).
  - Flatten: best fit, average normal, X, Y or Z.
  - Slide Edge Loop (`editor.map.mesh_slide_factor`).
  - Snap Edge to Edge: welds onto the edge picked last.
  - Delete Face.
- **Mesh cleanup:** Weld and Merge by Distance (`editor.map.mesh_weld_distance`); a mesh with nothing to join is left alone.
- **Extrude to New Brush:** a brush face swept into a separate brush; a negative distance cuts the slab off instead.
- **Patch matrix (NetRadiant):** Insert/Remove Row and Column, Transpose and Invert, plus the Torus shape.

### 3.4 Multi-face selection, entity I/O and the rest (2026-10-06)

- **Faces mode holds several mesh faces:**
  - Shift+click adds a face of the same mesh and makes it active; Ctrl+click removes one.
  - Extrude, Inset and Quad Slice act on the active face.
  - Offset, Flatten, Detach, Delete, the texture commands and the gizmo act on all of them.
  - Bridge joins two selected faces with a tube.
- **Face queries:**
  - Select Coplanar, Faces Facing, Same Corner Count, Faces by Material, Face Path.
  - Grow, Shrink, Connected and Invert in Faces mode.
- **Bridge on edge chains:** two chains of open edges are joined strip by strip.
- **Geometry fix:** `MeshOps_SplitEdge` refused every open border edge (CORRUPT_STATE). It now splits one-sided borders, so Split Edges works on open meshes.
- **Select Inside and Select Tall (TrenchBroom):** use the selection's bounds; the selecting objects drop out.
- **Make Room (NetRadiant):** hollows a brush with the walls outside.
- **Thicken Patch (NetRadiant):** a back copy, plus seams along open borders.
- **Live previews:**
  - The Patch tool draws the patch's iso-lines while dragging; spheres take their depth from the dragged square.
  - The Polygon tool draws the extruded prism.
- **Entity I/O (Hammer's Outputs and Inputs):** `map.entity.io` opens a window.
  - Outputs are edited in place, one undo step per edit: output, target, input, parameter, delay, times.
  - Add, Duplicate, Remove and Move Up/Down.
  - Target names are suggested from the map; missing targets show in red.
  - Inputs list the connections that reach the entity.

### 3.5 Viewport interaction and tool fixes (2026-10-07)

- **Right-click editing menu in every 2D and 3D view** (`CypherMapGui_ViewMenu`):
  - It opens with the operations the current mode can run now.
  - Then Cut/Copy/Paste, Paste Here, Paste In Place, Duplicate and Delete.
  - Place Entity Here and Insert Prefab Here at the clicked point.
  - Submenus: Select, Mode, Object, CSG, Mesh, Texture, Patch, Entity, Go To.
  - Set Pivot Here / Clear Pivot, and the pane's View Options.
  - A right click on an unselected object selects it first.
- **Vertices are visible:**
  - Every pickable vertex shows as a rimmed square in 2D and 3D (wireframe and shaded, hidden ones hidden).
  - Hover and selection draw larger, in green and yellow.
- **Pivot:**
  - The object Rotate and Scale gizmos and drags, the numeric panels ("Pivot point") and the component transforms now use it. Before, only Rotate 90 and the flips did.
  - The Pivot tool drags the point and shows a labelled crosshair in every view, or a ghost at the selection centre.
- **Paint:**
  - Painted vertex colours now tint the 3D view's faces; before, painting had no visible effect.
  - Colour mode (`editor.map.paint_color`) is the default, Ctrl erases, and the stroke shows while dragging.
- **Workplane:** 3D placement (Entity, Polygon, Patch, Path, Curve, Pivot) lands on it, and its grid stays visible while set.
- **Disabled buttons say why** ("Unavailable: needs two or more vertices", `MapGeometryCommands_Requirement`).
  - Component modes show a start hint while nothing is picked.
  - Component operations no longer grey out under Translate, Rotate or Scale.
- **Brushes work with mesh operations:** Smooth, Subdivide and the booleans convert selected brushes to meshes in the same undo step.
- **CSG dropdown:** Hollow, Make Room, Carve, Merge, Extrude to New Brush, Clip, the booleans, and Convert to Mesh/Brush.
- **Vertex operations:** Bevel (corner chamfer), Dissolve (the faces around a vertex become one) and Fill Hole from two border vertices.
- **Library rule:** an edit cannot start while a face lacks identity.
  - Face-creating loops (Connect over several faces, Fill Hole over several holes, vertex Bevel) now run one transaction per face, still one undo step.
  - Before, the second face failed.

### 3.6 Vertex dragging and the Clip tool, TrenchBroom-style (2026-10-07)

- **Vertices and edges drag** (Select tool, Vertices or Edges mode; TrenchBroom's vertex and edge tools, NetRadiant's V/E modes):
  - Press on a handle and pull it.
  - A drag on an unselected handle selects it first. A drag on one handle of a selection moves the whole selection.
  - A click without moving picks just that handle. A drag from empty space box-selects (Shift adds, Ctrl removes).
  - Movement plane: the pane's plane in 2D; in 3D the floor plane, or vertical while Alt is held.
  - Snapping: by grid steps; Shift puts the handle on grid points, Ctrl turns snapping off.
  - Dropping one vertex onto another fuses them. Every drag is one undo step.
  - Picking on press stays the view's own (occlusion, Shift/Ctrl), and the drag starts after 3 px of movement.
  - In 2D the handle nearest the viewer moves, as in TrenchBroom. Box-select to move a stacked column.
- **One Clip tool for brushes and meshes** (`MapWorkspace_CanClipSelection`, `MapTopologyEdit_ClipMeshes`):
  - Brushes, meshes and mixed selections clip together, in 2D and 3D, as one undo step.
  - Front/Back caps the kept side and removes objects wholly on the discarded side. Both splits a mesh into two closed meshes, as it splits a brush.
  - The numeric plane panel and Slice (`map.mesh.slice`, which applies a staged plane) use the same path.
- **Clip readability:**
  - The part that goes away is a red dashed ghost.
  - 3D shows the plane as a translucent sheet across the selection.
  - Each half is tagged KEEP or REMOVE; where the plane faces the view, one tag says which side stays.
- **3D placement on surfaces** (`editor.map.clip_3d_placement`, default `surface`, TrenchBroom):
  - Draw the cut line across a face of the selection, or click point 1 and point 2. The cut runs straight through, perpendicular to that face.
  - Both points can be dragged afterwards. A press off the selection still uses the construction plane (`plane`, Codex's placement, pinned in its tests).

### 3.7 Hammer 5 workflow pass (2026-10-07, second round)

- **Status bar notices** (`CypherMapGui_Notice`): Hammer's coloured bar at the bottom left.
  - Green for done: 'Saved "<full path>"', opened maps, applied materials, extracted faces.
  - Red for a map action that could not run, with what it needs (from `MapGeometryCommands_Requirement`).
  - Amber for warnings. The console keeps every notice.
- **Clip:**
  - Entered from Vertices, Edges or Navigation mode it switches to Objects with the edited mesh selected. Before, it silently did nothing.
  - Keep Mode buttons (back, front, both) and "Cap new surfaces" (`editor.map.clip_cap`; off leaves mesh cuts open).
- **Shift+T (Apply Current Material)** works on the picked brush face, the picked mesh faces, or every face of the selected brushes, meshes and patches (`MapTextureEdit_SetMaterial`). Before, it worked on one brush face only.
- **Vertices and Edges mode look like Hammer 5:**
  - The object being edited has orange faces in 3D, a green outline in 2D, and a green dot on every vertex.
  - Selected vertices are yellow squares; selected edges are thick yellow with marked ends; what a click would grab is cyan.
  - A click on an object's body makes it the object being edited.
- **Faces operations in Hammer 5's order:** Apply Material, Find/Replace, Merge, Extract Faces, Detach (Split), Bridge, Bevel, Thicken Faces, Radial Align, Combine Faces, Collapse, Remove Bad Faces, Clear Pivot, Edge Cut.
  - Then Quad Slice (U/V), Thicken distance and Subdivision sections.
  - New core operations: `MapTopologyEdit_ExtractFaces/ThickenFaces/CombineFaces/RemoveBadFaces`.
- **Displacement tool** (`map.tool.displace`, Hammer 5's Paint Displacement):
  - Brushes: Push/Pull (Ctrl lowers), Smooth, Flatten, Inflate, Pinch, Noise.
  - Settings: radius, strength, smooth amount, normal mode (average, brush, view, Z), falloff (smooth, linear, sharp, constant), apply to selected or everything.
  - The surface moves in every view while painting. Each stroke is one undo step, and Escape puts a stroke back.
  - Needs vertices to move: Quad Slice or Subdivide first; the tool's panel has both.
- **Object Properties has Properties | Outputs | Inputs tabs.** Tab titles show connection counts. They use the same pages as the Entity I/O window.
- **Texture section** in Faces mode and the Texture tool (`CypherMapGui_FaceEdit`):
  - Material with Apply, Scale U/V with Set (`map.texture.set_scale`, Hammer's 0.25 = 128 units per repeat), Shift arrows with a step, Rotate with a step.
  - Justify L, R, T, B, Center, Fit; Align World, Face, Islands; Flip U, V.
  - A single brush face shows its current scale, shift and rotation.

### 3.8 Consuming the rest of the geometry library (2026-10-07, third round)

An include-graph inventory found 66 of 158 geometry modules unreachable from the editor. After this round, 21 remain, all of them infrastructure, superseded APIs, or renderer-side (see 4).

- **Modifier stacks** (`CypherMap_Modifiers`):
  - Per-mesh, non-destructive Mirror X/Y/Z, Array, Radial Array, Bend, Taper and Subdivide, stored in the mesh's map record as `modifiers { version = 1; stages = [...] }`. Meshes without a record get one.
  - The wireframe draws the evaluated surface. Smooth faces carry their cage face's ID, so picking, highlighting and Faces mode still work; the cage stays editable and drawn green in 3D in component modes.
  - Hammer 5's subdivision: Increase/Decrease/Bake and Level 0-5.
  - Apply, Remove Last and Clear Modifiers.
  - Mirroring a closed mesh across its own face moves the plane half a grid step out (the library refuses a shared face).
- **Real mesh CSG** (`GeometryDocument_TryCsgMeshes`/`TryCsgSplitMesh`): Union, Subtract and Intersect now handle concave shapes (L-shapes, tunnels). New: Exclusive (symmetric difference) and Split by Mesh.
- **Create Displacement** (`Displacement_TryBuildMesh`): a Source-style power 1-4 grid on a picked 4-cornered face, with elevation and seeded fractal noise. It is ready for the Displacement tool.
- **Compile Map** (`CypherMap_Compile`):
  - Runs the cook pipeline from one snapshot: surface soup, dependency keys, collision, visibility hulls, walkable surfaces, lightmap charts and render batches.
  - Writes `<map>.interchange.cykv` (cypher.geometry.interchange v1) beside the map.
  - Reports every product in the status bar and every skipped object in the console.
- **Surfacing:** Box, Cylindrical and Planar mapping, Set/Clear Smoothing Group, Auto Hard Edges (`MeshSurfacing_*`).
- **Curve tool shapes:**
  - Straight strip.
  - Smooth sweep, a rectangle or round profile along a Catmull-Rom curve (`Sweep_TryAlongPath`).
  - Lathe around the vertical through the first point (`Sweep_TryLathe`).
  - Walls along a path, with mitred, bevelled or round corners (`Planar_TryStrokePolyline` + `PlanarExtrude_TryExtrude`).
- **Brush tools:**
  - Patch from Face (`BrushPatches_TryFromFace`).
  - Brush Sweep: straight, arc or S-bend, with segments and repeats (`BrushSweep_TryBuild`).
  - Vertex clumping: the Vertex tool moves a corner shared by several selected brushes in all of them (`BrushVertexOps_TryMoveVertexClump`).
- **UV seams and islands, repair:**
  - Mark/Clear Seam (`MeshUv_TryEditSeams`) and Select UV Island (`MeshUv_TryDiscoverIslands`).
  - Repair Mesh (`Repair_TryPreviewMesh`): weld, drop degenerate and duplicate faces, fix winding, cap flat holes, with a report.

### 3.9 Tool audit through the real views (2026-10-11)

A hidden smoke test, `cypher_mason_smoke_tests "[.tool-audit]"`, drives every tool with mouse and key events sent to the actual panes of a Mason window and prints what each changed, then lists the operations each selection mode offers for a brush and for a mesh. What it found, and what changed:

| Problem | Fix |
| --- | --- |
| Knife, Loop Cut, Paint and Displace did nothing on brushes (the Block tool makes brushes; these tools only took meshes) | A mesh tool used on a brush makes it a mesh first (same ID, same face IDs), one undo step, with a notice. Setting: `editor.map.components_convert_brushes`. |
| Faces mode on a brush face greyed out 58 of 85 operations (Extrude, Inset, Subdivide, Delete...) | Mesh operations are wrapped (`InstallBrushAdapters`): on a brush face or selected brushes they convert first, then run; when the operation fails or still does not apply, the conversion is undone. Only selections of brushes and meshes qualify. |
| Edges mode: clicking exactly on a brush edge selected the brush, not the edge (the pick ray grazed past the brush) | The 3D view finds the brush by its nearest visible edge (`PickBrushEdge`) before converting. |
| Extrude tool: dragging a mesh face did nothing (push-pull existed for brush faces only) | Mesh-face push-pull in 3D (`ExtrudeEvent`): out extrudes, in moves the faces, snapped, with a preview. |
| Knife: a click exactly on an edge was dropped | Falls back to the edge or corner under the cursor. |
| Displace refused with nothing selected; painted nothing on a fresh box (8 corners) | Nothing selected paints any visible mesh; a face wider than the brush is sliced into cells a quarter of the brush across first (`editor.map.displace_auto_tessellate`). Paint does the same (`editor.map.paint_auto_tessellate`). |
| A displacement grid lying on its base brush lost the pick to the brush | Surface picks prefer the mesh on a depth tie. |

### 3.10 Making every tool work as expected (2026-10-11, second pass)

From real use: displacement left the box unusable for the cut tools, Knife and Loop Cut failed silently with `GEOMETRY_FAILED`, Terrain could not sculpt, and many clicks did nothing without a word.

| Problem | Fix |
| --- | --- |
| Displace sliced faces in place: neighbours became many-sided, sculpted cells became non-planar, then Knife and Loop Cut failed | A face wider than the brush becomes its own displacement grid (Source/Hammer): the face leaves its mesh and a separate grid replaces it in one undo step; the object keeps flat faces. Undo puts the face back. |
| Knife / Loop Cut / Edge Cut / Edge Arc failures were silent | Each says why (bent cells, not a quad ring, the bend would leave the plane); Knife keeps its points so Backspace can try another path. |
| Terrain tool could only create a flat heightfield; sculpt buttons were placeholders | Raise (Ctrl lowers), Lower, Smooth, Flatten to the height pressed, with radius and strength; live preview (`MapWireframe_RefreshTerrain`), one undo step per stroke, Escape cancels. |
| Paint replaced faces when they were coarse | Paint paints the mesh's own vertices again and says when the stroke reached none. |
| Many tools silently ignored a click they could not use | A miss pass in `[.tool-audit]` clicks every tool on empty sky and ground with nothing selected; every tool now either acts or says what it needs (Clip, Move/Rotate/Scale, Block, Vertex, Extrude, Polygon, Curve, Path, Patch, Measure, Pivot, Mirror, Eyedropper, Workplane, Decal, Overlay, Entity). |
| A drawn block was lost when another tool was picked | It is created instead; Create blocks on release (TrenchBroom) skips the Enter step; Block tool creates brush/mesh is in the Block panel. |
| Edge Arc needed edges picked with another tool first | Click an edge, Shift+click the next, drag to bend, all in the tool. |
| Applying a material faces already have reported a failure | It is done, not failed. |

### 3.11 Menu audit (2026-10-13)

A hidden smoke test, `cypher_mason_smoke_tests "[.menu-audit]"`, runs every Mesh, CSG, Surface, Texture, Patch, Brush, Group, Layer, selection and entity command against 23 kinds of selection: nothing selected, a brush, two brushes, faces, edges and vertices of a mesh, a group, a patch, and setups such as a mesh with a modifier, a patch with spare rows, two entities of one class, and a second layer. `CYPHER_AUDIT_PREFIX` narrows it to one family. Most greyed items were correct, but they did not say why, and many commands failed silently.

| Problem | Fix |
| --- | --- |
| A greyed item gave no reason | Its tooltip says what it needs ("Needs selected mesh faces (Faces mode)"), from `MapGeometryCommands_Requirement`. |
| A command that ran and did nothing said nothing, or left an old message up | Every edit records APPLIED, UNCHANGED or FAILED (`map_workspace_t::lastEdit`); Mason says "nothing to change" with the reason, or "could not be applied" with the cause. |
| Navigation mode was missing | Walkable floor tints green, slopes steeper than the compile slope red; entering the mode or clicking an object reports walkable area and steepest slope. |
| Array and Radial Array failed on one box; Exclusive failed on overlapping boxes | Array spaces copies apart and Radial puts its axis beside the object; Exclusive makes the two leftover pieces two objects. |
| Weld, Merge by Distance, Snap to Grid and Flatten made a brush a mesh even with nothing to do, and left a Redo step when they did nothing | The conversion is undone and dropped from the redo list (`EditorHistory_DiscardRedo`). |
| Merge was offered for one object; aliases such as Justify Top were enabled with nothing selected | Merge needs two; aliases follow their command's state. |
| Cancelling the Move to Layer or OBJ file dialog was reported as a failure | Cancelling is a quiet no-op. |
| Select Same Class and Select Same Material changed the selection without a word | They say how many they selected. |

Correct refusals that now explain themselves: Slide Edge Loop (needs a loop of four-edge corners), Bridge (the faces must have room between them; inset first), brush Merge (only brushes that make one convex shape; Union otherwise), Patch Cap (only a border that closes on itself).

### 3.12 Game profile in Object Properties and the views (2026-10-13)

| Before | Now |
| --- | --- |
| Object Properties listed only the keys an entity had, edited as raw text in a dialog | For a class the profile defines, every property shows in profile order (sections after), labelled, with an editor for its type (`CypherMapGui_PropertyEditors`): checkbox, dropdown of choices, number field with limits and unit (Up/Down step), colour swatch with hex field, X/Y/Z or pitch/yaw/roll fields, asset picker listing the content files of its extensions, target picker of entity names, object ID field. Unwritten values show the default in italics; setting one writes it. Each change is one undo step. A mixed selection shows "Multiple values" and writes one value to all. Invalid and missing required values are flagged on the row. |
| The class was typed in a dialog | A class dropdown of the profile's placeable classes of the same kind, and an inline name field. Remove on a profile key returns it to the default; profile keys cannot be renamed. |
| Point entities were 16-unit cubes in their visgroup colour | Boxes take the class `size` (picking and framing follow) and colour: the entity's `editor.color`, else the class's `color_from` property (lights in their light colour), else its `color` (`CypherMapGui_EntityLook`). |
| No helpers | Selected entities, and entities owning selected brushes, draw their class helpers in 2D and 3D: facing arrow, radius, spot cone, line to a target, door travel (the brush's depth along the direction, less the lip) and size box. |

## 4. Still missing — honest list

| Command / tool | Why |
| --- | --- |
| `map.run`, `map.stop`, `map.leak.show` | Compile Map cooks and writes the interchange file, but there is no BSP/visibility compiler to run, so no leak file or game launch. |
| Terrain tool and `map.terrain.*` | Codex is building terrain creation and sculpting (`MapWorkspace_CreateTerrain`, `map.terrain.create`). |
| Game profile follow-ups | Typed editors, class sizes, colours and helpers are done (3.12). Still to do: applying `migrations` to older maps, drawing class models and icons (`model`, `icon`), filled entity boxes in the 3D view, and pickers for `objects` and `faces` lists (they use the value dialog). |
| Smooth shading of smoothing groups and hard edges | The 3D view is a CPU painter with one flat colour per face; `MeshCornerNormals` needs per-corner normals in a GPU renderer (Codex's CypherRender). The data is authored and saved. |
| Geometry modules not reached directly (19) | Infrastructure the editor reaches through other paths (Transaction, Delta, Euler, SelectionSet, SpatialIndex, ReplacementIdentity, OperationContext, Diagnostics, Kernel_CoordinateKey, TriangleSoup); APIs newer modules replace (MeshBooleans, MeshSubdivision, CookSourceMapping, MeshBuilder, BrushEditPipeline); renderer-side (Cook_RenderMesh, MeshCornerNormals); PickingQueries and Workplane (the editor has its own picking and workplane). BrushQueries and BrushFaceQueries are now used (Wrap Texture Across Coplanar Faces), as are PatchPrimitive revolve/extrude (curve tool Patch output). |

### 4.1 Workflow features the reference editors have and Mason lacks

- **TrenchBroom**:
  - Linked groups.
  - Point file and portal file loading (needs a compiler).
  - Compile profiles.
- **NetRadiant**:
  - Regions (Mason's cordon covers part of this).
- **Hammer 5**:
  - Linked prefab instances.
  - Interactive Bridge and Bevel tools (both are commands with settings).
  - Logic graph view for entity I/O.
  - Run the map.

Done on 2026-10-09: game profiles (`CypherMap_GameProfile`: classes with bases, includes, typed defaults, map settings, migrations decoded, tolerant validation; the Entity tool's class list and Next Class, defaults written on placement and Tie to Entity, Entity I/O output and input suggestions, Object Properties help, Check for Problems game rules with Fix, Reload Game Profile; a REAP profile in `assets/game/reap.cygame`), Selection Sets editing (new, add, remove, rename, delete, hide, show), the hand-drawn Custom falloff curve, Outliner F2 / F keys, and the View menu's render entries.

Done since the last revision of this list: hotspot texturing (`.cyrect` sidecar files, Apply Hotspots, regions marked in Fast Texture), Block shapes Arch / Stairs / Spiral Stairs / Pipe / Torus, Tie to Entity / Move to World / Move to Layer, Find Entities and Find and Replace, Wavefront OBJ export and import, group outlines in Groups mode, the displacement brush icon row and falloff curves, the Fast Texture window (Use Existing / Unwrap Planar / Unwrap Square, flips, inset), Patch Cap, issue auto-fixes (Check for Problems Fix / Fix All, with a mesh health check), autosave, Thicken on faces, compile (cook + interchange), concave mesh booleans (CSG), 3D clip between vertices with a third point, group membership commands and group-aware duplicate / delete / clipboard.

## 5. Order of work that follows from this

1. Applying game profile migrations when an older map opens.
2. Hotspot regions in materials, once the format is decided.
3. Linked prefab instances, once the views load prefab files.
4. Build pipeline: compile, run, leak.

## 6. Parity review against TrenchBroom and Hammer 5 (2026-10-13)

Method: TrenchBroom's 179 menu actions (`docs/trenchbroom_ui_command_inventory.md`, from its source) and the 825 Hammer 5 key bindings (`docs/mason_hammer_binding_discovery.csv`, an unversioned mirror, so names are reliable but defaults are discovery-only) were compared one by one with Mason's 336 registered commands, and the uncertain cases checked in Mason's code. `[.tool-audit]` and `[.menu-audit]` were run the same day: every tool acts or says what it needs.

### 6.1 Already at parity

File (new, open, recent, save, save as, OBJ import/export, reload profile and assets); Edit (undo, redo, repeat, cut, copy, paste, paste in place, duplicate, delete, flip, transform dialog); CSG (merge, subtract/carve, hollow, intersect, union, exclusive); vertex snap to grid; patch from face and the patch matrix; texture lock and scale lock; replace materials; selection (all, none, invert, touching, inside, tall, similar, grow, shrink, loop, ring, ribs, connected, coplanar, path, UV island); groups (create, ungroup, add, remove, select, rename); tools (block, polygon, clip, rotate, scale, sweep, vertex, edge and face modes, patch control points, knife, loop cut, edge cut, edge arc, extrude, mirror, paint, displacement, terrain, path, curve, decal, overlay, workplane, pivot, measure, entity, eyedropper); grid (show, snap, sizes, angle and scale snap); camera (focus selection, frame all, speeds); hide selected, hide unselected, show all; Shift-drag clone; selection sets; auto visgroups; cordon; Check for Problems with fixes; entity I/O; game profile with typed properties, class sizes, colours and helpers; Hammer's Faces/Edges/Vertices operation sets (extrude, inset, bevel, bridge, collapse, combine, connect, dissolve, extend, fill hole, merge, split, weld UVs, normals, subdivision, quad slice, radial align, thicken, extract, hotspots, justify, fit, align world/face, wrap).

### 6.2 Missing: everyday workflow (no outside dependency)

| Feature | Reference | Notes |
| --- | --- | --- |
| Drop to floor | Hammer `MoveObjectDownByTrace` (Ctrl+Num2) | Moves each selected object down onto the surface below it. |
| Pick the next object under the cursor | Hammer `SelectNext/SelectPrev` (PgUp/PgDn), TrenchBroom repeated click | Clicking where objects overlap only ever reaches the nearest. |
| Rotate by the angle snap from the keyboard | Hammer `NudgeRotate*` (Alt+arrows) | Nudge only moves. |
| Texture flip, rotate 90°, align to view, nudge offset | Hammer `FlipTexture*` (Alt+D/E), `RotateTexture90*` (Alt+A/Q), `AlignView` (V), `NudgeTexture*` | Only through the Fast Texture window or typed amounts. |
| Clear rotation and scale | Hammer `ClearRotationAndScale` (Ctrl+Num0) | Entities keep their angles and scale until edited by hand. |
| Bounding mesh around the selection | Hammer `CreateBoundingMesh` (Ctrl+Alt+C) | |
| Snap to grid per object | Hammer `SnapToGridIndividually` (Ctrl+Shift+B) | Snap moves the selection as one. |
| Revert to saved | TrenchBroom Revert Document, Hammer `FileReload` (Ctrl+Shift+R) | |
| Layers: hide, lock, current layer | TrenchBroom layer editor | Mason layers can be created and filled, not hidden, locked, or chosen for new objects. |
| Snap to vertices while moving | Hammer `ToggleSnapToVertices` (Ctrl+Shift+X), `SnapToVertex` (B) | Clip snaps to vertices; moves do not. |
| Select through and back faces | Hammer `ToggleSelectThrough` (Ctrl+Shift+L), `ToggleSelectBackfacing` | Box selection of components takes only what faces the view. |
| Show helpers, show tool materials | Hammer `ToggleShowHelpers` (Ctrl+Shift+H), `ToggleToolsMaterials` | |
| Entity class visibility | TrenchBroom View Options | Only the auto visgroups (lights, triggers, ...). |
| Vertex merge to target or last selected | Hammer `MergeTarget` (Ctrl+M), `MergeFixed` (Shift+M) | Merge goes to the average. |
| Shear | TrenchBroom Shear Tool | Done 2026-10-14 (6.9). |
| Freehand lasso | Hammer `LassoSelect` | Box selection only. |
| Typed values during a gizmo drag | Hammer `GizmoNumKey*` | The Transform panel takes exact values, not mid-drag typing. |

### 6.3 Missing: larger features

| Feature | Reference | Notes |
| --- | --- | --- |
| Linked groups | TrenchBroom Create Linked Duplicate, Select Linked, Separate, protected properties | Edits to one copy repeat in every copy. |
| Prefab instances with an edit scope | Hammer `CreateInstanceFromSelection`, `BakeSelectedInstances`, `ExitActiveEditScope` | Mason prefabs insert copies; there is no live instance. |
| UV editor | TrenchBroom face inspector UV view, Hammer Fast Texture UV widget | Done 2026-10-14 (6.8). |
| Interactive Bevel, Bridge, Thicken, Face Bevel | Hammer `BevelTool`, `BridgeTool`, `ThickenTool`, `FaceBevelTool` | Mason has them as commands with settings. |
| Entity parenting | Hammer `ReparentSelection` (P), `UnparentSelection`, `SelectParent` | Done 2026-10-14 (6.10). |
| Pick an entity for a target field | Hammer `ToolPickEntity` | Target fields are typed or picked from a list. |
| Asset spray | Hammer `ToolAssetSpray` | Scattering props; needs models in the views to be useful. |
| Physics placement | Hammer `ToolPhysics` | Needs CypherPhysics. |

### 6.4 Blocked on other subsystems

A GPU viewport (CypherRender into Mason: lighting preview, smooth shading, materials at full quality, speed on large maps); compile, run, leak, point and portal files (a BSP/visibility compiler and a game); models and icons in the views; light baking; nav mesh generation.

### 6.5 Order of work

1. Section 6.2 in four batches: object workflow (drop to floor, pick cycling, rotate nudge, clear rotation, bounding mesh, per-object snap, revert); textures (flip, rotate 90°, align to view, nudge); layers (hide, lock, current layer); viewport toggles and snapping (vertex snap, select through, helpers, tool materials, class visibility, merge to target).
2. Linked groups, then the UV editor, then interactive Bevel and Bridge, then entity parenting and the entity picker.
3. Prefab instances once linked groups exist (they share the propagation machinery).

### 6.6 Done from this review (2026-10-13)

- **Object workflow** (`CypherMapGui_ObjectTools`): Drop to Floor (Ctrl+Num2 or Ctrl+Shift+Down; each object lands on the surface below it, selected objects do not hold each other up); Alt+click picks the next object under the cursor in 2D and 3D, with Select Next/Previous Under Cursor commands; Rotate/Tilt by Angle Snap (Alt+arrows, about the Pivot when placed); Clear Rotation and Scale (Ctrl+Num0 or Ctrl+Shift+0); Create Bounding Box (Ctrl+Alt+C); Revert to Saved (File menu).
- **Entities turn with the selection**: rotating or quarter-turning a selection that holds point entities or prefab instances turned them before; it used to fail. Their origin follows and the rotation composes onto `angles` (removed again when it comes back to zero). Scaling moves them without resizing them. `MapWorkspace_CanTurnSelection` gates Rotate and Scale; Resize and Flip keep the stricter rule.
- **Materials from the keyboard** (Faces mode, Hammer keys): flip horizontally/vertically (Alt+D/E), quarter turns (Alt+A/Q), nudge by the grid (Ctrl+Alt+arrows).
- **Layers** (`CypherMapGui_Layers`, TrenchBroom): a current layer new objects, entities and pastes go to (`map_document_t::creationLayer`, not saved; undo keeps the choice); hide, lock (shown, not pickable), isolate, show all, select objects in a layer; the Outliner marks the current, hidden and locked layers, double-click makes a layer current, right-click has the layer actions.
- **Snap to Vertices** (Ctrl+Shift+X, `editor.map.snap_to_vertices`): moves lock a corner of the selection onto a corner of other geometry within the geometry snap distance, in 2D and 3D, with a ring where they meet. Target corners are indexed once per drag.
- **Show Helpers** toggle (View menu; `editor.viewport.helpers` never/selected/always), **Pick Entity** (an eyedropper on target fields: the next entity clicked in a view, or the entity owning the brush clicked), **entity class visibility** (a Classes section in Auto Vis Groups; a hidden class takes its entities' brushes with it).

Still open from 6.2: select through and back faces, the tool-materials toggle, merge to target vertex, shear, freehand lasso, typed values during a gizmo drag, flipping entities. From 6.3: linked groups or prefab instances (needs a decision, below), the UV editor, interactive Bevel/Bridge/Thicken, entity parenting.

**Decision needed for linked copies.** CYMAP already specifies prefab instances (6.11: a placed reference to a `.cyprefab` with origin, angles, scale and per-instance overrides; Core loads and keeps the records, and translating one moves its origin). Building Hammer's instance workflow on that needs no format change: draw each instance's prefab in the views, pick and transform instances, Create Instance from Selection, edit the prefab (open it, or in place) and have every instance follow, Collapse to copy the objects in. TrenchBroom's linked groups (copies inside the map, edits to one repeated in the others) would need new group members (a link set and a transform per copy) in CYMAP 6.10.

### 6.7 Prefab instances (2026-10-14, the user chose Hammer's instances over linked groups)

`CypherMapGui_Instances`: instance records (CYMAP.md 6.11) are drawn from their `.cyprefab` files as one PREFAB object each (a new `map_wire_kind_t`), picked in 2D and 3D, moved, turned (angles), copied, deleted, saved and loaded. Commands: Create Instance from Selection (writes the prefab into a content folder), Place Instance (at the map cursor), Collapse Instance (copies the prefab in, overrides applied by entity name), Edit Prefab (in place, as a group) with Finish (writes the file; every instance follows) and Cancel, Reload Prefabs. Compile collapses instances into a copy first. Check for Problems reports instances whose file is missing (they show a marker box). Object Properties shows the instance; the Outliner names it after its prefab and filters it apart. Every rebuild of the live wireframe now goes through `MapWorkspace_WireRebuilt` (instances, entity looks).

Not yet: editing an instance's overrides in Object Properties, a prefab list in the Asset Browser, nested instances inside prefab files.

### 6.8 UV Editor (2026-10-14)

`CypherMapGui_UvEditor` (Window and Texture menus, `view.uv_editor`): TrenchBroom's face inspector UV view and Hammer 5's Fast Texture UV widget in one panel. In Faces mode it draws the picked brush side or mesh faces in texture space over their tiled material (a checker when the image is missing), with the repeat lines and, zoomed in, the texel grid. Drag the faces to move the texture (whole texels), the ring to turn it (15°), a square to scale along U or V (sixteenths; Shift both), Alt and a square to shear, the cross to move the pivot (snaps to corners and texels); Ctrl drags freely, the wheel zooms, the middle or right button pans. The faces follow the drag as a preview; release is one undo step for every face shown, and the faces stay picked. The toolbar has quarter turns, flips, fit, align to face and frame.

Core: `MapTextureEdit_Describe` (a face's material, corners and UVs) and `MapTextureEdit_EditUv` (move, turn, scale, shear about a pivot in texture space). Brush sides hold every one of these exactly through `UvTools_TryTransformUv`: a 2x2 map factors as R(phi) * diag(sx, sy) * R(beta), and R(beta) is a turn of the side's u/v axes within the face plane, so they stay orthonormal. A scale across a turned texture or a shear turns the axes; a turn or a scale along the texture's own axes leaves them alone. Mesh faces use `MeshUv_TryTransform` and the new `MeshUv_TryShear`.

Not yet: picking and dragging single UV corners of mesh faces (stitch and weld), and a world-space view of the texture on the face (needs the textured GPU viewport).

### 6.9 Shear tool (2026-10-14)

`map.tool.shear` (G, TrenchBroom's key and Shear tool; palette and Tools menu beside Mirror): drag a side of the selection's bounds along itself and the opposite side stays while everything between leans. In 2D the four outline edges are the sides; in 3D the face the cursor enters, a wall side sliding across the floor (Alt: up and down), the top and bottom sliding anywhere across. Grid snap on the slide (Ctrl free). While dragging, the views show the sheared selection and bounds over the dotted originals and the slide amount; release is one undo step, Escape drops it. A click away from the bounds selects as usual.

Core: `MapEdit_Shear` (an AFFINE kind in the transform path, so meshes, patches and entity origins go the way they go for Rotate and Scale; terrain refuses). Brush textures stay put exactly with Texture Lock, through the new `BrushTransform_TextureLockAffine`: TrenchBroom manages this only for Valve 220 maps; here any affine transform keeps every texel on its surface point (see `CypherGeometry_UvProjection`, shared with the UV Editor).

### 6.10 Entity parenting (2026-10-14)

CYMAP 6.2 already had `parent` on entities ("attached to, moves with it"); now it is edited and honoured, as in Hammer 5's hierarchy. Core `CypherMap_Hierarchy` reads and sets it (no loops, entities only) and `CypherMap_Edit` expands a selection by it the way it does by geometry ownership: moving, turning, scaling, shearing, copying or deleting a parent does the same to everything hanging from it, at any depth; copies of a parent and its children are attached to each other, a child copied alone keeps its parent. Commands (`CypherMapGui_Hierarchy`, Hammer keys): Parent to Last Selected (P; the selection is sorted, so the pick order is tracked from selection changes), Unparent (Ctrl+P), Select Parent (Ctrl+PageUp), Select Children (Ctrl+PageDown, every depth); selected geometry stands for its owning entity. The views draw a dashed line from each attached entity to its parent when either is selected (with the helpers setting). Check for Problems reports an entity whose parent is gone and detaches it.

Not yet: nesting in the Outliner, dragging onto a parent in the Outliner, picking the parent in Object Properties.
