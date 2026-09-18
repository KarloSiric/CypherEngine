//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapDocument.cpp
//  Purpose: Implements the Qt-independent tile-map authoring document.
//  Details: Mutations are transactional, grouped edits form one bounded undo
//           entry, and validation produces stable machine-readable diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapDocument.h"
#include "CypherCommon/Tier2/CypherCommon_DataValidation.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace cypher::tools::tile_editor
{

namespace
{

constexpr usize TILE_MAP_HISTORY_LABEL_CAPACITY = 64u;

enum class tile_map_history_change_kind_t : u8 {
    CELL = 0u,
    MARKER
};

// A deliberately plain record keeps history copying deterministic. Cell edits
// ignore marker fields and marker edits ignore cell fields.
struct tile_map_history_change_t {
    tile_map_history_change_kind_t kind{
        tile_map_history_change_kind_t::CELL
    };
    usize iElement{ 0u };

    tile_map_cell_t cellBefore{};
    tile_map_cell_t cellAfter{};

    tile_map_marker_t markerBefore{};
    tile_map_marker_t markerAfter{};
    bool_t bMarkerBeforeExists{ CY_FALSE };
    bool_t bMarkerAfterExists{ CY_FALSE };
    usize iSequence{ 0u };
};

// Material paths are allocated only for a material command; ordinary cell
// changes keep their existing small record size and history budget.
struct tile_map_material_history_change_t {
    tile_map_material_binding_t before{};
    tile_map_material_binding_t after{};
    usize iElement{ 0u };
    bool_t bBeforeExists{ CY_FALSE };
    bool_t bAfterExists{ CY_FALSE };
};

// Resizing never discards authored data, so history needs only the dimensions
// and metrics. The document retains its largest cell capacity to make undo and
// redo allocation-free; it does not keep a dense grid copy for every command.
struct tile_map_description_history_change_t {
    tile_map_document_desc_t before{};
    tile_map_document_desc_t after{};
};

struct tile_map_history_entry_t {
    tile_map_description_history_change_t *pDescriptionChange{ nullptr };
    tile_map_material_history_change_t *pMaterialChange{ nullptr };
    tile_map_history_change_t *pChanges{ nullptr };
    usize nChangeCount{ 0u };
    usize cbChanges{ 0u };
    u64 nBeforeRevision{ 0u };
    u64 nAfterRevision{ 0u };
    char label[TILE_MAP_HISTORY_LABEL_CAPACITY]{};
    usize cchLabel{ 0u };
};

static_assert(
    is_trivially_copyable_v<tile_map_history_change_t>,
    "Tile-map history changes are copied into exact owned arrays." );
static_assert(
    is_trivially_copyable_v<tile_map_history_entry_t>,
    "The bounded history compacts entry ownership records with memmove." );

bool_t TileMapCell_Equals(
    const tile_map_cell_t &left,
    const tile_map_cell_t &right ) noexcept
{
    return left.nFloorLevel == right.nFloorLevel &&
           left.nWallHeightLevels == right.nWallHeightLevels &&
           left.nMaterialSlot == right.nMaterialSlot &&
           left.flags == right.flags &&
           left.shape == right.shape &&
           left.nStairSteps == right.nStairSteps;
}

bool_t TileMapPaint_IsValid( const tile_map_paint_t &paint ) noexcept
{
    return paint.nWallHeightLevels > 0u &&
           CypherTileMapCellShape_IsValid( paint.shape ) &&
           paint.nStairSteps >= TILE_MAP_MIN_STAIR_STEPS &&
           paint.nStairSteps <= TILE_MAP_MAX_STAIR_STEPS &&
           ( paint.shape == tile_map_cell_shape_t::FLAT ||
             paint.nFloorLevel < CY_I16_MAX );
}

bool_t TileMapCoord_Equals(
    tile_map_grid_coord_t left,
    tile_map_grid_coord_t right ) noexcept
{
    return left.x == right.x && left.y == right.y;
}

bool_t TileMapMarker_Equals(
    const tile_map_marker_t &left,
    const tile_map_marker_t &right ) noexcept
{
    return UniqueId_Equals( left.id, right.id ) &&
           left.kind == right.kind &&
           TileMapCoord_Equals( left.cell, right.cell ) &&
           left.yawDegrees == right.yawDegrees &&
           left.side == right.side;
}

bool_t TileMapDocument_AreDimensionsValid(
    u32 nWidth,
    u32 nHeight ) noexcept
{
    return nWidth > 0u &&
           nHeight > 0u &&
           nWidth <= TILE_MAP_MAX_WIDTH &&
           nHeight <= TILE_MAP_MAX_HEIGHT;
}

bool_t TileMapDocument_AreMetricsValid(
    f32 nCellSize,
    f32 nLevelHeight ) noexcept
{
    return std::isfinite( nCellSize ) &&
           std::isfinite( nLevelHeight ) &&
           nCellSize >= TILE_MAP_MIN_CELL_SIZE &&
           nLevelHeight >= TILE_MAP_MIN_LEVEL_HEIGHT;
}

bool_t TileMapDocument_DescriptionFitsWorld(
    const tile_map_document_t &document,
    const tile_map_document_desc_t &desc ) noexcept
{
    const f64 maximum = CY_F32_MAX;
    if ( static_cast<f64>( desc.nWidth ) * desc.nCellSize > maximum ||
         static_cast<f64>( desc.nHeight ) * desc.nCellSize > maximum ) {
        return CY_FALSE;
    }
    for ( usize i = 0u; i < document.cells.nCount; ++i ) {
        const auto &cell = document.cells.pData[i];
        if ( ( cell.flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) {
            continue;
        }
        const f64 floorZ = static_cast<f64>( cell.nFloorLevel ) * desc.nLevelHeight;
        const f64 topZ = ( static_cast<f64>( cell.nFloorLevel ) +
            std::max( static_cast<u32>( cell.nWallHeightLevels ), 1u ) ) * desc.nLevelHeight;
        if ( floorZ - TILE_MAP_MIN_LEVEL_HEIGHT < -maximum || topZ > maximum ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t TileMapDocument_IsCanonicalEmpty(
    const tile_map_document_t &document ) noexcept
{
    return document.pAllocator == nullptr &&
           !UniqueId_IsValid( document.mapId ) &&
           document.nWidth == 0u &&
           document.nHeight == 0u &&
           document.nCellSize == 0.0f &&
           document.nLevelHeight == 0.0f &&
           document.cells.pData == nullptr &&
           document.cells.nCount == 0u &&
           document.cells.nCapacity == 0u &&
           document.cells.pAllocator == nullptr &&
           document.markers.pData == nullptr &&
           document.markers.nCount == 0u &&
           document.markers.nCapacity == 0u &&
           document.markers.pAllocator == nullptr &&
           document.materialBindings.pData == nullptr &&
           document.materialBindings.nCount == 0u &&
           document.materialBindings.nCapacity == 0u &&
           document.materialBindings.pAllocator == nullptr &&
           document.nCurrentRevision == 0u &&
           document.nSavedRevision == 0u &&
           document.nNextRevision == 0u &&
           document.pHistoryState == nullptr;
}

void TileMapDocument_ResetMetadata(
    tile_map_document_t &document ) noexcept
{
    document.pAllocator = nullptr;
    document.mapId = CY_UNIQUE_ID_INVALID;
    document.nWidth = 0u;
    document.nHeight = 0u;
    document.nCellSize = 0.0f;
    document.nLevelHeight = 0.0f;
    document.nCurrentRevision = 0u;
    document.nSavedRevision = 0u;
    document.nNextRevision = 0u;
    document.pHistoryState = nullptr;
}

void TileMap_CopyLabel(
    char ( &destination )[TILE_MAP_HISTORY_LABEL_CAPACITY],
    usize &cchDestination,
    string_view_t source ) noexcept
{
    const usize cchMaximum = TILE_MAP_HISTORY_LABEL_CAPACITY - 1u;
    cchDestination = source.cchLength < cchMaximum
        ? source.cchLength
        : cchMaximum;
    if ( cchDestination != 0u ) {
        Cy_MemCopy( destination, source.pData, cchDestination );
    }
    destination[cchDestination] = '\0';
}

bool_t TileMapRect_IsInside(
    const tile_map_document_t &document,
    const tile_map_grid_rect_t &rect ) noexcept
{
    if ( rect.nWidth == 0u || rect.nHeight == 0u ||
         rect.x < 0 || rect.y < 0 ) {
        return CY_FALSE;
    }
    const i64 xEnd = static_cast<i64>( rect.x ) + rect.nWidth;
    const i64 yEnd = static_cast<i64>( rect.y ) + rect.nHeight;
    return xEnd <= static_cast<i64>( document.nWidth ) &&
           yEnd <= static_cast<i64>( document.nHeight );
}

usize TileMapCellIndex(
    const tile_map_document_t &document,
    tile_map_grid_coord_t coordinate ) noexcept
{
    return static_cast<usize>( coordinate.y ) * document.nWidth +
           static_cast<usize>( coordinate.x );
}

bool_t TileMapCell_HasFloor( const tile_map_cell_t &cell ) noexcept
{
    return ( cell.flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0u;
}

tile_map_grid_coord_t TileMapMarker_NeighborCoordinate(
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side ) noexcept
{
    switch ( side ) {
        case tile_map_marker_side_t::NORTH: --coordinate.y; break;
        case tile_map_marker_side_t::EAST: ++coordinate.x; break;
        case tile_map_marker_side_t::SOUTH: ++coordinate.y; break;
        case tile_map_marker_side_t::WEST: --coordinate.x; break;
        case tile_map_marker_side_t::NONE: break;
    }
    return coordinate;
}

} // namespace

struct tile_map_history_state_t {
    tile_map_history_entry_t entries[TILE_MAP_HISTORY_MAX_ENTRIES]{};
    usize nEntryCount{ 0u };
    usize iCursor{ 0u };
    usize cbHistory{ 0u };

    vector_t<tile_map_history_change_t> activeChanges{};
    bool_t bGroupOpen{ CY_FALSE };
    u64 nGroupBeforeRevision{ 0u };
    u64 nGroupAfterRevision{ 0u };
    char activeLabel[TILE_MAP_HISTORY_LABEL_CAPACITY]{};
    usize cchActiveLabel{ 0u };
};

namespace
{

bool_t TileMapHistory_IsValid(
    const tile_map_document_t &document ) noexcept
{
    const tile_map_history_state_t *pState = document.pHistoryState;
    return pState != nullptr &&
           pState->nEntryCount <= TILE_MAP_HISTORY_MAX_ENTRIES &&
           pState->iCursor <= pState->nEntryCount &&
           pState->cbHistory <= TILE_MAP_HISTORY_BYTE_BUDGET &&
           Vector_IsValid( &pState->activeChanges ) &&
           pState->activeChanges.pAllocator == document.pAllocator;
}

void TileMapHistory_ReleaseEntry(
    tile_map_document_t &document,
    tile_map_history_entry_t &entry ) noexcept
{
    if ( entry.pChanges != nullptr ) {
        Allocator_FreeArrayStorage(
            document.pAllocator,
            entry.pChanges,
            entry.nChangeCount );
    }
    if ( entry.pMaterialChange != nullptr ) {
        Allocator_FreeArrayStorage( document.pAllocator, entry.pMaterialChange, 1u );
    }
    if ( entry.pDescriptionChange != nullptr ) {
        Allocator_FreeArrayStorage( document.pAllocator, entry.pDescriptionChange, 1u );
    }
    entry = {};
}

void TileMapHistory_ClearCommitted(
    tile_map_document_t &document ) noexcept
{
    tile_map_history_state_t &history = *document.pHistoryState;
    for ( usize iEntry = 0u;
          iEntry < history.nEntryCount;
          ++iEntry ) {
        TileMapHistory_ReleaseEntry( document, history.entries[iEntry] );
    }
    history.nEntryCount = 0u;
    history.iCursor = 0u;
    history.cbHistory = 0u;
}

void TileMapHistory_DiscardRedo(
    tile_map_document_t &document ) noexcept
{
    tile_map_history_state_t &history = *document.pHistoryState;
    for ( usize iEntry = history.iCursor;
          iEntry < history.nEntryCount;
          ++iEntry ) {
        history.cbHistory -= history.entries[iEntry].cbChanges;
        TileMapHistory_ReleaseEntry( document, history.entries[iEntry] );
    }
    history.nEntryCount = history.iCursor;
}

tile_map_document_status_t TileMapHistory_NormalizeActiveChanges(
    const tile_map_document_t &document,
    vector_t<tile_map_history_change_t> &normalized ) noexcept
{
    const tile_map_history_state_t &history = *document.pHistoryState;
    const usize nSourceChanges = Vector_Count( &history.activeChanges );
    if ( !Vector_Init(
             &normalized,
             document.pAllocator,
             nSourceChanges ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    // Marker edits retain chronological order because insertion/removal indices
    // are order-dependent. They commute with cell replacements, so cell edits
    // can be grouped after them and normalized independently.
    for ( usize iChange = 0u;
          iChange < nSourceChanges;
          ++iChange ) {
        tile_map_history_change_t change =
            history.activeChanges.pData[iChange];
        if ( change.kind != tile_map_history_change_kind_t::MARKER ) {
            continue;
        }
        const bool_t bNoOp =
            change.bMarkerBeforeExists == change.bMarkerAfterExists &&
            ( !change.bMarkerBeforeExists ||
              TileMapMarker_Equals(
                  change.markerBefore,
                  change.markerAfter ) );
        if ( !bNoOp ) {
            change.iSequence = iChange;
            if ( !Vector_PushBack( &normalized, change ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
        }
    }
    const usize nMarkerChanges = Vector_Count( &normalized );

    for ( usize iChange = 0u;
          iChange < nSourceChanges;
          ++iChange ) {
        tile_map_history_change_t change =
            history.activeChanges.pData[iChange];
        if ( change.kind != tile_map_history_change_kind_t::CELL ) {
            continue;
        }
        change.iSequence = iChange;
        if ( !Vector_PushBack( &normalized, change ) ) {
            return tile_map_document_status_t::ALLOCATION_FAILED;
        }
    }

    tile_map_history_change_t *pCellBegin =
        normalized.pData + nMarkerChanges;
    tile_map_history_change_t *pCellEnd =
        normalized.pData + Vector_Count( &normalized );
    std::sort(
        pCellBegin,
        pCellEnd,
        []( const tile_map_history_change_t &left,
            const tile_map_history_change_t &right ) noexcept {
            return left.iElement < right.iElement ||
                   ( left.iElement == right.iElement &&
                     left.iSequence < right.iSequence );
        } );

    usize iRead = nMarkerChanges;
    usize iWrite = nMarkerChanges;
    while ( iRead < Vector_Count( &normalized ) ) {
        tile_map_history_change_t merged = normalized.pData[iRead++];
        while ( iRead < Vector_Count( &normalized ) &&
                normalized.pData[iRead].iElement == merged.iElement ) {
            merged.cellAfter = normalized.pData[iRead].cellAfter;
            ++iRead;
        }
        if ( !TileMapCell_Equals( merged.cellBefore, merged.cellAfter ) ) {
            normalized.pData[iWrite++] = merged;
        }
    }
    (void) Vector_Resize( &normalized, iWrite );
    return tile_map_document_status_t::OK;
}

void TileMapHistory_EvictOldest(
    tile_map_document_t &document ) noexcept
{
    tile_map_history_state_t &history = *document.pHistoryState;
    if ( history.nEntryCount == 0u ) {
        return;
    }
    history.cbHistory -= history.entries[0].cbChanges;
    TileMapHistory_ReleaseEntry( document, history.entries[0] );
    if ( history.nEntryCount > 1u ) {
        Cy_MemMove(
            history.entries,
            history.entries + 1u,
            ( history.nEntryCount - 1u ) *
                sizeof( tile_map_history_entry_t ) );
    }
    --history.nEntryCount;
    if ( history.iCursor > 0u ) {
        --history.iCursor;
    }
    history.entries[history.nEntryCount] = {};
}

bool_t TileMap_ApplyMarkerChange(
    tile_map_document_t &document,
    const tile_map_history_change_t &change,
    bool_t bForward ) noexcept
{
    const bool_t bFromExists = bForward
        ? change.bMarkerBeforeExists
        : change.bMarkerAfterExists;
    const bool_t bToExists = bForward
        ? change.bMarkerAfterExists
        : change.bMarkerBeforeExists;
    const tile_map_marker_t &to = bForward
        ? change.markerAfter
        : change.markerBefore;

    if ( bFromExists && bToExists ) {
        if ( change.iElement >= Vector_Count( &document.markers ) ) {
            return CY_FALSE;
        }
        document.markers.pData[change.iElement] = to;
        return CY_TRUE;
    }
    if ( !bFromExists && bToExists ) {
        return Vector_Insert( &document.markers, change.iElement, to );
    }
    if ( bFromExists && !bToExists ) {
        if ( change.iElement >= Vector_Count( &document.markers ) ) {
            return CY_FALSE;
        }
        Vector_Erase( &document.markers, change.iElement );
    }
    return CY_TRUE;
}

bool_t TileMap_ApplyChange(
    tile_map_document_t &document,
    const tile_map_history_change_t &change,
    bool_t bForward ) noexcept
{
    if ( change.kind == tile_map_history_change_kind_t::CELL ) {
        if ( change.iElement >= Vector_Count( &document.cells ) ) {
            return CY_FALSE;
        }
        document.cells.pData[change.iElement] = bForward
            ? change.cellAfter
            : change.cellBefore;
        return CY_TRUE;
    }
    return TileMap_ApplyMarkerChange( document, change, bForward );
}

bool_t TileMap_ApplyChanges(
    tile_map_document_t &document,
    const tile_map_history_change_t *pChanges,
    usize nChangeCount,
    bool_t bForward ) noexcept
{
    if ( bForward ) {
        for ( usize iChange = 0u;
              iChange < nChangeCount;
              ++iChange ) {
            if ( !TileMap_ApplyChange(
                     document,
                     pChanges[iChange],
                     CY_TRUE ) ) {
                return CY_FALSE;
            }
        }
        return CY_TRUE;
    }
    for ( usize iChange = nChangeCount;
          iChange != 0u;
          --iChange ) {
        if ( !TileMap_ApplyChange(
                 document,
                 pChanges[iChange - 1u],
                 CY_FALSE ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t TileMapDocument_ApplyDescription(
    tile_map_document_t &document,
    const tile_map_document_desc_t &desc ) noexcept
{
    const usize nNewCount = static_cast<usize>( desc.nWidth ) * desc.nHeight;
    if ( nNewCount > Vector_Capacity( &document.cells ) ) {
        return CY_FALSE;
    }
    if ( document.nWidth != desc.nWidth || document.nHeight != desc.nHeight ) {
        const u32 nRows = std::min( document.nHeight, desc.nHeight );
        const u32 nColumns = std::min( document.nWidth, desc.nWidth );
        // Construct new elements before moving rows, and keep old elements
        // alive until all retained rows have reached their new indices.
        (void) Vector_Resize( &document.cells, std::max( document.cells.nCount, nNewCount ) );
        for ( u32 iRow = 0u; iRow < nRows; ++iRow ) {
            // Growing the stride moves rows backward to avoid overwriting a
            // later source row. Shrinking it moves rows forward instead.
            const u32 y = desc.nWidth > document.nWidth ? nRows - iRow - 1u : iRow;
            tile_map_cell_t *pDestination = document.cells.pData +
                static_cast<usize>( y ) * desc.nWidth;
            const tile_map_cell_t *pSource = document.cells.pData +
                static_cast<usize>( y ) * document.nWidth;
            Cy_MemMove( pDestination, pSource, sizeof( tile_map_cell_t ) * nColumns );
            std::fill( pDestination + nColumns, pDestination + desc.nWidth, tile_map_cell_t{} );
        }
        std::fill( document.cells.pData + static_cast<usize>( nRows ) * desc.nWidth,
            document.cells.pData + nNewCount, tile_map_cell_t{} );
        (void) Vector_Resize( &document.cells, nNewCount );
    }
    document.nWidth = desc.nWidth;
    document.nHeight = desc.nHeight;
    document.nCellSize = desc.nCellSize;
    document.nLevelHeight = desc.nLevelHeight;
    return CY_TRUE;
}

bool_t TileMap_ApplyHistoryEntry(
    tile_map_document_t &document, const tile_map_history_entry_t &entry,
    bool_t bForward ) noexcept
{
    if ( entry.pDescriptionChange != nullptr ) {
        return TileMapDocument_ApplyDescription( document,
            bForward ? entry.pDescriptionChange->after : entry.pDescriptionChange->before );
    }
    if ( entry.pMaterialChange == nullptr ) {
        return TileMap_ApplyChanges( document, entry.pChanges, entry.nChangeCount, bForward );
    }
    const auto &change = *entry.pMaterialChange;
    const bool_t fromExists = bForward ? change.bBeforeExists : change.bAfterExists;
    const bool_t toExists = bForward ? change.bAfterExists : change.bBeforeExists;
    const auto &to = bForward ? change.after : change.before;
    if ( fromExists && toExists ) {
        if ( change.iElement >= Vector_Count( &document.materialBindings ) ) return CY_FALSE;
        document.materialBindings.pData[change.iElement] = to;
    } else if ( toExists ) {
        // Capacity is reserved before publishing the original command and never
        // shrunk. Undo/redo cannot allocate for an insertion into this table.
        return Vector_Insert( &document.materialBindings, change.iElement, to );
    } else if ( fromExists ) {
        if ( change.iElement >= Vector_Count( &document.materialBindings ) ) return CY_FALSE;
        Vector_Erase( &document.materialBindings, change.iElement );
    }
    return CY_TRUE;
}

u64 TileMapDocument_AllocateRevision(
    tile_map_document_t &document ) noexcept
{
    if ( document.nNextRevision == 0u ) {
        return 0u;
    }
    const u64 revision = document.nNextRevision;
    document.nNextRevision = revision == CY_U64_MAX
        ? 0u
        : revision + 1u;
    return revision;
}

tile_map_document_status_t TileMapDocument_ApplyBatch(
    tile_map_document_t &document,
    span_t<const tile_map_history_change_t> changes,
    string_view_t automaticLabel ) noexcept
{
    if ( changes.nCount == 0u ) {
        return tile_map_document_status_t::OK;
    }

    tile_map_history_state_t &history = *document.pHistoryState;
    const bool_t bAutomaticGroup = !history.bGroupOpen;
    if ( bAutomaticGroup ) {
        const tile_map_document_status_t beginStatus =
            CypherTileMapDocument_BeginEditGroup(
                &document,
                automaticLabel );
        if ( beginStatus != tile_map_document_status_t::OK ) {
            return beginStatus;
        }
    }

    const usize nActiveBefore = Vector_Count( &history.activeChanges );
    constexpr usize nMaximumChanges =
        TILE_MAP_HISTORY_BYTE_BUDGET /
        sizeof( tile_map_history_change_t );
    if ( changes.nCount > nMaximumChanges - nActiveBefore ) {
        if ( bAutomaticGroup ) {
            CypherTileMapDocument_CancelEditGroup( &document );
        }
        return tile_map_document_status_t::HISTORY_LIMIT_REACHED;
    }
    if ( !Vector_Append( &history.activeChanges, changes ) ) {
        if ( bAutomaticGroup ) {
            CypherTileMapDocument_CancelEditGroup( &document );
        }
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    if ( nActiveBefore == 0u ) {
        history.nGroupAfterRevision =
            TileMapDocument_AllocateRevision( document );
        if ( history.nGroupAfterRevision == 0u ) {
            (void) Vector_Resize(
                &history.activeChanges,
                nActiveBefore );
            if ( bAutomaticGroup ) {
                CypherTileMapDocument_CancelEditGroup( &document );
            }
            return tile_map_document_status_t::HISTORY_LIMIT_REACHED;
        }
    }

    if ( !TileMap_ApplyChanges(
             document,
             changes.pData,
             changes.nCount,
             CY_TRUE ) ) {
        // Roll back only this batch. Earlier changes in an explicit group stay
        // live and can still be committed or cancelled by the caller.
        (void) TileMap_ApplyChanges(
            document,
            changes.pData,
            changes.nCount,
            CY_FALSE );
        (void) Vector_Resize(
            &history.activeChanges,
            nActiveBefore );
        if ( nActiveBefore == 0u ) {
            history.nGroupAfterRevision = 0u;
            document.nCurrentRevision = history.nGroupBeforeRevision;
        }
        if ( bAutomaticGroup ) {
            CypherTileMapDocument_CancelEditGroup( &document );
        }
        return tile_map_document_status_t::INVALID_STATE;
    }

    document.nCurrentRevision = history.nGroupAfterRevision;
    if ( !bAutomaticGroup ) {
        return tile_map_document_status_t::OK;
    }

    const tile_map_document_status_t commitStatus =
        CypherTileMapDocument_CommitEditGroup( &document );
    if ( commitStatus != tile_map_document_status_t::OK ) {
        CypherTileMapDocument_CancelEditGroup( &document );
    }
    return commitStatus;
}

tile_map_document_status_t TileMapDocument_EditRect(
    tile_map_document_t &document,
    const tile_map_grid_rect_t &rect,
    const tile_map_cell_t &replacement,
    string_view_t label ) noexcept
{
    if ( !TileMapRect_IsInside( document, rect ) ) {
        return tile_map_document_status_t::OUT_OF_BOUNDS;
    }

    usize nChanges = 0u;
    for ( u32 y = 0u; y < rect.nHeight; ++y ) {
        for ( u32 x = 0u; x < rect.nWidth; ++x ) {
            const tile_map_grid_coord_t coordinate{
                rect.x + static_cast<i32>( x ),
                rect.y + static_cast<i32>( y )
            };
            const tile_map_cell_t &cell =
                document.cells.pData[TileMapCellIndex( document, coordinate )];
            if ( !TileMapCell_Equals( cell, replacement ) ) {
                ++nChanges;
            }
        }
    }
    if ( nChanges == 0u ) {
        return tile_map_document_status_t::OK;
    }

    vector_t<tile_map_history_change_t> changes{};
    if ( !Vector_Init( &changes, document.pAllocator, nChanges ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    for ( u32 y = 0u; y < rect.nHeight; ++y ) {
        for ( u32 x = 0u; x < rect.nWidth; ++x ) {
            const tile_map_grid_coord_t coordinate{
                rect.x + static_cast<i32>( x ),
                rect.y + static_cast<i32>( y )
            };
            const usize iCell = TileMapCellIndex( document, coordinate );
            const tile_map_cell_t &cell = document.cells.pData[iCell];
            if ( TileMapCell_Equals( cell, replacement ) ) {
                continue;
            }
            const tile_map_history_change_t change{
                tile_map_history_change_kind_t::CELL,
                iCell,
                cell,
                replacement
            };
            if ( !Vector_PushBack( &changes, change ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
        }
    }
    return TileMapDocument_ApplyBatch(
        document,
        { changes.pData, changes.nCount },
        label );
}

bool_t TileMapValidation_Push(
    vector_t<tile_map_validation_diagnostic_t> &diagnostics,
    tile_map_validation_code_t code,
    unique_id_t markerId = CY_UNIQUE_ID_INVALID,
    tile_map_grid_coord_t cell = {} ) noexcept
{
    return Vector_PushBack(
        &diagnostics,
        tile_map_validation_diagnostic_t{ code, markerId, cell } );
}

enum class tile_map_region_operation_t {
    MOVE,
    COPY,
    DELETE,
    HEIGHT,
    ROTATE
};

bool_t TileMapRect_Contains(
    const tile_map_grid_rect_t &rect,
    tile_map_grid_coord_t coordinate ) noexcept
{
    return coordinate.x >= rect.x && coordinate.y >= rect.y &&
           static_cast<i64>( coordinate.x ) <
               static_cast<i64>( rect.x ) + rect.nWidth &&
           static_cast<i64>( coordinate.y ) <
               static_cast<i64>( rect.y ) + rect.nHeight;
}

tile_map_cell_shape_t TileMapRegion_RotateShape(
    tile_map_cell_shape_t shape ) noexcept
{
    switch ( shape ) {
        case tile_map_cell_shape_t::STAIRS_NORTH:
            return tile_map_cell_shape_t::STAIRS_EAST;
        case tile_map_cell_shape_t::STAIRS_EAST:
            return tile_map_cell_shape_t::STAIRS_SOUTH;
        case tile_map_cell_shape_t::STAIRS_SOUTH:
            return tile_map_cell_shape_t::STAIRS_WEST;
        case tile_map_cell_shape_t::STAIRS_WEST:
            return tile_map_cell_shape_t::STAIRS_NORTH;
        case tile_map_cell_shape_t::FLAT: return shape;
    }
    return shape;
}

tile_map_marker_side_t TileMapRegion_RotateSide(
    tile_map_marker_side_t side ) noexcept
{
    switch ( side ) {
        case tile_map_marker_side_t::NORTH: return tile_map_marker_side_t::EAST;
        case tile_map_marker_side_t::EAST: return tile_map_marker_side_t::SOUTH;
        case tile_map_marker_side_t::SOUTH: return tile_map_marker_side_t::WEST;
        case tile_map_marker_side_t::WEST: return tile_map_marker_side_t::NORTH;
        case tile_map_marker_side_t::NONE: return side;
    }
    return side;
}

tile_map_document_status_t TileMapRegion_PushChange(
    vector_t<tile_map_history_change_t> &changes,
    const tile_map_history_change_t &change ) noexcept
{
    // Reject before allocating an unbounded staging buffer. The committed
    // action must fit in exactly the same byte budget as ordinary edits.
    constexpr usize maximumChanges =
        TILE_MAP_HISTORY_BYTE_BUDGET / sizeof( tile_map_history_change_t );
    if ( changes.nCount >= maximumChanges ) {
        return tile_map_document_status_t::HISTORY_LIMIT_REACHED;
    }
    return Vector_PushBack( &changes, change )
        ? tile_map_document_status_t::OK
        : tile_map_document_status_t::ALLOCATION_FAILED;
}

tile_map_document_status_t TileMapDocument_EditRegion(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &source,
    tile_map_region_operation_t operation,
    i32 dx = 0,
    i32 dy = 0,
    i32 levelDelta = 0 ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( CypherTileMapDocument_IsEditGroupOpen( pDocument ) ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    auto &document = *pDocument;
    if ( !TileMapRect_IsInside( document, source ) ) {
        return tile_map_document_status_t::OUT_OF_BOUNDS;
    }
    const bool bCopy = operation == tile_map_region_operation_t::COPY;
    const bool bRotate = operation == tile_map_region_operation_t::ROTATE;
    const bool bTransfer = bCopy || bRotate ||
        operation == tile_map_region_operation_t::MOVE;
    const bool bDelete = operation == tile_map_region_operation_t::DELETE;
    tile_map_grid_rect_t destination = source;
    if ( bTransfer ) {
        const i64 destinationX = static_cast<i64>( source.x ) + dx;
        const i64 destinationY = static_cast<i64>( source.y ) + dy;
        if ( destinationX < 0 || destinationY < 0 ||
             destinationX >= document.nWidth || destinationY >= document.nHeight ) {
            return tile_map_document_status_t::OUT_OF_BOUNDS;
        }
        destination.x = static_cast<i32>( destinationX );
        destination.y = static_cast<i32>( destinationY );
        if ( bRotate ) std::swap( destination.nWidth, destination.nHeight );
        if ( !TileMapRect_IsInside( document, destination ) ) {
            return tile_map_document_status_t::OUT_OF_BOUNDS;
        }
        // Include empty source holes in collision checks: copying a room's
        // footprint must never silently erase unrelated authored content.
        for ( u32 y = 0u; y < destination.nHeight; ++y ) {
            for ( u32 x = 0u; x < destination.nWidth; ++x ) {
                const tile_map_grid_coord_t point{
                    destination.x + static_cast<i32>( x ),
                    destination.y + static_cast<i32>( y ) };
                if ( !bCopy && TileMapRect_Contains( source, point ) ) continue;
                if ( !CypherTileMapCell_IsCanonicalEmpty(
                         document.cells.pData[TileMapCellIndex( document, point )] ) ) {
                    return tile_map_document_status_t::INVALID_STATE;
                }
            }
        }
        for ( usize i = 0u; i < document.markers.nCount; ++i ) {
            const auto &marker = document.markers.pData[i];
            if ( TileMapRect_Contains( destination, marker.cell ) &&
                 ( bCopy || !TileMapRect_Contains( source, marker.cell ) ) ) {
                return tile_map_document_status_t::INVALID_STATE;
            }
        }
    }

    vector_t<tile_map_history_change_t> changes{};
    if ( !Vector_Init( &changes, document.pAllocator ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    const i32 xBegin = std::min( source.x, destination.x );
    const i32 yBegin = std::min( source.y, destination.y );
    const i32 xEnd = std::max( source.x + static_cast<i32>( source.nWidth ),
                              destination.x + static_cast<i32>( destination.nWidth ) );
    const i32 yEnd = std::max( source.y + static_cast<i32>( source.nHeight ),
                              destination.y + static_cast<i32>( destination.nHeight ) );
    for ( i32 y = yBegin; y < yEnd; ++y ) {
        for ( i32 x = xBegin; x < xEnd; ++x ) {
            const tile_map_grid_coord_t point{ x, y };
            const bool bInSource = TileMapRect_Contains( source, point );
            const bool bInDestination = TileMapRect_Contains( destination, point );
            if ( !bInSource && !bInDestination ) continue;
            const usize index = TileMapCellIndex( document, point );
            const auto &before = document.cells.pData[index];
            tile_map_cell_t after = before;
            if ( bTransfer && bInDestination ) {
                // Read every replacement from the unchanged source document.
                // This inverse mapping makes overlapping moves deterministic.
                const tile_map_grid_coord_t original = bRotate
                    ? tile_map_grid_coord_t{
                        source.x + ( y - destination.y ),
                        source.y + static_cast<i32>( source.nHeight ) - 1 -
                            ( x - destination.x ) }
                    : tile_map_grid_coord_t{ x - dx, y - dy };
                after = document.cells.pData[TileMapCellIndex( document, original )];
                if ( bRotate ) after.shape = TileMapRegion_RotateShape( after.shape );
            } else if ( bInSource && ( bDelete || ( bTransfer && !bCopy ) ) ) {
                after = {};
            } else if ( operation == tile_map_region_operation_t::HEIGHT &&
                        TileMapCell_HasFloor( before ) ) {
                const i64 newLevel = static_cast<i64>( before.nFloorLevel ) + levelDelta;
                const i64 maximumLevel = before.shape == tile_map_cell_shape_t::FLAT
                    ? CY_I16_MAX : CY_I16_MAX - 1;
                if ( newLevel < CY_I16_MIN || newLevel > maximumLevel ) {
                    return tile_map_document_status_t::INVALID_CELL;
                }
                after.nFloorLevel = static_cast<i16>( newLevel );
            }
            if ( TileMapCell_Equals( before, after ) ) continue;
            const auto status = TileMapRegion_PushChange( changes,
                { tile_map_history_change_kind_t::CELL, index, before, after } );
            if ( status != tile_map_document_status_t::OK ) return status;
        }
    }

    usize nCopiedDoors = 0u;
    // Delete from the end so history indices stay valid. Other operations
    // replace markers in-place or append copies; they never reorder originals.
    for ( usize i = document.markers.nCount; i != 0u; --i ) {
        const usize index = i - 1u;
        const auto &marker = document.markers.pData[index];
        if ( !TileMapRect_Contains( source, marker.cell ) ||
             operation == tile_map_region_operation_t::HEIGHT ) continue;
        if ( bCopy && marker.kind != tile_map_marker_kind_t::DOOR ) continue;
        tile_map_history_change_t change{};
        change.kind = tile_map_history_change_kind_t::MARKER;
        change.iElement = index;
        change.markerBefore = marker;
        change.bMarkerBeforeExists = CY_TRUE;
        if ( !bDelete ) {
            change.markerAfter = marker;
            change.bMarkerAfterExists = CY_TRUE;
            change.markerAfter.cell = bRotate
                ? tile_map_grid_coord_t{
                    destination.x + static_cast<i32>( source.nHeight ) - 1 -
                        ( marker.cell.y - source.y ),
                    destination.y + ( marker.cell.x - source.x ) }
                : tile_map_grid_coord_t{ marker.cell.x + dx, marker.cell.y + dy };
            if ( bRotate ) {
                change.markerAfter.side = TileMapRegion_RotateSide( marker.side );
                if ( marker.kind == tile_map_marker_kind_t::PLAYER_SPAWN ) {
                    if ( !std::isfinite( marker.yawDegrees ) ) {
                        return tile_map_document_status_t::INVALID_CELL;
                    }
                    change.markerAfter.yawDegrees = std::fmod( marker.yawDegrees, 360.0f ) + 90.0f;
                    if ( change.markerAfter.yawDegrees < 0.0f ) change.markerAfter.yawDegrees += 360.0f;
                    if ( change.markerAfter.yawDegrees >= 360.0f ) change.markerAfter.yawDegrees -= 360.0f;
                }
            }
            if ( bCopy ) {
                if ( document.markers.nCount >= TILE_MAP_MAX_MARKERS ||
                     nCopiedDoors >= TILE_MAP_MAX_MARKERS - document.markers.nCount ) {
                    return tile_map_document_status_t::MARKER_LIMIT_REACHED;
                }
                if ( !UniqueId_CreateRandom( &change.markerAfter.id ) ) {
                    return tile_map_document_status_t::IDENTITY_CREATION_FAILED;
                }
                change.iElement = document.markers.nCount + nCopiedDoors++;
                change.markerBefore = {};
                change.bMarkerBeforeExists = CY_FALSE;
            } else if ( TileMapMarker_Equals( marker, change.markerAfter ) ) {
                continue;
            }
        }
        const auto status = TileMapRegion_PushChange( changes, change );
        if ( status != tile_map_document_status_t::OK ) return status;
    }
    if ( nCopiedDoors != 0u &&
         !Vector_Reserve( &document.markers, document.markers.nCount + nCopiedDoors ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    const char *label = "Move selection";
    switch ( operation ) {
        case tile_map_region_operation_t::MOVE: break;
        case tile_map_region_operation_t::COPY: label = "Duplicate selection"; break;
        case tile_map_region_operation_t::DELETE: label = "Delete selection"; break;
        case tile_map_region_operation_t::HEIGHT: label = "Change selection elevation"; break;
        case tile_map_region_operation_t::ROTATE: label = "Rotate selection clockwise"; break;
    }
    return TileMapDocument_ApplyBatch( document,
        { changes.pData, changes.nCount }, StringView_FromCString( label ) );
}

enum class tile_map_selection_operation_t { MOVE, COPY, DELETE, FLOOR_HEIGHT, WALL_HEIGHT, ROTATE, PROPERTIES };

struct tile_map_selection_destination_t {
    usize destination{ 0u };
    usize source{ 0u };
};

tile_map_document_status_t TileMapSelection_PatchCell( const tile_map_cell_t &before,
    const tile_map_selection_patch_t &patch, tile_map_cell_t &after ) noexcept
{
    after = before;
    const bool enableFlag = ( patch.fields & TILE_MAP_SELECTION_PROPERTY_FLOOR_ENABLED ) != 0u;
    if ( enableFlag && !patch.bFloorEnabled ) {
        after = {};
        return tile_map_document_status_t::OK;
    }
    if ( !TileMapCell_HasFloor( before ) ) {
        if ( !enableFlag ) return tile_map_document_status_t::OK;
        after = {};
        after.flags = TILE_MAP_CELL_FLAG_FLOOR;
    }
    if ( patch.fields & TILE_MAP_SELECTION_PROPERTY_FLOOR_LEVEL ) after.nFloorLevel = patch.nFloorLevel;
    if ( patch.fields & TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT ) after.nWallHeightLevels = patch.nWallHeightLevels;
    if ( patch.fields & TILE_MAP_SELECTION_PROPERTY_MATERIAL ) after.nMaterialSlot = patch.nMaterialSlot;
    if ( patch.fields & TILE_MAP_SELECTION_PROPERTY_SHAPE ) after.shape = patch.shape;
    if ( patch.fields & TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS ) after.nStairSteps = patch.nStairSteps;
    const tile_map_paint_t properties{ after.nFloorLevel, after.nWallHeightLevels,
        after.nMaterialSlot, after.shape, after.nStairSteps };
    return TileMapPaint_IsValid( properties ) ? tile_map_document_status_t::OK
        : tile_map_document_status_t::INVALID_CELL;
}

tile_map_document_status_t TileMapDocument_EditSelection(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection,
    tile_map_selection_operation_t operation, i32 dx = 0, i32 dy = 0, i32 delta = 0,
    const tile_map_selection_patch_t &patch = {} ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) return tile_map_document_status_t::NOT_INITIALIZED;
    if ( CypherTileMapDocument_IsEditGroupOpen( pDocument ) ) return tile_map_document_status_t::INVALID_STATE;
    if ( selection.nCount != 0u && selection.pData == nullptr ) return tile_map_document_status_t::INVALID_ARGUMENT;
    auto &document = *pDocument;
    constexpr flags32_t allFields = TILE_MAP_SELECTION_PROPERTY_FLOOR_ENABLED |
        TILE_MAP_SELECTION_PROPERTY_FLOOR_LEVEL | TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT |
        TILE_MAP_SELECTION_PROPERTY_MATERIAL | TILE_MAP_SELECTION_PROPERTY_SHAPE |
        TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS;
    if ( operation == tile_map_selection_operation_t::PROPERTIES ) {
        if ( ( patch.fields & ~allFields ) != 0u ) return tile_map_document_status_t::INVALID_ARGUMENT;
        if ( ( ( patch.fields & TILE_MAP_SELECTION_PROPERTY_WALL_HEIGHT ) && patch.nWallHeightLevels == 0u ) ||
             ( ( patch.fields & TILE_MAP_SELECTION_PROPERTY_SHAPE ) && !CypherTileMapCellShape_IsValid( patch.shape ) ) ||
             ( ( patch.fields & TILE_MAP_SELECTION_PROPERTY_STAIR_STEPS ) &&
               ( patch.nStairSteps < TILE_MAP_MIN_STAIR_STEPS || patch.nStairSteps > TILE_MAP_MAX_STAIR_STEPS ) ) )
            return tile_map_document_status_t::INVALID_CELL;
    }
    for ( usize i = 0u; i < selection.nCount; ++i ) {
        if ( !CypherTileMapDocument_ContainsCell( &document, selection.pData[i] ) )
            return tile_map_document_status_t::OUT_OF_BOUNDS;
    }
    if ( selection.nCount == 0u ||
         ( operation == tile_map_selection_operation_t::MOVE && dx == 0 && dy == 0 ) ||
         ( ( operation == tile_map_selection_operation_t::FLOOR_HEIGHT ||
             operation == tile_map_selection_operation_t::WALL_HEIGHT ) && delta == 0 ) ||
         ( operation == tile_map_selection_operation_t::PROPERTIES && patch.fields == 0u ) )
        return tile_map_document_status_t::OK;

    // A bounded one-byte membership table deduplicates even repeated coordinates
    // without growing storage with the input's duplicate count. Sorted unique
    // indices then let source/destination merges avoid per-cell linear searches.
    vector_t<u8> membership{};
    vector_t<usize> selected{};
    if ( !Vector_Init( &membership, document.pAllocator, document.cells.nCount ) ||
         !Vector_Resize( &membership, document.cells.nCount ) ||
         !Vector_Init( &selected, document.pAllocator,
             std::min( selection.nCount, document.cells.nCount ) ) )
        return tile_map_document_status_t::ALLOCATION_FAILED;
    std::fill_n( membership.pData, membership.nCount, u8{ 0u } );
    i32 minX = static_cast<i32>( document.nWidth ), minY = static_cast<i32>( document.nHeight );
    i32 maxY = 0;
    for ( usize i = 0u; i < selection.nCount; ++i ) {
        const auto point = selection.pData[i];
        const usize index = TileMapCellIndex( document, point );
        if ( membership.pData[index] != 0u ) continue;
        membership.pData[index] = 1u;
        if ( !Vector_PushBack( &selected, index ) ) return tile_map_document_status_t::ALLOCATION_FAILED;
        minX = std::min( minX, point.x );
        minY = std::min( minY, point.y );
        maxY = std::max( maxY, point.y );
    }
    std::sort( selected.pData, selected.pData + selected.nCount );
    const bool copy = operation == tile_map_selection_operation_t::COPY;
    const bool rotate = operation == tile_map_selection_operation_t::ROTATE;
    const bool transfer = copy || rotate || operation == tile_map_selection_operation_t::MOVE;
    const bool erase = operation == tile_map_selection_operation_t::DELETE ||
        ( operation == tile_map_selection_operation_t::PROPERTIES &&
          ( patch.fields & TILE_MAP_SELECTION_PROPERTY_FLOOR_ENABLED ) && !patch.bFloorEnabled );
    const auto destinationPoint = [&]( tile_map_grid_coord_t point ) noexcept {
        return rotate ? tile_map_grid_coord_t{ minX + maxY - point.y, minY + point.x - minX }
            : tile_map_grid_coord_t{ static_cast<i32>( static_cast<i64>( point.x ) + dx ),
                                    static_cast<i32>( static_cast<i64>( point.y ) + dy ) };
    };
    vector_t<tile_map_selection_destination_t> destinations{};
    if ( !Vector_Init( &destinations, document.pAllocator, transfer ? selected.nCount : 0u ) )
        return tile_map_document_status_t::ALLOCATION_FAILED;
    if ( transfer ) {
        for ( usize i = 0u; i < selected.nCount; ++i ) {
            const usize source = selected.pData[i];
            const tile_map_grid_coord_t point{ static_cast<i32>( source % document.nWidth ),
                static_cast<i32>( source / document.nWidth ) };
            const i64 x = rotate ? static_cast<i64>( minX ) + maxY - point.y : static_cast<i64>( point.x ) + dx;
            const i64 y = rotate ? static_cast<i64>( minY ) + point.x - minX : static_cast<i64>( point.y ) + dy;
            if ( x < 0 || y < 0 || x >= document.nWidth || y >= document.nHeight )
                return tile_map_document_status_t::OUT_OF_BOUNDS;
            const usize destination = TileMapCellIndex( document, { static_cast<i32>( x ), static_cast<i32>( y ) } );
            if ( ( copy || membership.pData[destination] == 0u ) &&
                 !CypherTileMapCell_IsCanonicalEmpty( document.cells.pData[destination] ) )
                return tile_map_document_status_t::INVALID_STATE;
            if ( !Vector_PushBack( &destinations, tile_map_selection_destination_t{ destination, source } ) )
                return tile_map_document_status_t::ALLOCATION_FAILED;
        }
        std::sort( destinations.pData, destinations.pData + destinations.nCount,
            []( const auto &left, const auto &right ) noexcept { return left.destination < right.destination; } );
        for ( usize i = 0u; i < document.markers.nCount; ++i ) {
            const auto point = document.markers.pData[i].cell;
            if ( !CypherTileMapDocument_ContainsCell( &document, point ) ) continue;
            const usize index = TileMapCellIndex( document, point );
            const auto *found = std::lower_bound( destinations.pData, destinations.pData + destinations.nCount,
                index, []( const auto &entry, usize value ) noexcept { return entry.destination < value; } );
            if ( found != destinations.pData + destinations.nCount && found->destination == index &&
                 ( copy || membership.pData[index] == 0u ) ) return tile_map_document_status_t::INVALID_STATE;
        }
    }

    vector_t<tile_map_history_change_t> changes{};
    if ( !Vector_Init( &changes, document.pAllocator ) ) return tile_map_document_status_t::ALLOCATION_FAILED;
    usize sourceCursor = 0u, destinationCursor = 0u;
    while ( sourceCursor < selected.nCount || destinationCursor < destinations.nCount ) {
        const usize sourceIndex = sourceCursor < selected.nCount ? selected.pData[sourceCursor] : CY_USIZE_MAX;
        const usize destinationIndex = destinationCursor < destinations.nCount
            ? destinations.pData[destinationCursor].destination : CY_USIZE_MAX;
        const usize index = std::min( sourceIndex, destinationIndex );
        const auto &before = document.cells.pData[index];
        tile_map_cell_t after = before;
        if ( destinationIndex == index ) {
            after = document.cells.pData[destinations.pData[destinationCursor].source];
            if ( rotate ) after.shape = TileMapRegion_RotateShape( after.shape );
        } else if ( erase || ( transfer && !copy ) ) {
            after = {};
        } else if ( operation == tile_map_selection_operation_t::PROPERTIES ) {
            const auto status = TileMapSelection_PatchCell( before, patch, after );
            if ( status != tile_map_document_status_t::OK ) return status;
        } else if ( TileMapCell_HasFloor( before ) ) {
            if ( operation == tile_map_selection_operation_t::FLOOR_HEIGHT ) {
                const i64 level = static_cast<i64>( before.nFloorLevel ) + delta;
                const i64 maximum = before.shape == tile_map_cell_shape_t::FLAT ? CY_I16_MAX : CY_I16_MAX - 1;
                if ( level < CY_I16_MIN || level > maximum ) return tile_map_document_status_t::INVALID_CELL;
                after.nFloorLevel = static_cast<i16>( level );
            } else if ( operation == tile_map_selection_operation_t::WALL_HEIGHT ) {
                const i64 height = static_cast<i64>( before.nWallHeightLevels ) + delta;
                if ( height < 1 || height > CY_U16_MAX ) return tile_map_document_status_t::INVALID_CELL;
                after.nWallHeightLevels = static_cast<u16>( height );
            }
        }
        if ( !TileMapCell_Equals( before, after ) ) {
            const auto status = TileMapRegion_PushChange( changes,
                { tile_map_history_change_kind_t::CELL, index, before, after } );
            if ( status != tile_map_document_status_t::OK ) return status;
        }
        if ( sourceIndex == index ) ++sourceCursor;
        if ( destinationIndex == index ) ++destinationCursor;
    }

    usize copiedDoors = 0u;
    if ( erase || transfer ) {
        // Descending removal preserves marker indices through undo/redo. Copies
        // append new identities, while moves and rotations replace in place.
        for ( usize i = document.markers.nCount; i != 0u; --i ) {
            const usize index = i - 1u;
            const auto &marker = document.markers.pData[index];
            if ( !CypherTileMapDocument_ContainsCell( &document, marker.cell ) ||
                 membership.pData[TileMapCellIndex( document, marker.cell )] == 0u ||
                 ( copy && marker.kind != tile_map_marker_kind_t::DOOR ) ) continue;
            tile_map_history_change_t change{};
            change.kind = tile_map_history_change_kind_t::MARKER;
            change.iElement = index;
            change.markerBefore = marker;
            change.bMarkerBeforeExists = CY_TRUE;
            if ( !erase ) {
                change.markerAfter = marker;
                change.bMarkerAfterExists = CY_TRUE;
                change.markerAfter.cell = destinationPoint( marker.cell );
                if ( rotate ) {
                    change.markerAfter.side = TileMapRegion_RotateSide( marker.side );
                    if ( marker.kind == tile_map_marker_kind_t::PLAYER_SPAWN ) {
                        if ( !std::isfinite( marker.yawDegrees ) ) return tile_map_document_status_t::INVALID_CELL;
                        change.markerAfter.yawDegrees = std::fmod( marker.yawDegrees, 360.0f ) + 90.0f;
                        if ( change.markerAfter.yawDegrees < 0.0f ) change.markerAfter.yawDegrees += 360.0f;
                        if ( change.markerAfter.yawDegrees >= 360.0f ) change.markerAfter.yawDegrees -= 360.0f;
                    }
                }
                if ( copy ) {
                    if ( document.markers.nCount >= TILE_MAP_MAX_MARKERS ||
                         copiedDoors >= TILE_MAP_MAX_MARKERS - document.markers.nCount )
                        return tile_map_document_status_t::MARKER_LIMIT_REACHED;
                    if ( !UniqueId_CreateRandom( &change.markerAfter.id ) )
                        return tile_map_document_status_t::IDENTITY_CREATION_FAILED;
                    change.iElement = document.markers.nCount + copiedDoors++;
                    change.markerBefore = {};
                    change.bMarkerBeforeExists = CY_FALSE;
                } else if ( TileMapMarker_Equals( marker, change.markerAfter ) ) continue;
            }
            const auto status = TileMapRegion_PushChange( changes, change );
            if ( status != tile_map_document_status_t::OK ) return status;
        }
    }
    if ( copiedDoors != 0u && !Vector_Reserve( &document.markers, document.markers.nCount + copiedDoors ) )
        return tile_map_document_status_t::ALLOCATION_FAILED;
    const char *label = "Move selection";
    switch ( operation ) {
        case tile_map_selection_operation_t::MOVE: break;
        case tile_map_selection_operation_t::COPY: label = "Duplicate selection"; break;
        case tile_map_selection_operation_t::DELETE: label = "Delete selection"; break;
        case tile_map_selection_operation_t::FLOOR_HEIGHT: label = "Change selection elevation"; break;
        case tile_map_selection_operation_t::WALL_HEIGHT: label = "Change selection wall height"; break;
        case tile_map_selection_operation_t::ROTATE: label = "Rotate selection clockwise"; break;
        case tile_map_selection_operation_t::PROPERTIES: label = "Change selection properties"; break;
    }
    return TileMapDocument_ApplyBatch( document, { changes.pData, changes.nCount }, StringView_FromCString( label ) );
}

} // namespace

bool_t CypherTileMapDocument_IsInitialized(
    const tile_map_document_t *pDocument ) noexcept
{
    if ( pDocument == nullptr ||
         !Allocator_IsValid( pDocument->pAllocator ) ||
         !UniqueId_IsValid( pDocument->mapId ) ||
         !TileMapDocument_AreDimensionsValid(
             pDocument->nWidth,
             pDocument->nHeight ) ||
         !TileMapDocument_AreMetricsValid(
             pDocument->nCellSize,
             pDocument->nLevelHeight ) ||
         !Vector_IsValid( &pDocument->cells ) ||
         !Vector_IsValid( &pDocument->markers ) ||
         !Vector_IsValid( &pDocument->materialBindings ) ||
         pDocument->cells.pAllocator != pDocument->pAllocator ||
         pDocument->markers.pAllocator != pDocument->pAllocator ||
         pDocument->materialBindings.pAllocator != pDocument->pAllocator ||
         !TileMapHistory_IsValid( *pDocument ) ) {
        return CY_FALSE;
    }

    const usize nExpectedCellCount =
        static_cast<usize>( pDocument->nWidth ) * pDocument->nHeight;
    return Vector_Count( &pDocument->cells ) == nExpectedCellCount &&
           pDocument->nCurrentRevision != 0u &&
           pDocument->nNextRevision != 1u;
}

tile_map_document_status_t CypherTileMapDocument_Init(
    tile_map_document_t *pDocument,
    const allocator_t *pAllocator,
    const tile_map_document_desc_t &desc ) noexcept
{
    if ( pDocument == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }
    if ( CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::ALREADY_INITIALIZED;
    }
    if ( !TileMapDocument_IsCanonicalEmpty( *pDocument ) ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    if ( !TileMapDocument_AreDimensionsValid(
             desc.nWidth,
             desc.nHeight ) ) {
        return tile_map_document_status_t::INVALID_DIMENSIONS;
    }
    if ( !TileMapDocument_AreMetricsValid(
             desc.nCellSize,
             desc.nLevelHeight ) ) {
        return tile_map_document_status_t::INVALID_METRICS;
    }

    unique_id_t mapId{};
    if ( !UniqueId_CreateRandom( &mapId ) ) {
        return tile_map_document_status_t::IDENTITY_CREATION_FAILED;
    }

    const usize nCellCount =
        static_cast<usize>( desc.nWidth ) * desc.nHeight;
    vector_t<tile_map_cell_t> cells{};
    vector_t<tile_map_marker_t> markers{};
    vector_t<tile_map_material_binding_t> materialBindings{};
    if ( !Vector_Init( &cells, pAllocator, nCellCount ) ||
         !Vector_Resize( &cells, nCellCount ) ||
         !Vector_Init( &markers, pAllocator, 8u ) ||
         !Vector_Init( &materialBindings, pAllocator, 0u ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    void *pHistoryMemory = Allocator_Allocate(
        pAllocator,
        sizeof( tile_map_history_state_t ),
        alignof( tile_map_history_state_t ) );
    if ( pHistoryMemory == nullptr ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    auto *pHistory = ::new ( pHistoryMemory ) tile_map_history_state_t{};
    if ( !Vector_Init( &pHistory->activeChanges, pAllocator, 64u ) ) {
        pHistory->~tile_map_history_state_t();
        Allocator_Free(
            pAllocator,
            pHistoryMemory,
            sizeof( tile_map_history_state_t ),
            alignof( tile_map_history_state_t ) );
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    pDocument->pAllocator = pAllocator;
    pDocument->mapId = mapId;
    pDocument->nWidth = desc.nWidth;
    pDocument->nHeight = desc.nHeight;
    pDocument->nCellSize = desc.nCellSize;
    pDocument->nLevelHeight = desc.nLevelHeight;
    Vector_Move( &pDocument->cells, &cells );
    Vector_Move( &pDocument->markers, &markers );
    Vector_Move( &pDocument->materialBindings, &materialBindings );
    pDocument->nCurrentRevision = 1u;
    pDocument->nSavedRevision = 0u;
    pDocument->nNextRevision = 2u;
    pDocument->pHistoryState = pHistory;
    return tile_map_document_status_t::OK;
}

tile_map_document_status_t CypherTileMapDocument_SetDescription(
    tile_map_document_t *pDocument,
    const tile_map_document_desc_t &desc ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    auto &history = *pDocument->pHistoryState;
    if ( history.bGroupOpen ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    if ( !TileMapDocument_AreDimensionsValid( desc.nWidth, desc.nHeight ) ) {
        return tile_map_document_status_t::INVALID_DIMENSIONS;
    }
    if ( !TileMapDocument_AreMetricsValid( desc.nCellSize, desc.nLevelHeight ) ) {
        return tile_map_document_status_t::INVALID_METRICS;
    }
    if ( pDocument->nWidth == desc.nWidth && pDocument->nHeight == desc.nHeight &&
         pDocument->nCellSize == desc.nCellSize && pDocument->nLevelHeight == desc.nLevelHeight ) {
        return tile_map_document_status_t::OK;
    }
    if ( desc.nWidth < pDocument->nWidth || desc.nHeight < pDocument->nHeight ) {
        for ( u32 y = 0u; y < pDocument->nHeight; ++y ) {
            const u32 xBegin = y >= desc.nHeight ? 0u : std::min( desc.nWidth, pDocument->nWidth );
            for ( u32 x = xBegin; x < pDocument->nWidth; ++x ) {
                if ( !CypherTileMapCell_IsCanonicalEmpty(
                         pDocument->cells.pData[static_cast<usize>( y ) * pDocument->nWidth + x] ) ) {
                    return tile_map_document_status_t::OUT_OF_BOUNDS;
                }
            }
        }
        for ( usize i = 0u; i < pDocument->markers.nCount; ++i ) {
            const auto coordinate = pDocument->markers.pData[i].cell;
            if ( coordinate.x < 0 || coordinate.y < 0 ||
                 static_cast<u32>( coordinate.x ) >= desc.nWidth ||
                 static_cast<u32>( coordinate.y ) >= desc.nHeight ) {
                return tile_map_document_status_t::OUT_OF_BOUNDS;
            }
        }
    }
    if ( pDocument->nNextRevision == 0u ) {
        return tile_map_document_status_t::HISTORY_LIMIT_REACHED;
    }
    if ( !TileMapDocument_DescriptionFitsWorld( *pDocument, desc ) ) {
        return tile_map_document_status_t::INVALID_METRICS;
    }

    // Stage any replacement storage without touching the live grid or redo
    // branch. Reuse existing capacity when possible, including during shrink.
    const usize nNewCount = static_cast<usize>( desc.nWidth ) * desc.nHeight;
    vector_t<tile_map_cell_t> pendingCells{};
    if ( nNewCount > Vector_Capacity( &pDocument->cells ) ) {
        if ( !Vector_Init( &pendingCells, pDocument->pAllocator, nNewCount ) ||
             !Vector_Resize( &pendingCells, pDocument->cells.nCount ) ) {
            return tile_map_document_status_t::ALLOCATION_FAILED;
        }
        Cy_MemCopy( pendingCells.pData, pDocument->cells.pData,
            pDocument->cells.nCount * sizeof( tile_map_cell_t ) );
    }
    auto *pChange = Allocator_AllocateArrayStorage<tile_map_description_history_change_t>(
        pDocument->pAllocator, 1u );
    if ( pChange == nullptr ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    pChange->before = { pDocument->nWidth, pDocument->nHeight,
        pDocument->nCellSize, pDocument->nLevelHeight };
    pChange->after = desc;

    // All fallible work is complete. Capacity now guarantees row remapping,
    // history traversal, and revision publication cannot allocate or fail.
    if ( pendingCells.pAllocator != nullptr ) {
        Vector_Shutdown( &pDocument->cells );
        Vector_Move( &pDocument->cells, &pendingCells );
    }
    (void) TileMapDocument_ApplyDescription( *pDocument, desc );
    tile_map_history_entry_t pending{};
    pending.pDescriptionChange = pChange;
    pending.cbChanges = sizeof( *pChange );
    pending.nBeforeRevision = pDocument->nCurrentRevision;
    pending.nAfterRevision = TileMapDocument_AllocateRevision( *pDocument );
    TileMap_CopyLabel( pending.label, pending.cchLabel,
        StringView_FromCString( "Map properties" ) );
    TileMapHistory_DiscardRedo( *pDocument );
    while ( history.nEntryCount == TILE_MAP_HISTORY_MAX_ENTRIES ||
            pending.cbChanges > TILE_MAP_HISTORY_BYTE_BUDGET - history.cbHistory ) {
        TileMapHistory_EvictOldest( *pDocument );
    }
    history.entries[history.nEntryCount++] = pending;
    history.iCursor = history.nEntryCount;
    history.cbHistory += pending.cbChanges;
    pDocument->nCurrentRevision = pending.nAfterRevision;
    return tile_map_document_status_t::OK;
}

void CypherTileMapDocument_Shutdown(
    tile_map_document_t *pDocument ) noexcept
{
    if ( pDocument == nullptr ) {
        return;
    }

    if ( pDocument->pHistoryState != nullptr &&
         Allocator_IsValid( pDocument->pAllocator ) ) {
        CypherTileMapDocument_CancelEditGroup( pDocument );
        TileMapHistory_ClearCommitted( *pDocument );
        tile_map_history_state_t *pHistory = pDocument->pHistoryState;
        pHistory->~tile_map_history_state_t();
        Allocator_Free(
            pDocument->pAllocator,
            pHistory,
            sizeof( tile_map_history_state_t ),
            alignof( tile_map_history_state_t ) );
        pDocument->pHistoryState = nullptr;
    }
    if ( Vector_IsValid( &pDocument->materialBindings ) ) {
        Vector_Shutdown( &pDocument->materialBindings );
    }
    if ( Vector_IsValid( &pDocument->markers ) ) {
        Vector_Shutdown( &pDocument->markers );
    }
    if ( Vector_IsValid( &pDocument->cells ) ) {
        Vector_Shutdown( &pDocument->cells );
    }
    TileMapDocument_ResetMetadata( *pDocument );
}

bool_t CypherTileMapDocument_IsDirty(
    const tile_map_document_t *pDocument ) noexcept
{
    return CypherTileMapDocument_IsInitialized( pDocument ) &&
           pDocument->nCurrentRevision != pDocument->nSavedRevision;
}

void CypherTileMapDocument_MarkSaved(
    tile_map_document_t *pDocument ) noexcept
{
    if ( CypherTileMapDocument_IsInitialized( pDocument ) &&
         !CypherTileMapDocument_IsEditGroupOpen( pDocument ) ) {
        pDocument->nSavedRevision = pDocument->nCurrentRevision;
    }
}

bool_t CypherTileMapDocument_ContainsCell(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept
{
    return CypherTileMapDocument_IsInitialized( pDocument ) &&
           coordinate.x >= 0 &&
           coordinate.y >= 0 &&
           static_cast<u32>( coordinate.x ) < pDocument->nWidth &&
           static_cast<u32>( coordinate.y ) < pDocument->nHeight;
}

tile_map_cell_t *CypherTileMapDocument_CellAt(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept
{
    return CypherTileMapDocument_ContainsCell( pDocument, coordinate )
        ? pDocument->cells.pData +
              TileMapCellIndex( *pDocument, coordinate )
        : nullptr;
}

const tile_map_cell_t *CypherTileMapDocument_CellAt(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept
{
    return CypherTileMapDocument_ContainsCell( pDocument, coordinate )
        ? pDocument->cells.pData +
              TileMapCellIndex( *pDocument, coordinate )
        : nullptr;
}

bool_t CypherTileMapDocument_CellHasFloor(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept
{
    const tile_map_cell_t *pCell =
        CypherTileMapDocument_CellAt( pDocument, coordinate );
    return pCell != nullptr && TileMapCell_HasFloor( *pCell );
}

bool_t CypherTileMapCell_IsCanonicalEmpty(
    const tile_map_cell_t &cell ) noexcept
{
    return TileMapCell_Equals( cell, tile_map_cell_t{} );
}

bool_t CypherTileMapCellShape_IsValid( tile_map_cell_shape_t shape ) noexcept
{
    return shape >= tile_map_cell_shape_t::FLAT &&
           shape <= tile_map_cell_shape_t::STAIRS_WEST;
}

const char *CypherTileMapCellShape_Name( tile_map_cell_shape_t shape ) noexcept
{
    switch ( shape ) {
        case tile_map_cell_shape_t::FLAT: return "flat";
        case tile_map_cell_shape_t::STAIRS_NORTH: return "stairs_north";
        case tile_map_cell_shape_t::STAIRS_EAST: return "stairs_east";
        case tile_map_cell_shape_t::STAIRS_SOUTH: return "stairs_south";
        case tile_map_cell_shape_t::STAIRS_WEST: return "stairs_west";
    }
    return "unknown";
}

tile_map_document_status_t CypherTileMapDocument_BeginEditGroup(
    tile_map_document_t *pDocument,
    string_view_t label ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !StringView_IsValid( label ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }
    tile_map_history_state_t &history = *pDocument->pHistoryState;
    if ( history.bGroupOpen ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    Vector_Clear( &history.activeChanges );
    history.bGroupOpen = CY_TRUE;
    history.nGroupBeforeRevision = pDocument->nCurrentRevision;
    history.nGroupAfterRevision = 0u;
    TileMap_CopyLabel(
        history.activeLabel,
        history.cchActiveLabel,
        label );
    return tile_map_document_status_t::OK;
}

tile_map_document_status_t CypherTileMapDocument_CommitEditGroup(
    tile_map_document_t *pDocument ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    tile_map_history_state_t &history = *pDocument->pHistoryState;
    if ( !history.bGroupOpen ) {
        return tile_map_document_status_t::INVALID_STATE;
    }

    const usize nChangeCount = Vector_Count( &history.activeChanges );
    if ( nChangeCount == 0u ) {
        history.bGroupOpen = CY_FALSE;
        history.nGroupBeforeRevision = 0u;
        history.nGroupAfterRevision = 0u;
        history.cchActiveLabel = 0u;
        history.activeLabel[0] = '\0';
        return tile_map_document_status_t::OK;
    }

    bool_t bMayIncreaseActiveCells = CY_FALSE;
    for ( usize iChange = 0u;
          iChange < nChangeCount;
          ++iChange ) {
        const tile_map_history_change_t &change =
            history.activeChanges.pData[iChange];
        if ( change.kind == tile_map_history_change_kind_t::CELL &&
             !TileMapCell_HasFloor( change.cellBefore ) &&
             TileMapCell_HasFloor( change.cellAfter ) ) {
            bMayIncreaseActiveCells = CY_TRUE;
            break;
        }
    }
    if ( bMayIncreaseActiveCells ) {
        usize nActiveCells = 0u;
        for ( usize iCell = 0u;
              iCell < Vector_Count( &pDocument->cells );
              ++iCell ) {
            nActiveCells += TileMapCell_HasFloor(
                pDocument->cells.pData[iCell] );
        }
        if ( nActiveCells > TILE_MAP_MAX_ACTIVE_CELLS ) {
            return tile_map_document_status_t::ACTIVE_CELL_LIMIT_REACHED;
        }
    }

    vector_t<tile_map_history_change_t> normalized{};
    const tile_map_document_status_t normalizeStatus =
        TileMapHistory_NormalizeActiveChanges(
            *pDocument,
            normalized );
    if ( normalizeStatus != tile_map_document_status_t::OK ) {
        return normalizeStatus;
    }
    const usize nNormalizedChangeCount = Vector_Count( &normalized );
    if ( nNormalizedChangeCount == 0u ) {
        pDocument->nCurrentRevision = history.nGroupBeforeRevision;
        Vector_Clear( &history.activeChanges );
        history.bGroupOpen = CY_FALSE;
        history.nGroupBeforeRevision = 0u;
        history.nGroupAfterRevision = 0u;
        history.cchActiveLabel = 0u;
        history.activeLabel[0] = '\0';
        return tile_map_document_status_t::OK;
    }

    usize cbChanges = 0u;
    if ( !Cy_TryArrayByteCount<tile_map_history_change_t>(
             nNormalizedChangeCount,
             cbChanges ) ||
         cbChanges > TILE_MAP_HISTORY_BYTE_BUDGET ) {
        return tile_map_document_status_t::HISTORY_LIMIT_REACHED;
    }

    tile_map_history_change_t *pCopy =
        Allocator_AllocateArrayStorage<tile_map_history_change_t>(
            pDocument->pAllocator,
            nNormalizedChangeCount );
    if ( pCopy == nullptr ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    Cy_MemCopy(
        pCopy,
        normalized.pData,
        cbChanges );

    // Every fallible step completed. History can now branch and evict entries
    // without risking loss on allocation failure.
    TileMapHistory_DiscardRedo( *pDocument );
    while ( history.nEntryCount == TILE_MAP_HISTORY_MAX_ENTRIES ||
            cbChanges > TILE_MAP_HISTORY_BYTE_BUDGET - history.cbHistory ) {
        TileMapHistory_EvictOldest( *pDocument );
    }

    tile_map_history_entry_t &entry =
        history.entries[history.nEntryCount++];
    entry.pChanges = pCopy;
    entry.nChangeCount = nNormalizedChangeCount;
    entry.cbChanges = cbChanges;
    entry.nBeforeRevision = history.nGroupBeforeRevision;
    entry.nAfterRevision = history.nGroupAfterRevision;
    TileMap_CopyLabel(
        entry.label,
        entry.cchLabel,
        { history.activeLabel, history.cchActiveLabel } );
    history.iCursor = history.nEntryCount;
    history.cbHistory += cbChanges;

    Vector_Clear( &history.activeChanges );
    history.bGroupOpen = CY_FALSE;
    history.nGroupBeforeRevision = 0u;
    history.nGroupAfterRevision = 0u;
    history.cchActiveLabel = 0u;
    history.activeLabel[0] = '\0';
    return tile_map_document_status_t::OK;
}

void CypherTileMapDocument_CancelEditGroup(
    tile_map_document_t *pDocument ) noexcept
{
    if ( pDocument == nullptr ||
         pDocument->pHistoryState == nullptr ) {
        return;
    }
    tile_map_history_state_t &history = *pDocument->pHistoryState;
    if ( !history.bGroupOpen ) {
        return;
    }
    (void) TileMap_ApplyChanges(
        *pDocument,
        history.activeChanges.pData,
        Vector_Count( &history.activeChanges ),
        CY_FALSE );
    pDocument->nCurrentRevision = history.nGroupBeforeRevision;
    Vector_Clear( &history.activeChanges );
    history.bGroupOpen = CY_FALSE;
    history.nGroupBeforeRevision = 0u;
    history.nGroupAfterRevision = 0u;
    history.cchActiveLabel = 0u;
    history.activeLabel[0] = '\0';
}

bool_t CypherTileMapDocument_IsEditGroupOpen(
    const tile_map_document_t *pDocument ) noexcept
{
    return pDocument != nullptr &&
           pDocument->pHistoryState != nullptr &&
           pDocument->pHistoryState->bGroupOpen;
}

tile_map_document_status_t CypherTileMapDocument_PaintCell(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    const tile_map_paint_t &paint ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !TileMapPaint_IsValid( paint ) ) {
        return tile_map_document_status_t::INVALID_CELL;
    }
    if ( !CypherTileMapDocument_ContainsCell( pDocument, coordinate ) ) {
        return tile_map_document_status_t::OUT_OF_BOUNDS;
    }
    const tile_map_grid_rect_t rect{ coordinate.x, coordinate.y, 1u, 1u };
    const tile_map_cell_t replacement{
        paint.nFloorLevel,
        paint.nWallHeightLevels,
        paint.nMaterialSlot,
        TILE_MAP_CELL_FLAG_FLOOR,
        paint.shape,
        paint.nStairSteps
    };
    return TileMapDocument_EditRect(
        *pDocument,
        rect,
        replacement,
        StringView_FromCString( "Paint tile" ) );
}

tile_map_document_status_t CypherTileMapDocument_EraseCell(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !CypherTileMapDocument_ContainsCell( pDocument, coordinate ) ) {
        return tile_map_document_status_t::OUT_OF_BOUNDS;
    }
    return TileMapDocument_EditRect(
        *pDocument,
        { coordinate.x, coordinate.y, 1u, 1u },
        {},
        StringView_FromCString( "Erase tile" ) );
}

tile_map_document_status_t CypherTileMapDocument_PaintRect(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    const tile_map_paint_t &paint ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !TileMapPaint_IsValid( paint ) ) {
        return tile_map_document_status_t::INVALID_CELL;
    }
    return TileMapDocument_EditRect(
        *pDocument,
        rect,
        {
            paint.nFloorLevel,
            paint.nWallHeightLevels,
            paint.nMaterialSlot,
            TILE_MAP_CELL_FLAG_FLOOR,
            paint.shape,
            paint.nStairSteps
        },
        StringView_FromCString( "Paint rectangle" ) );
}

tile_map_document_status_t CypherTileMapDocument_EraseRect(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    return TileMapDocument_EditRect(
        *pDocument,
        rect,
        {},
        StringView_FromCString( "Erase rectangle" ) );
}

tile_map_document_status_t CypherTileMapDocument_MoveRegion(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    i32 dx,
    i32 dy ) noexcept
{
    return TileMapDocument_EditRegion(
        pDocument, rect, tile_map_region_operation_t::MOVE, dx, dy );
}

tile_map_document_status_t CypherTileMapDocument_CopyRegion(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    i32 dx,
    i32 dy ) noexcept
{
    return TileMapDocument_EditRegion(
        pDocument, rect, tile_map_region_operation_t::COPY, dx, dy );
}

tile_map_document_status_t CypherTileMapDocument_DeleteRegion(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect ) noexcept
{
    return TileMapDocument_EditRegion(
        pDocument, rect, tile_map_region_operation_t::DELETE );
}

tile_map_document_status_t CypherTileMapDocument_AdjustRegionFloorLevel(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect,
    i32 delta ) noexcept
{
    return TileMapDocument_EditRegion(
        pDocument, rect, tile_map_region_operation_t::HEIGHT, 0, 0, delta );
}

tile_map_document_status_t CypherTileMapDocument_RotateRegionClockwise(
    tile_map_document_t *pDocument,
    const tile_map_grid_rect_t &rect ) noexcept
{
    return TileMapDocument_EditRegion(
        pDocument, rect, tile_map_region_operation_t::ROTATE );
}

tile_map_document_status_t CypherTileMapDocument_MoveSelection(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection, i32 dx, i32 dy ) noexcept
{
    return TileMapDocument_EditSelection( pDocument, selection, tile_map_selection_operation_t::MOVE, dx, dy );
}

tile_map_document_status_t CypherTileMapDocument_CopySelection(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection, i32 dx, i32 dy ) noexcept
{
    return TileMapDocument_EditSelection( pDocument, selection, tile_map_selection_operation_t::COPY, dx, dy );
}

tile_map_document_status_t CypherTileMapDocument_DeleteSelection(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection ) noexcept
{
    return TileMapDocument_EditSelection( pDocument, selection, tile_map_selection_operation_t::DELETE );
}

tile_map_document_status_t CypherTileMapDocument_AdjustSelectionFloorLevel(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection, i32 delta ) noexcept
{
    return TileMapDocument_EditSelection( pDocument, selection, tile_map_selection_operation_t::FLOOR_HEIGHT, 0, 0, delta );
}

tile_map_document_status_t CypherTileMapDocument_AdjustSelectionWallHeight(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection, i32 delta ) noexcept
{
    return TileMapDocument_EditSelection( pDocument, selection, tile_map_selection_operation_t::WALL_HEIGHT, 0, 0, delta );
}

tile_map_document_status_t CypherTileMapDocument_RotateSelectionClockwise(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection ) noexcept
{
    return TileMapDocument_EditSelection( pDocument, selection, tile_map_selection_operation_t::ROTATE );
}

tile_map_document_status_t CypherTileMapDocument_ApplySelectionProperties(
    tile_map_document_t *pDocument, span_t<const tile_map_grid_coord_t> selection,
    const tile_map_selection_patch_t &patch ) noexcept
{
    return TileMapDocument_EditSelection( pDocument, selection, tile_map_selection_operation_t::PROPERTIES, 0, 0, 0, patch );
}

tile_map_document_status_t CypherTileMapDocument_PlacePlayerSpawn(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    f32 yawDegrees ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !std::isfinite( yawDegrees ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }
    if ( !CypherTileMapDocument_ContainsCell( pDocument, coordinate ) ) {
        return tile_map_document_status_t::OUT_OF_BOUNDS;
    }

    usize iFirstSpawn = CY_USIZE_MAX;
    usize nSpawnCount = 0u;
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &pDocument->markers );
          ++iMarker ) {
        if ( pDocument->markers.pData[iMarker].kind ==
             tile_map_marker_kind_t::PLAYER_SPAWN ) {
            if ( iFirstSpawn == CY_USIZE_MAX ) {
                iFirstSpawn = iMarker;
            }
            ++nSpawnCount;
        }
    }

    if ( nSpawnCount == 0u && Vector_Count( &pDocument->markers ) >= TILE_MAP_MAX_MARKERS ) {
        return tile_map_document_status_t::MARKER_LIMIT_REACHED;
    }
    vector_t<tile_map_history_change_t> changes{};
    const usize nCapacity = nSpawnCount == 0u ? 1u : nSpawnCount;
    if ( !Vector_Init( &changes, pDocument->pAllocator, nCapacity ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    if ( nSpawnCount == 0u ) {
        if ( !Vector_Reserve(
                 &pDocument->markers,
                 Vector_Count( &pDocument->markers ) + 1u ) ) {
            return tile_map_document_status_t::ALLOCATION_FAILED;
        }
        tile_map_marker_t marker{};
        if ( !UniqueId_CreateRandom( &marker.id ) ) {
            return tile_map_document_status_t::IDENTITY_CREATION_FAILED;
        }
        marker.kind = tile_map_marker_kind_t::PLAYER_SPAWN;
        marker.cell = coordinate;
        marker.yawDegrees = yawDegrees;
        tile_map_history_change_t change{};
        change.kind = tile_map_history_change_kind_t::MARKER;
        change.iElement = Vector_Count( &pDocument->markers );
        change.markerAfter = marker;
        change.bMarkerAfterExists = CY_TRUE;
        (void) Vector_PushBack( &changes, change );
    } else {
        const tile_map_marker_t &current =
            pDocument->markers.pData[iFirstSpawn];
        tile_map_marker_t replacement = current;
        replacement.cell = coordinate;
        replacement.yawDegrees = yawDegrees;
        if ( !TileMapMarker_Equals( current, replacement ) ) {
            tile_map_history_change_t change{};
            change.kind = tile_map_history_change_kind_t::MARKER;
            change.iElement = iFirstSpawn;
            change.markerBefore = current;
            change.markerAfter = replacement;
            change.bMarkerBeforeExists = CY_TRUE;
            change.bMarkerAfterExists = CY_TRUE;
            (void) Vector_PushBack( &changes, change );
        }

        // Descending removal preserves every recorded source index. Undo runs
        // in reverse and therefore reinserts duplicates in ascending order.
        for ( usize iMarker = Vector_Count( &pDocument->markers );
              iMarker != 0u;
              --iMarker ) {
            const usize iCandidate = iMarker - 1u;
            if ( iCandidate == iFirstSpawn ||
                 pDocument->markers.pData[iCandidate].kind !=
                     tile_map_marker_kind_t::PLAYER_SPAWN ) {
                continue;
            }
            tile_map_history_change_t change{};
            change.kind = tile_map_history_change_kind_t::MARKER;
            change.iElement = iCandidate;
            change.markerBefore = pDocument->markers.pData[iCandidate];
            change.bMarkerBeforeExists = CY_TRUE;
            (void) Vector_PushBack( &changes, change );
        }
    }

    return TileMapDocument_ApplyBatch(
        *pDocument,
        { changes.pData, changes.nCount },
        StringView_FromCString( "Place player spawn" ) );
}

tile_map_document_status_t CypherTileMapDocument_RemovePlayerSpawn(
    tile_map_document_t *pDocument ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    usize nSpawnCount = 0u;
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &pDocument->markers );
          ++iMarker ) {
        if ( pDocument->markers.pData[iMarker].kind ==
             tile_map_marker_kind_t::PLAYER_SPAWN ) {
            ++nSpawnCount;
        }
    }
    if ( nSpawnCount == 0u ) {
        return tile_map_document_status_t::PLAYER_SPAWN_NOT_FOUND;
    }

    vector_t<tile_map_history_change_t> changes{};
    if ( !Vector_Init( &changes, pDocument->pAllocator, nSpawnCount ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    for ( usize iMarker = Vector_Count( &pDocument->markers );
          iMarker != 0u;
          --iMarker ) {
        const usize iCandidate = iMarker - 1u;
        if ( pDocument->markers.pData[iCandidate].kind !=
             tile_map_marker_kind_t::PLAYER_SPAWN ) {
            continue;
        }
        tile_map_history_change_t change{};
        change.kind = tile_map_history_change_kind_t::MARKER;
        change.iElement = iCandidate;
        change.markerBefore = pDocument->markers.pData[iCandidate];
        change.bMarkerBeforeExists = CY_TRUE;
        (void) Vector_PushBack( &changes, change );
    }
    return TileMapDocument_ApplyBatch(
        *pDocument,
        { changes.pData, changes.nCount },
        StringView_FromCString( "Remove player spawn" ) );
}

const tile_map_marker_t *CypherTileMapDocument_PlayerSpawn(
    const tile_map_document_t *pDocument ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return nullptr;
    }
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &pDocument->markers );
          ++iMarker ) {
        if ( pDocument->markers.pData[iMarker].kind ==
             tile_map_marker_kind_t::PLAYER_SPAWN ) {
            return pDocument->markers.pData + iMarker;
        }
    }
    return nullptr;
}

bool_t CypherTileMapMarkerSide_IsCardinal(
    tile_map_marker_side_t side ) noexcept
{
    return side == tile_map_marker_side_t::NORTH ||
           side == tile_map_marker_side_t::EAST ||
           side == tile_map_marker_side_t::SOUTH ||
           side == tile_map_marker_side_t::WEST;
}

const tile_map_marker_t *CypherTileMapDocument_DoorAt(
    const tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ||
         !CypherTileMapMarkerSide_IsCardinal( side ) ) {
        return nullptr;
    }
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &pDocument->markers );
          ++iMarker ) {
        const tile_map_marker_t &marker = pDocument->markers.pData[iMarker];
        if ( marker.kind == tile_map_marker_kind_t::DOOR &&
             TileMapCoord_Equals( marker.cell, coordinate ) &&
             marker.side == side ) {
            return pDocument->markers.pData + iMarker;
        }
    }
    return nullptr;
}

const tile_map_marker_t *CypherTileMapDocument_DoorById(
    const tile_map_document_t *pDocument,
    unique_id_t doorId ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ||
         !UniqueId_IsValid( doorId ) ) {
        return nullptr;
    }
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &pDocument->markers );
          ++iMarker ) {
        const tile_map_marker_t &marker = pDocument->markers.pData[iMarker];
        if ( marker.kind == tile_map_marker_kind_t::DOOR &&
             UniqueId_Equals( marker.id, doorId ) ) {
            return pDocument->markers.pData + iMarker;
        }
    }
    return nullptr;
}

tile_map_document_status_t CypherTileMapDocument_PlaceDoor(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side,
    unique_id_t *pDoorIdOut ) noexcept
{
    if ( pDoorIdOut != nullptr ) {
        *pDoorIdOut = CY_UNIQUE_ID_INVALID;
    }
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !CypherTileMapMarkerSide_IsCardinal( side ) ) {
        return tile_map_document_status_t::INVALID_MARKER_SIDE;
    }
    if ( !CypherTileMapDocument_ContainsCell( pDocument, coordinate ) ) {
        return tile_map_document_status_t::OUT_OF_BOUNDS;
    }

    const tile_map_marker_t *pExisting = CypherTileMapDocument_DoorAt(
        pDocument,
        coordinate,
        side );
    if ( pExisting != nullptr ) {
        if ( pDoorIdOut != nullptr ) {
            *pDoorIdOut = pExisting->id;
        }
        return tile_map_document_status_t::OK;
    }
    if ( Vector_Count( &pDocument->markers ) >= TILE_MAP_MAX_MARKERS ) {
        return tile_map_document_status_t::MARKER_LIMIT_REACHED;
    }
    if ( !Vector_Reserve(
             &pDocument->markers,
             Vector_Count( &pDocument->markers ) + 1u ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    tile_map_marker_t marker{};
    if ( !UniqueId_CreateRandom( &marker.id ) ) {
        return tile_map_document_status_t::IDENTITY_CREATION_FAILED;
    }
    marker.kind = tile_map_marker_kind_t::DOOR;
    marker.cell = coordinate;
    marker.side = side;

    tile_map_history_change_t change{};
    change.kind = tile_map_history_change_kind_t::MARKER;
    change.iElement = Vector_Count( &pDocument->markers );
    change.markerAfter = marker;
    change.bMarkerAfterExists = CY_TRUE;
    const tile_map_document_status_t status = TileMapDocument_ApplyBatch(
        *pDocument,
        { &change, 1u },
        StringView_FromCString( "Place door" ) );
    if ( status == tile_map_document_status_t::OK &&
         pDoorIdOut != nullptr ) {
        *pDoorIdOut = marker.id;
    }
    return status;
}

tile_map_document_status_t CypherTileMapDocument_RemoveDoor(
    tile_map_document_t *pDocument,
    unique_id_t doorId ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !UniqueId_IsValid( doorId ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &pDocument->markers );
          ++iMarker ) {
        const tile_map_marker_t &marker = pDocument->markers.pData[iMarker];
        if ( marker.kind != tile_map_marker_kind_t::DOOR ||
             !UniqueId_Equals( marker.id, doorId ) ) {
            continue;
        }
        tile_map_history_change_t change{};
        change.kind = tile_map_history_change_kind_t::MARKER;
        change.iElement = iMarker;
        change.markerBefore = marker;
        change.bMarkerBeforeExists = CY_TRUE;
        return TileMapDocument_ApplyBatch(
            *pDocument,
            { &change, 1u },
            StringView_FromCString( "Remove door" ) );
    }
    return tile_map_document_status_t::DOOR_NOT_FOUND;
}

tile_map_document_status_t CypherTileMapDocument_RemoveDoorAt(
    tile_map_document_t *pDocument,
    tile_map_grid_coord_t coordinate,
    tile_map_marker_side_t side ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !CypherTileMapMarkerSide_IsCardinal( side ) ) {
        return tile_map_document_status_t::INVALID_MARKER_SIDE;
    }
    if ( !CypherTileMapDocument_ContainsCell( pDocument, coordinate ) ) {
        return tile_map_document_status_t::OUT_OF_BOUNDS;
    }
    const tile_map_marker_t *pDoor = CypherTileMapDocument_DoorAt(
        pDocument,
        coordinate,
        side );
    return pDoor != nullptr
        ? CypherTileMapDocument_RemoveDoor( pDocument, pDoor->id )
        : tile_map_document_status_t::DOOR_NOT_FOUND;
}

bool_t CypherTileMapMaterialPath_IsValid( string_view_t path ) noexcept
{
    return DataValidation_Succeeded( DataValidation_CheckResourcePath(
        path, StringView_FromCString( ".cymat" ), TILE_MAP_MATERIAL_PATH_CAPACITY - 1u ) );
}

bool_t CypherTileMapMaterialBinding_IsValid(
    const tile_map_material_binding_t &binding ) noexcept
{
    usize length = 0u;
    while ( length < TILE_MAP_MATERIAL_PATH_CAPACITY && binding.path[length] != '\0' ) ++length;
    return length < TILE_MAP_MATERIAL_PATH_CAPACITY &&
           CypherTileMapMaterialPath_IsValid( { binding.path, length } );
}

const tile_map_material_binding_t *CypherTileMapDocument_FindMaterialBinding(
    const tile_map_document_t *pDocument, u16 nSlot ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) return nullptr;
    for ( usize i = 0u; i < Vector_Count( &pDocument->materialBindings ); ++i ) {
        if ( pDocument->materialBindings.pData[i].nSlot == nSlot )
            return &pDocument->materialBindings.pData[i];
    }
    return nullptr;
}

tile_map_document_status_t CypherTileMapDocument_SetMaterialBinding(
    tile_map_document_t *pDocument, u16 nSlot, string_view_t path ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) )
        return tile_map_document_status_t::NOT_INITIALIZED;
    auto &history = *pDocument->pHistoryState;
    if ( history.bGroupOpen ) return tile_map_document_status_t::INVALID_STATE;
    const bool_t remove = path.cchLength == 0u;
    if ( !remove && !CypherTileMapMaterialPath_IsValid( path ) )
        return tile_map_document_status_t::INVALID_MATERIAL_PATH;
    const usize count = Vector_Count( &pDocument->materialBindings );
    usize index = 0u;
    while ( index < count && pDocument->materialBindings.pData[index].nSlot != nSlot ) ++index;
    const bool_t exists = index < count;
    if ( remove && !exists ) return tile_map_document_status_t::OK;
    if ( exists && !remove && StringView_Equals(
            StringView_FromCString( pDocument->materialBindings.pData[index].path ), path ) )
        return tile_map_document_status_t::OK;
    if ( !exists && count >= TILE_MAP_MAX_MATERIAL_BINDINGS )
        return tile_map_document_status_t::MATERIAL_BINDING_LIMIT_REACHED;
    if ( pDocument->nNextRevision == 0u ) return tile_map_document_status_t::HISTORY_LIMIT_REACHED;

    auto *change = Allocator_AllocateArrayStorage<tile_map_material_history_change_t>(
        pDocument->pAllocator, 1u );
    if ( change == nullptr ) return tile_map_document_status_t::ALLOCATION_FAILED;
    *change = {};
    change->iElement = index;
    change->bBeforeExists = exists;
    change->bAfterExists = !remove;
    if ( exists ) change->before = pDocument->materialBindings.pData[index];
    change->after.nSlot = nSlot;
    if ( !remove ) Cy_MemCopy( change->after.path, path.pData, path.cchLength );
    if ( !exists && !Vector_Reserve( &pDocument->materialBindings, count + 1u ) ) {
        Allocator_FreeArrayStorage( pDocument->pAllocator, change, 1u );
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    tile_map_history_entry_t pending{};
    pending.pMaterialChange = change;
    pending.cbChanges = sizeof( *change );
    pending.nBeforeRevision = pDocument->nCurrentRevision;
    if ( !TileMap_ApplyHistoryEntry( *pDocument, pending, CY_TRUE ) ) {
        Allocator_FreeArrayStorage( pDocument->pAllocator, change, 1u );
        return tile_map_document_status_t::INVALID_STATE;
    }
    // All fallible work precedes redo truncation and history eviction.
    pending.nAfterRevision = TileMapDocument_AllocateRevision( *pDocument );
    TileMap_CopyLabel( pending.label, pending.cchLabel,
        StringView_FromCString( remove ? "Clear material binding" : "Assign material binding" ) );
    TileMapHistory_DiscardRedo( *pDocument );
    while ( history.nEntryCount == TILE_MAP_HISTORY_MAX_ENTRIES ||
            pending.cbChanges > TILE_MAP_HISTORY_BYTE_BUDGET - history.cbHistory )
        TileMapHistory_EvictOldest( *pDocument );
    history.entries[history.nEntryCount++] = pending;
    history.iCursor = history.nEntryCount;
    history.cbHistory += pending.cbChanges;
    pDocument->nCurrentRevision = pending.nAfterRevision;
    return tile_map_document_status_t::OK;
}

bool_t CypherTileMapDocument_CanUndo(
    const tile_map_document_t *pDocument ) noexcept
{
    return CypherTileMapDocument_IsInitialized( pDocument ) &&
           !pDocument->pHistoryState->bGroupOpen &&
           pDocument->pHistoryState->iCursor != 0u;
}

bool_t CypherTileMapDocument_CanRedo(
    const tile_map_document_t *pDocument ) noexcept
{
    return CypherTileMapDocument_IsInitialized( pDocument ) &&
           !pDocument->pHistoryState->bGroupOpen &&
           pDocument->pHistoryState->iCursor <
               pDocument->pHistoryState->nEntryCount;
}

tile_map_document_status_t CypherTileMapDocument_Undo(
    tile_map_document_t *pDocument ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( pDocument->pHistoryState->bGroupOpen ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    if ( !CypherTileMapDocument_CanUndo( pDocument ) ) {
        return tile_map_document_status_t::HISTORY_EMPTY;
    }
    tile_map_history_state_t &history = *pDocument->pHistoryState;
    tile_map_history_entry_t &entry = history.entries[history.iCursor - 1u];
    if ( !TileMap_ApplyHistoryEntry( *pDocument, entry, CY_FALSE ) ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    --history.iCursor;
    pDocument->nCurrentRevision = entry.nBeforeRevision;
    return tile_map_document_status_t::OK;
}

tile_map_document_status_t CypherTileMapDocument_Redo(
    tile_map_document_t *pDocument ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( pDocument->pHistoryState->bGroupOpen ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    if ( !CypherTileMapDocument_CanRedo( pDocument ) ) {
        return tile_map_document_status_t::HISTORY_EMPTY;
    }
    tile_map_history_state_t &history = *pDocument->pHistoryState;
    tile_map_history_entry_t &entry = history.entries[history.iCursor];
    if ( !TileMap_ApplyHistoryEntry( *pDocument, entry, CY_TRUE ) ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    ++history.iCursor;
    pDocument->nCurrentRevision = entry.nAfterRevision;
    return tile_map_document_status_t::OK;
}

string_view_t CypherTileMapDocument_UndoLabel(
    const tile_map_document_t *pDocument ) noexcept
{
    if ( !CypherTileMapDocument_CanUndo( pDocument ) ) {
        return {};
    }
    const tile_map_history_state_t &history = *pDocument->pHistoryState;
    const tile_map_history_entry_t &entry =
        history.entries[history.iCursor - 1u];
    return { entry.label, entry.cchLabel };
}

string_view_t CypherTileMapDocument_RedoLabel(
    const tile_map_document_t *pDocument ) noexcept
{
    if ( !CypherTileMapDocument_CanRedo( pDocument ) ) {
        return {};
    }
    const tile_map_history_state_t &history = *pDocument->pHistoryState;
    const tile_map_history_entry_t &entry =
        history.entries[history.iCursor];
    return { entry.label, entry.cchLabel };
}

usize CypherTileMapDocument_HistoryCount(
    const tile_map_document_t *pDocument ) noexcept
{
    return CypherTileMapDocument_IsInitialized( pDocument )
        ? pDocument->pHistoryState->nEntryCount
        : 0u;
}

bool_t CypherTileMapDocument_HistoryInfo(
    const tile_map_document_t *pDocument,
    tile_map_history_info_t *pInfoOut ) noexcept
{
    if ( pInfoOut == nullptr ) return CY_FALSE;
    *pInfoOut = {};
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ||
         !TileMapHistory_IsValid( *pDocument ) ) {
        return CY_FALSE;
    }

    const tile_map_history_state_t &history = *pDocument->pHistoryState;
    pInfoOut->nEntryCount = history.nEntryCount;
    pInfoOut->iCursor = history.iCursor;
    pInfoOut->cbStoredChanges = history.cbHistory;
    pInfoOut->bEditGroupOpen = history.bGroupOpen;
    return CY_TRUE;
}

bool_t CypherTileMapDocument_HistoryEntryInfo(
    const tile_map_document_t *pDocument,
    usize iEntry,
    tile_map_history_entry_info_t *pInfoOut ) noexcept
{
    if ( pInfoOut == nullptr ) return CY_FALSE;
    *pInfoOut = {};
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ||
         !TileMapHistory_IsValid( *pDocument ) ||
         iEntry >= pDocument->pHistoryState->nEntryCount ) {
        return CY_FALSE;
    }

    const tile_map_history_entry_t &entry =
        pDocument->pHistoryState->entries[iEntry];
    pInfoOut->label = { entry.label, entry.cchLabel };
    pInfoOut->nBeforeRevision = entry.nBeforeRevision;
    pInfoOut->nAfterRevision = entry.nAfterRevision;
    pInfoOut->nAffectedElementCount = entry.nChangeCount +
        ( entry.pMaterialChange != nullptr ? 1u : 0u ) +
        ( entry.pDescriptionChange != nullptr ? 1u : 0u );
    pInfoOut->cbStoredChanges = entry.cbChanges;
    return CY_TRUE;
}

tile_map_document_status_t CypherTileMapValidationReport_Init(
    tile_map_validation_report_t *pReport,
    const allocator_t *pAllocator ) noexcept
{
    if ( pReport == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }
    if ( pReport->pAllocator != nullptr ||
         pReport->diagnostics.pData != nullptr ||
         pReport->diagnostics.nCount != 0u ||
         pReport->diagnostics.nCapacity != 0u ||
         pReport->diagnostics.pAllocator != nullptr ) {
        return tile_map_document_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pReport->diagnostics, pAllocator, 8u ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    pReport->pAllocator = pAllocator;
    return tile_map_document_status_t::OK;
}

void CypherTileMapValidationReport_Shutdown(
    tile_map_validation_report_t *pReport ) noexcept
{
    if ( pReport == nullptr ) {
        return;
    }
    if ( Vector_IsValid( &pReport->diagnostics ) ) {
        Vector_Shutdown( &pReport->diagnostics );
    }
    pReport->pAllocator = nullptr;
}

tile_map_document_status_t CypherTileMapDocument_Validate(
    const tile_map_document_t *pDocument,
    tile_map_validation_report_t *pReport ) noexcept
{
    if ( pDocument == nullptr || pReport == nullptr ||
         !Allocator_IsValid( pReport->pAllocator ) ||
         !Vector_IsValid( &pReport->diagnostics ) ||
         pReport->diagnostics.pAllocator != pReport->pAllocator ||
         !Vector_IsValid( &pDocument->cells ) ||
         !Vector_IsValid( &pDocument->markers ) ||
         !Vector_IsValid( &pDocument->materialBindings ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }

    vector_t<tile_map_validation_diagnostic_t> pending{};
    if ( !Vector_Init( &pending, pReport->pAllocator, 8u ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    const usize bindingCount = Vector_Count( &pDocument->materialBindings );
    if ( bindingCount > TILE_MAP_MAX_MATERIAL_BINDINGS &&
         !TileMapValidation_Push( pending, tile_map_validation_code_t::MATERIAL_BINDING_LIMIT_EXCEEDED ) )
        return tile_map_document_status_t::ALLOCATION_FAILED;
    for ( usize i = 0u; i < bindingCount; ++i ) {
        const auto &binding = pDocument->materialBindings.pData[i];
        if ( !CypherTileMapMaterialBinding_IsValid( binding ) &&
             !TileMapValidation_Push( pending, tile_map_validation_code_t::INVALID_MATERIAL_BINDING ) )
            return tile_map_document_status_t::ALLOCATION_FAILED;
        for ( usize j = 0u; j < i; ++j ) {
            if ( binding.nSlot == pDocument->materialBindings.pData[j].nSlot ) {
                if ( !TileMapValidation_Push( pending, tile_map_validation_code_t::DUPLICATE_MATERIAL_SLOT ) )
                    return tile_map_document_status_t::ALLOCATION_FAILED;
                break;
            }
        }
    }

    const bool_t bDimensionsValid = TileMapDocument_AreDimensionsValid(
        pDocument->nWidth,
        pDocument->nHeight );
    const bool_t bMetricsValid = TileMapDocument_AreMetricsValid(
        pDocument->nCellSize,
        pDocument->nLevelHeight );
    const usize nExpectedCells = bDimensionsValid
        ? static_cast<usize>( pDocument->nWidth ) * pDocument->nHeight
        : 0u;
    const bool_t bCellStorageValid =
        bDimensionsValid &&
        Vector_Count( &pDocument->cells ) == nExpectedCells;

    if ( !bDimensionsValid &&
         !TileMapValidation_Push(
             pending,
             tile_map_validation_code_t::INVALID_DIMENSIONS ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    if ( !bMetricsValid &&
         !TileMapValidation_Push(
             pending,
             tile_map_validation_code_t::INVALID_METRICS ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    if ( bDimensionsValid && !bCellStorageValid &&
         !TileMapValidation_Push(
             pending,
             tile_map_validation_code_t::CELL_STORAGE_MISMATCH ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    if ( bCellStorageValid ) {
        usize nActiveCells = 0u;
        bool_t bReportedActiveLimit = CY_FALSE;
        for ( usize iCell = 0u;
              iCell < nExpectedCells;
              ++iCell ) {
            const tile_map_cell_t &cell = pDocument->cells.pData[iCell];
            const tile_map_grid_coord_t coordinate{
                static_cast<i32>( iCell % pDocument->nWidth ),
                static_cast<i32>( iCell / pDocument->nWidth )
            };
            if ( TileMapCell_HasFloor( cell ) ) {
                const bool_t bPropertiesValid =
                    cell.flags == TILE_MAP_CELL_FLAG_FLOOR &&
                    cell.nWallHeightLevels > 0u &&
                    ( cell.shape == tile_map_cell_shape_t::FLAT ||
                      cell.nFloorLevel < CY_I16_MAX );
                if ( ( !bPropertiesValid &&
                       !TileMapValidation_Push( pending,
                           tile_map_validation_code_t::INVALID_CELL_PROPERTIES,
                           CY_UNIQUE_ID_INVALID, coordinate ) ) ||
                     ( !CypherTileMapCellShape_IsValid( cell.shape ) &&
                       !TileMapValidation_Push( pending,
                           tile_map_validation_code_t::INVALID_CELL_SHAPE,
                           CY_UNIQUE_ID_INVALID, coordinate ) ) ||
                     ( ( cell.nStairSteps < TILE_MAP_MIN_STAIR_STEPS ||
                         cell.nStairSteps > TILE_MAP_MAX_STAIR_STEPS ) &&
                       !TileMapValidation_Push( pending,
                           tile_map_validation_code_t::INVALID_STAIR_STEPS,
                           CY_UNIQUE_ID_INVALID, coordinate ) ) ) {
                    return tile_map_document_status_t::ALLOCATION_FAILED;
                }
                ++nActiveCells;
                if ( !bReportedActiveLimit &&
                     nActiveCells > TILE_MAP_MAX_ACTIVE_CELLS ) {
                    if ( !TileMapValidation_Push(
                             pending,
                             tile_map_validation_code_t::
                                 ACTIVE_CELL_LIMIT_EXCEEDED,
                             CY_UNIQUE_ID_INVALID,
                             coordinate ) ) {
                        return tile_map_document_status_t::ALLOCATION_FAILED;
                    }
                    bReportedActiveLimit = CY_TRUE;
                }
            } else if ( !CypherTileMapCell_IsCanonicalEmpty( cell ) &&
                        !TileMapValidation_Push(
                            pending,
                            tile_map_validation_code_t::
                                NONCANONICAL_EMPTY_CELL,
                            CY_UNIQUE_ID_INVALID,
                            coordinate ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
        }
    }

    usize nPlayerSpawns = 0u;
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &pDocument->markers );
          ++iMarker ) {
        const tile_map_marker_t &marker = pDocument->markers.pData[iMarker];
        if ( !UniqueId_IsValid( marker.id ) ) {
            if ( !TileMapValidation_Push(
                     pending,
                     tile_map_validation_code_t::INVALID_MARKER_ID,
                     marker.id,
                     marker.cell ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
        } else {
            for ( usize iPrevious = 0u;
                  iPrevious < iMarker;
                  ++iPrevious ) {
                if ( UniqueId_Equals(
                         pDocument->markers.pData[iPrevious].id,
                         marker.id ) ) {
                    if ( !TileMapValidation_Push(
                             pending,
                             tile_map_validation_code_t::DUPLICATE_MARKER_ID,
                             marker.id,
                             marker.cell ) ) {
                        return tile_map_document_status_t::ALLOCATION_FAILED;
                    }
                    break;
                }
            }
        }

        const bool_t bInBounds = bDimensionsValid &&
            marker.cell.x >= 0 && marker.cell.y >= 0 &&
            static_cast<u32>( marker.cell.x ) < pDocument->nWidth &&
            static_cast<u32>( marker.cell.y ) < pDocument->nHeight;

        if ( marker.kind == tile_map_marker_kind_t::PLAYER_SPAWN ) {
            ++nPlayerSpawns;
            if ( nPlayerSpawns > 1u &&
                 !TileMapValidation_Push(
                     pending,
                     tile_map_validation_code_t::DUPLICATE_PLAYER_SPAWN,
                     marker.id,
                     marker.cell ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
            if ( !bInBounds ) {
                if ( !TileMapValidation_Push(
                         pending,
                         tile_map_validation_code_t::
                             PLAYER_SPAWN_OUT_OF_BOUNDS,
                         marker.id,
                         marker.cell ) ) {
                    return tile_map_document_status_t::ALLOCATION_FAILED;
                }
                continue;
            }
            const usize iCell = TileMapCellIndex( *pDocument, marker.cell );
            if ( bCellStorageValid &&
                 pDocument->cells.pData[iCell].shape != tile_map_cell_shape_t::FLAT &&
                 !TileMapValidation_Push( pending,
                     tile_map_validation_code_t::PLAYER_SPAWN_ON_STAIRS,
                     marker.id, marker.cell ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
            if ( bCellStorageValid &&
                 !TileMapCell_HasFloor( pDocument->cells.pData[iCell] ) &&
                 !TileMapValidation_Push(
                     pending,
                     tile_map_validation_code_t::PLAYER_SPAWN_OUTSIDE_FLOOR,
                     marker.id,
                     marker.cell ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
            continue;
        }

        if ( marker.kind == tile_map_marker_kind_t::DOOR ) {
            const bool_t bSideValid =
                CypherTileMapMarkerSide_IsCardinal( marker.side );
            if ( !bSideValid &&
                 !TileMapValidation_Push(
                     pending,
                     tile_map_validation_code_t::DOOR_INVALID_SIDE,
                     marker.id,
                     marker.cell ) ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
            if ( !bInBounds ) {
                if ( !TileMapValidation_Push(
                         pending,
                         tile_map_validation_code_t::DOOR_OUT_OF_BOUNDS,
                         marker.id,
                         marker.cell ) ) {
                    return tile_map_document_status_t::ALLOCATION_FAILED;
                }
                continue;
            }

            const usize iCell = TileMapCellIndex( *pDocument, marker.cell );
            if ( bCellStorageValid &&
                 !TileMapCell_HasFloor( pDocument->cells.pData[iCell] ) ) {
                if ( !TileMapValidation_Push(
                         pending,
                         tile_map_validation_code_t::DOOR_OUTSIDE_FLOOR,
                         marker.id,
                         marker.cell ) ) {
                    return tile_map_document_status_t::ALLOCATION_FAILED;
                }
                continue;
            }
            if ( bCellStorageValid && bSideValid ) {
                if ( pDocument->cells.pData[iCell].shape != tile_map_cell_shape_t::FLAT &&
                     !TileMapValidation_Push( pending,
                         tile_map_validation_code_t::DOOR_ON_STAIRS,
                         marker.id, marker.cell ) ) {
                    return tile_map_document_status_t::ALLOCATION_FAILED;
                }
                const tile_map_grid_coord_t neighbor =
                    TileMapMarker_NeighborCoordinate(
                        marker.cell,
                        marker.side );
                const bool_t bNeighborInBounds =
                    neighbor.x >= 0 && neighbor.y >= 0 &&
                    static_cast<u32>( neighbor.x ) < pDocument->nWidth &&
                    static_cast<u32>( neighbor.y ) < pDocument->nHeight;
                if ( bNeighborInBounds &&
                     TileMapCell_HasFloor(
                         pDocument->cells.pData[
                             TileMapCellIndex( *pDocument, neighbor )] ) &&
                     !TileMapValidation_Push(
                         pending,
                         tile_map_validation_code_t::DOOR_NOT_ON_BOUNDARY,
                         marker.id,
                         marker.cell ) ) {
                    return tile_map_document_status_t::ALLOCATION_FAILED;
                }

                for ( usize iPrevious = 0u;
                      iPrevious < iMarker;
                      ++iPrevious ) {
                    const tile_map_marker_t &previous =
                        pDocument->markers.pData[iPrevious];
                    if ( previous.kind == tile_map_marker_kind_t::DOOR &&
                         previous.side == marker.side &&
                         TileMapCoord_Equals( previous.cell, marker.cell ) ) {
                        if ( !TileMapValidation_Push(
                                 pending,
                                 tile_map_validation_code_t::
                                     DUPLICATE_DOOR_EDGE,
                                 marker.id,
                                 marker.cell ) ) {
                            return tile_map_document_status_t::
                                ALLOCATION_FAILED;
                        }
                        break;
                    }
                }
            }
            continue;
        }

        if ( !TileMapValidation_Push(
                 pending,
                 tile_map_validation_code_t::INVALID_MARKER_KIND,
                 marker.id,
                 marker.cell ) ) {
            return tile_map_document_status_t::ALLOCATION_FAILED;
        }
    }
    if ( nPlayerSpawns == 0u &&
         !TileMapValidation_Push(
             pending,
             tile_map_validation_code_t::MISSING_PLAYER_SPAWN ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    Vector_Shutdown( &pReport->diagnostics );
    Vector_Move( &pReport->diagnostics, &pending );
    return tile_map_document_status_t::OK;
}

bool_t CypherTileMapValidationReport_IsValid(
    const tile_map_validation_report_t *pReport ) noexcept
{
    return pReport != nullptr &&
           Allocator_IsValid( pReport->pAllocator ) &&
           Vector_IsValid( &pReport->diagnostics ) &&
           Vector_IsEmpty( &pReport->diagnostics );
}

const char *CypherTileMapDocument_StatusName(
    tile_map_document_status_t status ) noexcept
{
    switch ( status ) {
        case tile_map_document_status_t::OK: return "OK";
        case tile_map_document_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case tile_map_document_status_t::ALREADY_INITIALIZED:
            return "ALREADY_INITIALIZED";
        case tile_map_document_status_t::NOT_INITIALIZED:
            return "NOT_INITIALIZED";
        case tile_map_document_status_t::INVALID_STATE:
            return "INVALID_STATE";
        case tile_map_document_status_t::INVALID_DIMENSIONS:
            return "INVALID_DIMENSIONS";
        case tile_map_document_status_t::INVALID_METRICS:
            return "INVALID_METRICS";
        case tile_map_document_status_t::INVALID_CELL:
            return "INVALID_CELL";
        case tile_map_document_status_t::IDENTITY_CREATION_FAILED:
            return "IDENTITY_CREATION_FAILED";
        case tile_map_document_status_t::ALLOCATION_FAILED:
            return "ALLOCATION_FAILED";
        case tile_map_document_status_t::OUT_OF_BOUNDS:
            return "OUT_OF_BOUNDS";
        case tile_map_document_status_t::PLAYER_SPAWN_NOT_FOUND:
            return "PLAYER_SPAWN_NOT_FOUND";
        case tile_map_document_status_t::DOOR_NOT_FOUND:
            return "DOOR_NOT_FOUND";
        case tile_map_document_status_t::INVALID_MARKER_SIDE:
            return "INVALID_MARKER_SIDE";
        case tile_map_document_status_t::HISTORY_EMPTY:
            return "HISTORY_EMPTY";
        case tile_map_document_status_t::HISTORY_LIMIT_REACHED:
            return "HISTORY_LIMIT_REACHED";
        case tile_map_document_status_t::ACTIVE_CELL_LIMIT_REACHED:
            return "ACTIVE_CELL_LIMIT_REACHED";
        case tile_map_document_status_t::INVALID_MATERIAL_PATH: return "INVALID_MATERIAL_PATH";
        case tile_map_document_status_t::MATERIAL_BINDING_LIMIT_REACHED: return "MATERIAL_BINDING_LIMIT_REACHED";
        case tile_map_document_status_t::MARKER_LIMIT_REACHED:
            return "MARKER_LIMIT_REACHED";
    }
    return "UNKNOWN";
}

const char *CypherTileMapValidation_CodeName(
    tile_map_validation_code_t code ) noexcept
{
    switch ( code ) {
        case tile_map_validation_code_t::INVALID_MATERIAL_BINDING: return "INVALID_MATERIAL_BINDING";
        case tile_map_validation_code_t::DUPLICATE_MATERIAL_SLOT: return "DUPLICATE_MATERIAL_SLOT";
        case tile_map_validation_code_t::MATERIAL_BINDING_LIMIT_EXCEEDED: return "MATERIAL_BINDING_LIMIT_EXCEEDED";
        case tile_map_validation_code_t::INVALID_DIMENSIONS:
            return "INVALID_DIMENSIONS";
        case tile_map_validation_code_t::INVALID_METRICS:
            return "INVALID_METRICS";
        case tile_map_validation_code_t::CELL_STORAGE_MISMATCH:
            return "CELL_STORAGE_MISMATCH";
        case tile_map_validation_code_t::ACTIVE_CELL_LIMIT_EXCEEDED:
            return "ACTIVE_CELL_LIMIT_EXCEEDED";
        case tile_map_validation_code_t::NONCANONICAL_EMPTY_CELL:
            return "NONCANONICAL_EMPTY_CELL";
        case tile_map_validation_code_t::INVALID_MARKER_ID:
            return "INVALID_MARKER_ID";
        case tile_map_validation_code_t::DUPLICATE_MARKER_ID:
            return "DUPLICATE_MARKER_ID";
        case tile_map_validation_code_t::INVALID_MARKER_KIND:
            return "INVALID_MARKER_KIND";
        case tile_map_validation_code_t::MISSING_PLAYER_SPAWN:
            return "MISSING_PLAYER_SPAWN";
        case tile_map_validation_code_t::DUPLICATE_PLAYER_SPAWN:
            return "DUPLICATE_PLAYER_SPAWN";
        case tile_map_validation_code_t::PLAYER_SPAWN_OUT_OF_BOUNDS:
            return "PLAYER_SPAWN_OUT_OF_BOUNDS";
        case tile_map_validation_code_t::PLAYER_SPAWN_OUTSIDE_FLOOR:
            return "PLAYER_SPAWN_OUTSIDE_FLOOR";
        case tile_map_validation_code_t::DOOR_OUT_OF_BOUNDS:
            return "DOOR_OUT_OF_BOUNDS";
        case tile_map_validation_code_t::DOOR_INVALID_SIDE:
            return "DOOR_INVALID_SIDE";
        case tile_map_validation_code_t::DOOR_OUTSIDE_FLOOR:
            return "DOOR_OUTSIDE_FLOOR";
        case tile_map_validation_code_t::DOOR_NOT_ON_BOUNDARY:
            return "DOOR_NOT_ON_BOUNDARY";
        case tile_map_validation_code_t::DUPLICATE_DOOR_EDGE:
            return "DUPLICATE_DOOR_EDGE";
        case tile_map_validation_code_t::INVALID_CELL_PROPERTIES:
            return "INVALID_CELL_PROPERTIES";
        case tile_map_validation_code_t::INVALID_CELL_SHAPE:
            return "INVALID_CELL_SHAPE";
        case tile_map_validation_code_t::INVALID_STAIR_STEPS:
            return "INVALID_STAIR_STEPS";
        case tile_map_validation_code_t::PLAYER_SPAWN_ON_STAIRS:
            return "PLAYER_SPAWN_ON_STAIRS";
        case tile_map_validation_code_t::DOOR_ON_STAIRS:
            return "DOOR_ON_STAIRS";
    }
    return "UNKNOWN";
}

} // namespace cypher::tools::tile_editor
