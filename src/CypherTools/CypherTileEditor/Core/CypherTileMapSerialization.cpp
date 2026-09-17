//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapSerialization.cpp
//  Purpose: Implements deterministic CYKV persistence for authored tile maps.
//  Details: Source text stores only active cells. Decode validates structure,
//           values, duplicates, and limits before initializing the destination.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapSerialization.h"

#include <cmath>

namespace cypher::tools::tile_editor
{

namespace
{

constexpr flags16_t TILE_MAP_KNOWN_CELL_FLAGS = TILE_MAP_CELL_FLAG_FLOOR;

struct key_value_document_owner_t {
    key_value_document_t *pDocument{ nullptr };

    ~key_value_document_owner_t() noexcept
    {
        KeyValue_DestroyDocument( pDocument );
    }
};

struct decoded_cell_t {
    tile_map_grid_coord_t coordinate{};
    tile_map_cell_t cell{};
};

CYPHER_NODISCARD string_view_t TileMapText(
    const char *pText ) noexcept
{
    return StringView_FromCString( pText );
}

void TileMapSerialization_CopyField(
    tile_map_serialization_result_t &result,
    string_view_t field ) noexcept
{
    usize cchCopy = field.cchLength;
    if ( cchCopy >= TILE_MAP_SERIALIZATION_FIELD_CAPACITY ) {
        cchCopy = TILE_MAP_SERIALIZATION_FIELD_CAPACITY - 1u;
    }
    for ( usize iChar = 0u; iChar < cchCopy; ++iChar ) {
        result.field[iChar] = field.pData[iChar];
    }
    result.field[cchCopy] = '\0';
}

CYPHER_NODISCARD tile_map_serialization_result_t
TileMapSerialization_Failure(
    tile_map_serialization_status_t status,
    const char *pField = nullptr,
    usize iElement = CY_INVALID_SIZE ) noexcept
{
    tile_map_serialization_result_t result{};
    result.status = status;
    result.iElement = iElement;
    if ( pField != nullptr ) {
        TileMapSerialization_CopyField( result, TileMapText( pField ) );
    }
    return result;
}

CYPHER_NODISCARD bool_t TileMapDocument_IsFresh(
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

CYPHER_NODISCARD bool_t TileMapCoordinate_IsInside(
    u32 nWidth,
    u32 nHeight,
    tile_map_grid_coord_t coordinate ) noexcept
{
    return coordinate.x >= 0 && coordinate.y >= 0 &&
           static_cast<u32>( coordinate.x ) < nWidth &&
           static_cast<u32>( coordinate.y ) < nHeight;
}

CYPHER_NODISCARD bool_t TileMapMarker_IsBefore(
    const tile_map_marker_t &left,
    const tile_map_marker_t &right ) noexcept
{
    return UniqueId_Compare( left.id, right.id ) < 0;
}

CYPHER_NODISCARD const char *TileMapMarkerSide_Name(
    tile_map_marker_side_t side ) noexcept
{
    switch ( side ) {
        case tile_map_marker_side_t::NORTH: return "north";
        case tile_map_marker_side_t::EAST: return "east";
        case tile_map_marker_side_t::SOUTH: return "south";
        case tile_map_marker_side_t::WEST: return "west";
        case tile_map_marker_side_t::NONE: break;
    }
    return nullptr;
}

CYPHER_NODISCARD bool_t TileMapMarkerSide_Parse(
    string_view_t text,
    tile_map_marker_side_t &sideOut ) noexcept
{
    if ( StringView_Equals( text, TileMapText( "north" ) ) ) {
        sideOut = tile_map_marker_side_t::NORTH;
        return CY_TRUE;
    }
    if ( StringView_Equals( text, TileMapText( "east" ) ) ) {
        sideOut = tile_map_marker_side_t::EAST;
        return CY_TRUE;
    }
    if ( StringView_Equals( text, TileMapText( "south" ) ) ) {
        sideOut = tile_map_marker_side_t::SOUTH;
        return CY_TRUE;
    }
    if ( StringView_Equals( text, TileMapText( "west" ) ) ) {
        sideOut = tile_map_marker_side_t::WEST;
        return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD tile_map_serialization_result_t
TileMapSerialization_ValidateSource(
    const tile_map_document_t &document,
    usize &nActiveCellsOut ) noexcept
{
    nActiveCellsOut = 0u;
    if ( !CypherTileMapDocument_IsInitialized( &document ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::NOT_INITIALIZED );
    }
    if ( !UniqueId_IsValid( document.mapId ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::INVALID_ID,
            "map_id" );
    }

    const usize nBindings = Vector_Count( &document.materialBindings );
    if ( nBindings > TILE_MAP_MAX_MATERIAL_BINDINGS )
        return TileMapSerialization_Failure( tile_map_serialization_status_t::LIMIT_EXCEEDED, "materials" );
    for ( usize i = 0u; i < nBindings; ++i ) {
        const auto &binding = document.materialBindings.pData[i];
        if ( !CypherTileMapMaterialBinding_IsValid( binding ) )
            return TileMapSerialization_Failure( tile_map_serialization_status_t::INVALID_MATERIAL_PATH,
                "materials[].path", i );
        for ( usize j = 0u; j < i; ++j ) {
            if ( binding.nSlot == document.materialBindings.pData[j].nSlot )
                return TileMapSerialization_Failure( tile_map_serialization_status_t::DUPLICATE_MATERIAL_SLOT,
                    "materials[].slot", i );
        }
    }

    const usize nCellCount = Vector_Count( &document.cells );
    for ( usize iCell = 0u; iCell < nCellCount; ++iCell ) {
        const tile_map_cell_t &cell = document.cells.pData[iCell];
        if ( ( cell.flags & ~TILE_MAP_KNOWN_CELL_FLAGS ) != 0u ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::INVALID_DOCUMENT,
                "cells[].flags",
                iCell );
        }
        if ( ( cell.flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) {
            if ( !CypherTileMapCell_IsCanonicalEmpty( cell ) ) {
                return TileMapSerialization_Failure(
                    tile_map_serialization_status_t::INVALID_DOCUMENT,
                    "cells[]",
                    iCell );
            }
            continue;
        }
        if ( cell.nWallHeightLevels == 0u ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                "cells[].wall_height_levels",
                iCell );
        }
        if ( !CypherTileMapCellShape_IsValid( cell.shape ) ||
             cell.nStairSteps < TILE_MAP_MIN_STAIR_STEPS ||
             cell.nStairSteps > TILE_MAP_MAX_STAIR_STEPS ||
             ( cell.shape != tile_map_cell_shape_t::FLAT &&
               cell.nFloorLevel == CY_I16_MAX ) ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                !CypherTileMapCellShape_IsValid( cell.shape )
                    ? "cells[].shape"
                    : cell.nStairSteps < TILE_MAP_MIN_STAIR_STEPS ||
                          cell.nStairSteps > TILE_MAP_MAX_STAIR_STEPS
                        ? "cells[].stair_steps"
                        : "cells[].floor_level",
                iCell );
        }
        ++nActiveCellsOut;
        if ( nActiveCellsOut > TILE_MAP_MAX_ACTIVE_CELLS ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::LIMIT_EXCEEDED,
                "cells" );
        }
    }

    const usize nMarkers = Vector_Count( &document.markers );
    if ( nMarkers > TILE_MAP_SERIALIZATION_MAX_MARKERS ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::LIMIT_EXCEEDED,
            "markers" );
    }
    for ( usize iMarker = 0u; iMarker < nMarkers; ++iMarker ) {
        const tile_map_marker_t &marker = document.markers.pData[iMarker];
        if ( !UniqueId_IsValid( marker.id ) ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::INVALID_ID,
                "markers[].id",
                iMarker );
        }
        if ( marker.kind != tile_map_marker_kind_t::PLAYER_SPAWN &&
             marker.kind != tile_map_marker_kind_t::DOOR ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::UNSUPPORTED_MARKER_KIND,
                "markers[].kind",
                iMarker );
        }
        if ( !TileMapCoordinate_IsInside(
                 document.nWidth,
                 document.nHeight,
                 marker.cell ) ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                "markers[].cell",
                iMarker );
        }
        if ( marker.kind == tile_map_marker_kind_t::PLAYER_SPAWN &&
             ( !std::isfinite( marker.yawDegrees ) ||
               marker.side != tile_map_marker_side_t::NONE ) ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                marker.side != tile_map_marker_side_t::NONE
                    ? "markers[].side"
                    : "markers[].yaw_degrees",
                iMarker );
        }
        if ( marker.kind == tile_map_marker_kind_t::DOOR &&
             ( !CypherTileMapMarkerSide_IsCardinal( marker.side ) ||
               marker.yawDegrees != 0.0f ) ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                !CypherTileMapMarkerSide_IsCardinal( marker.side )
                    ? "markers[].side"
                    : "markers[].yaw_degrees",
                iMarker );
        }
        for ( usize iPrevious = 0u;
              iPrevious < iMarker;
              ++iPrevious ) {
            if ( UniqueId_Equals(
                     document.markers.pData[iPrevious].id,
                     marker.id ) ) {
                return TileMapSerialization_Failure(
                    tile_map_serialization_status_t::DUPLICATE_MARKER_ID,
                    "markers[].id",
                    iMarker );
            }
        }
    }

    return {};
}

