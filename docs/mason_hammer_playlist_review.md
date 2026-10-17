# Mason: requested Hammer playlist review

Review date: 15 October 2026. Reference: Eagle One Development Team's [Source 2 101 — Hammer Crash Course Tutorials](https://www.youtube.com/playlist?list=PLHSLq5FjjRw0mC-roui0E04GOl9Dnf-Yd), supplied by the user. All 47 public lesson titles and watch links were enumerated from the playlist UI. This is the requested playlist, not a substitute collection.

## Coverage and evidence limits

**The full playlist review is not complete.** Four complete, auto-generated English transcripts were retrieved and read: lessons 1, 4, 28 and 35. The transcript exporter reported no available transcript for 42 other lessons. Lesson 20's page readiness check timed out before export. These are retrieval results in this session, not proof that captions can never exist or that the videos are unavailable to the user. Repeated attempts on lesson 19 also left the visible transcript panel loading; lesson 23's player explicitly said captions were unavailable.

Lesson 23 was additionally inspected at the actual 4:54 player frame. Lesson 4's rotation frames at 3:59, 4:16 and 4:48 were inspected in the preceding audit and are recorded there. Neither is a claim of complete visual viewing. Video titles and creator descriptions establish topics, not complete operation semantics. Viewer comments are not used as technical evidence.

The small local inventory and four transcript exports are in `build-mason/artifacts/hammer_playlist_audit/`. They are research artifacts, not engine dependencies. No video media was downloaded, no dependency installed, and no live Mason document was manipulated during this research.

Evidence labels below have distinct meanings:

- **Transcript:** creator narration was read in full; automatic captions can misrecognize keys or omit a visual prerequisite.
- **Visual sample:** the named frame was actually inspected; actions outside that frame are not inferred as observed.
- **Code finding:** a separate Mason source review established the implementation behavior; this does not prove native UI usability.
- **Proposed contract:** an engineering requirement inferred from the demonstration and the user's workflow. It remains subject to implementation and acceptance testing.
- **Retrieval limited:** a bounded attempt failed. The lesson remains pending review, even though its title is known.

## What the accessible lessons establish

### 1 — Good workflow habits

