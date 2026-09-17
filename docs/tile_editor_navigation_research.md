# CypherTileEditor viewport navigation research

Research date: 2026-09-17.

This note compares navigation and closely related tool workflows that are useful
for CypherTileEditor. It does not propose copying another editor wholesale.
Cypher is a tile and region authoring tool with a Z-up world, so the useful goal
is a small, internally consistent input language that remains available while an
authoring tool is active.

## Source quality and limits

| Reference | Evidence used | Currency limit |
| --- | --- | --- |
| TrenchBroom | The official [2026.2 reference manual](https://trenchbroom.github.io/manual/latest/#camera-navigation), its [mouse and keyboard preferences](https://trenchbroom.github.io/manual/latest/#preferences), and the current upstream [3D camera tool](https://github.com/TrenchBroom/TrenchBroom/blob/master/lib/TbAppLib/src/CameraTool3D.cpp) and [camera preferences](https://github.com/TrenchBroom/TrenchBroom/blob/master/lib/TbPreferencesLib/include/prefs/Preferences.h). | Current release documentation and current upstream source at the research date. |
| GtkRadiant | The official project's [documentation index](https://icculus.org/projects/gtkradiant/documentation.html), [moving-around guide](https://pres.icculus.org/gtkradiant/documentation/q3radiant_manual/ch03/pg3_1.htm), [preferences](https://pres.icculus.org/gtkradiant/documentation/q3radiant_manual/ch01/pg1_2.htm), and [view commands](https://pres.icculus.org/gtkradiant/documentation/q3radiant_manual/ch10/pg10_1.htm). | The GtkRadiant site explicitly labels this manual old and dated. It is evidence of the Radiant interaction lineage, not a claim about a modern default. |
| NetRadiant | The current official upstream [repository](https://gitlab.com/xonotic/netradiant/-/tree/master) and [camera implementation](https://gitlab.com/xonotic/netradiant/-/blob/master/radiant/camwindow.cpp). | Current source is authoritative for behavior but is not a user-oriented manual. |
| Valve Hammer, Source 2 | The public Source 2 [navigation](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Navigation) and [Hammer overview](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Hammer_Overview), cross-checked against the official Dota 2 regional mirrors for [navigation](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Navigation.htm) and [tools](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Hammer_Overview.htm). Valve's current [CS2 Workshop FAQ](https://www.counter-strike.net/workshop/workshopfaq) confirms that CS2 Authoring Tools ship an updated Hammer. | The public control guide originated with the Dota 2 Source 2 tools and is historical. It is useful design evidence; it does not prove that every current CS2 build retains every binding. |
| Blender, secondary cross-check | The official manual's [viewport navigation](https://docs.blender.org/manual/en/5.0/editors/3dview/navigate/navigation.html), [walk/fly navigation](https://docs.blender.org/manual/en/5.0/editors/3dview/navigate/walk_fly.html), and [navigation preferences](https://docs.blender.org/manual/en/5.0/editors/preferences/navigation.html). | Used only where its general-purpose 3D viewport behavior directly clarifies a choice. It is not a target interaction preset for Cypher. |

## Capability matrix

| Capability | TrenchBroom 2026.2 | GtkRadiant / NetRadiant | Hammer, public Source 2 guide | CypherTileEditor after the current camera pass |
| --- | --- | --- | --- | --- |
| Look and fly activation | Hold RMB and drag to look. Fly movement actions default to `W/S/A/D`, with `Q/X` for up/down, and are rebindable. | GtkRadiant and current NetRadiant use an RMB click to enter persistent freelook and another RMB click to leave it. Current NetRadiant binds `W/S/A/D` plus arrow alternatives while freelooking. | Hold RMB to look and use `W/A/S/D`; `Z` toggles a persistent fly mode that does not require RMB. | Hold RMB, capture and hide the pointer, use `W/A/S/D` and `Q/E`, release RMB to stop. Escape, focus loss, hide, window deactivation, capture loss, and document replacement stop navigation. |
| World-up rule | No camera roll; positive Z remains up. | Pitch/yaw camera with an explicit level-view command. | FPS-like navigation plus authored axis views. | Z-up yaw/pitch camera with no roll and clamped pitch. `End` levels the camera in the current pass. |
| Orbit | `Alt+RMB` orbits around the closest geometry hit under the initial pointer; empty space falls back to a camera default point. Wheel changes radius during the gesture. | The core Radiant workflow emphasizes freelook rather than a persistent orbit camera. | `Alt+LMB` orbits about the screen center; `Shift+A` chooses the selected geometry's center as the pivot. | `Alt+RMB` temporarily orbits from Fly, or RMB orbits in Orbit mode. The pivot is currently implicit: `eye + forward * orbitDistance`; it is not chosen from the clicked surface. |
| Pan | MMB drag pans in camera right/up space. Optional `Alt+MMB` changes the vertical drag into forward/back movement for tablet use. | Current NetRadiant supports configurable Ctrl-modified mouse strafe modes, including up and forward variants. | `Alt+MMB` performs camera-view motion. | MMB drag pans. The current pass removed `Shift+RMB`, so Shift has the same fast-fly meaning regardless of press order. |
| Wheel in a 3D view | Bare wheel dollies along camera forward, optionally toward the pointer ray. `Shift+wheel` changes temporary optical zoom. While RMB look is active, wheel changes fly speed. | Wheel moves along camera forward; outside freelook it can optionally move toward the pointer. FOV and movement speed also have explicit commands. | `Alt+RMB` is documented as zoom in orbit navigation. | Bare wheel changes persistent fly speed in Fly mode and orbit distance in Orbit mode. Both notched `angleDelta` and smooth `pixelDelta` trackpad input are accepted; cursor-directed dolly and pinch remain future work. |
| Frame and focus | Focus Selection fits selected objects without rotating the camera and also brings the selection into the 2D views. Move Camera To accepts an exact position. | Current NetRadiant has Focus on Selected; the Radiant lineage also has Center/Level, floor up/down, and a 2D command that moves the 3D camera to a clicked location. | The public guide exposes pivot-to-selection and direct 2D view selection; older Hammer references also expose selection-centered viewing. | `F` frames selected generated geometry and falls back to the map; map framing preserves orientation. Commands also frame all views and move the camera to the player spawn. |
| Deterministic views | One to four pane layouts; 2D panes cover the axis views. | Quad layout and cycleable XY/XZ/YZ views; level camera and floor stepping. | Top/front/side can be cycled with `Ctrl+Space` or chosen with `F2/F3/F4`. | Perspective plus Top/Bottom/Front/Back/Left/Right inspection presets, camera level, and map-level up/down are in the current pass. These are axis-aligned perspective views, not orthographic projection in the 3D renderer. |
| 2D navigation | MMB or RMB drag pans; wheel zoom is anchored beneath the pointer. Multiple 2D panes can share scale and common-axis pan, controlled by a preference. | RMB drag pans; wheel or keys zoom. The legacy quad views may use different scales. `Ctrl+MMB` moves the 3D camera to a 2D location. | RMB pans; MMB zooms; `Ctrl+Space` cycles top/front/side. | MMB, RMB, Space+LMB, or Pan-tool LMB pans. Wheel zoom stays anchored under the pointer. Each pane currently owns independent pan and scale. |
| Settings and rebinding | Independent look, pan, move, and fly speeds; horizontal/vertical inversion; wheel inversion; optional cursor-directed dolly; FOV; linked 2D cameras; rebindable fly actions. | NetRadiant exposes FOV, move/rotation speed, linked strafe speed, inverse Y, pointer zoom, discrete movement, strafe mode, far clipping, render mode, colors, and statistics. | Public docs expose maximum forward speed, backplane distance, and a searchable key-binding command list. | Move/look/pan/wheel sensitivities, fast/slow multipliers, FOV, Y/wheel inversion, default mode, hints, and many commands are configurable. The continuous `W/A/S/D/Q/E` movement keys and mouse buttons remain hard-coded. |
| Tool interaction | Camera and selection are permanently available; shape tools are modal. Navigation does not require abandoning the current authoring tool. | Camera navigation coexists with component modes and 2D manipulation. | Tools are globally switchable. Tool Properties changes with the active tool; translate, rotate, scale, pivot, clipping, selection modes, Outliner, selection sets, undo history, and command history are explicit. | RMB/MMB camera gestures remain available while a tile authoring tool is active; LMB retains select/author semantics. Camera movement is view state and does not enter the map undo history. |
| Active viewport | Shortcut context follows the active view. | Camera and orthographic windows have distinct input contexts. | A red border identifies the active pane; a mouse press, not hover alone, changes it. | Click activation is the default. Hover activation, header emphasis, and the active border are separate options. |
| Saved inspection state | No camera bookmark workflow is emphasized in the current manual. | Radiant keeps camera preferences and commands but is not the strongest bookmark reference. | Selection sets and workspace panels help return to authored areas; the public guide does not document a general camera-bookmark system. | Four document-scoped, in-memory pose bookmarks are in the current pass. They have no names or persistence and deliberately restore pose without replacing settings, scene bounds, or the user's Fly/Orbit preference. |

Blender supports the same broad separation: ordinary MMB orbit, Shift+MMB pan,
wheel zoom, Frame Selected/All, and an explicitly invoked first-person mode with
WASD, vertical movement, temporary fast/slow modifiers, per-session wheel speed,
and cancel/confirm behavior. Its useful lesson for Cypher is separation of
ordinary inspection, temporary fly navigation, and preferences rather than its
specific MMB-centric keymap.

## What Cypher already gets right

The camera is not a missing placeholder. The [camera core](../src/CypherTools/CypherTileEditor/Core/CypherTileCamera.h)
already keeps Qt and platform input outside the math layer, and the
[viewport adapter](../src/CypherTools/CypherTileEditor/Gui/CypherTileRenderViewport.cpp)
already converts gestures into that camera state. The renderer receives the
resulting view/projection matrices. Picking is built from the same camera, so a
rendered tile and a clicked tile share a coordinate contract.

The current implementation also has several details that are easy to miss:

- continuous movement is time based, diagonal input is normalized, and long
  frame stalls are capped;
- switching Fly/Orbit does not jump the eye pose;
- panning is screen-scale aware, framing handles the complete geometry bounds,
  and map edits update clipping bounds without recentering an inspected view;
- pointer capture has explicit release paths rather than relying only on the
  initiating mouse-button release;
- camera settings are validated and participate in editor profile import/export;
- the 3D header exposes mode, framing, speed, help, and settings close to the
  viewport;
- the current extension adds six axis presets, level stepping, auto orbit, and
  four temporary bookmarks with tests for pose and preference separation;
- direction presets and auto orbit temporarily use Orbit without overwriting
  the user's configured Fly/Orbit navigation preference;
- the same pass removes the Shift+RMB ambiguity and hidden hard-coded `F`,
  leaves Tab/Space commands unbound by default, and accepts smooth trackpad
  scroll deltas.

That is a solid base. It should be refined rather than replaced with a large
generic editor-camera framework.

## Recommended Cypher interaction contract

The following contract takes the strongest common conventions while preserving
Cypher's existing LMB tile tools.

| Input | Recommended behavior | Reason |
| --- | --- | --- |
| LMB | Select or apply the active authoring tool. Modified LMB keeps the existing multi-selection semantics. | Authoring remains primary and Hammer's `Alt+LMB` orbit would collide with Cypher's Alt-subtract selection. |
| Hold RMB | Fly look. Capture the pointer; `W/A/S/D` move, `Q/E` move vertically, Shift is fast, and Ctrl is precision. Release, Escape, capture loss, deactivation, or focus loss ends the session. | Matches Cypher, TrenchBroom, and Hammer while avoiding Radiant's easy-to-forget persistent capture. |
| `Alt/Option+RMB` drag | Temporarily orbit. Choose the nearest geometry hit at press time; if there is no hit, use the selection center, then the existing pivot as fallback. Preserve that pivot throughout the drag. | Gives orbit a visible reason and matches TrenchBroom's surface-focused inspection without stealing LMB. |
| MMB drag | Pan in camera right/up space in both Fly and Orbit. Translate eye and pivot together. | One unambiguous pan gesture works regardless of mode or the active tool. |
| Bare wheel | In Orbit, change orbit radius. In Fly, default to dolly along camera forward or the pointer ray. Offer `Dolly` and `Adjust speed` as a user preference if compatibility with the present behavior matters. | Bare-wheel dolly is the common inspection behavior. Changing a saved speed merely by scrolling is surprising. |
| RMB + wheel | Adjust fly speed and show the resulting value briefly in the viewport HUD. | This is TrenchBroom's discoverable, contextual speed adjustment and leaves bare wheel useful. |
| Trackpad scroll/pinch | Consume `pixelDelta` as a continuous dolly/radius change; support native pinch when Qt reports it. Do not multiply line and pixel deltas from the same event. | The 2D views already accept smooth deltas; the 3D view should not feel broken on a laptop. |
| Frame action | `F` by default: frame selection; if empty, frame the map. The configured action is the only authority for the key. | Existing behavior is good, but a hard-coded fallback must not survive after the user rebinds the action. |
| Axis/level/bookmark commands | Keep menu entries and configurable commands. Leave uncommon commands unbound by default. | The capabilities remain discoverable without exhausting single-key shortcuts. |

Two invariants make every operation predictable:

```text
pivot = eye + forward * orbitDistance

orbit: rotate eye around fixed pivot
pan:   translate eye and pivot by the same vector
dolly: move eye toward/away from pivot and clamp distance
fly:   move eye; derive a sensible focus depth when orbit begins
```

`camera preferences`, `camera pose`, and `active navigation session` are three
different lifetimes even if the implementation keeps them in nearby structs:

```text
user/profile state      sensitivity, inversion, speed, bindings, default mode
workspace/document view eye, orientation, projection, pivot, bookmark poses
ephemeral gesture       initiating button, picked pivot, pressed actions,
                        pointer capture, pre-gesture mode
```

None of these operations should dirty the map or enter authored undo/redo.
Camera-history commands, if added, should use a separate view-history stack.

## Input conflicts to resolve before adding more bindings

| Current or borrowed binding | Conflict | Recommendation |
| --- | --- | --- |
| `Shift+RMB` pan | Shift held before RMB chooses Pan, while Shift pressed after RMB accelerates Fly. The result depends on press order. | Remove `Shift+RMB` pan. MMB already owns pan; Shift can then always mean fast movement during Fly. |
| `Tab` toggle Fly/Orbit | Tab is keyboard focus traversal in Qt, Focus on Selected in NetRadiant, and an editing-mode key in Blender. Capturing it in a viewport harms accessibility and transfer learning. | Leave Toggle Fly/Orbit unbound by default and keep it in the Camera menu. Users may assign it. |
| `Space` auto orbit | Space+LMB already pans 2D views; Space is also commonly temporary pan or tool confirmation. The same key changes meaning sharply between neighboring panes. | Leave Auto Orbit unbound by default. It is a presentation command, not a core navigation action. |
| Bare wheel changes Fly speed | TrenchBroom, Blender, and many general 3D views use bare wheel for spatial movement; the current gesture also persists a preference. | Default to Dolly, with `RMB+wheel` for speed. Preserve a configurable Speed option if desired. |
| Hard-coded `F` in the viewport | Rebinding `Frame Active View` can leave the old F behavior active as a hidden second binding. | Route framing through the action map only, or use a fallback only when no action is attached. |
| `Q/E`, `A/D`, and tool letters | The same letters are valid authoring shortcuts when the camera is idle. | Keep movement actions active only inside captured Fly input. Consume their shortcut overrides for the entire session, including auto-repeat and release. |
| Hammer's `Alt+LMB` orbit | Alt+LMB already means subtract selection in Cypher. | Keep `Alt+RMB`; do not add the Hammer binding as a second default. |
| Hammer's MMB zoom | MMB is already Pan across Cypher 2D/3D, TrenchBroom, and Blender. | Keep MMB Pan and use wheel/pinch for dolly. |

The current camera pass resolves the Shift+RMB, Tab, Space, hard-coded `F`, and
`pixelDelta` parts of this table. Orbit-pivot picking, coherent Fly-mode dolly,
pinch gestures, and named continuous-movement actions remain open.

## Priority for CypherTileEditor

### Must have now: make the existing camera dependable

1. **Pick a real orbit pivot.** On orbit begin, raycast the same generated boxes
   used for rendering/picking. Use the hit point, selection center, or existing
   pivot in that order. The current implicit forward point makes small objects
   slide unpredictably during inspection.
2. **Finish the wheel and trackpad contract.** Line and pixel deltas are now
   normalized. Add pinch where available, separate spatial dolly from speed
   adjustment, and make inversion apply consistently.
3. **Keep the resolved input contract stable.** Shift+RMB pan is gone, Tab and
   Space commands are optional bindings, and framing is routed only through its
   configurable action. Preserve these guarantees while adding new inputs.
4. **Route continuous movement through named actions.** `Move Forward/Back/Left/
   Right/Up/Down`, Fast, and Precision need configurable bindings just like the
   discrete camera commands. Keep authoring and camera contexts separate so `E`
   can remain Erase while idle and Up while captured.
5. **Verify capture on the supported platforms.** Exercise Windows, macOS, and
   Linux for pointer restoration, Ctrl-click behavior, focus changes, multiple
   monitors, high-DPI scaling, and an RMB release delivered outside the widget.
6. **Preserve the core/adapter/renderer boundary.** Camera math owns pose and
   matrices; Qt owns events and capture; CypherRender consumes view constants.
   Renderer hookup should not make the core camera depend on `QMouseEvent` or a
   renderer backend.

Axis presets, camera leveling, map-level stepping, framing, the Camera menu,
and temporary bookmarks are useful parts of this first tier now that their
behavior is tested. Do not add more default keys merely because the commands
exist.

### Next: reduce navigation repetition during real map work

- Add an optional **Link orthographic cameras** preference. Share zoom and only
  the axes common to the affected panes, as TrenchBroom does; keep independent
  views available for users who prefer the Radiant-style layout.
- Add **Focus under pointer** and **Move camera to coordinates**. Both are small,
  concrete recovery tools for a lost camera and make coordinate-based debugging
  much faster.
- Add a short **Back/Forward view history** for frame, preset, spawn, bookmark,
  and large navigation jumps. Keep it separate from map undo.
- Promote the four temporary slots to **named, persisted workspace bookmarks**
  only after session/workspace serialization has a clear owner. Bookmark data
  should remain local view state unless the project explicitly needs shared
  review cameras.
- Add a compact **orientation compass/view gizmo** when direct manipulation of
  it has a defined contract. It should call the same preset actions rather than
  creating another camera path.
- Offer a small number of navigation profiles only after all mouse and keyboard
  actions are remappable. Profiles should populate bindings, not fork camera
  behavior.

### Later: add only when a real workflow pays for it

- Walk mode with collision, gravity, eye height, step handling, and teleport is
  valuable for level-scale review, but it depends on trustworthy collision data
  and is not required for tile authoring.
- Acceleration, damping, and cinematic smoothing need explicit preferences and
  deterministic tests. Immediate motion should remain available for precise
  editing.
- Named camera paths, recording, thumbnails, and review shots belong with a
  cinematic/review workflow, not the first editor camera.
- Gamepad and 6-DOF devices need an input-action layer with dead zones and curves;
  they should not be special cases in the viewport widget.
- Orthographic projection in the rendered 3D pane and additional simultaneous
  rendered viewports should wait until CypherRender's hosted-surface and view
  submission paths support them cleanly.
- User-facing near/far clip controls should follow measured large-map problems.
  The current scene-bounds-derived clipping is a better default than exposing a
  performance knob prematurely.

Hammer's transform gizmos, pivot tools, mesh/component selection, clipping, and
selection sets are good long-term workflow references. They should arrive only
when Cypher's authored data can preserve their results through undo, save/load,
validation, rebuild, and runtime export. A toolbar icon cannot make a generated
tile boundary into a durable, independently editable wall.

## Acceptance checks

- Starting `Alt+RMB` over a tile keeps the picked world point visually fixed
  during orbit; empty-space orbit has a deterministic fallback.
- Holding Shift before or after RMB produces the same fast-fly behavior.
- One mouse-wheel notch, a smooth trackpad scroll, and a pinch produce bounded,
  monotonic movement with no double application.
- Rebinding Frame away from `F` makes `F` inert in the 3D view unless another
  command owns it.
- Focus loss, window deactivation, viewport hide, capture loss, and document
  replacement clear every movement action and restore the pointer exactly once.
- Switching Fly/Orbit or applying a preset does not jump the current focus point.
- Top and Bottom presets produce valid matrices at vertical pitch and retain a
  deterministic screen-up direction.
- Frame Selection preserves orientation and places every selected bound inside a
  padded frustum; empty selection frames the map.
- Bookmark recall changes pose only. It preserves current sensitivity, speed,
  mode preference, scene bounds, selection, and map state.
- Camera and 2D navigation never mark the map dirty or add authored undo steps.
- A malformed preference/profile import leaves the working navigation settings
  and current pose untouched.