CYPHER_NODISCARD key_value_t *TileMapSerialization_Insert(
    key_value_document_t *pDocument,
    key_value_t *pObject,
    const char *pName,
    key_value_type_t type ) noexcept
{
    return KeyValue_ObjectInsert(
        pDocument,
        pObject,
        TileMapText( pName ),
        type );
}

CYPHER_NODISCARD bool_t TileMapSerialization_InsertU64(
    key_value_document_t *pDocument,
    key_value_t *pObject,
    const char *pName,
    u64 value ) noexcept
{
    key_value_t *pValue = TileMapSerialization_Insert(
        pDocument,
        pObject,
        pName,
        key_value_type_t::U64 );
    return pValue != nullptr &&
           KeyValue_SetU64( pDocument, pValue, value );
}

CYPHER_NODISCARD bool_t TileMapSerialization_InsertI64(
    key_value_document_t *pDocument,
    key_value_t *pObject,
    const char *pName,
    i64 value ) noexcept
{
    key_value_t *pValue = TileMapSerialization_Insert(
        pDocument,
        pObject,
        pName,
        key_value_type_t::I64 );
    return pValue != nullptr &&
           KeyValue_SetI64( pDocument, pValue, value );
}

CYPHER_NODISCARD bool_t TileMapSerialization_InsertF64(
    key_value_document_t *pDocument,
    key_value_t *pObject,
    const char *pName,
    f64 value ) noexcept
{
    key_value_t *pValue = TileMapSerialization_Insert(
        pDocument,
        pObject,
        pName,
        key_value_type_t::F64 );
    return pValue != nullptr &&
           KeyValue_SetF64( pDocument, pValue, value );
}

CYPHER_NODISCARD bool_t TileMapSerialization_InsertString(
    key_value_document_t *pDocument,
    key_value_t *pObject,
    const char *pName,
    string_view_t value ) noexcept
{
    key_value_t *pValue = TileMapSerialization_Insert(
        pDocument,
        pObject,
        pName,
        key_value_type_t::STRING );
    return pValue != nullptr &&
           KeyValue_SetString( pDocument, pValue, value );
}

CYPHER_NODISCARD bool_t TileMapSerialization_WriteCell(
    key_value_document_t *pDocument,
    key_value_t *pCells,
    u32 x,
    u32 y,
    const tile_map_cell_t &cell ) noexcept
{
    key_value_t *pCell = KeyValue_ArrayAppend(
        pDocument,
        pCells,
        key_value_type_t::OBJECT );
    const bool_t bBaseWritten = pCell != nullptr &&
           TileMapSerialization_InsertU64( pDocument, pCell, "x", x ) &&
           TileMapSerialization_InsertU64( pDocument, pCell, "y", y ) &&
           TileMapSerialization_InsertI64(
               pDocument,
               pCell,
               "floor_level",
               cell.nFloorLevel ) &&
           TileMapSerialization_InsertU64(
               pDocument,
               pCell,
               "wall_height_levels",
               cell.nWallHeightLevels ) &&
           TileMapSerialization_InsertU64(
               pDocument,
               pCell,
               "material_slot",
               cell.nMaterialSlot ) &&
           TileMapSerialization_InsertU64(
               pDocument,
               pCell,
               "flags",
               cell.flags );
    if ( !bBaseWritten ) {
        return CY_FALSE;
    }
    if ( cell.shape == tile_map_cell_shape_t::FLAT &&
         cell.nStairSteps == TILE_MAP_DEFAULT_STAIR_STEPS ) {
        return CY_TRUE;
    }
    return TileMapSerialization_InsertString(
               pDocument, pCell, "shape",
               TileMapText( CypherTileMapCellShape_Name( cell.shape ) ) ) &&
           TileMapSerialization_InsertU64(
               pDocument, pCell, "stair_steps", cell.nStairSteps );
}

CYPHER_NODISCARD bool_t TileMapSerialization_WriteMarker(
    key_value_document_t *pDocument,
    key_value_t *pMarkers,
    const tile_map_marker_t &marker ) noexcept
{
    char id[CY_UNIQUE_ID_STRING_CAPACITY]{};
    if ( UniqueId_ToString( marker.id, id, sizeof( id ) ) !=
         CY_UNIQUE_ID_STRING_LENGTH ) {
        return CY_FALSE;
    }

    const bool_t bPlayerSpawn =
        marker.kind == tile_map_marker_kind_t::PLAYER_SPAWN;
    const char *pSide = bPlayerSpawn
        ? nullptr
        : TileMapMarkerSide_Name( marker.side );
    if ( !bPlayerSpawn && pSide == nullptr ) {
        return CY_FALSE;
    }

    key_value_t *pMarker = KeyValue_ArrayAppend(
        pDocument,
        pMarkers,
        key_value_type_t::OBJECT );
    if ( pMarker == nullptr ||
         !TileMapSerialization_InsertString(
             pDocument,
             pMarker,
             "id",
             TileMapText( id ) ) ||
         !TileMapSerialization_InsertString(
             pDocument,
             pMarker,
             "kind",
             TileMapText( bPlayerSpawn ? "player_spawn" : "door" ) ) ||
         !TileMapSerialization_InsertU64(
             pDocument,
             pMarker,
             "x",
             static_cast<u32>( marker.cell.x ) ) ||
         !TileMapSerialization_InsertU64(
             pDocument,
             pMarker,
             "y",
             static_cast<u32>( marker.cell.y ) ) ) {
        return CY_FALSE;
    }
    return bPlayerSpawn
        ? TileMapSerialization_InsertF64(
              pDocument,
              pMarker,
              "yaw_degrees",
              marker.yawDegrees )
        : TileMapSerialization_InsertString(
              pDocument,
              pMarker,
              "side",
              TileMapText( pSide ) );
}

