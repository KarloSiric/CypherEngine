//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapDocument.h
//  Purpose: Declares the Qt-independent tile-map authoring document.
//  Details: The document owns authored cells, gameplay markers, validation,
//           and bounded edit history. Qt and ImGui are presentation layers.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_TILEMAPDOCUMENT_H
#define CYPHER_TOOLS_TILEEDITOR_TILEMAPDOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_StringView.h"
#include "CypherCommon/Tier1/CypherCommon_UniqueId.h"
#include "CypherCommon/Tier1/CypherCommon_Vector.h"

namespace cypher::tools::tile_editor
{

using namespace cypher::common;

inline constexpr u32 TILE_MAP_DEFAULT_WIDTH = 64u;
inline constexpr u32 TILE_MAP_DEFAULT_HEIGHT = 64u;
inline constexpr u32 TILE_MAP_MAX_WIDTH = 1024u;
inline constexpr u32 TILE_MAP_MAX_HEIGHT = 1024u;
inline constexpr usize TILE_MAP_MAX_ACTIVE_CELLS = 1u << 18u;
inline constexpr usize TILE_MAP_MAX_MARKERS = 1u << 12u;
inline constexpr usize TILE_MAP_MAX_MATERIAL_BINDINGS = 256u;
inline constexpr usize TILE_MAP_MATERIAL_PATH_CAPACITY = 256u;

// Stable slots refer to canonical project-relative .cymat assets. The document
// owns all path bytes; presentation and runtime asset handles stay outside it.
struct tile_map_material_binding_t {
    u16 nSlot{ 0u };
    char path[TILE_MAP_MATERIAL_PATH_CAPACITY]{};
};

inline constexpr f32 TILE_MAP_MIN_CELL_SIZE = 0.20f;
inline constexpr f32 TILE_MAP_MIN_LEVEL_HEIGHT = 0.25f;
inline constexpr f32 TILE_MAP_DEFAULT_CELL_SIZE = 2.0f;
inline constexpr f32 TILE_MAP_DEFAULT_LEVEL_HEIGHT = 3.0f;
inline constexpr u16 TILE_MAP_DEFAULT_STAIR_STEPS = 8u;
inline constexpr u16 TILE_MAP_MIN_STAIR_STEPS = 2u;
inline constexpr u16 TILE_MAP_MAX_STAIR_STEPS = 32u;

// A history entry represents one committed user action. Entries are bounded
// by both count and copied change bytes so a long editor session has a known
// memory ceiling.
inline constexpr usize TILE_MAP_HISTORY_MAX_ENTRIES = 128u;
inline constexpr usize TILE_MAP_HISTORY_BYTE_BUDGET = 16u * CY_MIB;

enum tile_map_cell_flags_t : flags16_t {
    TILE_MAP_CELL_FLAG_NONE = 0u,
    TILE_MAP_CELL_FLAG_FLOOR =
        static_cast<flags16_t>( CYPHER_BIT32( 0 ) )
};

struct tile_map_grid_coord_t {
    i32 x{ 0 };
    i32 y{ 0 };
};

// Rectangles use a signed origin because mouse-to-grid conversion may produce
// negative coordinates. Width and height are extents, not inclusive ends.
struct tile_map_grid_rect_t {
    i32 x{ 0 };
    i32 y{ 0 };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
};

// A stair tile rises one level toward its named side. Its edges are open:
// adjacent flat tiles provide lower/upper landings, and walls remain separate.
enum class tile_map_cell_shape_t : u8 {
    FLAT = 0u,
    STAIRS_NORTH,
    STAIRS_EAST,
    STAIRS_SOUTH,
    STAIRS_WEST
};

struct tile_map_cell_t {
    i16 nFloorLevel{ 0 };
    u16 nWallHeightLevels{ 1u };
    u16 nMaterialSlot{ 0u };
    flags16_t flags{ TILE_MAP_CELL_FLAG_NONE };
    tile_map_cell_shape_t shape{ tile_map_cell_shape_t::FLAT };
    u16 nStairSteps{ TILE_MAP_DEFAULT_STAIR_STEPS };
};

// Painting always enables the floor flag. Erasing restores the complete cell
// to its canonical empty value.
struct tile_map_paint_t {
    i16 nFloorLevel{ 0 };
    u16 nWallHeightLevels{ 1u };
    u16 nMaterialSlot{ 0u };
    tile_map_cell_shape_t shape{ tile_map_cell_shape_t::FLAT };
    u16 nStairSteps{ TILE_MAP_DEFAULT_STAIR_STEPS };
};

// A property editor supplies only fields explicitly changed by the user.
// Unflagged values never replace mixed properties in a multi-cell selection.
enum tile_map_selection_property_t : flags32_t {
    TILE_MAP_SELECTION_PROPERTY_NONE = 0u,
    TILE_MAP_SELECTION_PROPERTY_FLOOR_ENABLED = CYPHER_BIT32( 0 ),
    TILE_MAP_SELECTION_PROPERTY_FLOOR_LEVEL = CYPHER_BIT32( 1 ),
    TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT = CYPHER_BIT32( 2 ),
    TILE_MAP_SELECTION_PROPERTY_MATERIAL = CYPHER_BIT32( 3 ),
    TILE_MAP_SELECTION_PROPERTY_SHAPE = CYPHER_BIT32( 4 ),
    TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS = CYPHER_BIT32( 5 )
};

struct tile_map_selection_patch_t {
    flags32_t fields{ TILE_MAP_SELECTION_PROPERTY_NONE };
    bool_t bFloorEnabled{ CY_TRUE };
    i16 nFloorLevel{ 0 };
    u16 nWallHeightLevels{ 1u };
    u16 nMaterialSlot{ 0u };
    tile_map_cell_shape_t shape{ tile_map_cell_shape_t::FLAT };
    u16 nStairSteps{ TILE_MAP_DEFAULT_STAIR_STEPS };
};

enum class tile_map_marker_kind_t : u8 {
    PLAYER_SPAWN = 0u,
    DOOR
};

// Marker sides use map-grid directions. North is negative grid Y, east is
// positive grid X, south is positive grid Y, and west is negative grid X.
// NONE is valid for markers without an edge orientation, such as spawns.
enum class tile_map_marker_side_t : u8 {
    NONE = 0u,
    NORTH,
    EAST,
    SOUTH,
    WEST
};

struct tile_map_marker_t {
    unique_id_t id{};
    tile_map_marker_kind_t kind{ tile_map_marker_kind_t::PLAYER_SPAWN };
    tile_map_grid_coord_t cell{};
    f32 yawDegrees{ 0.0f };
    tile_map_marker_side_t side{ tile_map_marker_side_t::NONE };
};

struct tile_map_document_desc_t {
    u32 nWidth{ TILE_MAP_DEFAULT_WIDTH };
    u32 nHeight{ TILE_MAP_DEFAULT_HEIGHT };
    f32 nCellSize{ TILE_MAP_DEFAULT_CELL_SIZE };
    f32 nLevelHeight{ TILE_MAP_DEFAULT_LEVEL_HEIGHT };
};

enum class tile_map_document_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    ALREADY_INITIALIZED,
    NOT_INITIALIZED,
    INVALID_STATE,
    INVALID_DIMENSIONS,
    INVALID_METRICS,
    INVALID_CELL,
    IDENTITY_CREATION_FAILED,
    ALLOCATION_FAILED,
    OUT_OF_BOUNDS,
    PLAYER_SPAWN_NOT_FOUND,
    DOOR_NOT_FOUND,
    INVALID_MARKER_SIDE,
    HISTORY_EMPTY,
    HISTORY_LIMIT_REACHED,
    ACTIVE_CELL_LIMIT_REACHED,
    MARKER_LIMIT_REACHED,
    INVALID_MATERIAL_PATH,
    MATERIAL_BINDING_LIMIT_REACHED
};

