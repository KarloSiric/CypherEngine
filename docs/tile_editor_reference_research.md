# Tile editor configuration and navigation references

Research date: 2026-09-17. This is a scoped comparison for CypherTileEditor's
current Qt tile-authoring workflow. The observations below come from project
documentation or source. Recommendations are Cypher design judgments, not claims
that the referenced editors share one architecture or that these features have
already been implemented here.

## Verified reference features

| Reference | Configuration and navigation | Authoring ideas relevant to Cypher |
| --- | --- | --- |
| **NetRadiant** | Camera preferences expose FOV, movement/rotation speed, linked strafe speed, Y inversion, pointer-directed zoom, discrete movement, far clipping, and render mode. Preferences also register camera background/selection colors and statistics. | A useful precedent for explicit, individually tunable viewport controls. Source: [camera implementation](https://gitlab.com/xonotic/netradiant/-/blob/master/radiant/camwindow.cpp), especially `Camera_constructPreferences` and preference registration; [official FOV announcement](https://netradiant.gitlab.io/). |
| **Hammer, Source 2** | The Dota 2 tools documentation describes RMB + WASD navigation, toggleable fly mode, orbiting, selection-centered framing, 2D panning, and cycling orthographic views. Its red border identifies the active viewport; that is an interaction convention rather than a geometry requirement. | Translate/rotate/scale and pivot tools, grid/angle snapping, explicit selection modes, tool properties, selection sets, and an undo-history panel make editing discoverable. Sources: [navigation](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Navigation.htm), [Hammer overview](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Hammer_Overview.htm). |
| **J.A.C.K.** | Predefined and user-created color schemes cover most viewport colors and application background; custom presets can live in a configuration file. User cameras can be created, moved, removed, and saved. 2D reference images have configurable scale, offset, brightness, and filtering. | Visibility groups, incremental save, autosave, internal-link preservation during cloning, and configurable selection previews are practical reliability features. Source: [official feature list](https://jack.hlfx.ru/en/features.html). |
| **TrenchBroom** | Separate look, pan, move, and fly controls; FOV; adjustable layout and grid appearance; editable shortcuts. Orthographic panes share zoom and common-axis panning; zoom keeps the world point under the cursor stable. Camera framing can fit selection without rotating the camera. | Grid-aware movement, groups/layers, hiding/isolation/locking, and command repetition support everyday map editing. Sources: [camera navigation](https://trenchbroom.github.io/manual/latest/#camera-navigation), [preferences](https://trenchbroom.github.io/manual/latest/#preferences), [keeping an overview](https://trenchbroom.github.io/manual/latest/#keeping-an-overview). |
| **Q3Edit** | Global Preferences govern shortcuts, themes, layouts, and defaults. Separate Project Settings now travel in the map and participate in history/recovery. Release notes document isometric view presets and drafting-style blue fill/wire rendering. | Reusable selection sets, linked groups, exact primitives, repeat transforms, and visible selection dimensions. Its notes emphasize centralized edits, revisions, and undo transactions. Source: [official release notes](https://q3edit.com/release-notes.html), July–August 2026 entries. |

Valve's wiki denied direct fetching during this research. The Hammer links above
are the official Dota 2 regional site hosting translated Valve documentation.
They describe the documented Dota 2 Source 2 workflow, not a claim that every
current CS2 or Alyx binding is identical. NetRadiant observations refer to its
upstream repository, not the separate NetRadiant-custom fork. **Hammer 5 Tools**
is a separate community toolkit, not the name of Valve's Hammer editor version;
its [own site](https://hammer5tools.github.io/) makes that scope clear.

## Cypher baseline observed before this pass

The editor already has four views, selectable pane positions, dock toggles,
`editor.ini`, appearance presets, configurable shortcuts, adaptive display grids,
selection framing, depth-tinted orthographic wireframes, and live map properties.
The 3D viewport already supports fly, orbit, pan, wheel input, and spawn framing.
These are described in the [editor guide](../src/CypherTools/CypherTileEditor/README.md).
The immediate camera problem is discoverability and the completeness of exposed
controls, rather than the absence of a camera implementation.

Cells own generated boundary walls. Region transforms move cells and their
markers. A generated wall therefore cannot become an independently movable
object merely by adding another toolbar icon: that requires authored wall data
with persistent identity and an explicit relationship to the generated boundary.

## Configuration ownership

| State | Owner | Expected behavior |
| --- | --- | --- |
| Theme, font/icon sizes, highlight policy, input sensitivity, shortcut bindings | User preferences / exported editor profile | Apply to the current editor without modifying the map or its undo history. Validate the whole incoming profile before activation. |
| Dock geometry, active pane, pane order, camera inspection pose, temporary isolation | Workspace/session state | Preserve while authoring and restore locally. A color preset should not rearrange the workspace. |
| Map dimensions and units, cells, doors, spawn, material references, future wall objects | Authored `.cymap` data serialized through CYKV | Edit through document commands, validate, mark dirty, update all consumers, and support undo/redo. |
| Shared asset roots, future build presets and gameplay definitions | Project configuration | Keep portable project settings separate from personal appearance and machine-specific executable paths. Add this layer when the project workflow needs it. |

CYKV is a serialization layer. Live editor controls should modify typed,
validated document state; saving writes that state through CYKV. Directly
mutating arbitrary serialized keys would bypass invariants, history, IDs, and
renderer rebuild notifications. User configuration uses the same principle:
parse into a candidate, validate, then apply the complete candidate.

## Priorities for Cypher

**NOW — finish the configuration and navigation surface.**

- Make active-pane emphasis optional and configurable: border visibility,
  thickness, color, and header emphasis. Disabling the border must retain a
  recognizable active pane through its title or another restrained cue.
- Expose independent pan/zoom/look sensitivity, wheel inversion, movement speed,
  fast/slow modifiers, FOV, and fly/orbit choice. Put navigation controls where
  the 3D viewport user can find them. Preserve pose when applying settings.
- Support import/export of validated preference profiles and reset-to-default
  operations with clear scope. Palette changes should affect colors alone.
- Keep displayed grid density distinct from authored dimensions and snapping.
  Cosmetic zoom changes must never resample the map.

**NEXT — deepen the current 2D editing loop.**

- Optional linked orthographic panning/zoom, clear selection dimensions, and
  reusable inspection-camera bookmarks.
- Drag preview for move/copy, explicit axis constraints, configurable snapping,
  rotation/mirror preview, and one transaction per gesture. Escape restores the
  pre-drag state. Define collisions and out-of-bounds behavior before committing.
- Independent wall objects and a Detach Wall command. Persist IDs, dimensions,
  material, orientation, and suppression of the original generated edge. Cover
  move/copy/delete and save/load in the same milestone.
- A searchable shape library backed by parameterized primitives and assemblies.
  Store dimensions, orientation, tags, and thumbnail recipe with each preset;
  show favorites/recent presets instead of loading a huge icon grid eagerly.
- Selection filters and temporary hide/isolate/lock controls to keep dense
  floor plans workable. A locked object should be excluded from accidental edits.

**LATER — add geometry capabilities when the map model can support them.**

Clipping, arbitrary rotations, ramps, convex brushes, vertex/face deformation,
curved stairs, texture alignment, layers, and reusable linked assemblies need
representation and validation beyond the current cell grid. Build them one
vertical slice at a time: authored data, command, undo, persistence, validation,
renderer output, then UI. Deep tools are valuable when their output remains
stable after rebuilding, reopening, and running the map.

## Acceptance checks for this direction

Changing a theme must preserve the map, camera, selection, and dock layout.
Malformed profile import must preserve the working preferences. Disabling pane
borders must not disable pane activation. Camera capture must release on Escape,
focus loss, and pane closure. One editing gesture must produce one undo step;
cancel must produce none. A future detached wall must survive save/load and
renderer regeneration as the same independently selectable object.