template <usize nFields>
CYPHER_NODISCARD bool_t TileMapSerialization_ValidateObjectFields(
    const key_value_t *pObject,
    const char *const ( &fields )[nFields],
    tile_map_serialization_result_t &result,
    const char *pContext,
    usize iElement = CY_INVALID_SIZE,
    usize nRequiredFields = nFields ) noexcept
{
    if ( KeyValue_Type( pObject ) != key_value_type_t::OBJECT ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::TYPE_MISMATCH,
            pContext,
            iElement );
        return CY_FALSE;
    }

    const usize nChildren = KeyValue_ChildCount( pObject );
    for ( usize iChild = 0u; iChild < nChildren; ++iChild ) {
        const key_value_t *pChild = KeyValue_ChildAt( pObject, iChild );
        const string_view_t name = KeyValue_Name( pChild );
        bool_t bKnown = CY_FALSE;
        for ( usize iField = 0u; iField < nFields; ++iField ) {
            if ( StringView_Equals( name, TileMapText( fields[iField] ) ) ) {
                bKnown = CY_TRUE;
                break;
            }
        }
        if ( !bKnown ) {
            result = TileMapSerialization_Failure(
                tile_map_serialization_status_t::UNKNOWN_FIELD,
                nullptr,
                iElement );
            TileMapSerialization_CopyField( result, name );
            return CY_FALSE;
        }
    }

    for ( usize iField = 0u; iField < nRequiredFields; ++iField ) {
        if ( KeyValue_Find( pObject, TileMapText( fields[iField] ) ) == nullptr ) {
            result = TileMapSerialization_Failure(
                tile_map_serialization_status_t::MISSING_FIELD,
                fields[iField],
                iElement );
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD const key_value_t *TileMapSerialization_Field(
    const key_value_t *pObject,
    const char *pName ) noexcept
{
    return KeyValue_Find( pObject, TileMapText( pName ) );
}

CYPHER_NODISCARD bool_t TileMapSerialization_GetU64(
    const key_value_t *pObject,
    const char *pName,
    usize iElement,
    tile_map_serialization_result_t &result,
    u64 &valueOut ) noexcept
{
    const key_value_t *pValue = TileMapSerialization_Field( pObject, pName );
    if ( pValue == nullptr || !KeyValue_GetU64( pValue, &valueOut ) ) {
        result = TileMapSerialization_Failure(
            pValue == nullptr
                ? tile_map_serialization_status_t::MISSING_FIELD
                : tile_map_serialization_status_t::TYPE_MISMATCH,
            pName,
            iElement );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t TileMapSerialization_GetI64(
    const key_value_t *pObject,
    const char *pName,
    usize iElement,
    tile_map_serialization_result_t &result,
    i64 &valueOut ) noexcept
{
    const key_value_t *pValue = TileMapSerialization_Field( pObject, pName );
    if ( pValue == nullptr || !KeyValue_GetI64( pValue, &valueOut ) ) {
        result = TileMapSerialization_Failure(
            pValue == nullptr
                ? tile_map_serialization_status_t::MISSING_FIELD
                : tile_map_serialization_status_t::TYPE_MISMATCH,
            pName,
            iElement );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t TileMapSerialization_GetF64(
    const key_value_t *pObject,
    const char *pName,
    usize iElement,
    tile_map_serialization_result_t &result,
    f64 &valueOut ) noexcept
{
    const key_value_t *pValue = TileMapSerialization_Field( pObject, pName );
    if ( pValue == nullptr || !KeyValue_GetF64( pValue, &valueOut ) ) {
        result = TileMapSerialization_Failure(
            pValue == nullptr
                ? tile_map_serialization_status_t::MISSING_FIELD
                : tile_map_serialization_status_t::TYPE_MISMATCH,
            pName,
            iElement );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t TileMapSerialization_GetString(
    const key_value_t *pObject,
    const char *pName,
    usize iElement,
    tile_map_serialization_result_t &result,
    string_view_t &valueOut ) noexcept
{
    const key_value_t *pValue = TileMapSerialization_Field( pObject, pName );
    if ( pValue == nullptr || !KeyValue_GetString( pValue, &valueOut ) ) {
        result = TileMapSerialization_Failure(
            pValue == nullptr
                ? tile_map_serialization_status_t::MISSING_FIELD
                : tile_map_serialization_status_t::TYPE_MISMATCH,
            pName,
            iElement );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t TileMapSerialization_DecodeCell(
    const key_value_t *pValue,
    usize iCell,
    u32 nWidth,
    u32 nHeight,
    u32 nSchemaVersion,
    vector_t<byte> &occupied,
    vector_t<decoded_cell_t> &cells,
    tile_map_serialization_result_t &result ) noexcept
{
    constexpr const char *legacyFields[]{
        "x",
        "y",
        "floor_level",
        "wall_height_levels",
        "material_slot",
        "flags"
    };
    constexpr const char *fields[]{
        "x", "y", "floor_level", "wall_height_levels", "material_slot",
        "flags", "shape", "stair_steps"
    };
    const bool_t bFieldsValid = nSchemaVersion == 1u
        ? TileMapSerialization_ValidateObjectFields(
            pValue, legacyFields, result, "cells[]", iCell )
        : TileMapSerialization_ValidateObjectFields(
            pValue, fields, result, "cells[]", iCell, 6u );
    if ( !bFieldsValid ) {
        return CY_FALSE;
    }

    u64 x = 0u;
    u64 y = 0u;
    i64 nFloorLevel = 0;
    u64 nWallHeightLevels = 0u;
    u64 nMaterialSlot = 0u;
    u64 flags = 0u;
    tile_map_cell_shape_t shape = tile_map_cell_shape_t::FLAT;
    u64 nStairSteps = TILE_MAP_DEFAULT_STAIR_STEPS;
    if ( !TileMapSerialization_GetU64(
             pValue, "x", iCell, result, x ) ||
         !TileMapSerialization_GetU64(
             pValue, "y", iCell, result, y ) ||
         !TileMapSerialization_GetI64(
             pValue, "floor_level", iCell, result, nFloorLevel ) ||
         !TileMapSerialization_GetU64(
             pValue,
             "wall_height_levels",
             iCell,
             result,
             nWallHeightLevels ) ||
         !TileMapSerialization_GetU64(
             pValue, "material_slot", iCell, result, nMaterialSlot ) ||
         !TileMapSerialization_GetU64(
             pValue, "flags", iCell, result, flags ) ) {
        return CY_FALSE;
    }

    if ( TileMapSerialization_Field( pValue, "shape" ) != nullptr ) {
        string_view_t shapeText{};
        if ( !TileMapSerialization_GetString(
                 pValue, "shape", iCell, result, shapeText ) ) {
            return CY_FALSE;
        }
        bool_t bKnownShape = CY_FALSE;
        for ( u8 value = 0u; value <= static_cast<u8>( tile_map_cell_shape_t::STAIRS_WEST ); ++value ) {
            const auto candidate = static_cast<tile_map_cell_shape_t>( value );
            if ( StringView_Equals( shapeText,
                    TileMapText( CypherTileMapCellShape_Name( candidate ) ) ) ) {
                shape = candidate;
                bKnownShape = CY_TRUE;
                break;
            }
        }
        if ( !bKnownShape ) {
            result = TileMapSerialization_Failure(
                tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                "shape", iCell );
            return CY_FALSE;
        }
    }
    if ( TileMapSerialization_Field( pValue, "stair_steps" ) != nullptr &&
         !TileMapSerialization_GetU64(
             pValue, "stair_steps", iCell, result, nStairSteps ) ) {
        return CY_FALSE;
    }
    if ( nStairSteps < TILE_MAP_MIN_STAIR_STEPS ||
         nStairSteps > TILE_MAP_MAX_STAIR_STEPS ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            "stair_steps", iCell );
        return CY_FALSE;
    }

    if ( x >= nWidth || y >= nHeight ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            x >= nWidth ? "x" : "y",
            iCell );
        return CY_FALSE;
    }
    if ( nFloorLevel < CY_I16_MIN || nFloorLevel > CY_I16_MAX ||
         ( shape != tile_map_cell_shape_t::FLAT && nFloorLevel == CY_I16_MAX ) ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            "floor_level",
            iCell );
        return CY_FALSE;
    }
    if ( nWallHeightLevels == 0u ||
         nWallHeightLevels > CY_U16_MAX ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            "wall_height_levels",
            iCell );
        return CY_FALSE;
    }
    if ( nMaterialSlot > CY_U16_MAX ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            "material_slot",
            iCell );
        return CY_FALSE;
    }
    if ( flags != TILE_MAP_CELL_FLAG_FLOOR ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            "flags",
            iCell );
        return CY_FALSE;
    }

    const usize iDense =
        static_cast<usize>( y ) * static_cast<usize>( nWidth ) +
        static_cast<usize>( x );
    if ( occupied.pData[iDense] != 0u ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::DUPLICATE_CELL,
            "cells",
            iCell );
        return CY_FALSE;
    }
    occupied.pData[iDense] = 1u;

    const decoded_cell_t decoded{
        {
            static_cast<i32>( x ),
            static_cast<i32>( y )
        },
        {
            static_cast<i16>( nFloorLevel ),
            static_cast<u16>( nWallHeightLevels ),
            static_cast<u16>( nMaterialSlot ),
            static_cast<flags16_t>( flags ),
            shape,
            static_cast<u16>( nStairSteps )
        }
    };
    if ( !Vector_PushBack( &cells, decoded ) ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "cells",
            iCell );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t TileMapSerialization_DecodeMarker(
    const key_value_t *pValue,
    usize iMarker,
    u32 nWidth,
    u32 nHeight,
    vector_t<tile_map_marker_t> &markers,
    tile_map_serialization_result_t &result ) noexcept
{
    if ( KeyValue_Type( pValue ) != key_value_type_t::OBJECT ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::TYPE_MISMATCH,
            "markers[]",
            iMarker );
        return CY_FALSE;
    }

    string_view_t kind{};
    if ( !TileMapSerialization_GetString(
             pValue, "kind", iMarker, result, kind ) ) {
        return CY_FALSE;
    }
    const bool_t bPlayerSpawn = StringView_Equals(
        kind,
        TileMapText( "player_spawn" ) );
    const bool_t bDoor = StringView_Equals(
        kind,
        TileMapText( "door" ) );
    if ( !bPlayerSpawn && !bDoor ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::UNSUPPORTED_MARKER_KIND,
            "kind",
            iMarker );
        return CY_FALSE;
    }

    constexpr const char *spawnFields[]{
        "id", "kind", "x", "y", "yaw_degrees"
    };
    constexpr const char *doorFields[]{
        "id", "kind", "x", "y", "side"
    };
    const bool_t bFieldsValid = bPlayerSpawn
        ? TileMapSerialization_ValidateObjectFields(
              pValue,
              spawnFields,
              result,
              "markers[]",
              iMarker )
        : TileMapSerialization_ValidateObjectFields(
              pValue,
              doorFields,
              result,
              "markers[]",
              iMarker );
    if ( !bFieldsValid ) {
        return CY_FALSE;
    }

    string_view_t idText{};
    u64 x = 0u;
    u64 y = 0u;
    if ( !TileMapSerialization_GetString(
             pValue, "id", iMarker, result, idText ) ||
         !TileMapSerialization_GetU64(
             pValue, "x", iMarker, result, x ) ||
         !TileMapSerialization_GetU64(
             pValue, "y", iMarker, result, y ) ) {
        return CY_FALSE;
    }

    unique_id_t id{};
    if ( !UniqueId_FromString( idText, &id ) || !UniqueId_IsValid( id ) ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::INVALID_ID,
            "id",
            iMarker );
        return CY_FALSE;
    }
    for ( usize iPrevious = 0u;
          iPrevious < Vector_Count( &markers );
          ++iPrevious ) {
        if ( UniqueId_Equals( markers.pData[iPrevious].id, id ) ) {
            result = TileMapSerialization_Failure(
                tile_map_serialization_status_t::DUPLICATE_MARKER_ID,
                "id",
                iMarker );
            return CY_FALSE;
        }
    }
    if ( x >= nWidth || y >= nHeight ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            x >= nWidth ? "x" : "y",
            iMarker );
        return CY_FALSE;
    }

    tile_map_marker_t marker{};
    marker.id = id;
    marker.kind = bPlayerSpawn
        ? tile_map_marker_kind_t::PLAYER_SPAWN
        : tile_map_marker_kind_t::DOOR;
    marker.cell = {
        static_cast<i32>( x ),
        static_cast<i32>( y )
    };
    if ( bPlayerSpawn ) {
        f64 yawDegrees = 0.0;
        if ( !TileMapSerialization_GetF64(
                 pValue,
                 "yaw_degrees",
                 iMarker,
                 result,
                 yawDegrees ) ||
             !std::isfinite( yawDegrees ) ||
             yawDegrees < -static_cast<f64>( CY_F32_MAX ) ||
             yawDegrees > static_cast<f64>( CY_F32_MAX ) ) {
            if ( result.status == tile_map_serialization_status_t::OK ) {
                result = TileMapSerialization_Failure(
                    tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                    "yaw_degrees",
                    iMarker );
            }
            return CY_FALSE;
        }
        marker.yawDegrees = static_cast<f32>( yawDegrees );
    } else {
        string_view_t sideText{};
        if ( !TileMapSerialization_GetString(
                 pValue,
                 "side",
                 iMarker,
                 result,
                 sideText ) ||
             !TileMapMarkerSide_Parse( sideText, marker.side ) ) {
            if ( result.status == tile_map_serialization_status_t::OK ) {
                result = TileMapSerialization_Failure(
                    tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
                    "side",
                    iMarker );
            }
            return CY_FALSE;
        }
    }
    if ( !Vector_PushBack( &markers, marker ) ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "markers",
            iMarker );
        return CY_FALSE;
    }
    return CY_TRUE;
}

} // namespace