enum class tile_map_validation_code_t : u8 {
    INVALID_DIMENSIONS = 0u,
    INVALID_METRICS,
    CELL_STORAGE_MISMATCH,
    ACTIVE_CELL_LIMIT_EXCEEDED,
    NONCANONICAL_EMPTY_CELL,
    INVALID_MARKER_ID,
    DUPLICATE_MARKER_ID,
    INVALID_MARKER_KIND,
    MISSING_PLAYER_SPAWN,
    DUPLICATE_PLAYER_SPAWN,
    PLAYER_SPAWN_OUT_OF_BOUNDS,
    PLAYER_SPAWN_OUTSIDE_FLOOR,
    DOOR_OUT_OF_BOUNDS,
    DOOR_INVALID_SIDE,
    DOOR_OUTSIDE_FLOOR,
    DOOR_NOT_ON_BOUNDARY,
    DUPLICATE_DOOR_EDGE,
    INVALID_CELL_PROPERTIES,
    INVALID_CELL_SHAPE,
    INVALID_STAIR_STEPS,
    PLAYER_SPAWN_ON_STAIRS,
    DOOR_ON_STAIRS,
    INVALID_MATERIAL_BINDING,
    DUPLICATE_MATERIAL_SLOT,
    MATERIAL_BINDING_LIMIT_EXCEEDED
};

