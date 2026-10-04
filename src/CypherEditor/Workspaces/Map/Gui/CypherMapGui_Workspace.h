//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Workspace.h
//  Purpose: Declares the Map workspace's editing session: the open map,
//           its wireframe, selection, grid, active tool, and the `map.*`
//           commands, shared by every view and panel of the workspace.
//  Details: Views and panels never talk to each other. Each one reads the
//           session and registers a listener; whoever changes the session
//           calls MapWorkspace_Notify with what changed, and every listener
//           refreshes what it shows. Adding a panel therefore touches no
//           existing panel.
//
//           Authored geometry edits use validated private documents and one
//           history entry. Commands whose backend is unavailable stay disabled.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_WORKSPACE_H
#define CYPHER_EDITOR_MAP_GUI_WORKSPACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"
#include "CypherMap_Edit.h"
#include "CypherMap_BrushEdit.h"
#include "CypherMap_Files.h"
#include "CypherMap_Wireframe.h"
#include "CypherGeometry_MeshSelection.h"
#include "CypherCommon/Mathlib/CypherMath_Affine3.h"

#include "CypherEditor_History.h"
#include "CypherEditor_Selection.h"
#include "CypherEditorGui_Application.h"

#include <QImage>
#include <QHash>
#include <QString>
#include <QStringList>