tile_map_serialization_result_t CypherTileMapSerialization_SaveToText(
    const tile_map_document_t *pDocument,
    text_buffer_t *pTextOut ) noexcept
{
    if ( pDocument == nullptr || pTextOut == nullptr ||
         !TextBuffer_IsValid( pTextOut ) ||
         !Allocator_IsValid( pTextOut->pAllocator ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::INVALID_ARGUMENT );
    }

    usize nActiveCells = 0u;
    tile_map_serialization_result_t result =
        TileMapSerialization_ValidateSource(
            *pDocument,
            nActiveCells );
    if ( result.status != tile_map_serialization_status_t::OK ) {
        return result;
    }

    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = pTextOut->pAllocator;
    documentDesc.nInitialNodes = 64u;
    documentDesc.cbInitialStrings = 4u * CY_KIB;
    key_value_document_owner_t owner{
        KeyValue_CreateDocument( documentDesc )
    };
    if ( owner.pDocument == nullptr ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY );
    }
    if ( !KeyValue_SetDocumentHeader(
             owner.pDocument,
             {
                 CYKV_LANGUAGE_VERSION,
                 TileMapText( TILE_MAP_SCHEMA_ID ),
                 TILE_MAP_SCHEMA_VERSION
             } ) ||
         !KeyValue_SetRootType(
             owner.pDocument,
             key_value_type_t::OBJECT ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY );
    }

    key_value_t *pRoot = KeyValue_Root( owner.pDocument );
    char mapId[CY_UNIQUE_ID_STRING_CAPACITY]{};
    if ( UniqueId_ToString(
             pDocument->mapId,
             mapId,
             sizeof( mapId ) ) != CY_UNIQUE_ID_STRING_LENGTH ||
         !TileMapSerialization_InsertString(
             owner.pDocument,
             pRoot,
             "map_id",
             TileMapText( mapId ) ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "map_id" );
    }

    key_value_t *pDimensions = TileMapSerialization_Insert(
        owner.pDocument,
        pRoot,
        "dimensions",
        key_value_type_t::OBJECT );
    if ( pDimensions == nullptr ||
         !TileMapSerialization_InsertU64(
             owner.pDocument,
             pDimensions,
             "width",
             pDocument->nWidth ) ||
         !TileMapSerialization_InsertU64(
             owner.pDocument,
             pDimensions,
             "height",
             pDocument->nHeight ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "dimensions" );
    }

    key_value_t *pMetrics = TileMapSerialization_Insert(
        owner.pDocument,
        pRoot,
        "metrics",
        key_value_type_t::OBJECT );
    if ( pMetrics == nullptr ||
         !TileMapSerialization_InsertF64(
             owner.pDocument,
             pMetrics,
             "cell_size",
             pDocument->nCellSize ) ||
         !TileMapSerialization_InsertF64(
             owner.pDocument,
             pMetrics,
             "level_height",
             pDocument->nLevelHeight ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "metrics" );
    }

    key_value_t *pCells = TileMapSerialization_Insert(
        owner.pDocument,
        pRoot,
        "cells",
        key_value_type_t::ARRAY );
    if ( pCells == nullptr ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "cells" );
    }
    usize nCellsWritten = 0u;
    for ( u32 y = 0u; y < pDocument->nHeight; ++y ) {
        for ( u32 x = 0u; x < pDocument->nWidth; ++x ) {
            const usize iCell =
                static_cast<usize>( y ) * pDocument->nWidth + x;
            const tile_map_cell_t &cell = pDocument->cells.pData[iCell];
            if ( ( cell.flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) {
                continue;
            }
            if ( !TileMapSerialization_WriteCell(
                     owner.pDocument,
                     pCells,
                     x,
                     y,
                     cell ) ) {
                return TileMapSerialization_Failure(
                    tile_map_serialization_status_t::OUT_OF_MEMORY,
                    "cells",
                    nCellsWritten );
            }
            ++nCellsWritten;
        }
    }
    if ( nCellsWritten != nActiveCells ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::INVALID_DOCUMENT,
            "cells" );
    }

    key_value_t *pMarkers = TileMapSerialization_Insert(
        owner.pDocument,
        pRoot,
        "markers",
        key_value_type_t::ARRAY );
    if ( pMarkers == nullptr ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "markers" );
    }

    vector_t<usize> markerOrder{};
    const usize nMarkers = Vector_Count( &pDocument->markers );
    if ( !Vector_Init(
             &markerOrder,
             pTextOut->pAllocator,
             nMarkers ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "markers" );
    }
    for ( usize iMarker = 0u; iMarker < nMarkers; ++iMarker ) {
        if ( !Vector_PushBack( &markerOrder, iMarker ) ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::OUT_OF_MEMORY,
                "markers" );
        }
    }
    // Marker array order is semantic-free. Sorting by stable UUID makes source
    // output independent of the sequence in which frontends created markers.
    for ( usize iMarker = 1u; iMarker < nMarkers; ++iMarker ) {
        const usize iValue = markerOrder.pData[iMarker];
        usize iInsert = iMarker;
        while ( iInsert > 0u &&
                TileMapMarker_IsBefore(
                    pDocument->markers.pData[iValue],
                    pDocument->markers.pData[
                        markerOrder.pData[iInsert - 1u] ] ) ) {
            markerOrder.pData[iInsert] =
                markerOrder.pData[iInsert - 1u];
            --iInsert;
        }
        markerOrder.pData[iInsert] = iValue;
    }
    for ( usize iMarker = 0u; iMarker < nMarkers; ++iMarker ) {
        if ( !TileMapSerialization_WriteMarker(
                 owner.pDocument,
                 pMarkers,
                 pDocument->markers.pData[markerOrder.pData[iMarker]] ) ) {
            return TileMapSerialization_Failure(
                tile_map_serialization_status_t::OUT_OF_MEMORY,
                "markers",
                iMarker );
        }
    }

    const usize nBindings = Vector_Count( &pDocument->materialBindings );
    if ( nBindings != 0u ) {
        key_value_t *pMaterials = TileMapSerialization_Insert(
            owner.pDocument, pRoot, "materials", key_value_type_t::ARRAY );
        if ( pMaterials == nullptr )
            return TileMapSerialization_Failure( tile_map_serialization_status_t::OUT_OF_MEMORY, "materials" );
        // Bindings are semantic-free in array order; persist ascending stable slots.
        usize order[TILE_MAP_MAX_MATERIAL_BINDINGS]{};
        for ( usize i = 0u; i < nBindings; ++i ) {
            usize insert = i;
            while ( insert != 0u && pDocument->materialBindings.pData[order[insert - 1u]].nSlot >
                    pDocument->materialBindings.pData[i].nSlot ) {
                order[insert] = order[insert - 1u];
                --insert;
            }
            order[insert] = i;
        }
        for ( usize i = 0u; i < nBindings; ++i ) {
            const auto &binding = pDocument->materialBindings.pData[order[i]];
            key_value_t *pMaterial = KeyValue_ArrayAppend( owner.pDocument, pMaterials, key_value_type_t::OBJECT );
            if ( pMaterial == nullptr ||
                 !TileMapSerialization_InsertU64( owner.pDocument, pMaterial, "slot", binding.nSlot ) ||
                 !TileMapSerialization_InsertString( owner.pDocument, pMaterial, "path", TileMapText( binding.path ) ) )
                return TileMapSerialization_Failure( tile_map_serialization_status_t::OUT_OF_MEMORY, "materials", i );
        }
    }

    key_value_write_options_t writeOptions{};
    writeOptions.flags = KEY_VALUE_WRITE_FLAG_PRETTY |
                         KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE;
    writeOptions.nIndentSpaces = 2u;
    writeOptions.nMaxDepth = 8u;

    const key_value_write_result_t measured = KeyValue_WriteText(
        pRoot,
        writeOptions,
        nullptr,
        0u );
    if ( measured.status != key_value_write_status_t::OUTPUT_TRUNCATED ) {
        result = TileMapSerialization_Failure(
            measured.status == key_value_write_status_t::OUT_OF_MEMORY
                ? tile_map_serialization_status_t::OUT_OF_MEMORY
                : tile_map_serialization_status_t::CYKV_WRITE_FAILED );
        result.writeStatus = measured.status;
        return result;
    }
    if ( measured.cchRequired > TILE_MAP_SERIALIZATION_MAX_TEXT_BYTES ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::LIMIT_EXCEEDED );
    }

    text_buffer_t pending{};
    if ( !TextBuffer_Init(
             &pending,
             pTextOut->pAllocator,
             measured.cchRequired ) ||
         !TextBuffer_Resize( &pending, measured.cchRequired ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY );
    }
    const key_value_write_result_t written = KeyValue_WriteText(
        pRoot,
        writeOptions,
        TextBuffer_Data( &pending ),
        TextBuffer_Capacity( &pending ) + 1u );
    if ( written.status != key_value_write_status_t::OK ||
         written.cchWritten != measured.cchRequired ) {
        result = TileMapSerialization_Failure(
            written.status == key_value_write_status_t::OUT_OF_MEMORY
                ? tile_map_serialization_status_t::OUT_OF_MEMORY
                : tile_map_serialization_status_t::CYKV_WRITE_FAILED );
        result.writeStatus = written.status;
        return result;
    }
    if ( !TextBuffer_Assign( pTextOut, TextBuffer_View( &pending ) ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY );
    }

    result = {};
    result.cchText = written.cchWritten;
    return result;
}