At [0:39–1:33](https://www.youtube.com/watch?v=pdSDojRatHw&t=39s), the creator makes a block, commits it and reverses its faces into a room interior. At 2:42–4:09, grid changes and cuts support deliberate polygon layout. At [4:19–4:55](https://www.youtube.com/watch?v=pdSDojRatHw&t=259s), subdivision, edge bevel and a pulled face form an opening and hallway. At 5:42–6:18, a mirror operation duplicates a room and selected connecting faces are deleted.

**Proposed contract:** room construction should flow between primitive creation, component editing, topology changes and mirroring without losing material assignments or needing whole-object workarounds. Face winding must be explicit. The creator's advice about room-sized objects and cubemaps is advice for that Source 2 workflow; it is not a universal Cypher object-layout rule. This lesson informs gates G1, G4 and G8 from the existing audit.

### 4 — Arches and Rotation manipulation

At [0:31–0:46](https://www.youtube.com/watch?v=pl6EbpXoH0Q&t=31s), the narration selects an edge, invokes the arc tool and commits an arch. Captions alone do not establish the initial edge's exact topology. At 0:51–1:29, boundary selection, filling and extrusion build the frame. At 1:40–2:50, isolation, extraction and connecting vertices clean its topology.

The key sequence is [3:26–4:51](https://www.youtube.com/watch?v=pl6EbpXoH0Q&t=206s): select the boundary rim, move the pivot, rotate with extrusion in 15-degree steps, repeat with Shift+G and then extend straight. The prior audit's sampled frames corroborate the displaced pivot and curved strip.

**Proposed contract:** retain the pivot, axis, angle and extension intent as an operation. Each repetition must target the newly created rim and preserve connected faces. Repeating a command that merely activates Rotate cannot reproduce this sequence. Gate G2 is the acceptance project, including Undo/Redo followed by another extension.

### 28 — Work Space Plane and Smoothed Archways

At [1:38–2:18](https://www.youtube.com/watch?v=LFR29A53dcU&t=98s), a workplane is aligned with angled architecture and the orthographic view/grid follows it. The narrator deliberately picks floor near the wall. At 2:19–2:40, extraction, isolation, cutting and beveling prepare the arch. At 3:13–3:24, Enter commits the arc. At [3:26–3:59](https://www.youtube.com/watch?v=LFR29A53dcU&t=206s), two edges are bridged before a closed boundary can be filled; extraction separates the resulting frame.

At [4:02–5:32](https://www.youtube.com/watch?v=LFR29A53dcU&t=242s), a selected arch contour receives successive offsets and extrusions, followed by face cleanup, smoothing and UV work. At 6:11–6:27 the workplane resets.

**Proposed contract:** a workplane needs a complete orientation, not only a normal or pivot point. The contour operation must change the profile appropriately rather than translate every selected vertex by one averaged vector. Bridge, fill, extraction and selection must agree on connected topology. Gates G2, G4 and G5 apply.

### 35 — S Curves

At 0:16–1:25, repeated edge selection and extension lay out terrain. The central sequence at [2:21–4:23](https://www.youtube.com/watch?v=P5mrF1aZ4Mo&t=141s) uses interpolated bridging to join sections with curved transitions. The narration relates different curve shapes to the orientation of their endpoints. Captions alone do not specify the interpolation formula or a universal rule for all parallel boundaries.

At [5:21–5:39](https://www.youtube.com/watch?v=P5mrF1aZ4Mo&t=321s), connected faces are selected, extracted and thickened. Later editing deals with back faces, visibility and UV alignment.

**Proposed contract:** bridge should expose endpoint correspondence, segment count and controlled curved interpolation, with useful preview. Connected-region extraction/thickening must preserve the intended skin and attributes. Linear interpolation between vertices is a valid basic bridge but cannot produce the demonstrated tangent-controlled transition by itself. Gates G3, G4, G5 and G8 apply.

### 23 — Pipe Tile Meshes: limited visual evidence

The creator's visible description identifies basic, intermediate and advanced pipe placement. The actual [4:54 frame](https://www.youtube.com/watch?v=YoSdiNLyZuI&t=294s) shows a pipe end/control edge selected with a transform gizmo; the surrounding pipe has rendered surface detail. This establishes that the lesson exposes editable controls associated with the pipe placement. It does **not** establish from that single frame how junctions, hollow topology, automatic corners or arbitrary curves are generated. Those operations remain pending review. A tile-mesh placement workflow must not be equated automatically with Mason's ordinary single-profile sweep.

## Concrete comparison with Mason

This table combines the primary evidence above with the separate source review performed during this checkpoint. Source anchors describe the review snapshot and may move as changes land. The [existing comprehensive audit](mason_tool_workflow_audit.md) remains the source of the wider tool inventory and failure findings.

| Reference expectation | Source review finding | Consequence / required proof |
|---|---|---|
| Lesson 4's narrated edge-to-arc step | `CypherMapGui_ToolGestures.cpp:3695` requires an eligible multi-edge chain, with subdivision as a preparatory route. | Describe Mason's actual precondition. Do not claim single-edge parity until the visual input and an end-to-end test establish it. |
| Lessons 4, 28 and 35 use rapid boundary/connected selection | The pre-change double-click path in `CypherMapGui_Views.cpp:7224` opened the inspector. | A boundary seed must resolve one connected rim, not every boundary of the mesh. The local implementation checkpoint now adds this selection path: the real viewport regression passes 760 assertions in Select/Translate/Rotate/Scale across Camera and Top. See implementation status below for publication and validation limits. |
| Lesson 4 repeats the last rotation/extrusion on the new rim | `CypherEditorGui_CommandHistory.cpp:131` repeats command invocations; a mouse transform's pivot, angle and extension intent are not the recorded modelling operation. | Add semantic repeat after the interaction checkpoint. Undo data and repeat data have different purposes. |
| Lesson 28 aligns construction and orthographic editing with angled walls | The existing workplane representation/consumers do not carry the demonstrated complete frame consistently; orthographic editing does not use it as that rotated grid. | Store/consume origin plus orthonormal axes; distinguish workplane, gizmo basis and pivot. Test creation, snapping and navigation together. |
| Lesson 28 offsets a selected arch contour | `CypherMap_TopologyEdit.cpp:673` uses a shared average displacement for the reviewed path. | A contour offset needs local geometry and join/collapse policy. Average translation does not establish that feature. |
| Lesson 35 creates interpolated S transitions | `CypherGeometry_MeshBridge.cpp:74` linearly interpolates positions. | Curved bridge requires tangent/orientation controls and validation; exposing more linear segments is insufficient. |
| Lessons 28/35 combine extraction and thickening | Existing extraction copies selected faces and Solidify supports a restricted disk-like open domain. | Clarify copy/extract/move semantics. General connected-region and holes/annular cases remain separate work, not a button-only task. |

The map GUI files above live in `src/CypherEditor/Workspaces/Map/Gui/`; topology is in `src/CypherEditor/Workspaces/Map/Core/`; mesh bridge is in `src/CypherEditor/Geometry/Mesh/`. Command history lives in the editor GUI subsystem. These are code-review observations, not claims that every cited workflow was exercised in the running editor.

## Implementation status from this review

Two bounded local milestones are delivered and recorded in both changelogs:

- **Hierarchy-safe clipboard:** recursive parent/child ownership survives Copy,
  Cut, Paste, Undo/Redo and saved reload. The focused Core, workspace and actual
  Mason keyboard routes passed 34 cases / 6,562 assertions. This addresses audit
  D1, not all of assembly gate G6.
- **Seeded boundary double-click:** a clicked open boundary resolves only its
  closed rim, with Shift add and Ctrl/Command toggle, across Select/Translate/
  Rotate/Scale in 3D and Top. The new viewport regression failed on old production
  and now passes 760 assertions; the geometry selection-query suite passes 9
  cases / 495 assertions. The library-only query is published in commit
  `065a274c5fe1d363d974436d21844046ca975fc9`. The local GUI uses that query but is
  not included in that commit. The final complete Map GUI suite also passed
  700 cases / 143,128 assertions, followed by the relinked Mason keyboard case
  (226 assertions). Mason was rebuilt and its local signatures verified.

GUI/input validation uses actual Qt widget events on the offscreen platform;
there is no claim of native clipboard interoperability or full interactive
playlist parity. A failed boundary query preserves selection at entry to the
second-click handler; the ordinary first click is not rolled back. The local
editor changes rely on unpublished V11/hierarchy prerequisites and were not
swept into the library commit. See `mason_geometry_workflow_research.md` and the
ignored validation artifacts for the precise checkpoints.

## Acceptance gates retained from the audit

The playlist is useful because it supplies projects that cross tool boundaries. It does not make a tool-name checklist sufficient. Preserve the eight gates already agreed in [the geometry audit](mason_tool_workflow_audit.md):

| Gate | Required result | Relevant verified evidence |
|---|---|---|
| G1 — Room blockout | Room, door/window cuts, multi-wall resize, mixed-grid alignment; preserve scope and surfaces. | Lesson 1's interior room and hallway sequence. |
| G2 — Curved corridor | Boundary Extend → offset pivot → Rotate-Extend → repeat → straight continuation → bridge/cap; stable rim after Undo/Redo. | Lesson 4; workplane setup from 28. |
| G3 — Pipes | Explicit straight hollow pipe, solid curved cable, hollow elbow and S-bend with meaningful wall/radius controls. | Lesson 35 informs curved transitions; lesson 23 needs further review before claiming tile-pipe parity. |
| G4 — Panel/frame | Measured inset, connected-region extrusion, bevel trim without duplicate internal walls. | Lessons 1 and 28 demonstrate linked component operations; exact Mason algorithms need tests. |
| G5 — Mesh repair | Targeted Merge/Connect/Dissolve, face construction and Bridge with predictable selection and attributes. | Lessons 4 and 28 supply connect/bridge/fill workflows. Lesson 30's detailed seam workflow remains pending. |
| G6 — Assembly | Correct hierarchy, groups, locked layers, prefab and clipboard behavior. | Existing source audit; no new playlist parity claim. The hierarchy clipboard checkpoint is separately tested. |
| G7 — Procedural shapes | Valid shapes and modifier transformations that preserve intent or explicitly bake/refuse. | Lesson 4's arc is relevant; sphere, spiral and modifier parity are not proved by the four transcripts. |
| G8 — Delivery | Save/reopen/export preserves geometry, UVs, normals, ownership and instances without silent loss. | Cross-cutting engineering requirement; playlist narration is not a substitute for persistence/export tests. |

The immediate modelling sequence to close after seeded rim selection is semantic repeat of a committed boundary transform. Further work should remain small: shared picking/gesture cancellation, repeated custom-pivot extension, connected-region operations, workplane orientation, interpolated bridge and hollow sweeps. Each must pass actual viewport input tests, history, persistence and allocation-failure checks where material. Runtime lighting, physics decoration and asset simulation lessons belong to later subsystem work; their presence in the playlist does not establish those Cypher capabilities.

## Complete playlist inventory and review status

**T:** full transcript read. **L:** transcript retrieval limited; lesson content remains pending. **P:** page readiness timed out before export. Visual samples are recorded explicitly. Titles below identify lessons only; unreviewed titles are not treated as demonstrated features.

| Lesson | Title / original video | Transcript | Visual evidence |
|---|---|---|---|
| 1 | [Good workflow habits](https://www.youtube.com/watch?v=pdSDojRatHw) | T | Not reviewed |
| 2 | [Angles and elevation](https://www.youtube.com/watch?v=aD2eZurIfuM) | L | Not reviewed |
| 3 | [Adding interior domes and windows](https://www.youtube.com/watch?v=DbFU9YGDuDM) | L | Not reviewed |
| 4 | [Arches and Rotation manipulation](https://www.youtube.com/watch?v=pl6EbpXoH0Q) | T | Prior audit: 3:59, 4:16, 4:48 |
| 5 | [Quick thicken and instances](https://www.youtube.com/watch?v=14eqVvuCDk0) | L | Not reviewed |
| 6 | [The great outdoors and skybox VIS basics](https://www.youtube.com/watch?v=OUr7KvKn5zs) | L | Not reviewed |
| 7 | [Environment light and VIS made EASY!](https://www.youtube.com/watch?v=MHphyhQfz2Y) | L | Not reviewed |
| 8 | [Light Probe Volumes and Cubemaps!](https://www.youtube.com/watch?v=BfGhpZHXOoc) | L | Not reviewed |
| 9 | [Preview lighting and compile types!](https://www.youtube.com/watch?v=oDrOAfqFiNQ) | L | Not reviewed |
| 10 | [Lighting bread and butter!](https://www.youtube.com/watch?v=efmoWv7Pstg) | L | Not reviewed |
| 11 | [God rays make everyone's day!](https://www.youtube.com/watch?v=w-8knfQj_5o) | L | Not reviewed |
| 12 | [Material Basics!](https://www.youtube.com/watch?v=rLn8DjIG0HM) | L | Not reviewed |
| 13 | [Blended materials](https://www.youtube.com/watch?v=bARsEdpBx9Q) | L | Not reviewed |
| 14 | [UV Peel those curves](https://www.youtube.com/watch?v=4fe78nuqibw) | L | Not reviewed |
| 15 | [Fast and furious texturing!](https://www.youtube.com/watch?v=VRgvXelEIBE) | L | Not reviewed |
| 16 | [Terrain Displacement](https://www.youtube.com/watch?v=r4b3uo07V88) | L | Not reviewed |
| 17 | [Cave and Ridgeline creations!](https://www.youtube.com/watch?v=wlsU48xqYaM) | L | Not reviewed |
| 18 | [Seamless schmexy blending](https://www.youtube.com/watch?v=WCLVkhMgr88) | L | Not reviewed |
| 19 | ["Carving" and Bridging](https://www.youtube.com/watch?v=WSrVW85gA7c) | L | Not reviewed |
| 20 | [Breaking bricks badly](https://www.youtube.com/watch?v=ABd7Yo5G2hw) | P | Not reviewed |
| 21 | [Smooth Soft normals](https://www.youtube.com/watch?v=nd05fNbEcRU) | L | Not reviewed |
| 22 | [Architecture Ascension 1](https://www.youtube.com/watch?v=8N5h-PMciKU) | L | Not reviewed |
| 23 | [Pipe Tile Meshes](https://www.youtube.com/watch?v=YoSdiNLyZuI) | L | 4:54 sample only |
| 24 | [Girder Tile Meshes](https://www.youtube.com/watch?v=f554eF2oULk) | L | Not reviewed |
| 25 | [Ventilation Tile Meshes](https://www.youtube.com/watch?v=yVTw1vJxZ0w) | L | Not reviewed |
| 26 | [Cable Classic Management](https://www.youtube.com/watch?v=j1xya_qWR9A) | L | Not reviewed |
| 27 | [Cable Tile Meshes](https://www.youtube.com/watch?v=jPH3mj-odxQ) | L | Not reviewed |
| 28 | [Work Space Plane and Smoothed Archways](https://www.youtube.com/watch?v=LFR29A53dcU) | T | Not reviewed |
| 29 | [Build Brick Buildings](https://www.youtube.com/watch?v=_fdx5DCGpXY) | L | Not reviewed |
| 30 | [Seam Sew and Stitch](https://www.youtube.com/watch?v=HH-rvtK25Ns) | L | Not reviewed |
| 31 | [Planting your roots](https://www.youtube.com/watch?v=3bdgIr3ZKnk) | L | Not reviewed |
| 32 | [Asset Spray Tool](https://www.youtube.com/watch?v=c68uUv0MfK8) | L | Not reviewed |
| 33 | [Foliage Fakery and Chipped Meshes](https://www.youtube.com/watch?v=LA2wPtJU614) | L | Not reviewed |
| 34 | [Frame your Terrain](https://www.youtube.com/watch?v=Z1S6sLm-z74) | L | Not reviewed |
| 35 | ["S" Curves](https://www.youtube.com/watch?v=P5mrF1aZ4Mo) | T | Not reviewed |
| 36 | [Canal Creation](https://www.youtube.com/watch?v=YdR5Q7t5uOw) | L | Not reviewed |
| 37 | [3D EZ Skyboxes](https://www.youtube.com/watch?v=IOVo70sgBvM) | L | Not reviewed |
| 38 | [Physics and Cloth Simulation Decoration](https://www.youtube.com/watch?v=tV7kFqPN2qQ) | L | Not reviewed |
| 39 | [Catwalk Tile Meshes](https://www.youtube.com/watch?v=hEddJMCN7XU) | L | Not reviewed |
| 40 | [Trim Textiles and Overlays](https://www.youtube.com/watch?v=zkE6YsuON9A) | L | Not reviewed |
| 41 | [VIS Quick Fix Sealing](https://www.youtube.com/watch?v=NEScQVMJvYk) | L | Not reviewed |
| 42 | [Wood Workin'](https://www.youtube.com/watch?v=v-LQ2XiTMKE) | L | Not reviewed |
| 43 | [Concrete Tile Meshes](https://www.youtube.com/watch?v=9Dc7lSRnPOQ) | L | Not reviewed |
| 44 | [Makeover Material Override](https://www.youtube.com/watch?v=7aGcw73KDTs) | L | Not reviewed |
| 45 | [Splice & Dice](https://www.youtube.com/watch?v=m2tfgJ8k-DM) | L | Not reviewed |
| 46 | [Destruction Construction](https://www.youtube.com/watch?v=ok9e0I4XSkk) | L | Not reviewed |
| 47 | [Spiral Staircases](https://www.youtube.com/watch?v=S-7njGeJEOE) | L | Not reviewed |

All retrieval-limited entries remain open research work. Reusing these findings must preserve the difference between a complete inventory, four complete transcript reviews, limited visual samples and a complete playlist review.