struct tile_map_validation_diagnostic_t {
    tile_map_validation_code_t code{
        tile_map_validation_code_t::INVALID_DIMENSIONS
    };
    unique_id_t markerId{};
    tile_map_grid_coord_t cell{};
};

struct tile_map_validation_report_t {
    tile_map_validation_report_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( tile_map_validation_report_t );

    const allocator_t *pAllocator{ nullptr };
    vector_t<tile_map_validation_diagnostic_t> diagnostics{};
};

struct tile_map_history_state_t;

struct tile_map_document_t {
    tile_map_document_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( tile_map_document_t );

    const allocator_t *pAllocator{ nullptr };
    unique_id_t mapId{};

    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    f32 nCellSize{ 0.0f };
    f32 nLevelHeight{ 0.0f };

    // Dense row-major storage: index = y * nWidth + x.
    vector_t<tile_map_cell_t> cells{};
    vector_t<tile_map_marker_t> markers{};
    vector_t<tile_map_material_binding_t> materialBindings{};

    // Revisions identify exact states, allowing undo back to a saved state to
    // clear the dirty flag correctly.
    u64 nCurrentRevision{ 0u };
    u64 nSavedRevision{ 0u };
    u64 nNextRevision{ 0u };

    // Opaque to frontends. It owns bounded history entries and one live group.
    tile_map_history_state_t *pHistoryState{ nullptr };
};

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_Init(
    tile_map_document_t *pDocument,
    const allocator_t *pAllocator,
    const tile_map_document_desc_t &desc ) noexcept;

// Changes dimensions and world metrics as one undoable action. Grid coordinates
// stay anchored at (0, 0); shrinking rejects authored cells or markers outside
// the new bounds instead of cropping them. Open edit groups must finish first.
// A successful resize may invalidate cell pointers. IDs and bindings are kept.
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_SetDescription(
    tile_map_document_t *pDocument,
    const tile_map_document_desc_t &desc ) noexcept;