tile_map_serialization_result_t CypherTileMapSerialization_LoadFromText(
    string_view_t text,
    const allocator_t *pAllocator,
    tile_map_document_t *pDocumentOut ) noexcept
{
    if ( !StringView_IsValid( text ) || text.cchLength == 0u ||
         !Allocator_IsValid( pAllocator ) || pDocumentOut == nullptr ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::INVALID_ARGUMENT );
    }
    if ( !TileMapDocument_IsFresh( *pDocumentOut ) ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::DESTINATION_NOT_EMPTY );
    }

    key_value_document_desc_t documentDesc{};
    documentDesc.pAllocator = pAllocator;
    key_value_document_owner_t owner{
        KeyValue_CreateDocument( documentDesc )
    };
    if ( owner.pDocument == nullptr ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY );
    }

    key_value_parse_options_t parseOptions{};
    parseOptions.cbMaxInput = TILE_MAP_SERIALIZATION_MAX_TEXT_BYTES;
    parseOptions.nMaxDepth = 8u;
    parseOptions.nMaxNodes =
        TILE_MAP_MAX_ACTIVE_CELLS * 10u +
        TILE_MAP_SERIALIZATION_MAX_MARKERS * 7u +
        TILE_MAP_MAX_MATERIAL_BINDINGS * 3u + 33u;
    parseOptions.nMaxContainerValues =
        TILE_MAP_MAX_ACTIVE_CELLS;
    parseOptions.nMaxCommentDepth = 8u;
    parseOptions.cbMaxStringData = 32u * CY_MIB;

    const key_value_parse_result_t parsed = KeyValue_ParseText(
        text,
        parseOptions,
        owner.pDocument );
    if ( parsed.status != key_value_parse_status_t::OK ) {
        tile_map_serialization_status_t status =
            tile_map_serialization_status_t::CYKV_PARSE_FAILED;
        switch ( parsed.status ) {
            case key_value_parse_status_t::INPUT_LIMIT:
            case key_value_parse_status_t::DEPTH_LIMIT:
            case key_value_parse_status_t::NODE_LIMIT:
            case key_value_parse_status_t::CONTAINER_LIMIT:
            case key_value_parse_status_t::COMMENT_DEPTH_LIMIT:
            case key_value_parse_status_t::STRING_LIMIT:
                status = tile_map_serialization_status_t::LIMIT_EXCEEDED;
                break;
            case key_value_parse_status_t::OUT_OF_MEMORY:
                status = tile_map_serialization_status_t::OUT_OF_MEMORY;
                break;
            default:
                break;
        }
        tile_map_serialization_result_t result =
            TileMapSerialization_Failure(
                status );
        result.parseStatus = parsed.status;
        result.location = parsed.errorLocation;
        result.cchText = text.cchLength;
        return result;
    }

    const key_value_document_header_t header =
        KeyValue_DocumentHeader( owner.pDocument );
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ) {
        tile_map_serialization_result_t result =
            TileMapSerialization_Failure(
                tile_map_serialization_status_t::HEADER_MISMATCH,
                "@cykv" );
        result.location = parsed.languageVersionLocation;
        result.cchText = text.cchLength;
        return result;
    }
    if ( !StringView_Equals(
             header.schemaId,
             TileMapText( TILE_MAP_SCHEMA_ID ) ) ) {
        tile_map_serialization_result_t result =
            TileMapSerialization_Failure(
                tile_map_serialization_status_t::HEADER_MISMATCH,
                "@schema.id" );
        result.location = parsed.schemaIdLocation;
        result.cchText = text.cchLength;
        return result;
    }
    if ( header.nSchemaVersion != 1u && header.nSchemaVersion != 2u &&
         header.nSchemaVersion != TILE_MAP_SCHEMA_VERSION ) {
        tile_map_serialization_result_t result =
            TileMapSerialization_Failure(
                tile_map_serialization_status_t::HEADER_MISMATCH,
                "@schema.version" );
        result.location = parsed.schemaVersionLocation;
        result.cchText = text.cchLength;
        return result;
    }

    const key_value_t *pRoot = KeyValue_Root( owner.pDocument );
    if ( KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
        return TileMapSerialization_Failure(
            tile_map_serialization_status_t::ROOT_TYPE_MISMATCH,
            "$" );
    }
    constexpr const char *rootFields[]{
        "map_id",
        "dimensions",
        "metrics",
        "cells",
        "markers"
    };
    constexpr const char *rootFieldsV3[]{
        "map_id", "dimensions", "metrics", "cells", "markers", "materials"
    };
    tile_map_serialization_result_t result{};
    const bool_t fieldsValid = header.nSchemaVersion >= 3u
        ? TileMapSerialization_ValidateObjectFields( pRoot, rootFieldsV3, result, "$", CY_INVALID_SIZE, 5u )
        : TileMapSerialization_ValidateObjectFields( pRoot, rootFields, result, "$" );
    if ( !fieldsValid ) {
        result.cchText = text.cchLength;
        return result;
    }

    string_view_t mapIdText{};
    if ( !TileMapSerialization_GetString(
             pRoot,
             "map_id",
             CY_INVALID_SIZE,
             result,
             mapIdText ) ) {
        result.cchText = text.cchLength;
        return result;
    }
    unique_id_t mapId{};
    if ( !UniqueId_FromString( mapIdText, &mapId ) ||
         !UniqueId_IsValid( mapId ) ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::INVALID_ID,
            "map_id" );
        result.cchText = text.cchLength;
        return result;
    }

    const key_value_t *pDimensions = TileMapSerialization_Field(
        pRoot,
        "dimensions" );
    constexpr const char *dimensionFields[]{ "width", "height" };
    if ( !TileMapSerialization_ValidateObjectFields(
             pDimensions,
             dimensionFields,
             result,
             "dimensions" ) ) {
        result.cchText = text.cchLength;
        return result;
    }
    u64 nWidth = 0u;
    u64 nHeight = 0u;
    if ( !TileMapSerialization_GetU64(
             pDimensions,
             "width",
             CY_INVALID_SIZE,
             result,
             nWidth ) ||
         !TileMapSerialization_GetU64(
             pDimensions,
             "height",
             CY_INVALID_SIZE,
             result,
             nHeight ) ) {
        result.cchText = text.cchLength;
        return result;
    }
    if ( nWidth == 0u || nWidth > TILE_MAP_MAX_WIDTH ||
         nHeight == 0u || nHeight > TILE_MAP_MAX_HEIGHT ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            nWidth == 0u || nWidth > TILE_MAP_MAX_WIDTH
                ? "dimensions.width"
                : "dimensions.height" );
        result.cchText = text.cchLength;
        return result;
    }

    const key_value_t *pMetrics = TileMapSerialization_Field(
        pRoot,
        "metrics" );
    constexpr const char *metricFields[]{
        "cell_size",
        "level_height"
    };
    if ( !TileMapSerialization_ValidateObjectFields(
             pMetrics,
             metricFields,
             result,
             "metrics" ) ) {
        result.cchText = text.cchLength;
        return result;
    }
    f64 nCellSize = 0.0;
    f64 nLevelHeight = 0.0;
    if ( !TileMapSerialization_GetF64(
             pMetrics,
             "cell_size",
             CY_INVALID_SIZE,
             result,
             nCellSize ) ||
         !TileMapSerialization_GetF64(
             pMetrics,
             "level_height",
             CY_INVALID_SIZE,
             result,
             nLevelHeight ) ) {
        result.cchText = text.cchLength;
        return result;
    }
    const bool_t bCellSizeRepresentable =
        std::isfinite( nCellSize ) &&
        nCellSize <= static_cast<f64>( CY_F32_MAX );
    const bool_t bLevelHeightRepresentable =
        std::isfinite( nLevelHeight ) &&
        nLevelHeight <= static_cast<f64>( CY_F32_MAX );
    const f32 nCellSizeF32 = bCellSizeRepresentable
        ? static_cast<f32>( nCellSize )
        : 0.0f;
    const f32 nLevelHeightF32 = bLevelHeightRepresentable
        ? static_cast<f32>( nLevelHeight )
        : 0.0f;
    const bool_t bCellSizeValid =
        bCellSizeRepresentable &&
        nCellSizeF32 >= TILE_MAP_MIN_CELL_SIZE;
    const bool_t bLevelHeightValid =
        bLevelHeightRepresentable &&
        nLevelHeightF32 >= TILE_MAP_MIN_LEVEL_HEIGHT;
    if ( !bCellSizeValid || !bLevelHeightValid ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::VALUE_OUT_OF_RANGE,
            !bCellSizeValid
                ? "metrics.cell_size"
                : "metrics.level_height" );
        result.cchText = text.cchLength;
        return result;
    }

    const key_value_t *pCells = TileMapSerialization_Field(
        pRoot,
        "cells" );
    if ( KeyValue_Type( pCells ) != key_value_type_t::ARRAY ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::TYPE_MISMATCH,
            "cells" );
        result.cchText = text.cchLength;
        return result;
    }
    const usize nActiveCells = KeyValue_ChildCount( pCells );
    if ( nActiveCells > TILE_MAP_MAX_ACTIVE_CELLS ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::LIMIT_EXCEEDED,
            "cells" );
        result.cchText = text.cchLength;
        return result;
    }

    vector_t<decoded_cell_t> decodedCells{};
    vector_t<byte> occupied{};
    const usize nDenseCells =
        static_cast<usize>( nWidth ) * static_cast<usize>( nHeight );
    if ( !Vector_Init( &decodedCells, pAllocator, nActiveCells ) ||
         !Vector_Init( &occupied, pAllocator, nDenseCells ) ||
         !Vector_Resize( &occupied, nDenseCells ) ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY );
        result.cchText = text.cchLength;
        return result;
    }
    for ( usize iCell = 0u; iCell < nActiveCells; ++iCell ) {
        if ( !TileMapSerialization_DecodeCell(
                 KeyValue_ChildAt( pCells, iCell ),
                 iCell,
                 static_cast<u32>( nWidth ),
                 static_cast<u32>( nHeight ),
                 header.nSchemaVersion,
                 occupied,
                 decodedCells,
                 result ) ) {
            result.cchText = text.cchLength;
            return result;
        }
    }

    const key_value_t *pMarkers = TileMapSerialization_Field(
        pRoot,
        "markers" );
    if ( KeyValue_Type( pMarkers ) != key_value_type_t::ARRAY ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::TYPE_MISMATCH,
            "markers" );
        result.cchText = text.cchLength;
        return result;
    }
    const usize nMarkers = KeyValue_ChildCount( pMarkers );
    if ( nMarkers > TILE_MAP_SERIALIZATION_MAX_MARKERS ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::LIMIT_EXCEEDED,
            "markers" );
        result.cchText = text.cchLength;
        return result;
    }

    vector_t<tile_map_marker_t> decodedMarkers{};
    if ( !Vector_Init( &decodedMarkers, pAllocator, nMarkers ) ) {
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY );
        result.cchText = text.cchLength;
        return result;
    }
    for ( usize iMarker = 0u; iMarker < nMarkers; ++iMarker ) {
        if ( !TileMapSerialization_DecodeMarker(
                 KeyValue_ChildAt( pMarkers, iMarker ),
                 iMarker,
                 static_cast<u32>( nWidth ),
                 static_cast<u32>( nHeight ),
                 decodedMarkers,
                 result ) ) {
            result.cchText = text.cchLength;
            return result;
        }
    }

    vector_t<tile_map_material_binding_t> decodedMaterials{};
    const key_value_t *pMaterials = TileMapSerialization_Field( pRoot, "materials" );
    const usize nBindings = pMaterials != nullptr ? KeyValue_ChildCount( pMaterials ) : 0u;
    if ( pMaterials != nullptr && KeyValue_Type( pMaterials ) != key_value_type_t::ARRAY )
        return TileMapSerialization_Failure( tile_map_serialization_status_t::TYPE_MISMATCH, "materials" );
    if ( nBindings > TILE_MAP_MAX_MATERIAL_BINDINGS )
        return TileMapSerialization_Failure( tile_map_serialization_status_t::LIMIT_EXCEEDED, "materials" );
    if ( !Vector_Init( &decodedMaterials, pAllocator, nBindings ) )
        return TileMapSerialization_Failure( tile_map_serialization_status_t::OUT_OF_MEMORY, "materials" );
    for ( usize i = 0u; i < nBindings; ++i ) {
        const auto *pBinding = KeyValue_ChildAt( pMaterials, i );
        constexpr const char *fields[]{ "slot", "path" };
        u64 slot = 0u;
        string_view_t path{};
        if ( !TileMapSerialization_ValidateObjectFields( pBinding, fields, result, "materials[]", i ) ||
             !TileMapSerialization_GetU64( pBinding, "slot", i, result, slot ) ||
             !TileMapSerialization_GetString( pBinding, "path", i, result, path ) ) {
            result.cchText = text.cchLength;
            return result;
        }
        if ( slot > CY_U16_MAX )
            return TileMapSerialization_Failure( tile_map_serialization_status_t::VALUE_OUT_OF_RANGE, "materials[].slot", i );
        if ( !CypherTileMapMaterialPath_IsValid( path ) )
            return TileMapSerialization_Failure( tile_map_serialization_status_t::INVALID_MATERIAL_PATH, "materials[].path", i );
        for ( usize j = 0u; j < i; ++j ) {
            if ( decodedMaterials.pData[j].nSlot == slot )
                return TileMapSerialization_Failure( tile_map_serialization_status_t::DUPLICATE_MATERIAL_SLOT, "materials[].slot", i );
        }
        tile_map_material_binding_t binding{};
        binding.nSlot = static_cast<u16>( slot );
        Cy_MemCopy( binding.path, path.pData, path.cchLength );
        if ( !Vector_PushBack( &decodedMaterials, binding ) )
            return TileMapSerialization_Failure( tile_map_serialization_status_t::OUT_OF_MEMORY, "materials", i );
    }

    const tile_map_document_desc_t mapDesc{
        static_cast<u32>( nWidth ),
        static_cast<u32>( nHeight ),
        nCellSizeF32,
        nLevelHeightF32
    };
    const tile_map_document_status_t documentStatus =
        CypherTileMapDocument_Init(
            pDocumentOut,
            pAllocator,
            mapDesc );
    if ( documentStatus != tile_map_document_status_t::OK ) {
        result = TileMapSerialization_Failure(
            documentStatus == tile_map_document_status_t::ALLOCATION_FAILED
                ? tile_map_serialization_status_t::OUT_OF_MEMORY
                : tile_map_serialization_status_t::DOCUMENT_INIT_FAILED );
        result.documentStatus = documentStatus;
        result.cchText = text.cchLength;
        return result;
    }

    // Persistence adapter points: identity and marker mutation are public data in
    // tile_map_document_t, while cell mutation already has a document API.
    pDocumentOut->mapId = mapId;
    if ( !Vector_Reserve( &pDocumentOut->markers, nMarkers ) ) {
        CypherTileMapDocument_Shutdown( pDocumentOut );
        result = TileMapSerialization_Failure(
            tile_map_serialization_status_t::OUT_OF_MEMORY,
            "markers" );
        result.cchText = text.cchLength;
        return result;
    }
    for ( usize iCell = 0u;
          iCell < Vector_Count( &decodedCells );
          ++iCell ) {
        const decoded_cell_t &decoded = decodedCells.pData[iCell];
        tile_map_cell_t *pCell = CypherTileMapDocument_CellAt(
            pDocumentOut,
            decoded.coordinate );
        if ( pCell == nullptr ) {
            CypherTileMapDocument_Shutdown( pDocumentOut );
            result = TileMapSerialization_Failure(
                tile_map_serialization_status_t::INVALID_DOCUMENT,
                "cells",
                iCell );
            result.cchText = text.cchLength;
            return result;
        }
        *pCell = decoded.cell;
    }
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &decodedMarkers );
          ++iMarker ) {
        if ( !Vector_PushBack(
                 &pDocumentOut->markers,
                 decodedMarkers.pData[iMarker] ) ) {
            CypherTileMapDocument_Shutdown( pDocumentOut );
            result = TileMapSerialization_Failure(
                tile_map_serialization_status_t::OUT_OF_MEMORY,
                "markers",
                iMarker );
            result.cchText = text.cchLength;
            return result;
        }
    }
    Vector_Shutdown( &pDocumentOut->materialBindings );
    Vector_Move( &pDocumentOut->materialBindings, &decodedMaterials );
    CypherTileMapDocument_MarkSaved( pDocumentOut );

    result = {};
    result.cchText = text.cchLength;
    return result;
}