namespace cypher::editor::map
{

inline constexpr const char *MAP_WORKSPACE_DEFAULT_GAME = "reap"; // Until projects name the game profile.
inline constexpr common::f64 MAP_GRID_MIN = 1.0;
inline constexpr common::f64 MAP_GRID_MAX = 4096.0;   // editor.grid.size's limit (CYSETTINGS.md 4.5).
inline constexpr common::f64 MAP_GRID_DEFAULT = 16.0;  // editor.grid.size's default.
inline constexpr common::usize MAP_WORKSPACE_MAX_LISTENERS = 32u;

enum class map_tool_t : common::u8 {
    SELECT = 0u,
    CAMERA,
    ENTITY,
    BLOCK,
    TEXTURE,
    DECAL,
    OVERLAY,
    CLIP,
    VERTEX,
    PATH,
    MEASURE,
    TERRAIN,
    PATCH,
    TRANSLATE, // Hammer 5's transform tools, beside Select.
    ROTATE,
    SCALE,
    PIVOT,
    POLYGON,
    MIRROR,
    PAINT,
    EXTRUDE,    // Push/pull faces.
    KNIFE,
    LOOP_CUT,
    EYEDROPPER, // Pick a surface's material.
    WORKPLANE,
    CURVE,      // Curve networks and sweeps.
    NONE,       // Navigation only; retain inspection context without an editing tool.
    COUNT
};

// What a click selects (Hammer 5's selection modes on the top toolbar).
enum class map_element_mode_t : common::u8 {
    VERTICES = 0u,
    EDGES,
    FACES,
    MESHES,
    OBJECTS,
    GROUPS,
    NAVIGATION,
    COUNT
};

// Hammer 5's Auto Vis Groups: every object falls in one category by what it
// is, and a category can be hidden for the session without touching the map.
enum class map_visgroup_t : common::u8 {
    BRUSHES = 0u,
    MESHES,
    PATCHES,
    TERRAIN,
    TIED,           // Geometry tied to an entity (triggers, doors, detail).
    LIGHTS,
    INFO,           // info_* markers: spawns, targets, nodes.
    TRIGGERS,
    PROPS,
    OTHER_ENTITIES,
    COUNT
};

enum map_change_flags_t : common::u32 {
    MAP_CHANGE_NONE = 0u,
    MAP_CHANGE_DOCUMENT = 1u << 0u,  // Another map, or its content changed: rebuild.
    MAP_CHANGE_SELECTION = 1u << 1u,
    MAP_CHANGE_VIEW = 1u << 2u,      // Grid, tool, or display options.
    MAP_CHANGE_TITLE = 1u << 3u,     // Path or modified flag.
    MAP_CHANGE_CURSOR = 1u << 4u,    // Cursor position under a 2D view.
    MAP_CHANGE_FRAME = 1u << 5u,     // Views should frame frameBounds.
    MAP_CHANGE_HISTORY = 1u << 6u    // An edit, undo, redo, or save changed the history.
};

enum class map_frame_target_t : common::u8 { ALL = 0u, ORTHOGRAPHIC, PERSPECTIVE };

// Selection modes are the framework's; the short names read better here.
using map_select_mode_t = editor_select_mode_t;
inline constexpr map_select_mode_t MAP_SELECT_REPLACE = EDITOR_SELECT_REPLACE;
inline constexpr map_select_mode_t MAP_SELECT_TOGGLE = EDITOR_SELECT_TOGGLE;
inline constexpr map_select_mode_t MAP_SELECT_ADD = EDITOR_SELECT_ADD;
inline constexpr map_select_mode_t MAP_SELECT_REMOVE = EDITOR_SELECT_REMOVE;

using map_listener_fn = void ( * )( void *pContext, common::u32 changes );

struct map_listener_t {
    map_listener_fn pfnChanged{ nullptr };
    void *pContext{ nullptr };
};

// A clip line lies on a construction plane perpendicular to extrusionAxis.
// Its two editable anchors define a plane extending along that world axis.
struct map_clip_guide_t {
    common::bool_t bHas{ common::CY_FALSE };
    math::vec3d_t points[2]{};
    common::u32 extrusionAxis{ 2u };
};

enum class map_transform_preview_kind_t : common::u8 { NONE, TRANSLATE, SCALE, ROTATE };
// An immutable description of the current gesture, replayed over the live
// wireframe in every pane. No geometry is copied or allocated while dragging.
// Publication still uses the validated document edit and one undo snapshot.
struct map_transform_preview_t {
    map_transform_preview_kind_t kind{ map_transform_preview_kind_t::NONE };
    math::vec3d_t pivot{}, delta{}, factors{ 1, 1, 1 }, degrees{};
    common::bool_t bClone{ common::CY_FALSE };
    common::bool_t bResize{ common::CY_FALSE }; // Bounds resize, published through Scale geometry.
    common::bool_t bResizeFromCenter{ common::CY_FALSE }; // Captured gesture anchor; never authored map data.
    common::bool_t bResizeIndividually{ common::CY_FALSE }; // Equal signed delta, each root keeps its own anchor.
    math::vec3d_t resizeSides{}; // -1 minimum, +1 maximum, 0 unchanged; used by individual resizing.
};
struct map_preview_visibility_t {
    common::u64 hiddenRevision{};
    common::u32 hiddenVisgroups{};
    common::bool_t bCordonActive{ common::CY_FALSE };
    map_bounds_t cordon{};
};

// Transform previews publish on release. Block construction and clip planes
// remain staged until confirmation. Every pane draws the same private geometry.
struct map_edit_preview_t {
    common::bool_t bActive{ common::CY_FALSE };
    map_bounds_t bounds{};
    map_primitive_desc_t primitive{};
    map_status_t status{ map_status_t::OK };
    common::bool_t bClip{ common::CY_FALSE };
    math::planed_t clipPlane{};
    map_brush_clip_mode_t clipMode{ map_brush_clip_mode_t::BOTH };
    char clipMaterial[MAP_MATERIAL_PATH_MAX + 1]{};
    common::u64 documentRevision{}, selectionRevision{};
    map_clip_guide_t clipGuide{};
    map_transform_preview_t transform{};
    map_tool_t tool{ map_tool_t::SELECT };
    map_element_mode_t mode{ map_element_mode_t::OBJECTS };
    map_preview_visibility_t visibility{};
    common::bool_t bStagedBlock{ common::CY_FALSE };
    const map_document_t *pBlockDocument{ nullptr }; // Identity as well as revision guards new/open/undo.
    common::u32 blockDepthAxis{ 2u };               // The omitted axis of the original footprint pane.
    common::bool_t bBlockCommitFailed{ common::CY_FALSE }; // Private geometry is still valid and retryable.
    char blockMaterial[MAP_MATERIAL_PATH_MAX + 1]{};        // Captured with the primitive; never truncated.
    common::bool_t bBlockMaterialValid{ common::CY_FALSE };
};

// Images for material paths, from whoever owns the asset catalogue (Mason's
// asset browser). A null image means none: the panel draws a stand-in.
using map_material_image_fn = QImage ( * )( void *pContext, const QString &path );

struct map_workspace_t {
    map_workspace_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( map_workspace_t );
    ~map_workspace_t() noexcept;

