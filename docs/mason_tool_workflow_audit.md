# Mason tool and workflow audit — 15 October 2026

**Assessment:** Mason has a substantial geometry and map-authoring foundation. Its weakest area is the consistency of the interaction contract between selection, picking, transforms, topology operations, cancellation and history. It is not yet reasonable to call the complete Hammer-style modelling workflow dependable. Adding more tool names before repairing those boundaries would increase the number of inconsistent paths.

This is a review of the **current local working tree**, including unpublished editor adapters, against the user's screenshots, primary documentation and two relevant tutorial transcripts. It is not a review of HEAD alone. Source anchors are for this snapshot and will move as fixes land.

**Evidence and limits:** The findings below are source traces, checked against existing tests and prior validation artifacts. The initial review did not rebuild Mason, change implementation, run new regression tests, or manipulate the live map. The follow-up capability audit at the end records additional runs of existing Geometry and Map Core tests, including a full Map Core timeout. A source-confirmed failure path is distinguished from a newly reproduced native UI failure. Passing tests establish their covered scenarios; they do not establish full tool usability. No percentage of “Hammer parity” or renderer completeness is claimed.

The original video URLs associated with the user's screenshots were not supplied. Relevant demonstrations were independently located: the complete auto-generated transcripts of [Eagle One's Arches and Rotation manipulation](https://www.youtube.com/watch?v=pl6EbpXoH0Q) and [Hosomi's Archway tutorial](https://www.youtube.com/watch?v=SU5nEMaxf78) were read; selected pivot/rotation frames of the former were inspected in the browser. This is not a claim that every referenced video or every frame was watched. The second video's transcript, rather than a complete visual viewing, supports its workflow comparison.

**What the references actually demonstrate**

The Eagle One tutorial's 3:26–4:51 sequence selects a boundary rim, moves the pivot, rotates with extrusion in 15-degree steps, repeats the edit with Shift+G, then extends the result straight. The important capability is a persistent modelling operation applied to the newly created rim. It is not just a rotation ring drawn beside an object. [Original demonstration](https://www.youtube.com/watch?v=pl6EbpXoH0Q&t=206s).

Hosomi's short tutorial combines a cylinder-derived roof, edge extension into walls, bridging into a floor, and scaling an extruded rim around a repositioned pivot to make trim. That is a useful small acceptance project because several tools must preserve compatible topology and selection. [Original demonstration](https://www.youtube.com/watch?v=SU5nEMaxf78).

Valve's mesh-editing lessons document component modes, face extrusion, edge loops/rings and connected mesh editing; later lessons cover bridging, cutting, pivot extrusion, local axes and workplanes. The Valve Developer Community pages returned access errors during this audit, so the Valve-attributed translations hosted on Dota 2's official Chinese site were used. These are historical Source 2/Dota tool references, not a promise that every shortcut is identical in every current Hammer game branch. [Mesh Editing 1](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Basic_Construction/Mesh_Editing_1.htm), [Mesh Editing 2](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Basic_Construction/Mesh_Editing_2.htm), [Mesh Editing 3](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Basic_Construction/Mesh_Editing_3.htm), [Mesh Editing 4](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Basic_Construction/Mesh_Editing_4.htm).

TrenchBroom provides useful complementary references for command repetition, camera navigation, geometry-aware interaction and a navigable issue browser. Its brush-oriented geometry model is not interchangeable with Source 2's open mesh modelling. Borrow workflow principles without forcing its topology restrictions onto Mason's mesh tools. [Official manual](https://trenchbroom.github.io/manual/latest/index.html#command-repetition).

**Current tool inventory**

“Implemented” here means the control reaches a real document/library operation. It does not mean all workflows, topology cases or interaction routes are complete.

| Area | What exists now | Assessment and important limit |
|---|---|---|
| Block and primitives | Icon palette: Box, Quad, Wedge, Cylinder, Spike, Sphere, Arch, Stairs, Spiral, Pipe, Torus; staged construction and preview | Good foundation. Sphere detail is coarse/fixed; numeric control and shape-quality checks matter more than adding another icon. |
| Vertex editing | Selection, transform, connect, merge/collapse/dissolve; drag-to-weld | Real operations, but Merge target semantics, projected depth columns and one-mesh selection are not obvious to the user. |
| Edge editing | Boundary Move/Extend, transformed extension and semantic repeat; loop/ring selection, split/connect, bridge, bevel, dissolve | Native G2 reference now covers the curved profile, straight extension, floor Bridge, exit cap, Save/Open and entrance continuation. Local axes and broader supported topology remain. |
| Face editing | Component transforms; connected coplanar dedicated/numeric Extrude, single-face Corner Inset, offset, extraction, thickening and deletion | Region extrusion adds perimeter walls once and preserves cap data. Transform-gizmo extrusion remains single-face; constant-width region Inset and general individual/noncoplanar semantics remain. |
| Translate/Rotate/Scale/Pivot | Root and component transformations, offset pivot and preview; recent rotated edge extension | World-axis basis only. The component Scale gizmo is uniform/radial, or uniform in the visible 2D plane. Numeric component scaling accepts independent X/Y/Z factors; per-axis interactive handles are missing. |
| Clip | Brush and mesh slicing, keep front/back/both, matching preview/commit and closed-shell caps | Mixed open/closed-shell cap policy repaired locally, including cavity holes; broader room-blockout gate remains open. |
| Knife / Edge Cut | Multi-point cuts across reachable faces; private brush candidate and one-step convert/cut | Atomic conversion/cut repaired locally; initial brush surface admission and failure diagnostics retain documented limits. |
| Loop Cut | Quad traversal/insertion, direction-correct off-centre fractions and atomic brush conversion/cut validated locally | Native candidate edges share viewport visibility/depth rules. Brush admission still depends on the surface hit; brush hover has no loop preview. The initial brush surface hit and missing brush hover preview remain limits. |
| Edge Arc | Deforms an open edge chain into an arc | Distinct from repeated rim extrusion. Requires suitable chain/topology; not a universal curved-tunnel tool. |
| Polygon / Path / Curve | Polygon construction, path geometry, walls, sweeps and lathe-related construction | Substantial creation capability. Validate transitions and workplane use through real projects; do not confuse parameterized sweep repetition with Repeat Last Edit. |
| Workplane | Picked surface plane and construction grid/placement | Useful, but does not orient transform gizmos. Pivot location and transform orientation are separate concepts. |
| Mirror / Shear | Drawn 2D mirror plane and bounds-side shear | Real, constrained workflows; not complete component/local-axis parity. |
| Texture / Eyedropper | Material assignment, align/fit/shift/rotate/scale, projection, UV weld and other operations | Backend capability exceeds panel usability. Precise Texture State fields are read-only. |
| UV Editor | Face UV move/rotate/scale/shear and pivot | Correctness defects in press/release and cancellation; no independent UV vertex/edge/island selection. |
| Paint | Mesh corner-colour painting; atomic brush conversion/stroke and pane-owned cancellation validated locally | It does not implement terrain blend-material painting; dabs are applied on release. Keep naming and descriptions within that scope. |
| Terrain / Patch / Displacement | Construction and surface edits; private Sculpt preview with connected coarse-quad refinement and one-step publication | Sculpt transaction/capture repair validated locally. Average-plane surface placement and modifier preview limits remain; terrain material painting and outdoor-world systems are unfinished. |
| Entity / Decal / Overlay | Placement records, schema-aware properties and entity I/O | Authoring exists. Runtime execution/preview cannot be inferred from the presence of entity records. |
| Measure / Camera | Measurement and navigation tools; remappable held-camera actions, speed control, framing/orbit | Existing targeted navigation tests are valuable. Need consistent takeover/cancellation of component gestures. |
| Clipboard / Groups / Instances | Mixed-root clipboard, groups, standalone prefabs and linked instances; edit/reload | Useful foundation. Prefab external-file/map publication needs failure-boundary repair. |
| Map Check | Geometry/entity/schema/asset checks, navigation and some undoable fixes | Good direction. Destructive repair currently loses surface information and needs clearer separation. |
| Keybindings / Settings | Larger simplified keymap page, profiles, import/export, conflicts; settings transfer | Some saved declarations are not routed. Settings export references separately installed theme/keymap/layout files, not a complete portable bundle. |
| Asset Browser | Multi-kind catalog, preview/inspection and activation callback | Generic “Use” is enabled for kinds Mason cannot activate. Cataloguing is ahead of placement/use. |
| Tile/world authoring | Separate TileEditor has a real document and rendered viewport | Not integrated into Mason as a tile workspace; not evidence of open-world streaming, runtime terrain materials or gameplay. |
| Runtime map preview | Separate CypherRender work exists | Mason perspective still uses QPainter; Run Map is disabled. This remains a real integration milestone. |

Inventory anchors: [primitive palette](/Users/karlosiric/Documents/MyProjects/CYPHER/src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolPanels.cpp:643), [tool descriptors](/Users/karlosiric/Documents/MyProjects/CYPHER/src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolPanels.cpp:1569), [real command registrations](/Users/karlosiric/Documents/MyProjects/CYPHER/src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_GeometryCommands.cpp:3790), [registration before placeholder filtering](/Users/karlosiric/Documents/MyProjects/CYPHER/src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_Workspace.cpp:1994), [Extrude tool](/Users/karlosiric/Documents/MyProjects/CYPHER/src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolGestures.cpp:1301), [component targeting and commit](/Users/karlosiric/Documents/MyProjects/CYPHER/src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolGestures.cpp:3255).

Do not use the old `kPlanned` array as a missing-feature count. Real geometry commands are registered first; placeholders are added only when their IDs are still absent. A static comparison of those IDs found terrain paint still missing in that particular list; that does not mean terrain paint is the editor's only unfinished feature.

**Priority findings: interaction correctness first**

**A1. Selection and manipulation do not share one edge-picking result.** High-confidence source finding; first priority.

**Implementation checkpoint, 16 October:** repaired for native viewport edge
acquisition. Both panes supply a mode-independent HIT/MISS/FAILED query, with
mesh/plane/edge filters, canonical endpoints and the original world-edge
fraction. Direct grabs/regrabs, Knife, Edge Cut, Loop Cut and Arc now use the
viewport's clipping, depth and physical visibility rules. Knife corner snapping
in camera views validates visible incident endpoints too. Initial query failure
cannot become brush conversion or face placement. Eight focused GUI cases pass
1,272 assertions, including the registered Connect command regression; full
checkpoint evidence and remaining boundaries are in the progress notes.
Different tool hit radii remain deliberate. General component vertex/face
picking, Knife's face-plane lookup and atomic brush conversion are separate work.

**Selection follow-up, 17 October:** native camera picking now preserves
selection on incomplete foreground queries and accepts a known foreground
strictly in front of failed-source bounds. Lost camera-button capture is
retired on the next press. Modified Select clicks over handles toggle/add;
actual drags retain their captured handle action, snap and resize policies.
Warped simple authoring polygons explicitly use projected-triangle ray tests;
strict default query semantics remain. Root picking uses cached evaluated
modifier surfaces and original owner IDs, including array copies. Modified
component-body clicks no longer bypass the single-owner policy. Native
selection/Clip/Knife evidence and remaining generated-face/brush-admission
limits are recorded in the
[17 October checkpoint](mason_geometry_workflow_research.md#selection-shell-aware-clip-and-atomic-knife--2026-10-17).

**Loop Cut follow-up, 16 October:** provisional brush edges now use this same
native picker inside a private conversion/cut transaction. A thin-occluder test
proves that the edge's visibility ray, not merely the nearby cursor ray, controls
admission. Off-centre loop fractions also now follow topological direction;
see the [Loop Cut checkpoint](mason_geometry_workflow_research.md#atomic-loop-cut-and-direction-correct-rings--2026-10-16).

Original failure: the viewport picker clipped a segment crossing the near plane and calculated perspective-correct depth. The tool picker required both original endpoints to project, chose by screen distance, and reused a screen-linear fraction between world-space endpoints. Consequently:

- A visible near-clipped edge can be selected but fail when directly dragged or cut.
- On an edge receding into depth, a cut or grabbed point can land away from the clicked pixel.
- Loop Cut and Edge Arc do not apply the same physical occlusion rule as the main selection path. A separate re-grab path can also choose an obscured selected edge.

These are related failures of a duplicated picking contract. They should not be patched independently in every tool. Deliberate X-ray selection is valid only when the view explicitly requests it.

Evidence: `CypherMapGui_Views.cpp:5504–5522`; `CypherMapGui_ToolGestures.cpp:385–427,1615–1650,1731–1751,2972–2988,3756–3785`.

Acceptance: put one end of an edge behind the near plane, then hover, click, drag and cut the visible part. Repeat on a strongly receding edge and behind an occluder. All operations must agree on identity and world position; a cut must reproject to the clicked pixel. Use actual view events rather than injecting a prepared selection.

**A2. Component gestures can survive cancellation boundaries that already cancel whole-object gestures.** High-confidence source finding; first priority.

**Implementation checkpoint, 15 October:** repaired for pending/direct vertex and
edge drags, component transform gizmos and selection lassos. Both viewport types
now cancel only the capture owned by the invalidated pane on focus loss,
navigation/projection changes, hide/deactivation/ungrab and lost-button movement.
Custom pivots and staged construction points survive. New actual widget-event
tests cover 80 active interruption scenarios, other-pane isolation, pending/lasso
cancellation, and a supported first-press brush conversion continuing into a
drag. The focused component/UV/conversion run passes 7 cases / 8,880 assertions.
This is a local checkpoint; specialized Arc, dedicated Extrude, Shear and sculpt
capture paths were not covered by this owner-specific component API. A later
16 October checkpoint extends the same owner cancellation boundary to dedicated
face Extrude, with document/selection epochs and layer-lock admission; see the
[Extrude follow-up](mason_geometry_workflow_research.md#dedicated-extrude-capture-and-lock-safety--2026-10-16).

The later [Paint checkpoint](mason_geometry_workflow_research.md#atomic-paint-strokes-and-capture-safety--2026-10-16)
adds pane-owned cancellation and synchronous target/context validation for
Paint, including unselected target hide/show and conversion off/on without
intervening input. Ten native camera-event cases pass. **Arc/Shear follow-up, 17 October:** both held gestures now capture pane,
source/selection epochs and fixed targets, with synchronous invalidation and
owner cancellation. Shear's affected hierarchy is prepared once; child locks
cancel, live Alt remains usable, and final release is resampled. Eight new
Cocoa cases pass 5,709 assertions. Descendant/modifier preview completeness and
Arc's supported-chain/diagnostic limits remain; see the
[Arc/Shear checkpoint](mason_geometry_workflow_research.md#edge-arc-and-shear-held-capture-safety--2026-10-17).
Other specialised captures still need their own workflow checks.

**Sculpt follow-up, 17 October:** Displace now owns a private map/wire preview,
fixed eligible targets and captured brush parameters. Pane/context interruption,
locks/visibility, stale identity and allocation refusal cancel without changing
the source; release publishes conversion, connected refinement and deformation
together. Native Camera/Top tests cover late input, fresh retry, adjacent targets
and foreign-pane isolation. See the
[Sculpt checkpoint](mason_geometry_workflow_research.md#atomic-sculpt-connected-refinement-and-shell-preflight--2026-10-17).

Original failure: viewport focus-loss, resize and camera-takeover hooks cancelled the view's `m_drag`. Components/gizmos live in a separate `ToolGestures` state map. Their document/selection guards do not substitute for focus or projection invalidation. A late release could reach the still-active component gesture after the camera or viewport had changed.

Evidence: `CypherMapGui_Views.cpp:3294–3306,4760–4785`; `CypherMapGui_ToolGestures.cpp:3044–3048,3532–3539,3577–3582`.

Acceptance: begin vertex/edge/face Move, Rotate and Extend; interrupt each with focus loss, resize, navigation, zoom and framing; release late. Require no geometry/history mutation, no stuck preview, and a clean subsequent gesture. The existing whole-object cancellation matrix in `CypherMapGui_Views_Tests.cpp:4007` is a starting point, not evidence that components already pass.

**A3. A UV click can repeat the previous transform, and UV gestures lack a complete cancel path.** High-confidence source finding; first priority.

**Implementation checkpoint, 15 October:** repaired. Every press starts with an
empty pending edit; only the initiating button can release it, and identity edits
do not publish. Escape, focus loss, hide/deactivation/ungrab, lost-button movement,
wheel/resize, Fit, document and selection replacement cancel the capture. A
cancelled pivot/pan restores its starting local state, while a completed pivot is
retained. UV translation now moves an implicit pivot exactly once after document
refresh. Three new widget-event cases cover brush/mesh faces, stationary clicks,
identity drags, interruption and late-release safety, fresh edits and Undo/Redo.
Pan/Fit rollback and allocator-failure pivot retention were reviewed but are not
separately injected in those cases. See the progress notes for checkpoint logs.

Original failure: UV press reset the preview but not `m_edit`; only movement reset `m_edit`. After a real UV edit, a later stationary press/release on a transform area committed the previous edit again. Hover movement did not clear it because the idle branch returned first. The canvas also lacked an Escape/focus-loss gesture cancellation route, and refused refresh while a drag was active.

Evidence: `CypherMapGui_UvEditor.cpp:67–81,226–249,318–324,363–389`.

Acceptance: move UVs, then click without moving on each handle and the canvas; UVs and undo depth must remain unchanged. Start a UV move then Escape/focus loss/document replacement, release, and verify no commit. Initializing pending input at press and tracking a real nonidentity edit are part of the minimal repair.

**A4. Component selection modifiers differ between clicks and lassos.** High-confidence source finding; first priority.

**Implementation checkpoint, 16 October:** repaired for the scoped native
click/column/lasso/direct-drag ownership routes. Component selection remains on
one mesh: plain input may switch; Shift adds; Ctrl/Command toggles clicks and
subtracts lassos, taking precedence over Shift. Modified foreign input preserves
state and explains the limit before implicit brush conversion. Plain lasso
ranking uses eligible current mesh/root, distinct eligible-component count and
stable object-ID ties; edges require both endpoints inside. Fallible scratch and
final publication preserve state on failure. Modified no-ops retain selection
revision, edge seed and column metadata. See the progress checkpoint for native
widget coverage and the retained boolean occlusion/separate vertex-picker limits.

Original failures follow for context.

With components selected on mesh A, Ctrl/Command-lasso over mesh B can clear A despite not touching it. Shift-lasso can replace A, while Shift-click B is refused under the one-mesh selection policy. The fallback intended to choose the mesh with most candidates can stop at the first unordered-map entry instead.

Follow-up source review on 16 October also found bypasses in orthographic vertex
column selection and direct vertex-drag fallback: they can replace mesh A through
a modified interaction with B despite the singleton click policy. A repair must
cover those routes as well as lasso, use deterministic eligible-component counts,
and retain the existing selection when a modifier cannot span meshes. These
findings were addressed in the A4/A5 checkpoint above; A1 alone did not fix them.

Evidence: `CypherMapGui_ToolGestures.cpp:2888–2944`; `CypherMapGui_Edit.cpp:1051,1104`.

Acceptance: additive/subtractive click and lasso on A, B and both. A subtract operation over B must not clear A. Choose a deterministic supported policy for cross-mesh addition and expose the restriction instead of changing semantics between gestures.

**A5. Face picking still requires leaving the transform tool.** High-confidence workflow blocker.

**Implementation checkpoint, 16 October:** repaired. Select, Translate,
Rotate and Scale now share the same face-pick helper after gizmo dispatch in both
pane types. Shift adds and Ctrl/Command toggles within one mesh; refused or
failed additions never fall back to replacement. Mesh face replacement/addition
prepares roots and face storage privately before one publication, supports
borrowed live spans and duplicate inputs, and preserves state on allocation
failure. Brush root/face publication is prepared together too. Actual handles
keep gesture priority and brush faces remain single selection. Tests select
faces after entering each transform, exercise modifiers and next-face picks,
then check actual selected-corner movement and geometry Undo/Redo. Full historical
face-set restoration is separate: existing edit snapshots store the active face.

Original failure follows for context.

Vertices and edges can be picked through expanded transform-mode routing. Faces remain gated to Select, Extrude or Texture. A face already selected can be transformed, but selecting the next face while Translate/Rotate/Scale stays active fails. Root-transform guards correctly prevent accidental parent motion here; this finding is blocked workflow, not a claim that this path still moves the entire mesh.

Before widening the tool gate, repair the face modifier path too: failed
`MapWorkspace_AddMeshFace` currently falls back to replacement, including a
cross-mesh refusal or allocation failure. Ctrl/Command on an unselected face can
also replace the current set. Tool gating and modifier semantics need one tested
increment so selecting the next face cannot silently discard earlier selection.

Evidence: `CypherMapGui_Views.cpp:3489–3505,5113–5131`. Existing `CypherMapGui_ToolGestures_Tests.cpp:764` preselects faces in Select before changing tools, missing the problematic order.

Acceptance: enter each transform first, choose Faces, pick/add/remove faces, transform, then pick the next face without changing tools. Repeat in 2D and Perspective.

**A6. Fast Texture window and asset callback lifetimes can cross workspace shutdown.** Newly reproduced during the 16 October implementation checkpoint; repaired locally.

A full-suite run failed a Fast Texture fit, then crashed while opening the next
workspace; LLDB stopped in a retired resolver capture's `QTemporaryDir::path()`.
The window cache retained a non-null dialog until deferred deletion even after
its workspace pointer was cleared. Reuse of the workspace address returned that
detached dialog. Failed fixture cleanup then exposed a separate production gap:
workspace shutdown left asset resolver/lister callbacks and roots registered.

Fast Texture now detaches its listener and cache synchronously on `done`, close
(including hidden close), shutdown and destruction. Cache removal checks dialog
identity, and detached controls refuse edits. Workspace shutdown clears the
existing resolver/lister/root APIs after shutdown notifications. Three focused
lifetime cases check immediate close/reject/hidden-close reopening, same-address
reinitialization, late deletion and captured-object release. Combined focused
checks pass 21 cases / 3,682 assertions on offscreen and native Cocoa; the complete
Map GUI suite passes 730 cases / 155,369 assertions with the failing seed reused.
These checks cover Fast Texture and the shared asset registries, not every
modeless editor window.

**Priority findings: geometry and publication boundaries**

**B1. Connect Edges silently subdivides an unrelated selected edge if another pair can connect.** High-confidence source finding.

**Implementation checkpoint, 16 October:** repaired. Connect preflights all input
edges against original faces before splitting: every edge must participate in a
two-edge face pair, and more than two on any face rejects the batch. Duplicate,
missing and unrelated edges reject without source or ID mutation. Split-returned
vertex handles are resolved to assigned IDs, so coincident shells are no longer
confused by midpoint-position matching. The registered command reports the
selection requirement and tests one-step Undo/Redo and save/reopen. All Map Core
topology cases pass (44 cases / 10,742 assertions), including new invalid-input,
coincident-shell and preflight allocator failures. Later transaction failures
still require discarding the private working copy; this is not a new atomic
multi-transaction Core API.

Original failure: the operation split all selected edges first and then found faces containing exactly two new midpoint vertices. A valid cofacial pair plus a third edge on a disconnected shell could succeed while leaving the third edge unnecessarily split.

Evidence: `CypherMap_TopologyEdit.cpp:577–627`.

Acceptance: connect two compatible edges with a third unrelated edge selected. Either reject the selection atomically or connect the supported pair while preserving unrelated topology, with explicit feedback. Do not silently perform a different operation on the third edge.

**B2. Clip capping is decided per mesh rather than per closed shell.** High-confidence source finding.

**Implementation checkpoint, 17 October:** repaired locally. Map Clip preview
and commit opt into original-closed-shell capping. Open sheets remain open;
closed cavity outlines are aggregated to retain holes. The raw kernel default
is unchanged. Native merged box/sheet mouse clipping, preview agreement,
Undo/Redo and save/reload pass; Core tests add FRONT/BACK/BOTH, retained sheet
surfacing and target-snapshot allocation refusal, and kernel tests cover
nested cavities and allocation sweeps. This does not make direct multi-root
Core calls independent transactions; GUI edits still use private candidates.
The original finding follows for context.

`closeCut` is enabled only when the entire mesh has zero boundary edges. A mesh containing both a closed box and a disconnected open Quad therefore disables capping even if the cut intersects only the box.

Evidence: `CypherMap_TopologyEdit.cpp:1167–1188`.

Acceptance: combine a closed box and a separate open surface into one mesh; cut only the box with Cap enabled. The box's cut must close while the intentionally open surface remains open. Separate-open/separate-closed tests do not cover this mixed-shell case.

**B3. Cancelling Knife can leave an implicit brush conversion behind.** High-confidence behaviour; workflow policy needs correction or explicit presentation.

**Implementation checkpoint, 17 October:** repaired locally for Knife and
Edge Cut. Points use a private brush conversion; confirm converts/cuts a fresh
current-document copy in one undo step. Candidate topology/IDs are verified,
current metadata is retained, and cancellation or stale contexts discard the
stage. Native Camera/Top input covers cancellation, navigation, exact history,
allocation refusal/retry and edge snapping. Brush admission still needs its
initial physical surface hit, and identity cursor drift cancels instead of
remapping a staged path. The original finding follows for context.

**Related checkpoints, 16 October:** Paint and Loop Cut's equivalent defects
are repaired locally. Conversion and painting share one transaction; Loop Cut
converts, resolves its candidate edge through the native picker, and cuts in
one private copy. Failed/no-effect attempts preserve the original brush.
The later 17 October checkpoint repairs Knife with retained candidate IDs and
fresh current-document verification, and Displace with a private prepared
stroke. These are separate lifetime policies rather than copied single-event
Loop Cut callbacks. See the [Paint checkpoint](mason_geometry_workflow_research.md#atomic-paint-strokes-and-capture-safety--2026-10-16)
and [Loop Cut checkpoint](mason_geometry_workflow_research.md#atomic-loop-cut-and-direction-correct-rings--2026-10-16).

Original failure: the first Knife point can run a standalone “Convert Brush to Mesh” edit before any cut is committed. Escape cancels the staged cut, not that conversion. Loop Cut could likewise convert before a later failure. There was an informational notice, but cancellation was not equivalent to returning to the pre-tool document. This describes the original pre-repair Knife path.

Evidence: `CypherMapGui_ToolGestures.cpp:518–541,1627–1658,1731–1751`; existing success tests at `CypherMapGui_ToolGestures_Tests.cpp:1056–1085` expect separate undo steps.

Acceptance: first Knife point on a brush, Escape, then compare source kind, document revision and history. Prefer preparing conversion inside the eventual cut transaction. If conversion remains a distinct operation, make it an explicit user action and keep cancellation claims precise.

**B4. Finish Editing Prefab can save its external file before failing the map transition.**

**Recovery checkpoint, 17 October:** a real file-save followed by injected map
allocation failure is now tested. Finish retains and reports the saved-file fact,
editing group and retry path; Cancel no longer says the saved file is unchanged.
Override/session-capture failure aborts before map publication. This is explicit
partial-save recovery, not a cross-resource atomic commit: B4 remains open.
The original failure path follows for context.

The `.cyprefab` file is committed before the failure-prone map clone/delete/recreate/history operation. If that later operation fails, the command reports failure while the file has already changed, the edit session remains active and instance reload is skipped. A subsequent Cancel can say the prefab is unchanged, which is no longer true.

Evidence: `CypherMapGui_Instances.cpp:613,623–639`.

Acceptance: inject failure after preparation at each file/map publication boundary. Prepare all allocatable state before replacing the file, or explicitly support and report partial publication. Undoing a map command also must not silently imply that an external asset save was undone.

**Missing capabilities that directly explain the Hammer workflow gap**

**C1. Repeat Last Command is not Repeat Last Modelling Operation.**

**Implementation checkpoint, 17 October:** repaired locally for component
transforms and edge extension. Shift+G captures affine/pivot/extension meaning
and resolves the current resulting rim; explicitly selected successful Command
History entries retain precedence. Native Cocoa tests cover one mouse step and
five multi-edge profile repeats, with midway Undo/Redo; full Mason tests send
physical Shift+G. Recipes remain session state and do not cover arbitrary mouse
macros. The subsequent G2 reference scene now also passes straight continuation,
Bridge, exit cap and save/reopen checks; this does not close every modelling
gate. See the
[repeat/history checkpoint](mason_geometry_workflow_research.md#component-selection-history-and-semantic-modelling-repeat--2026-10-17).
The original finding follows for context.

The Shift+G binding exists. Command History records command ID and text arguments, then executes them again. A mouse component transform commits directly and does not record its affine transform, pivot, extension mode or next-rim policy. Selecting Rotate itself is considered repeatable, so the shortcut can simply activate Rotate again rather than add another curved strip.

Evidence: `CypherMason_MainWindow.cpp:1055–1060`; `CypherEditorGui_CommandHistory.cpp:48,131–187,289–295`; `CypherMapGui_ToolGestures.cpp:3480–3527`. The smoke test at `CypherMason_Smoke_Tests.cpp:1899–1919` repeats grid enlargement, not geometry.

A repeat record needs the operation's meaning and parameters: rotate/extrude, the captured pivot and orientation, angle/distance, target policy and successful result selection. A successful repeat must operate on the current resulting rim. It must not replay stale component IDs or unrelated UI commands. Parameterized sweep iterations are useful but do not replace this capability.

Acceptance: create one mouse Rotate-Extend step, press physical Shift+G twice, obtain two additional connected strips, then undo/redo and continue. Inspect topology, selection and history, not only pixels.

**C2. Local/selection/workplane transform axes are missing.** High-confidence architectural gap.

Custom pivot changes the origin; all component gizmo axes and rotation planes still use canonical world X/Y/Z. The workplane affects construction placement but not those axes. An angled tunnel rim therefore lacks a direct “continue along this rim's orientation” transform workflow.

Evidence: `CypherMapGui_ToolGestures.cpp:70,1065–1067,3313–3316,3391,3438–3456,3650`; root transforms also use world axes in `CypherMapGui_Views.cpp:1391,2541,5194`.

Design direction: expose a small explicit World / Selection / Workplane orientation choice. Use the same basis for drawing, picking, constraints, snapping, preview and commit. Handle degenerate selections with a defined fallback and visible status. Do not confuse changing the pivot position with changing this basis.

**C3. Undo/redo does not restore complete component selection.**

**Implementation checkpoint, 17 October:** repaired locally for stable-ID
vertex/edge sets, edge seed, full mesh-face set/active face and relevant vertex
columns. New rim selection is prepared before history captures the after-state.
Undo/Redo retains current mode and locks; invalid identities are pruned, and
fallible preparation preserves the live map/history on failure. Actual native
repeat/continuation tests and explicit command regressions pass. The original
finding follows for context.

History restores geometry and root selection plus limited face context; publishing clears `meshSelection`. Undo/redo after edge extension therefore loses the selected continuation rim even when the shape restores correctly.

Evidence: `CypherMapGui_Edit.cpp:37–41,79–82,103–124`.

Acceptance: Extend, Undo, Redo, immediately Extend again without repicking. Preserve complete relevant component selection by stable source identity, while clearing references genuinely invalidated by topology changes. Geometry restoration alone is insufficient for continuous modelling.

**C4. Component scope, scale and snapping remain narrower than root transforms.** Implemented limits, not all accidental bugs.

One `meshId` owns a component selection. Cross-mesh vertex/edge/face manipulation is not the same as already-supported multi-object movement. The component Scale gizmo is radial/uniform; numeric component scaling already accepts independent X/Y/Z factors (`CypherMapGui_ToolPanels.cpp:939`, `CypherMapGui_ToolGestures.cpp:3898`). Per-axis interactive handles remain missing. Component gizmo translation snaps deltas; direct dragging and whole-root movement have different modifier/absolute/geometry-alignment rules. Brush faces intentionally have a narrower transform path than editable mesh faces.

Evidence: `CypherMapGui_GeometryCommands.cpp:145–150`; `CypherMapGui_ToolGestures.cpp:2777–2810,3255–3300,3423–3475`; `CypherMapGui_Views.cpp:1419`.

**Implementation checkpoint, 17 October:** direct vertex dragging now acquires
stationary authored handles within nine logical pixels on the same movement
constraint, before normal grid snapping. Native Top/Camera cases cover off-grid
columns, ordinary pixel tolerance, release-only acquisition, upright-plane
stability, modifiers and allocator refusal/retry. Same-mesh scope remains.

Dedicated and numeric Extrude now process a connected coplanar face region as
one operation, preserving cap identities and adding only perimeter walls. A
negative dedicated drag moves each shared vertex once. Disconnected or
noncoplanar regions are explicitly refused; numeric Corner Inset requires one
face. Region cap attributes and generated wall charts have dedicated coverage.
Transform-gizmo face extrusion remains a separate one-face path; this change
does not add per-axis Scale or local/selection/workplane gizmo orientations.

**The sphere and vertex complaints require precise distinctions**

The current sphere construction uses latitude rings, with fixed 16×8 or 24×10 detail, on mesh and brush paths. Unequal bounds produce an ellipsoid. Fixed resolution and flat faces can look faceted; that alone is not evidence of incorrect topology. New roundness/preview/persistence tests exist. They do not prove that the user's exact spiky example is visually resolved. Compare a fresh equal-axis sphere in Top/Front/Side/Perspective, both output kinds, both detail levels, and after save/reopen. If spikes remain, compare generated positions and face winding with rendered triangulation before changing the generator again. Evidence: `CypherMap_Edit.cpp:431–463,487–517`; `CypherMapGui_Edit.cpp:1576–1604`; `CypherMapGui_Block_Tests.cpp:146–223`.

Connect and Merge are different operations. Connect joins two existing, nonadjacent vertices by splitting a shared face; it does not create a missing center vertex. Three selected corners on one face are ambiguous in the current implementation. Merge moves vertices toward a group center; in orthographic projected-column editing, equal-depth layers merge separately. Dragging onto a stationary vertex is a separate weld path preserving that target. A visible pivot/cap center is not necessarily an authored vertex. These facts can explain a failed attempt, but cannot establish what the user selected in an old screenshot.

Evidence: `CypherMap_TopologyEdit.cpp:318–427`; `CypherMapGui_GeometryCommands.cpp:429–455,546–565`; `CypherMapGui_ToolGestures.cpp:2814`; cap-connect test `CypherMapGui_ToolGestures_Tests.cpp:2282–2303`.

The nine-pixel direct-drag target marker is now implemented. Further improvements: explicit Merge to Center / Active / Target semantics; a separate insert-center/fan workflow if needed; selection-aware reasons such as “vertices do not share a face.” Do not make one Merge button guess between welding, connecting, joining objects and inserting new topology.

**Properties, assets and feedback need to tell the truth**

- Texture State fields are read-only, while common buttons execute fixed defaults such as 90-degree rotation or 2× scale. Exact numeric operations exist underneath but need ordinary editable U/V controls, units and mixed-selection handling. Evidence: `CypherMapGui_ToolPanels.cpp:1082,1148–1150,2568–2571`; `CypherMapGui_GeometryCommands.cpp:1929–1963,2029–2039`.
- Bevel can remain enabled for an open boundary it cannot bevel; Connect can remain enabled for edges without a compatible shared face. Disabled-with-reason is better than a failed click, while runtime validation remains necessary. Evidence: `CypherMapGui_GeometryCommands.cpp:546–564,679–703`.
- Asset Browser's generic Use/double-click reaches Mason's material-only activation callback for unsupported kinds. Prefabs, models and sounds need real kind-specific actions or an inspect-only state. Evidence: `CypherEditorGui_AssetBrowser.cpp:1371–1375,1403–1405`; `CypherMason_MainWindow.cpp:2005–2010`.
- Some keymap declarations can be saved but are deliberately not routed: geometry/2D mouse handlers and multistroke viewport bindings. Availability is hidden in the normal table. Keep format compatibility, but clearly mark unsupported rebinding instead of implying it works. Evidence: `CypherEditorGui_KeymapSettings.cpp:365,1011–1026`.
- Paint's command label/description now say Vertex Paint and mesh corner colours (17 October). Terrain material painting remains unavailable. Evidence: `CypherMapGui_Workspace.cpp:1790–1797`; `CypherMapGui_ToolGestures.cpp:1010–1049`; `CypherMapGui_ToolPanels.cpp:1677–1679`.
- Settings import/export is useful, but theme/keymap/layout IDs reference separately installed files. Full portable workspace packaging remains unfinished. Evidence: `CypherEditorGui_SettingsTransfer.cpp:168`.
- Map repair warns that faces take the first material and UVs are reprojected. Preserve surviving attributes in the normal repair path; reserve destructive reconstruction for a clearly distinct option. Evidence: `CypherMapGui_Check.cpp:479,543`.

These improvements can preserve the accepted layout and theme. They concern the meaning and behaviour of controls rather than a wholesale sidebar redesign.

**What should be kept**

The existing geometry library and document edit boundary are valuable. GUI edits commonly prepare a cloned document, wire representation and history before publication, with allocation-failure coverage. Direct boundary Extend and custom-pivot transformed extension now have actual Qt event tests. Clipboard operations cover mixed ownership, persistence and failure. Camera tests cover held Shift ownership, remapping, speed controls and focus clearing. Root resizing already has multi-object tests. Schema-aware entity properties, I/O, linked prefab editing, terrain sculpting and navigable Map Check are substantive features.

The recent local validation artifact records 695 Map GUI cases and 69 Mason cases passing, plus focused/native pivot tests. Those results were from the preceding implementation checkpoint, not rerun by this audit. The missing cases are often transitions between systems: picker→drag, drag→navigation, edit→repeat, undo→continue. Passing operation tests and a large assertion count cannot substitute for those sequences.

Reference: `build-mason/artifacts/mason_pivot_edge_validation.json`; `CypherMapGui_Edit.cpp:173–236`; `CypherMapGui_Clipboard_Tests.cpp`; `CypherMapGui_ToolGestures_Tests.cpp:2923`; `CypherMapGui_Views_Tests.cpp:4007`.

**Small architectural repair, not a wholesale rewrite**

Use a shared hit result containing object/component identity, clipped screen geometry, world hit point and visibility policy. The tool should not recalculate a different hit after selection has already succeeded.

Give every active gesture the same lifetime contract: source document and selection identity; owning viewport and projection generation; captured tool intent; preview; explicit commit or cancel. Camera takeover and focus/projection changes must have a defined effect on that session. Retaining a multi-click Knife construction while navigating can be valid, but retaining an old mouse-down transform and later committing it accidentally is not.

Capture a semantic edit separately from the UI command that activated the tool. Undo stores the before/after state needed to restore the document; Repeat stores the operation and target-selection policy needed to apply it again. Those are related but different responsibilities.

Pseudocode for the intended flow:

```text
press:
    resolve one shared hit and explicit tool intent
    capture source, selection, viewport, pivot and orientation
    prepare gesture state without changing the document

move:
    validate the captured context
    solve constraints and snapping in the captured basis
    build and validate the candidate edit
    show the candidate and any rejection reason

cancel:
    discard candidate and all pending input
    preserve the original document and history

commit:
    prepare geometry, attributes, next selection and history
    publish the successful edit once
    retain a repeatable semantic operation when supported
```

Existing transaction machinery should be reused. The refactor is about shared contracts and ownership at the seams, not replacing every tool with a new framework.

**Acceptance projects and recommended order**

1. **Interaction correctness:** repair shared picking, stale component gestures, UV lifecycle, lasso modifiers and face repicking. Run ordinary clicks, small drags, overlapping handles, depth-skewed/near-clipped geometry, all viewport types, modifier remaps and late releases. Keep geometry and history invariant on cancelled/invalid input.
2. **Geometry safety:** Connect mixed selections, Clip mixed shells, conversion cancellation and prefab partial saves. Add failure injection where publication spans allocations or files. Preserve source identities, face attributes and untouched topology.
3. **Complete the small modelling project:** Quad → select boundary → Extend wall → select rim → offset pivot → Rotate-Extend 15° → Repeat twice → straight Extend → Undo/Redo → continue → save/reopen. Then do the cylinder-derived arch/trim project. This should work through normal mouse/keyboard controls without fixture-injected selections.
4. **Precision and productivity:** local/workplane orientation, per-axis component scale handles, explicit merge target, selection-aware operation explanations, editable UV numbers, and component alignment to existing geometry. Validate grid16/32 geometry against current grid64 without moving unrelated vertices.
5. **Capability honesty and workspace continuity:** asset activation by type, routed-keybinding status, full settings/theme/profile packaging, attribute-preserving repair and coherent content-browser actions.
6. **Renderer/editor integration:** wire the renderer into Mason's scene viewport with matching camera/picking/overlays and resource invalidation, then measure representative maps. This is separate from fixing topology and input. GPU rendering will not repair a wrong edge parameter or stale drag state.

For every acceptance project, require preview/commit agreement, meaningful undo steps, preserved UV/material data, clear invalid-operation feedback, cancel/retry, save/reopen, and successful continuation from the resulting selection. Test the user's intended sequence, not just that a button returns success.

Mason's current perspective still clips/sorts/draws faces using QPainter (`CypherMapGui_Views.cpp:6168,6249`), Run Map is disabled (`CypherMapGui_Workspace.cpp:1844`), and compile serializes interchange (`CypherMap_Compile.cpp:119`). The separate TileEditor has an actual rendered viewport (`CypherTileEditorMainWindow.cpp:1161`), but it is not a Mason tile workspace. Neither those authoring tools nor geometry-product counts establish a playable runtime, baked lightmaps, sound, physics, scripting execution or open-world streaming.

The release gate should therefore be **a small set of complete, repeatable mapping tasks**, followed by measured renderer integration. More tool buttons and more isolated tests are useful only when those tasks become more reliable.

---

**Follow-up: complete geometry capability audit — 15 October 2026**

This extends the interaction review to vertices, edges, faces, mesh objects, brushes, curved surfaces, groups, parenting, instances and geometry exchange. It answers whether the remaining work is merely connecting existing library features to Mason. **Some is integration, some is repair of existing behavior, and some needs new geometry algorithms or a wider supported topology domain.**

The historical “Already at parity” section in `docs/mason_toolset_gap_analysis.md:379` is a command-inventory assessment, not an acceptance certificate. Later entries supersede earlier missing-feature rows, and the current source does not support broad face-bevel or region-editing parity. Use the concrete domains and tests below when planning work.

**Capability matrix: component editing**

“Exposed” means there is a real GUI-to-document path; it does not certify every input or interaction. “Library only” means reusable machinery exists but the complete editor operation does not.

| Area | Existing capability | What remains |
|---|---|---|
| Vertex transforms | Individual vertex movement, affine transforms, grid snapping, depth-column selection and target-pair welding | Consistent picking/cancellation; explicit active/last-selected merge target; general vertex slide. Components remain scoped to one mesh. |
| Vertex Merge / Connect | Center merge; stationary-target welding; Connect splits affected faces between two selected nonadjacent corners | Merge target machinery exists but is not a clear command choice. Connect is not arbitrary network/fan construction: three selected corners on one affected face are rejected. |
| Edges | Split, dissolve, collapse, connect, normals, loop/ring/ribs/path selection; boundary extension by translation or affine transform | Repeat the actual geometry operation; preserve active rim through undo; shared visibility-aware picking; local/workplane gizmo orientation; consistent mixed-boundary selection eligibility. |
| Edge bevel | Real joined multi-edge bevel with width, segments, attribute transfer and validation | Interactive preview; explicit overlap policy; profile/miter controls if needed. Current kernel does not accept arbitrary open borders, reflex cases or every valence. |
| Vertex bevel | Interior-fan truncation and replacement cap | Boundary vertices are excluded; no rounded-segment control. Per-edge clamping can produce unequal actual cut distances and needs visible feedback. |
| Face Move / Rotate / Scale | Real component transforms; numeric independent XYZ scaling | Repick faces while a transform tool stays active; per-axis interactive scaling. Numeric scaling is already independent, so it must not be described as uniformly limited. |
| Face extrusion | Dedicated/numeric connected coplanar region operation with perimeter-only walls; single-face transform-gizmo route | Region cap IDs/attributes and unique inward motion verified. Noncoplanar/individual modes, broader preview and full G4 construction remain. |
| Face inset | Active-face inner polygon plus surrounding faces | Multi-face/region semantics; constant-width border construction. Current corners move toward the centroid, which gives unequal border widths on a rectangle. |
| Face bevel | Button listed in face properties | No face branch in the command: only vertices and edges are accepted. This is a visible capability mismatch. |
| Bridge | Two-face, equal-length chain and equal-count boundary-loop bridges | Clear correspondence/twist and segment controls. Edge-chain GUI hardcodes one segment; face segments are command arguments. Mismatched-resolution bridging needs more algorithm work. Targets must belong to one mesh. |
| Fill / construct faces | Fill-hole operation; source-aware `TryAddFace` can stitch existing/new corners | Interactive Poly Pen/face construction on an existing mesh is library-only. Drawing a separate Polygon mesh is a different operation. |
| Loop slide | Source-aware baseline-relative plan, preserving topology/attributes, with stale-plan checks; one-shot GUI factor command | Interactive use of the plan. Current domain is a regular closed quad loop, not every edge chain or vertex. |
| Thicken / Solidify | Selected-face extraction and solidification of supported open disk-like shells | Annular shells, closed shells and mixed open/closed input are rejected. A sheet containing a window hole is outside the current supported domain. This is not solved by adding a button. |
| Subdivision | Catmull–Clark evaluation, live levels, baking and cage editing; Linear/boundary/crease support below the GUI | Expose useful crease/scheme controls; preserve/remap map face records during baking. The one-shot Map operation rejects meshes with persistent `faces` records. |
| Normals / UV / colour | Hard/soft edges, smoothing groups, smooth corner normals, UV transforms and corner colours; attribute-transfer machinery | Consistent visible smooth shading; UV gesture correctness; editable UV numbers; independent UV-corner/island tools. Normal hardness and subdivision crease weight are separate controls. |

Evidence: `Gui/CypherMapGui_GeometryCommands.cpp:437,468,514,643–703,3215`; `Gui/CypherMapGui_Edit.cpp:1228`; `Gui/CypherMapGui_ToolGestures.cpp:1301–1350`; `Gui/CypherMapGui_ToolPanels.cpp:1583–1591`; `Core/CypherMap_TopologyEdit.cpp:318–427,550,1326,1491,2201`. These paths are relative to `src/CypherEditor/Workspaces/Map/`.

Library evidence: `src/CypherEditor/Geometry/Mesh/CypherGeometry_MeshSourceTopology.h:151`, `CypherGeometry_MeshSourceModeling.h:44`, `CypherGeometry_MeshBevel.h:18`, `CypherGeometry_MeshBridge.h:60`, `CypherGeometry_MeshBoundaryOps.h:238`, `CypherGeometry_MeshLoopSlide.h:86`, `CypherGeometry_MeshSolidify.h:67`, `CypherGeometry_SubdivisionSurface.h:72`.

**Inset is a concrete example of reusable machinery versus a complete tool.** The current implementation normalizes the vector from each corner to the vertex-average centroid and moves the corner by `margin`. For a 256 × 128 rectangle and margin 16, the two border widths are about 14.31 and 7.16, rather than 16. This follows directly from the code and geometry; it is not a new native UI reproduction. `Planar_TryOffsetRegion` already supports constant-distance contour insets/outsets and holes. Reusing it still requires mapping the result back into the selected mesh, preserving face/corner attributes and identities, handling collapsed regions, and creating the surrounding faces. It is not a drop-in replacement call.

Evidence: `src/CypherEditor/Geometry/Mesh/CypherGeometry_MeshTopologyOps.cpp:2817–2861`; `src/CypherEditor/Geometry/Core/CypherGeometry_Planar_Offset.h:85`. Existing inset tests mainly establish counts, Euler characteristic and failure atomicity, not rectangular border width (`tests/CypherEditor/Geometry/Mesh/CypherGeometry_MeshTopologyOps_Tests.cpp:1195`).

**Shape construction: pipes, bends and architectural pieces**

| Desired result | Current route | Assessment |
|---|---|---|
| Box, wedge, cylinder, cone/spike, quad | Block palette | Implemented. Brush and mesh output must remain explicit because their valid edits differ. |
| Round sphere / ellipsoid | Block Sphere with latitude bands | Current construction has roundness and convexity tests. Two detail presets, 16×8 and 24×10, remain fixed; equal dimensions give a sphere, unequal dimensions an ellipsoid. Existing saved geometry is not regenerated. |
| Straight hollow pipe | Block Pipe with axis, sides and wall thickness | Implemented annular extrusion. It does not follow a path. Elliptical radii are reduced by the same amount, so normal wall thickness is not generally constant around an ellipse. |
| Curved wall / arch | Block Arch | Implemented partial elliptical ring extruded to a height: a rectangular wall cross-section, not a circular pipe elbow. |
| Closed donut | Block Torus | Implemented solid tube cross-section around a closed ring. No internal passage through that tube and no partial-arc elbow control. |
| Curved solid rod / cable | Curve → Smooth + Round | Implemented circular-profile sweep along clicked Catmull–Rom points, capped at the ends. This is a solid rod, not a hollow pipe. |
| Rectangular path / walls | Curve Straight / Walls | Rectangular solid path or planar wall stroke. Straight path rejects vertical paths; Walls has miter/bevel/round joins. |
| Surface of revolution | Curve Lathe | Implemented radius/height profile revolution. Partial turns produce open surfaces, not automatically capped hollow elbows. |
| Curved sheets / patch forms | Patch primitives, Curve Patch output, control matrix | Implemented editable curved surfaces. A patch surface alone is not a closed pipe wall. |
| Straight / spiral stairs | Block Stairs / Spiral Stairs; Brush Sweep for segmented runs | Implemented construction routes. Test actual rise/run, end alignment, material continuity and continued editing. |
| Arched tunnel / rim-built bend | Boundary extension about an offset pivot, then bridge/cap | Substantial ingredients exist. Repeat, selection continuity, picking and bridge restrictions keep the full workflow incomplete. |
| Hollow pipe following a 3D curve | No dedicated completed route | Needs outer and inner contours, annular rims, sampling and intersection policy. Current single-contour sweep and restricted Solidify do not directly provide this operation. Manual mesh construction/CSG is distinct from a dependable pipe tool. |
| Pipe tee / Y junction / variable-section duct | Manual modelling and Boolean ingredients | No direct validated junction or general profile-loft workflow was found. Do not advertise automatic junction quality from the existence of Boolean union. |

Shape evidence: `src/CypherEditor/Workspaces/Map/Core/CypherMap_ObjectCreate.cpp:289–349,394–409,461–510,513–586,641–672,787–837,915–945`; `src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolGestures.cpp:680–752`; `src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolPanels.cpp:641–733,1727–1731`.

The Sweep library takes **one** profile contour. Closed contours generate a surface around the profile and optional filled end caps; there is no outer/inner-contour contract. Start/end scale already exists in `sweep_options_t`, but Mason leaves both at 1. The editor constructs a scratch curve network, samples with fixed maximum spacing 8, publishes a mesh and destroys the path network. Moving a path control point after creation is therefore not a retained procedural edit. That requires a saved recipe, or the interface must explicitly describe conversion to an ordinary mesh.

Some Curve properties are misleading: Round ignores the separate Height value; Patch output does not use the selected solid profile; Patch Lathe does not use the partial Lathe Angle. Controls should be applicable to the selected output or disabled with an explanation.

Evidence: `src/CypherEditor/Geometry/Surfaces/CypherGeometry_Sweep.h:13–20,53–76`; `src/CypherEditor/Workspaces/Map/Core/CypherMap_ObjectCreate.cpp:297–314`; `src/CypherEditor/Workspaces/Map/Gui/CypherMapGui_ToolGestures.cpp:704–746`.

**Objects, groups and relationships are separate contracts**

| Concept | Meaning in Mason | Completion issue |
|---|---|---|
| Mesh / brush / patch / terrain root | Owns authored geometry | Operation eligibility must be consistent by kind. Terrain can Copy but its omission from Move eligibility also blocks ordinary Delete/Duplicate. Delete must not require movement support. |
| Geometry owner | Entity to which geometry belongs | New Boolean/fragment results must inherit intended owner and layer. |
| Entity parent | Attachment relationship; descendants follow edits | Duplicate remaps relationships, but clipboard does not share that policy. |
| Group | Persistent organizational membership, including nested groups | Not a transform node or full enter/exit editing scope. Replacement geometry needs membership lineage. |
| Selection set / visgroup | Reusable selection or visibility membership | Does not override layer locks; replacement topology needs an explicit membership policy. |
| Layer | Storage/organization, visibility, locking and creation destination | Lock enforcement cannot exist only in direct viewport picking. |
| Prefab instance | Linked external asset with placement and overrides | Copy/Cut eligibility, scale, edit-scope membership, failure-safe file publication and export need repair. |
| Modifier stack | Evaluated result derived from a source mesh cage | Coordinate frame, composition with object transforms, join/Boolean behavior and export failures must be defined. Arrays here are mesh modifiers, not general arrays of object hierarchies. |

**D1 — Highest priority: parent Cut/Paste does not preserve the copied graph.**

**Update, 2026-10-15:** repaired in the local hierarchy-clipboard checkpoint. Recursive Copy/Cut, fresh parent/owner remapping, cross-map placement, persistence, undo/redo and actual Mason shortcut routing now have targeted regressions. See [checkpoint details](mason_geometry_workflow_research.md#hierarchy-safe-portable-clipboard--2026-10-15). The original finding below records the pre-fix behavior; broader assembly gate G6 remains open.

Clipboard copying expands entity-owned geometry but not attached child entities. Delete expands both ownership and the recursive parent hierarchy. Cut copies first, then invokes that broader Delete. Selecting only parent A can therefore delete B and C without placing them in the clipboard. If parent and child are explicitly copied together, Paste copies the old `parent` field without remapping it to the newly allocated parent ID. Same-map paste can attach to the original parent; cross-map paste can leave an orphan or wrong reference. Undo is a recovery mechanism, not proof that Cut/Paste is correct.

Evidence: `Core/CypherMap_Clipboard.cpp:297–305,405–407`; `Core/CypherMap_Edit.cpp:140–151,670`; `Gui/CypherMapGui_Edit.cpp:685–694`. Existing hierarchy tests exercise Duplicate; clipboard tests exercise geometry ownership. Their individual success does not cover this combination.

Acceptance: A→B→C, each owning geometry; select only A, Cut/Paste in Place, then cross-map paste with colliding old IDs. Every intended object and relationship must survive with fresh identities. Failures must preserve both the source document and prior clipboard.

**D2 — Locked layers can be bypassed by indirect selection.**

**Implementation checkpoint, 16 October:** the indirect selection defect and
hierarchy-expanded root edits are repaired locally. A batch containing any
locked object is rejected as a whole; Groups, saved sets and Outliner share this
rule. Outliner rows resynchronize after refusal, and component admission cannot
convert a locked brush. Move/Rotate/Scale/Delete/Duplicate/Cut check owned geometry
and attached descendants. Copy stays read-only; Undo/Redo restores geometry but
clears a historical root batch containing a currently locked object. Actual lock
changes invalidate captured parent drags, including lock then unlock before
release. Failed target collection preserves selection, history and authored data.

Seven new lock cases pass on native Cocoa; the full Map GUI suite passes 737
cases / 156,481 assertions. See [checkpoint details](mason_geometry_workflow_research.md#layer-lock-selection-and-hierarchy-protection--2026-10-16).
The dedicated face Extrude follow-up now refuses locked presses, binds capture
to its actual admitted face set and cancels lock/unlock or view/context changes.
It also refuses failed Shift-add/replacement preparation without a fallback.
This is covered by real camera-event and allocation-refusal tests, not just
topology-kernel calls; see the [Extrude follow-up](mason_geometry_workflow_research.md#dedicated-extrude-capture-and-lock-safety--2026-10-16).

Paint now checks its explicit target, refuses locked brush/mesh presses and
retires active strokes on lock or visibility changes. Ten native camera-event
cases also cover safe conversion, cancellation and publication refusal; see the
[Paint follow-up](mason_geometry_workflow_research.md#atomic-paint-strokes-and-capture-safety--2026-10-16).

Loop Cut now refuses locked brush and mesh targets before conversion/cutting;
the [Loop Cut checkpoint](mason_geometry_workflow_research.md#atomic-loop-cut-and-direction-correct-rings--2026-10-16)
includes native refusal and retry checks. This does not establish the write
targets of the independent tools listed below.

D2 is **not universally closed**: commands that target unselected objects and
modeless/captured explicit targets need separate write-target checks. Carve,
map-wide material replacement, Displace/sculpt and entity I/O
are concrete follow-ups. Generic edit callbacks do not declare their write set,
so guarding only the current selection there would be insufficient. The original
finding below records the pre-fix behavior; assembly gate G6 remains open.

Direct viewport picking checks locking, but Groups can insert all members into selection. Selection sets and Outliner routes also need the same policy. Ordinary Objects/Groups edit gates do not enforce locking for those indirectly selected roots. A locked object can consequently participate in a group edit.

Evidence: `Gui/CypherMapGui_Workspace.cpp:1115–1128,1546`; `Gui/CypherMapGui_GeometryCommands.cpp:2792`; `Gui/CypherMapGui_Edit.cpp:334`.

Acceptance: group across unlocked and locked layers; select through every route; Move/Delete must either reject the whole operation clearly or consistently restrict it to editable members. Neither selection route nor keyboard shortcut should silently change this rule.

**D3 checkpoint — 2026-10-17:** whole-object modifier transforms now preflight
all expanded roots. Representable transforms update retained stages; unsupported
ones refuse explicitly before live publication, rather than silently deforming
the evaluated shape. Signed-axis, custom-pivot, unknown-stage, mixed-root,
Undo/Redo, persistence and allocation refusal cases pass. Arbitrary modifier
frames/baking remain outside this fix. See the
[authoring-preservation checkpoint](mason_geometry_workflow_research.md#hollow-pipes-modifier-refusal-and-delivery-preservation--2026-10-17).
The original finding below records the pre-fix behavior.

**D3 — Rotating live modifiers can change the shape.**

The root transform rotates the cage. `MapModifiers_Transform` can represent a mirrored plane only when its normal remains axis aligned; otherwise it leaves the plane unchanged. Bend moves its origin but can retain old axes; Taper has a similar restriction. Thus the result can deform when the user intended a rigid whole-object rotation. Linear array steps and radial array direction/origin do transform; this is not a claim that all arrays fail ordinary rotation.

Example oracle: a small source near `(100,0,0)`, mirrored about X=0, rotated 45° around Z. Expected mirrored center is approximately `(-70.71,-70.71,0)`; reflecting the rotated cage in the unchanged X plane gives `(-70.71,+70.71,0)`.

Evidence: `Core/CypherMap_Edit.cpp:230–239`; `Core/CypherMap_Modifiers.h:66`; `Core/CypherMap_Modifiers.cpp:356–395`. This is source/mathematical evidence, not a new native reproduction.

Acceptance: compare “evaluate, then transform” with “transform source and modifier, then evaluate” at 15°, 45°, 90° and 180°. Use a local modifier frame, or explicitly reject/bake unsupported transforms. Also test signed quarter turns; the current axis mapper discards sign.

**D4 Scale checkpoint — 2026-10-17:** representable positive linked-instance
scales update both placement and local scale; shear/malformed inputs refuse
before any expanded target changes. Native custom-pivot gizmo, numeric controls,
preview lifetime, Undo/Redo, Save/Open and publication refusal/retry pass.
This validates placement rather than external asset bounds. Prefab clipboard
support remains open. See the
[measured inset/placement checkpoint](mason_geometry_workflow_research.md#measured-inset-linked-placement-and-publication--2026-10-17).
The original finding below records the pre-fix behavior.

**D4 — Prefab transforms and root-operation gates disagree.**

Prefab Scale is accepted, but the generic entity/prefab transform branch updates origin and returns for a non-Rotate operation; it does not update instance scale. Copy/Cut excludes prefab instances, although Duplicate supports them. Terrain support has the inverse mismatch described above. The operation matrix needs to be per operation and kind, not inherited from a broad “CanMove” predicate.

Evidence: `Core/CypherMap_Edit.cpp:269–284`; `Gui/CypherMapGui_Edit.cpp:347–368,567–612`. Acceptance: transform/copy/cut/delete/duplicate each object kind individually and in mixed selections; verify prefab dimensions after save/reload and Collapse.

**D5 checkpoint — 2026-10-17:** Hollow adopts each brush's own replacement
fragments into the original nested group, preserving owner/layer, group identity
and root/face surfacing. The two-brush/distinct-group batch, private publication,
retry, Undo/Redo and Save/Open checks pass. Per-member entry comments and the
separate Join/other replacement policies still need follow-up; D5 is not closed
universally. The original finding below records the pre-fix behavior.

**D5 — Geometry replacement can discard object meaning.**

CSG Split allocates a new piece without carrying the original owner/layer into its placement record. SeparateShells already does preserve both and is a useful precedent. Hollow replaces a brush with new roots but group pruning does not automatically adopt those replacements. Join combines raw cages and retains the survivor's modifier stack; donor stacks disappear and the retained stack now affects the combined cage. These need deliberate policies, not accidental defaults.

Evidence: `Core/CypherMap_TopologyEdit.cpp:1547,1629–1631,1815–1820,1862–1865`; `Core/CypherMap_Document.cpp:1102–1110`; `Core/CypherMap_BrushEdit.cpp:245`; `Core/CypherMap_Groups.cpp:284`.

Acceptance: split owned geometry on a named layer; hollow a grouped brush; join two differently modified meshes. Preserve the intended ownership and memberships, and either operate on evaluated geometry or clearly refuse incompatible modifier stacks. Save/reload and undo must retain that meaning.

**D6 checkpoint — 2026-10-17:** failed/unsupported enabled modifier evaluation
now refuses OBJ instead of falling back to the cage; generated parts use private
IDs. GUI resolves transformed prefab contents privately and refuses missing
sources. Core explicitly counts unresolved instances and terrain. Existing files
survive preparation failure, with truthful partial-output feedback if the second
file commit fails. OBJ/MTL are not a pair-atomic transaction. Native export and
allocation/persistence cases pass. Other compiler interchange is not newly
established. The original finding below records the pre-fix behavior.

**D6 — OBJ export can silently omit or substitute visible geometry.**

OBJ iterates meshes and patches after private brush conversion. It does not collapse prefab instances, unlike Compile, and does not count those instances as skipped. Failed modifier evaluation/build falls back to the source cage while the command describes modifiers as applied. Terrain is explicitly counted as skipped. Export needs to distinguish unsupported content from failed evaluation and must not silently substitute a different shape.

Evidence: `Core/CypherMap_Export.cpp:112–164`; `Gui/CypherMapGui_GeometryCommands.cpp:1269,1354–1368`.

Acceptance: export a mesh, modified mesh, transformed prefab and terrain. Supported output matches evaluated geometry; unsupported content is explicitly reported; evaluation failure refuses publication rather than exporting the cage as success.

**Architecture to preserve and strengthen**

The shared geometry layer already provides persistent source identities, topology validation, attribute-transfer operations, boundary/bridge/bevel machinery, subdivision and independent corner normals. Map/GUI transactions already support private edits and undo publication. Reuse those foundations.

Keep these boundaries explicit:

```text
viewport hit + component selection + tool intent
    -> constrained candidate operation
    -> geometry result + identity/attribute/replacement mappings
    -> map ownership/layer/group/reference propagation
    -> one undoable publication + next component selection
    -> viewport feedback, save/load and export
```

Most reported failures are between these steps. A correct mesh alone does not imply correct clipboard contents, retained selection, ownership or export. A robust library function alone does not provide preview, cancellation, useful property controls or a repeatable gesture.

**A bounded geometry-authoring completion gate**

The goal is dependable map construction. It is not necessary to claim every feature of a general-purpose modelling package before renderer integration. The following projects exercise the requested everyday work without relying on command counts:

| Gate | Mapping task | Required observable result |
|---|---|---|
| G1 — Room blockout | Create room, clip door/window, resize several walls, align grid16 geometry while current grid is64 | Correct scope, alignment and capping; no unrelated movement; preserved surfaces and ownership. |
| G2 — Curved corridor | Quad/profile → boundary Extend → offset pivot → Rotate-Extend15° → repeat twice → straight extension → bridge/cap | Continuous topology; repeat targets the new rim; same active rim after Undo/Redo; normal mouse/keyboard selection. |
| G3 — Pipes | Straight hollow pipe; solid curved cable; 90° hollow elbow; S-bend | Explicit solid/hollow choice, measurable radius/wall, visible passage, stable frame, consistent UVs, no accidental end disks. Tight/self-intersecting bends refuse safely. |
| G4 — Panel and frame | Adjacent faces → measured inset → region extrusion → bevel trim | Constant border width and intentional region/individual semantics; no duplicate internal walls; useful live preview. |
| G5 — Mesh repair | Merge to target, Connect pair, Dissolve, pick corners to fill missing face, Bridge rims | Predictable targets, unsupported topology explained before commit, unchanged unrelated faces and surfacing. |
| G6 — Assembly | Nested groups, entity hierarchy, locked layer, linked prefab; Copy/Cut/Paste/Duplicate and transforms | Correct recursive object graph, remapped references, respected locks, consistent scale and group membership. |
| G7 — Procedural shapes | Sphere, stairs, lathe, mirrored/bent mesh, subdivision; rotate and join | Valid silhouettes; rigid root rotation; modifier semantics preserved or explicitly baked/refused; usable shading feedback. |
| G8 — Delivery | Save/reopen; OBJ and compiler interchange; inspect diagnostics | Geometry, UVs, normals, ownership and instances agree with the authored result; no silent drops or fallback shapes. |

Every gate also needs preview/commit agreement, Escape/focus-loss cancellation, meaningful undo/redo, save/reopen continuation, and allocation-failure checks at fallible publication boundaries. New regression cases should target missing combinations, not mirror an implementation or inflate assertion counts.

Recommended order: repair **D1** and mutation/publication safety first; fix shared interaction/selection/repeat; complete the curved-corridor workflow; add region inset/extrusion and the hollow sweep; complete object-kind/relationship propagation and export; then use the same acceptance scenes to integrate and measure the renderer. Standalone algorithm fixes and renderer integration remain separate milestones. Smooth-shading inspection does depend on the viewport rendering work, so “finish every visual editor feature before rendering” is not a workable strict dependency.

**Validation performed for this follow-up**

Reused the existing `build-mason` binaries. No dependency installation, additional build tree, Mason rebuild, implementation change or live-map manipulation occurred. The audited Geometry/Map Core source and test files were not newer than their corresponding binaries; this timestamp check is limited provenance, not a replacement for a future clean CI build.

| Run | Result | What it establishes |
|---|---|---|
| `cypher_editor_geometry_contract_tests` | Passed: 1,271 cases, 374,675 assertions; 32.79s under CTest | Existing library contracts and their supported domains. |
| Full `cypher_editor_map_core_contract_tests` under CTest | **Timed out at 120.06s**, configured limit 120s | Full suite is not green from this run; no completion/whole-suite pass is claimed. |
| Focused existing Map Core tests for creation, topology, modifiers, hierarchy, clipboard and sphere roundness, excluding allocation/OOM tags in this focused run | Passed: 65 cases, 4,950 assertions | Existing covered scenarios pass; does not test the newly identified combinations above. |

The focused run is recorded in `build-mason/artifacts/mason_geometry_audit_core.log`. CTest details are in `build-mason/Testing/Temporary/LastTest.log` until another CTest run replaces them. Source-confirmed findings D1–D6 and the newly identified capability gaps still need dedicated regressions and native interaction verification. No assertion that all tools work perfectly follows from these passes.

**Later completion-gate update — 2026-10-17:** G4 now has measured-width Inset
for one planar convex authored face, with preserved surfacing, undo/persistence
and refusal checks. It still lacks the proposed connected-region/live-preview
workflow. D5 Hollow now also preserves individual group-member comments through
sorted saving; other replacement/Join policies remain independent. Native Select
reliability and prefab-scale checks are included in the 28-case Cocoa run; the
full local Map GUI 838 and Mason 71 suites pass. The inherited brush command
adapter remains a concrete transaction defect: it publishes conversion and the
operation separately, and refusal rollback may discard Redo or fail. The new
width command requires an authored mesh and does not enter that adapter. The
source publication does not mean every gate is complete.