const char *CypherTileMapSerialization_StatusName(
    tile_map_serialization_status_t status ) noexcept
{
    switch ( status ) {
        case tile_map_serialization_status_t::INVALID_MATERIAL_PATH: return "INVALID_MATERIAL_PATH";
        case tile_map_serialization_status_t::DUPLICATE_MATERIAL_SLOT: return "DUPLICATE_MATERIAL_SLOT";
        case tile_map_serialization_status_t::OK: return "OK";
        case tile_map_serialization_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case tile_map_serialization_status_t::DESTINATION_NOT_EMPTY: return "DESTINATION_NOT_EMPTY";
        case tile_map_serialization_status_t::NOT_INITIALIZED: return "NOT_INITIALIZED";
        case tile_map_serialization_status_t::INVALID_DOCUMENT: return "INVALID_DOCUMENT";
        case tile_map_serialization_status_t::LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
        case tile_map_serialization_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case tile_map_serialization_status_t::CYKV_WRITE_FAILED: return "CYKV_WRITE_FAILED";
        case tile_map_serialization_status_t::CYKV_PARSE_FAILED: return "CYKV_PARSE_FAILED";
        case tile_map_serialization_status_t::HEADER_MISMATCH: return "HEADER_MISMATCH";
        case tile_map_serialization_status_t::ROOT_TYPE_MISMATCH: return "ROOT_TYPE_MISMATCH";
        case tile_map_serialization_status_t::MISSING_FIELD: return "MISSING_FIELD";
        case tile_map_serialization_status_t::UNKNOWN_FIELD: return "UNKNOWN_FIELD";
        case tile_map_serialization_status_t::TYPE_MISMATCH: return "TYPE_MISMATCH";
        case tile_map_serialization_status_t::VALUE_OUT_OF_RANGE: return "VALUE_OUT_OF_RANGE";
        case tile_map_serialization_status_t::INVALID_ID: return "INVALID_ID";
        case tile_map_serialization_status_t::DUPLICATE_CELL: return "DUPLICATE_CELL";
        case tile_map_serialization_status_t::DUPLICATE_MARKER_ID: return "DUPLICATE_MARKER_ID";
        case tile_map_serialization_status_t::UNSUPPORTED_MARKER_KIND: return "UNSUPPORTED_MARKER_KIND";
        case tile_map_serialization_status_t::DOCUMENT_INIT_FAILED: return "DOCUMENT_INIT_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::tools::tile_editor
