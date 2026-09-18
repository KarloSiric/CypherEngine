# CypherTileEditor viewport navigation research

Research date: 2026-09-17.

This document defines the viewport input contract for CypherTileEditor. It uses
other editors as evidence, but the result is an original Cypher design for a
focused tile and region authoring tool. It is not a plan to reproduce a full
brush editor or the future Mason editor.

## Reproducible sources

The TrenchBroom behavior in this document is anchored to the stable `v2026.2`
tag, commit [`4c756be1a54a28d921d18c77716a5f170bdc31d0`](https://github.com/TrenchBroom/TrenchBroom/tree/4c756be1a54a28d921d18c77716a5f170bdc31d0),
and its official [2026.2 manual](https://trenchbroom.github.io/manual/latest/).
The following source files were inspected to distinguish documented behavior
from assumptions:

- [`MapViewActivationTracker.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MapViewActivationTracker.cpp)
  for viewport-group activation and pointer-enter focus;
- [`CameraTool3D.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/CameraTool3D.cpp)
  and [`FlyModeHelper.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/FlyModeHelper.cpp)
  for 3D gestures, held movement, and focus cleanup;
- [`CameraTool2D.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/CameraTool2D.cpp)
  and [`CameraLinkHelper.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/CameraLinkHelper.cpp)
  for pointer-anchored zoom and linked orthographic cameras;
- [`MultiPaneMapView.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MultiPaneMapView.cpp),
  [`FourPaneMapView.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/FourPaneMapView.cpp),
  and [`Splitter.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/Splitter.cpp)
  for pane focus, maximize/restore, and splitter behavior;
- [`SelectionTool.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/SelectionTool.cpp)
  and [`DrawShapeToolController2D.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/DrawShapeToolController2D.cpp)
  for permanent selection/navigation tools and modal drawing tools.

The comparison also uses these pinned or official primary sources:

| Editor | Primary evidence | Limit of the evidence |
| --- | --- | --- |
| NetRadiant | Upstream commit [`b4b295d7`](https://gitlab.com/xonotic/netradiant/-/tree/b4b295d7a37797cc2752e48aa8ce42492e7016f0), especially [`camwindow.cpp`](https://gitlab.com/xonotic/netradiant/-/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/camwindow.cpp), [`xywindow.cpp`](https://gitlab.com/xonotic/netradiant/-/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/xywindow.cpp), and [`mainframe.cpp`](https://gitlab.com/xonotic/netradiant/-/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/mainframe.cpp). | Source is authoritative for that revision but is not a tutorial. NetRadiant-custom is a separate project. |
| Q3Edit | Upstream commit [`02f87647`](https://github.com/drdator/q3edit/tree/02f87647162e5bf5e39fe61968f904efe8e19675), especially [`viewport2d-interaction.ts`](https://github.com/drdator/q3edit/blob/02f87647162e5bf5e39fe61968f904efe8e19675/src/viewport2d-interaction.ts), [`viewport3d.ts`](https://github.com/drdator/q3edit/blob/02f87647162e5bf5e39fe61968f904efe8e19675/src/viewport3d.ts), and [`ui-keyboard.ts`](https://github.com/drdator/q3edit/blob/02f87647162e5bf5e39fe61968f904efe8e19675/src/ui-keyboard.ts). | This is a current web editor, not a universal Quake-editor standard. |
| J.A.C.K. | Official [reference manual](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/496450/manuals/VDKManual.pdf?t=1736824236) and [feature list](https://jack.hlfx.ru/en/features.html). | The manual describes J.A.C.K.'s workflow; individual defaults may vary by configuration. |
| Valve Hammer | Valve's archived [3D and 2D views](https://valvearchive.com/Software/Hammer%20Editor/Documents/Half-Life%20Hammer%203.4%20Guide%20%28April%202002%29/html/the_3d_and_2d_views.htm) and [View menu](https://valvearchive.com/hammer/guides/wc_3.4/The_View_Menu.htm), plus the public Source 2 [navigation](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Navigation.htm) and [overview](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Hammer_Overview.htm). | The Source 2 pages document the Dota 2 tools era. They do not prove every current CS2 binding. |

## What the references actually establish

There is no single universal map-editor keymap. The common pattern is a stable
division of responsibilities:

1. pointer gestures operate on the viewport beneath the pointer;
2. a gesture remains owned by the pane where it began until release or cancel;
3. navigation and selection remain available while a modal authoring tool is
   selected;
4. view state is separate from map state and does not enter map undo history;
5. visible grid density is separate from the edit snap increment;
6. every long gesture has explicit commit, cancel, and focus-loss behavior.

TrenchBroom adds useful detail. Once its map-view group is active, entering a
pane gives it focus. When the group is inactive, the first left click is
consumed while activating it, which prevents an accidental edit; other mouse
buttons may activate the view without being discarded. This is more careful
than simply assigning all keyboard focus on every mouse move.

NetRadiant and Q3Edit do not switch every editing context merely because the
pointer crossed a pane. NetRadiant changes an orthographic pane through focus,
mouse press, or wheel input. Q3Edit chooses the transform/nudge axes on pointer
press. J.A.C.K. describes commands relative to the current active 2D view. These
differences are the reason Cypher needs an explicit input-routing rule rather
than a loose collection of event filters.

## Cypher viewport input ownership

CypherTileEditor uses a deliberate hybrid of hover routing and active-pane
fallback:

1. The workspace tracks a **hovered pane**, a **last active pane**, and an
   optional **gesture owner**.
2. Wheel, mouse-button, and drag input always goes to the pane under the pointer.
   A click solely to activate the pane is never required.
3. View-relative shortcuts such as frame, nudge, grid, maximize, and view mode
   use the hovered pane while the pointer is in a viewport. Outside the viewport
   area they use the last active pane.
4. Text input remains protected while the pointer stays in the console,
   property field, material search, combo box, or other editor control. Moving
   the pointer into a viewport deliberately transfers focus to that viewport,
   matching Cypher's no-preparatory-click requirement; viewport shortcuts never
   run while the input control still owns focus.
5. After a mouse gesture starts, the originating pane owns all move, wheel,
   modifier, and release input until commit or cancel, even when the pointer
   leaves its bounds.
6. Popup menus, modal dialogs, drag-and-drop, and active text composition suspend
   hover routing.
7. The active-input cue is a configurable thin header or border accent. Pointer
   hover does not tint the viewport contents.

The existing editor defaults to pointer-hover activation. The rules above state
the intended behavior more precisely: focus follows the pointer only after it
crosses into viewport chrome or content, and stays with text controls while the
pointer remains in their area.

## Authoritative Cypher control contract

This table is the default contract. The command registry may rebind actions,
but a gesture must have one meaning in every pane of the same type.

| Input | Orthographic pane | 3D pane |
| --- | --- | --- |
| LMB | Select or apply the current authoring tool. Drag empty space for marquee selection where the current tool permits it. | Select visible generated geometry or apply the current 3D-capable tool. |
| Ctrl/Cmd+LMB | Toggle selection membership. | Toggle selection membership. |
| Shift+LMB | Add to selection. Shift-drag of a movable selection may become duplicate-drag when that transaction is implemented. | Add to selection. |
| MMB drag | Pan. | Pan in camera right/up space; move eye and orbit pivot together. |
| Space+LMB drag | Temporary pan. Plain Space belongs to this chord; `Shift+Space` remains available for Maximize/Restore Viewport. | No default camera action. |
| RMB press and drag | Pan. | Capture pointer and look in Fly mode. In persistent Orbit mode, orbit around the current pivot. |
| RMB click without drag | Pane context menu. Use Qt's platform drag threshold to distinguish it from pan/look. | Pane context menu. A short press/release below the platform drag threshold must not rotate the camera. |
| Alt/Option+RMB drag | Optional alternate zoom only if assigned by a profile; no default action is required. | Temporarily orbit around the nearest geometry hit at press time. If there is no hit, use selection center, then the existing pivot. |
| Wheel | Zoom toward the pointer. Keep the world point under the pointer stable. Accept either `angleDelta` or `pixelDelta`, once per event. | Fly: dolly through space. Orbit: change distance to the fixed pivot. |
| RMB+wheel | No default action. | Adjust persistent fly speed and briefly show the resulting speed. |
| WASD while captured | No default action. | Move forward/back and strafe on the camera plane. |
| Q/E while captured | No default action. | Descend/ascend. These keys remain available to tools when the camera is not captured. |
| Shift while captured | No separate pan chord. | Fast movement, independent of whether Shift or RMB was pressed first. |
| Ctrl while captured | No camera action. | Precision movement. It must not trigger selection toggles during capture. |
| `F` command | Frame selected projected geometry; frame the document if the selection is empty. | Frame selected generated geometry; frame the map if the selection is empty. |
| `[` / `]` | Decrease/increase the authored edit grid, independent of display-grid adaptation. | Same document command when relevant. |
| Arrow keys | Nudge in the visible plane by the edit increment. | No default movement unless a navigation profile explicitly assigns it. |
| Page Up/Down | Nudge on the pane's hidden axis or change floor/elevation according to the selected element type. | Step map level when the command is invoked outside capture. |
| Escape | Cancel the active gesture or modal preview; otherwise clear selection. | Release pointer capture first, then cancel a modal preview, then clear selection on a later press. |

`Ctrl/Cmd+D`, Delete/Backspace, copy, paste, undo, and redo are document
commands. Their result must not depend on which pane has ordinary Qt focus once
the selection is known.

## Camera behavior and invariants

The most useful TrenchBroom conventions are specific rather than cosmetic:

- RMB look, MMB pan, and Alt+RMB orbit are three distinct gestures.
- Alt+RMB chooses the orbit point from the clicked surface rather than using an
  arbitrary fixed distance in front of the camera.
- Bare wheel moves the camera through space. While RMB look is active,
  RMB+wheel changes fly speed.
- A held movement session is cleared when focus or capture is lost.

Cypher should preserve these camera invariants:

```text
orbit: rotate eye and orientation around one fixed, visible pivot
pan:   translate eye and pivot by the same world-space vector
dolly: Fly moves along camera forward; Orbit changes radius without crossing pivot
fly:   move eye independently; derive a new focus depth only when orbit begins
frame: preserve orientation and solve distance from the selected bounds
```

Camera preferences, camera pose, and the active input session have different
lifetimes:

```text
user/profile state      sensitivity, inversion, speed, bindings, default mode
workspace view state    eye, orientation, projection, pivot, linked-view state
ephemeral gesture       owner pane, button, picked pivot, held actions, capture
document map state      cells, markers, dimensions, materials, stable IDs
```

Navigation changes only workspace view state. It must not dirty `.cymap`, start
a map undo transaction, or rebuild geometry. Framing after a document edit may
read new bounds without taking ownership of them.

## Orthographic navigation and linked cameras

TrenchBroom's linked 2D cameras share zoom and only the axes common to the
participating views. Cypher exposes that model as an explicit optional
preference. Independent cameras are the default because local inspection in one
pane must not disturb the composition of the other panes:

- Top and Front share X pan.
- Top and Side share Y pan.
- Front and Side share Z/elevation pan.
- All linked orthographic panes share a compatible zoom scale.
- Programmatic propagation has a recursion guard and never creates extra undo
  history.
- Duplicate pane roles are legal and participate by their represented axes.
- Independent cameras remain available for detailed work.

Adaptive display-grid spacing may change as the user zooms. The authored edit
grid and snap increment remain unchanged and visible in the toolbar/status bar.

## Tool interaction and cancellation

Selection and navigation are permanent tools. Paint, rectangle, line, door,
spawn, stair/ramp, and later tile-shape operations are modal authoring tools.
Choosing a modal tool does not disable pan, zoom, frame, or camera look.

A document-changing gesture follows this lifecycle:

```text
press       capture owner pane + selection snapshot + command parameters
drag        update a non-destructive preview in every relevant pane
release     validate and commit exactly one undoable document command
Escape      discard preview and restore the exact pre-gesture document state
focus loss  cancel safely unless the operation already committed
```

Viewport navigation can update continuously without a document transaction.
Document replacement, pane closure, application deactivation, and capture loss
must clear held camera keys and release the pointer exactly once.

## Current strengths and remaining gaps

Cypher already has a Qt-independent camera core, renderer-facing view/projection
matrices, geometry picking from the same camera, time-based normalized fly
movement, Fly/Orbit switching without an eye-position jump, picked-surface
Alt+RMB orbit, bare-wheel Fly dolly, contextual RMB+wheel speed adjustment, 2D
pointer-anchored angle/pixel wheel zoom, selection framing in every projection,
platform-threshold context clicks, a persistent inspected-point orbit pivot,
linked orthographic zoom/shared axes with guarded propagation, pane role
reassignment, maximize/restore, and pointer-capture cleanup. Top, Front, and
Side can author the current tile operations, and sparse selection is shared
across views.

The next navigation work should stay narrow:

1. Route continuous movement through named, rebindable camera actions.
2. Add a short view-history stack for large frame/preset/bookmark jumps.
3. Verify capture and pointer restoration on macOS, Windows, Linux, high-DPI,
   multiple-monitor, and trackpad input.

Walk navigation with collision, gravity, eye height, and step handling can wait
until the runtime collision representation is trustworthy. It is a map-review
mode, not a prerequisite for tile authoring.

## Licensing boundary

TrenchBroom is distributed under the
[`GNU GPL version 3`](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt).
Its repository also credits third-party game icons and other assets whose terms
must be checked separately. Cypher may study documented workflows and implement
the same general interaction ideas independently. Cypher must not copy or adapt
TrenchBroom source code, resource files, icons, artwork, or derived snippets into
this proprietary repository without an explicit licensing decision and a
complete asset-by-asset provenance review.

The same policy applies to every reference editor: links and behavioral notes
are design evidence, not permission to import its code or visual assets.

## Acceptance checks

- A text field retains input while the pointer stays over editor controls.
  Moving the pointer into a viewport deliberately transfers command focus to
  that pane without a preparatory click.
- A drag remains owned by its origin pane after the pointer crosses a splitter.
- A stationary RMB click opens the correct pane menu; a drag beyond the platform
  threshold performs pan, look, or orbit exactly once.
- Plain Space can start temporary 2D pan; `Shift+Space` can still invoke
  Maximize/Restore Viewport.
- One notched wheel event and one pixel-only trackpad event each apply one
  bounded, pointer-anchored 2D zoom step.
- Starting Alt+RMB over geometry keeps the picked world point visually fixed
  through orbit. Empty space uses the documented deterministic fallback.
- Bare 3D wheel changes position without changing the saved fly speed;
  RMB+wheel changes speed without moving the eye.
- Rebinding Frame away from `F` leaves no hidden hard-coded `F` behavior.
- Frame Selection uses projected selected bounds in Top, Front, and Side and
  generated 3D bounds in Perspective; empty selection frames the map.
- Focus loss, pane hide/close, window deactivation, capture loss, and document
  replacement clear all held movement and restore the pointer exactly once.
- Navigation never marks the map dirty or adds an authored undo step.
- Canceling a document gesture restores the exact prior map and adds no undo
  entry; committing it adds one entry.