    gui::editor_gui_t *pGui{ nullptr };
    map_document_t *pDocument{ nullptr };    // Owned; replaced by New and Open.
    map_wireframe_t wire{};
    editor_selection_t selection{};            // Selected object IDs.
    editor_selection_t hidden{};               // Objects hidden for the session (H, Shift+H; U shows them).
    editor_history_t history{};                // Undo history of the open map; cleared on New and Open.
    QString path{};                            // Root file; empty while untitled.
    QHash<QString, QStringList> persistedChunksByRoot{}; // Disk inventory; never restored by geometry history.
    common::f64 gridSize{ MAP_GRID_DEFAULT };
    map_tool_t tool{ map_tool_t::SELECT };
    map_element_mode_t elementMode{ map_element_mode_t::OBJECTS };
    common::bool_t bGridVisible{ common::CY_TRUE };
    common::bool_t bSnapToGrid{ common::CY_TRUE };    // editor.grid.snap
    common::bool_t bTextureLock{ common::CY_TRUE };   // editor.map.texture_lock
    common::f64 angleSnap{ 15.0 };                    // editor.grid.angle_snap; 0 disables.
    common::f64 scaleSnap{ 0.25 };                    // editor.grid.scale_snap; 0 disables.
    common::f64 lastAngleSnap{ 15.0 };                // Restore the last positive step when toggling on.
    common::f64 lastScaleSnap{ 0.25 };
    common::u32 hiddenVisgroups{ 0u };                // Bit n hides map_visgroup_t n in every view.
    math::vec3d_t cursor{};                    // World position under the last hovered 2D view.
    common::u32 cursorAxes{ 0u };              // Bit n: axis n of cursor is meaningful.
    map_bounds_t frameBounds{};                // Target of the last MAP_CHANGE_FRAME.
    map_frame_target_t frameTarget{ map_frame_target_t::ALL };
    map_listener_t listeners[MAP_WORKSPACE_MAX_LISTENERS]{};
    common::usize nListeners{ 0u };
    map_material_image_fn pfnMaterialImage{ nullptr };
    void *pMaterialImageContext{ nullptr };
    map_bounds_t cordon{};                     // NetRadiant's region, Hammer's cordon.
    common::bool_t bCordonActive{ common::CY_FALSE };
    map_edit_preview_t editPreview{};
    map_wireframe_t editPreviewWire{};          // Private construction geometry, shared by all panes.
    common::u64 selectedBrushFaceObject{ 0u };
    common::u64 selectedBrushFaceSide{ 0u };
    // One authored mesh face, addressed only by persistent source IDs.
    // Root selection remains separate; no live pool handle escapes picking.
    common::u64 selectedMeshFaceObject{ 0u };
    common::u64 selectedMeshFaceId{ 0u };
    // One authored mesh's vertices or edges, addressed only by persistent IDs.
    // Component modes never mix; edges use canonical endpoint pairs.
    geometry::mesh_selection_t meshSelection{};
    geometry::mesh_edge_ref_t selectedMeshEdgeSeed{};
};

// Starts a session with a new untitled map.
CYPHER_NODISCARD common::bool_t MapWorkspace_Init( map_workspace_t *pWorkspace, gui::editor_gui_t *pGui ) noexcept;
void MapWorkspace_Shutdown( map_workspace_t *pWorkspace ) noexcept;

// Replaces the map with a new untitled one.
CYPHER_NODISCARD map_status_t MapWorkspace_New( map_workspace_t *pWorkspace ) noexcept;

// Opens a map from disk. On failure the current map stays open.
CYPHER_NODISCARD map_files_result_t MapWorkspace_Open( map_workspace_t *pWorkspace, const QString &path );

// Saves to the current path; INVALID_ARGUMENT when untitled (ask for a path
// and call SaveAs).
CYPHER_NODISCARD map_files_result_t MapWorkspace_Save( map_workspace_t *pWorkspace );
CYPHER_NODISCARD map_files_result_t MapWorkspace_SaveAs( map_workspace_t *pWorkspace, const QString &path );

// The map differs from its file: the history is away from the saved point.
CYPHER_NODISCARD common::bool_t MapWorkspace_IsModified( const map_workspace_t *pWorkspace ) noexcept;

// Undo and redo one step. Map callbacks publish their ready wireframe and call
// DocumentChanged; history then refreshes the title and available steps.
CYPHER_NODISCARD editor_history_status_t MapWorkspace_Undo( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD editor_history_status_t MapWorkspace_Redo( map_workspace_t *pWorkspace ) noexcept;

// Tells the session the document's content changed (an edit, undo, or
// redo): optionally rebuilds the wireframe, drops selected IDs that no longer
// exist, and notifies. Use false only after publishing a prebuilt wireframe.
void MapWorkspace_DocumentChanged( map_workspace_t *pWorkspace, bool rebuildWire = true ) noexcept;

// Entity metadata editing resolves selected entities and tied geometry to
// distinct owning entities. A private document is published with one undo
// step; the original selection and selected face remain intact. Unknown
// properties retain their CYKV types. A no-op returns false without history.
CYPHER_NODISCARD bool MapWorkspace_CanEditEntityProperties( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_SetEntityProperty(
    map_workspace_t *pWorkspace, common::string_view_t key, const common::key_value_t *pValue ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_RenameEntityProperty(
    map_workspace_t *pWorkspace, common::string_view_t key, common::string_view_t newKey ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_RemoveEntityProperty(
    map_workspace_t *pWorkspace, common::string_view_t key ) noexcept;
// Only class and name are identity fields; IDs and ownership are not raw keys.
CYPHER_NODISCARD bool MapWorkspace_SetEntityIdentityField(
    map_workspace_t *pWorkspace, common::string_view_t field, common::string_view_t value ) noexcept;

// Authored geometry editing. A complete private document is validated before
// publication; each call records one undo step. Failed operations leave the
// live document and selection intact. Euler rotation is in world degrees.
CYPHER_NODISCARD bool MapWorkspace_CanEditSelection( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanMoveSelection( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanEditBrushSelection( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanMergeBrushSelection( const map_workspace_t *pWorkspace ) noexcept;
// Whole-object mesh operations reject component modes and active previews.
// Conversion keeps the selected roots; mesh edits address authored topology.
CYPHER_NODISCARD bool MapWorkspace_CanConvertBrushSelection( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanEditMeshSelection( const map_workspace_t *pWorkspace ) noexcept;
// All-triangle selections have no productive triangulation operation.
CYPHER_NODISCARD bool MapWorkspace_CanTriangulateMeshSelection( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_ConvertBrushSelection( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_FlipMeshNormals( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_TriangulateMeshSelection( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CreateBox( map_workspace_t *pWorkspace, const map_bounds_t &bounds ) noexcept;
// Block tool creation reads shape and UV defaults; explicit box creation
// remains available to callers that need a box regardless of the active tool.
CYPHER_NODISCARD map_primitive_desc_t MapWorkspace_PrimitiveDefaults( const map_workspace_t *pWorkspace, const map_bounds_t &bounds ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CreatePrimitive( map_workspace_t *pWorkspace, const map_bounds_t &bounds ) noexcept;
// Commits the descriptor shown by a successful construction preview, even if
// creation defaults have changed since the last pointer movement.
CYPHER_NODISCARD bool MapWorkspace_CreatePrimitive( map_workspace_t *pWorkspace, const map_primitive_desc_t &primitive ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_TranslateSelection( map_workspace_t *pWorkspace, math::vec3d_t delta, bool clone = false ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_ResizeSelection( map_workspace_t *pWorkspace, math::vec3d_t sides, math::vec3d_t delta, bool fromCenter ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_ScaleSelection( map_workspace_t *pWorkspace, math::vec3d_t factors, math::vec3d_t pivot ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_RotateSelection( map_workspace_t *pWorkspace, math::vec3d_t degrees, math::vec3d_t pivot ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_DeleteSelection( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_DuplicateSelection( map_workspace_t *pWorkspace, math::vec3d_t offset = {} ) noexcept;
inline constexpr const char *MAP_CLIPBOARD_MIME = "application/x-cypher-map-clipboard";
// Whole authored objects only. Copy also works on a read-only source map.
CYPHER_NODISCARD bool MapWorkspace_CanCopySelection( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanCutSelection( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanPaste( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CopySelection( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CutSelection( map_workspace_t *pWorkspace ) noexcept;
// Normal paste follows editor.map.paste_offset. In-place preserves coordinates.
CYPHER_NODISCARD bool MapWorkspace_Paste( map_workspace_t *pWorkspace, bool inPlace = false ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_HasBrushFace( const map_workspace_t *pWorkspace ) noexcept;
void MapWorkspace_SelectBrushFace( map_workspace_t *pWorkspace, common::u64 object, common::u64 side ) noexcept;
void MapWorkspace_ClearBrushFace( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_HasMeshFace( const map_workspace_t *pWorkspace ) noexcept;
void MapWorkspace_SelectMeshFace( map_workspace_t *pWorkspace, common::u64 object, common::u64 face ) noexcept;
void MapWorkspace_ClearMeshFace( map_workspace_t *pWorkspace ) noexcept;
// Selection only: read-only maps are supported. Add/toggle cannot span meshes.
// Preparing both component and root sets before publication keeps OOM atomic.
CYPHER_NODISCARD bool MapWorkspace_HasMeshVertices( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_SelectMeshVertex( map_workspace_t *pWorkspace, common::u64 object,
    geometry::geometry_source_id_t vertex, map_select_mode_t mode = MAP_SELECT_REPLACE ) noexcept;
void MapWorkspace_ClearMeshVertices( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_HasMeshEdges( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_SelectMeshEdge( map_workspace_t *pWorkspace, common::u64 object,
    geometry::mesh_edge_ref_t edge, map_select_mode_t mode = MAP_SELECT_REPLACE ) noexcept;
void MapWorkspace_ClearMeshEdges( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanSelectMeshEdgeTopology( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_SelectMeshEdgeLoop( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_SelectMeshEdgeRing( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanEditMeshFace( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_ExtrudeMeshFace( map_workspace_t *pWorkspace, common::f64 distance ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_InsetMeshFace( map_workspace_t *pWorkspace, common::f64 margin ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CanQuadSliceMeshFace( const map_workspace_t *pWorkspace, common::u32 cellsU, common::u32 cellsV ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_QuadSliceMeshFace( map_workspace_t *pWorkspace, common::u32 cellsU, common::u32 cellsV ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_PushPullFace( map_workspace_t *pWorkspace, common::f64 distance ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_ApplyFaceMaterial( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_HollowSelection( map_workspace_t *pWorkspace, common::f64 thickness ) noexcept;
// Exactly two compatible brushes; the lowest selected ID supplies root metadata.
// Source face materials, UVs and residual fields follow their exact ancestry.
CYPHER_NODISCARD bool MapWorkspace_MergeSelection( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_ClipSelection( map_workspace_t *pWorkspace, math::planed_t plane ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_ClipSelection( map_workspace_t *pWorkspace, math::planed_t plane, map_brush_clip_mode_t mode ) noexcept;
CYPHER_NODISCARD map_brush_clip_mode_t MapWorkspace_ClipMode( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD common::u32 MapWorkspace_ClipAxis( const map_workspace_t *pWorkspace ) noexcept;
// Builds only selected brush geometry. Failure clears stale ghost geometry;
// confirmation publishes exactly the captured plane, mode and cut material.
void MapWorkspace_SetClipPreview( map_workspace_t *pWorkspace, math::planed_t plane ) noexcept;
void MapWorkspace_SetClipGuide( map_workspace_t *pWorkspace, const map_clip_guide_t &guide ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CommitClipPreview( map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_SubtractBrush( map_workspace_t *pWorkspace, common::u64 cutterId ) noexcept;
// A Quad uses the captured footprint plane rather than the numeric primitive
// axis setting. 0..2 supplies that plane normal; 3 keeps the setting default.
// Its shared preview bounds are flat along that normal.
void MapWorkspace_SetEditPreview( map_workspace_t *pWorkspace, const map_bounds_t &bounds,
    common::u32 constructionPlaneAxis = 3u ) noexcept;
// A released Block footprint stays private and can be resized in any pane.
// Failed rebuild/publication retains its last good descriptor and wire cache.
CYPHER_NODISCARD bool MapWorkspace_HasBlockPreview( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_StageBlockPreview( map_workspace_t *pWorkspace, common::u32 depthAxis = 2u ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_SetBlockPreviewBounds( map_workspace_t *pWorkspace, const map_bounds_t &bounds ) noexcept;
CYPHER_NODISCARD bool MapWorkspace_CommitBlockPreview( map_workspace_t *pWorkspace ) noexcept;
// Exact bounds come from the displayed geometry, not rotated AABB corners.
// The descriptor is presentation data; it does not prevalidate a backend edit.
void MapWorkspace_SetTransformPreview( map_workspace_t *pWorkspace, const map_transform_preview_t &transform ) noexcept;
CYPHER_NODISCARD math::affine3d_t MapWorkspace_TransformPreviewAffine( const map_transform_preview_t &transform ) noexcept;
CYPHER_NODISCARD math::affine3d_t MapWorkspace_TransformPreviewObjectAffine( const map_transform_preview_t &transform, const map_wire_object_t &object ) noexcept;
CYPHER_NODISCARD math::vec3d_t MapWorkspace_TransformPreviewPoint( const map_transform_preview_t &transform, math::vec3d_t point ) noexcept;
CYPHER_NODISCARD common::bool_t MapWorkspace_IsTransformPreviewObject( const map_workspace_t *pWorkspace, const map_wire_object_t &object ) noexcept;
CYPHER_NODISCARD map_preview_visibility_t MapWorkspace_PreviewVisibility( const map_workspace_t *pWorkspace ) noexcept;
CYPHER_NODISCARD common::bool_t MapWorkspace_PreviewVisibilityMatches( const map_workspace_t *pWorkspace, const map_preview_visibility_t &visibility ) noexcept;
void MapWorkspace_ClearEditPreview( map_workspace_t *pWorkspace ) noexcept;

// "facility.cymap", or "Untitled".
CYPHER_NODISCARD QString MapWorkspace_DisplayName( const map_workspace_t *pWorkspace );

void MapWorkspace_Select( map_workspace_t *pWorkspace, common::u64 id, map_select_mode_t mode ) noexcept;
// Replaces the selection with a list (any order, repeats allowed), with one
// notification: what the outliner uses for multi-row selection.
void MapWorkspace_SetSelection( map_workspace_t *pWorkspace, const common::u64 *pIds, common::usize nIds ) noexcept;
CYPHER_NODISCARD common::bool_t MapWorkspace_IsSelected( const map_workspace_t *pWorkspace, common::u64 id ) noexcept;

// Grid size and visibility are the settings editor.grid.size and
// editor.grid.show: changing them here writes the narrowest scope when one is
// attached, so the choice persists and every view follows.
void MapWorkspace_SetGridSize( map_workspace_t *pWorkspace, common::f64 gridSize ) noexcept;
void MapWorkspace_SetGridVisible( map_workspace_t *pWorkspace, common::bool_t bVisible ) noexcept;

// Snapping and texture lock are settings too (editor.grid.snap,
// editor.grid.angle_snap, editor.grid.scale_snap, editor.map.texture_lock), written the same way.
void MapWorkspace_SetSnapToGrid( map_workspace_t *pWorkspace, common::bool_t bSnap ) noexcept;
void MapWorkspace_SetAngleSnap( map_workspace_t *pWorkspace, common::f64 degrees ) noexcept;
void MapWorkspace_SetScaleSnap( map_workspace_t *pWorkspace, common::f64 step ) noexcept;
void MapWorkspace_SetTextureLock( map_workspace_t *pWorkspace, common::bool_t bLock ) noexcept;

void MapWorkspace_SetElementMode( map_workspace_t *pWorkspace, map_element_mode_t mode ) noexcept;

CYPHER_NODISCARD map_visgroup_t MapWorkspace_VisgroupOf( const map_workspace_t *pWorkspace, const map_wire_object_t &object ) noexcept;
// Views draw and pick only visible objects.
CYPHER_NODISCARD common::bool_t MapWorkspace_IsVisible( const map_workspace_t *pWorkspace, const map_wire_object_t &object ) noexcept;
// Root eligibility for viewport selection, marquee and whole-root commands.
// Vertices/Edges can show their operation profiles while component picking is
// still unavailable; they must never fall back to selecting a parent root.
CYPHER_NODISCARD common::bool_t MapWorkspace_IsSelectableObject( const map_workspace_t *pWorkspace, const map_wire_object_t &object ) noexcept;
void MapWorkspace_SetVisgroupHidden( map_workspace_t *pWorkspace, map_visgroup_t group, common::bool_t bHidden ) noexcept;
CYPHER_NODISCARD common::bool_t MapWorkspace_IsVisgroupHidden( const map_workspace_t *pWorkspace, map_visgroup_t group ) noexcept;
CYPHER_NODISCARD const char *MapWorkspace_VisgroupName( map_visgroup_t group ) noexcept;
CYPHER_NODISCARD common::bool_t MapWorkspace_IsElementModeAvailable( map_element_mode_t mode ) noexcept;
CYPHER_NODISCARD const char *MapWorkspace_ElementModeName( map_element_mode_t mode ) noexcept;

// The map workspace's settings page (CYSETTINGS.md 4.6); static lifetime.
CYPHER_NODISCARD const common::setting_descriptor_t *MapWorkspace_SettingsCatalogue( common::usize *pnDescriptorsOut ) noexcept;
void MapWorkspace_SetTool( map_workspace_t *pWorkspace, map_tool_t tool ) noexcept;
CYPHER_NODISCARD common::bool_t MapWorkspace_IsToolAvailable( map_tool_t tool ) noexcept;
CYPHER_NODISCARD const char *MapWorkspace_ToolName( map_tool_t tool ) noexcept;

// Asks every view to show the whole map, or the selection when there is one.
void MapWorkspace_Frame( map_workspace_t *pWorkspace, common::bool_t bSelectionOnly,
                         map_frame_target_t target = map_frame_target_t::ALL ) noexcept;

// Records the cursor from a 2D view; axes is a bit mask of meaningful axes.
void MapWorkspace_SetCursor( map_workspace_t *pWorkspace, math::vec3d_t cursor, common::u32 axes ) noexcept;

CYPHER_NODISCARD common::bool_t MapWorkspace_AddListener( map_workspace_t *pWorkspace, map_listener_fn pfnChanged, void *pContext ) noexcept;
void MapWorkspace_RemoveListener( map_workspace_t *pWorkspace, map_listener_fn pfnChanged, void *pContext ) noexcept;
void MapWorkspace_Notify( map_workspace_t *pWorkspace, common::u32 changes ) noexcept;

// The cordon: while active, objects entirely outside its box are hidden in
// every view and cannot be picked, so one area of a large map can be worked
// on alone. Setting a box activates it; an empty box deactivates.
void MapWorkspace_SetCordon( map_workspace_t *pWorkspace, const map_bounds_t &bounds ) noexcept;
void MapWorkspace_SetCordonActive( map_workspace_t *pWorkspace, common::bool_t bActive ) noexcept;
CYPHER_NODISCARD common::bool_t MapWorkspace_IsCordonActive( const map_workspace_t *pWorkspace ) noexcept;

// Installs the material image source; panels showing materials redraw.
void MapWorkspace_SetMaterialImages( map_workspace_t *pWorkspace, map_material_image_fn pfnImage, void *pContext ) noexcept;
// The image for a material path; null without a source or an image.
CYPHER_NODISCARD QImage MapWorkspace_MaterialImage( const map_workspace_t *pWorkspace, const QString &path );

// Registers the `map.*` commands with pWorkspace as their context.
CYPHER_NODISCARD command_registry_status_t MapWorkspace_RegisterCommands( map_workspace_t *pWorkspace, command_registry_t *pRegistry ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_WORKSPACE_H