void CypherTileMapDocument_Shutdown(
    tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapDocument_IsInitialized(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapDocument_IsDirty(
    const tile_map_document_t *pDocument ) noexcept;

void CypherTileMapDocument_MarkSaved(
    tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapDocument_ContainsCell(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept;

CYPHER_NODISCARD tile_map_cell_t *CypherTileMapDocument_CellAt(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept;

CYPHER_NODISCARD const tile_map_cell_t *CypherTileMapDocument_CellAt(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapDocument_CellHasFloor(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapCell_IsCanonicalEmpty(
    const tile_map_cell_t &cell ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapCellShape_IsValid(
    tile_map_cell_shape_t shape ) noexcept;

// Stable lowercase names are also the persisted CYKV shape values.
CYPHER_NODISCARD const char *CypherTileMapCellShape_Name(
    tile_map_cell_shape_t shape ) noexcept;

// Explicit groups let mouse drags or multi-part commands become one undo step.
// Changes are visible while a group is open; cancellation restores the exact
// state from before BeginEditGroup.
CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_BeginEditGroup(
    tile_map_document_t *pDocument,
    string_view_t label ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_CommitEditGroup(
    tile_map_document_t *pDocument ) noexcept;

void CypherTileMapDocument_CancelEditGroup(
    tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapDocument_IsEditGroupOpen(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_PaintCell(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    const tile_map_paint_t &paint ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_EraseCell(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_PaintRect(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    const tile_map_paint_t &paint ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_EraseRect(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect ) noexcept;

// Region commands are standalone atomic history actions; an open edit group
// is rejected. Rectangles must be completely inside the map. Move/rotation
// preserve marker identities; copies create new doors and omit player spawns.
// Occupied destination cells or markers outside a moving source are rejected
// with INVALID_STATE. Copy requires an entirely empty destination footprint;
// marker growth beyond the authored-map limit returns MARKER_LIMIT_REACHED.
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_MoveRegion(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    i32 dx,
    i32 dy ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_CopyRegion(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    i32 dx,
    i32 dy ) noexcept;

// Unlike EraseRect, deleting a selection also removes all its owned markers.
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_DeleteRegion(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_AdjustRegionFloorLevel(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    i32 delta ) noexcept;

// Clockwise in the Top XY view; the upper-left anchor stays fixed and the
// resulting footprint is rect.nHeight by rect.nWidth. Stairs, door sides, and
// spawn heading rotate with the cells. No cell dimensions or metrics change.
CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_RotateRegionClockwise(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect ) noexcept;

// Arbitrary cell selections are deduplicated; holes are never included implicitly.
// Every coordinate must be in bounds. Empty selections/no-op edits preserve undo
// and redo. Each command owns one atomic history action and rejects open groups.
// Move/rotate may overlap selected cells but reject unselected occupied targets;
// copy requires empty target cells, duplicates doors, and keeps one player spawn.
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_MoveSelection(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection,
    i32 dx, i32 dy ) noexcept;
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_CopySelection(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection,
    i32 dx, i32 dy ) noexcept;
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_DeleteSelection(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection ) noexcept;
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_AdjustSelectionFloorLevel(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection,
    i32 delta ) noexcept;
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_AdjustSelectionWallHeight(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection,
    i32 delta ) noexcept;
// Clockwise around the selection's upper-left bounding origin; stairs and marker
// orientation rotate together. Only explicitly selected coordinates are changed.
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_RotateSelectionClockwise(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection ) noexcept;
// Empty cells stay unchanged unless FLOOR_ENABLED=true creates a default floor
// and applies the flagged fields. FLOOR_ENABLED=false erases cells and all owned
// markers. Invalid resulting properties reject the complete operation.
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_ApplySelectionProperties(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection,
    const tile_map_selection_patch_t &patch ) noexcept;

// Place enforces the editor's single-player-spawn invariant. It inserts a
// spawn when missing, moves the existing spawn, and collapses duplicate spawn
// records that may have arrived from malformed source data.
CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_PlacePlayerSpawn(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    f32 yawDegrees ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_RemovePlayerSpawn(
    tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD const tile_map_marker_t *
CypherTileMapDocument_PlayerSpawn(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapMarkerSide_IsCardinal(
    tile_map_marker_side_t side ) noexcept;

// Doors are authored on an edge of an owning cell. Place is idempotent for an
// existing door on the same cell edge and returns that door's stable identity.
CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_PlaceDoor(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side,
    unique_id_t *pDoorIdOut ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_RemoveDoor(
    tile_map_document_t *pDocument,
    unique_id_t doorId ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_RemoveDoorAt(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side ) noexcept;

CYPHER_NODISCARD const tile_map_marker_t *
CypherTileMapDocument_DoorById(
    const tile_map_document_t *pDocument,
    unique_id_t doorId ) noexcept;

CYPHER_NODISCARD const tile_map_marker_t *
CypherTileMapDocument_DoorAt(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapMaterialPath_IsValid(
    string_view_t path ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapMaterialBinding_IsValid(
    const tile_map_material_binding_t &binding ) noexcept;

// Borrowed until the next binding mutation; absent slots use the built-in palette.
CYPHER_NODISCARD const tile_map_material_binding_t *
CypherTileMapDocument_FindMaterialBinding(
    const tile_map_document_t *pDocument, u16 nSlot ) noexcept;

// A standalone atomic undo action. An empty path removes the binding, preserving
// every cell's slot. Active edit groups are rejected. No asset I/O occurs here.
CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapDocument_SetMaterialBinding(
    tile_map_document_t *pDocument, u16 nSlot, string_view_t path ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapDocument_CanUndo(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapDocument_CanRedo(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_Undo(
    tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_Redo(
    tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD string_view_t CypherTileMapDocument_UndoLabel(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD string_view_t CypherTileMapDocument_RedoLabel(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD usize CypherTileMapDocument_HistoryCount(
    const tile_map_document_t *pDocument ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t
CypherTileMapValidationReport_Init(
    tile_map_validation_report_t *pReport,
    const allocator_t *pAllocator ) noexcept;

void CypherTileMapValidationReport_Shutdown(
    tile_map_validation_report_t *pReport ) noexcept;

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapDocument_Validate(
    const tile_map_document_t *pDocument,
    tile_map_validation_report_t *pReport ) noexcept;

CYPHER_NODISCARD bool_t CypherTileMapValidationReport_IsValid(
    const tile_map_validation_report_t *pReport ) noexcept;

CYPHER_NODISCARD const char *CypherTileMapDocument_StatusName(
    tile_map_document_status_t status ) noexcept;

CYPHER_NODISCARD const char *CypherTileMapValidation_CodeName(
    tile_map_validation_code_t code ) noexcept;

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_TILEMAPDOCUMENT_H
