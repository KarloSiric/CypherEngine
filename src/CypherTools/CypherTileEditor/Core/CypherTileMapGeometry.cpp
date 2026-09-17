//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapGeometry.cpp
//  Purpose: Implements renderer-neutral blockout geometry generation.
//  Details: The builder emits one floor box per active cell and wall boxes
//           only for exposed edges. Cypher's world convention is Z-up.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileMapGeometry.h"

#include <cmath>

namespace cypher::tools::tile_editor
{

namespace
{

bool_t TileMapGeometry_IsCanonicalEmpty(
    const tile_map_geometry_t &geometry ) noexcept
{
    return geometry.pAllocator == nullptr &&
           geometry.boxes.pData == nullptr &&
           geometry.boxes.nCount == 0u &&
           geometry.boxes.nCapacity == 0u &&
           geometry.boxes.pAllocator == nullptr &&
           !geometry.bHasBounds;
}

bool_t TileMapGeometry_IsInitialized(
    const tile_map_geometry_t *pGeometry ) noexcept
{
    return pGeometry != nullptr &&
           Allocator_IsValid( pGeometry->pAllocator ) &&
           Vector_IsValid( &pGeometry->boxes ) &&
           pGeometry->boxes.pAllocator == pGeometry->pAllocator;
}

const tile_map_cell_t *TileMapGeometry_FloorCellAt(
    const tile_map_document_t &document,
    i32 x,
    i32 y ) noexcept
{
    if ( x < 0 || y < 0 ||
         static_cast<u32>( x ) >= document.nWidth ||
         static_cast<u32>( y ) >= document.nHeight ) {
        return nullptr;
    }
    const usize iCell = static_cast<usize>( y ) * document.nWidth +
                        static_cast<usize>( x );
    const tile_map_cell_t *pCell = document.cells.pData + iCell;
    return ( pCell->flags & TILE_MAP_CELL_FLAG_FLOOR ) != 0u
        ? pCell
        : nullptr;
}

tile_map_marker_side_t TileMapGeometry_MarkerSide(
    tile_map_geometry_side_t side ) noexcept
{
    switch ( side ) {
        case tile_map_geometry_side_t::NORTH:
            return tile_map_marker_side_t::NORTH;
        case tile_map_geometry_side_t::EAST:
            return tile_map_marker_side_t::EAST;
        case tile_map_geometry_side_t::SOUTH:
            return tile_map_marker_side_t::SOUTH;
        case tile_map_geometry_side_t::WEST:
            return tile_map_marker_side_t::WEST;
        case tile_map_geometry_side_t::NONE:
            break;
    }
    return tile_map_marker_side_t::NONE;
}

bool_t TileMapGeometry_HasValidDoor(
    const tile_map_document_t &document,
    tile_map_grid_coord_t coordinate,
    tile_map_geometry_side_t side ) noexcept
{
    const tile_map_marker_side_t markerSide =
        TileMapGeometry_MarkerSide( side );
    if ( !CypherTileMapMarkerSide_IsCardinal( markerSide ) ) {
        return CY_FALSE;
    }
    for ( usize iMarker = 0u;
          iMarker < Vector_Count( &document.markers );
          ++iMarker ) {
        const tile_map_marker_t &marker = document.markers.pData[iMarker];
        if ( marker.kind == tile_map_marker_kind_t::DOOR &&
             UniqueId_IsValid( marker.id ) &&
             marker.cell.x == coordinate.x &&
             marker.cell.y == coordinate.y &&
             marker.side == markerSide &&
             marker.yawDegrees == 0.0f ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

// The neighbor's high edge meets an upper landing. Comparing only its base
// level would otherwise seal this connection with a full-height cliff wall.
i32 TileMapGeometry_NeighborFloorLevel(
    const tile_map_cell_t &neighbor, i32 dx, i32 dy ) noexcept
{
    const bool_t bHighEdge =
        ( dx == 1 && neighbor.shape == tile_map_cell_shape_t::STAIRS_WEST ) ||
        ( dx == -1 && neighbor.shape == tile_map_cell_shape_t::STAIRS_EAST ) ||
        ( dy == 1 && neighbor.shape == tile_map_cell_shape_t::STAIRS_NORTH ) ||
        ( dy == -1 && neighbor.shape == tile_map_cell_shape_t::STAIRS_SOUTH );
    return static_cast<i32>( neighbor.nFloorLevel ) + ( bHighEdge ? 1 : 0 );
}

usize TileMapGeometry_SideCount(
    const tile_map_document_t &document,
    const tile_map_cell_t &cell,
    i32 x,
    i32 y ) noexcept
{
    if ( cell.shape != tile_map_cell_shape_t::FLAT ) {
        return 0u;
    }
    usize nEdges = 0u;
    constexpr tile_map_grid_coord_t offsets[]{
        { 0, -1 },
        { 1, 0 },
        { 0, 1 },
        { -1, 0 }
    };
    for ( const tile_map_grid_coord_t offset : offsets ) {
        const tile_map_cell_t *pNeighbor = TileMapGeometry_FloorCellAt(
            document,
            x + offset.x,
            y + offset.y );
        nEdges += pNeighbor == nullptr ||
                  cell.nFloorLevel > TileMapGeometry_NeighborFloorLevel(
                      *pNeighbor, offset.x, offset.y );
    }
    return nEdges;
}

bool_t TileMapGeometry_NarrowF32(
    f64 value,
    f32 &valueOut ) noexcept
{
    if ( !std::isfinite( value ) ||
         value < -static_cast<f64>( CY_F32_MAX ) ||
         value > static_cast<f64>( CY_F32_MAX ) ) {
        return CY_FALSE;
    }
    valueOut = static_cast<f32>( value );
    return std::isfinite( valueOut );
}

bool_t TileMapGeometry_IncludeBounds(
    tile_map_geometry_t &geometry,
    const tile_map_geometry_box_t &box ) noexcept
{
    f32 minX = 0.0f;
    f32 minY = 0.0f;
    f32 minZ = 0.0f;
    f32 maxX = 0.0f;
    f32 maxY = 0.0f;
    f32 maxZ = 0.0f;
    if ( !TileMapGeometry_NarrowF32(
             static_cast<f64>( box.centerX ) - box.halfExtentX,
             minX ) ||
         !TileMapGeometry_NarrowF32(
             static_cast<f64>( box.centerY ) - box.halfExtentY,
             minY ) ||
         !TileMapGeometry_NarrowF32(
             static_cast<f64>( box.centerZ ) - box.halfExtentZ,
             minZ ) ||
         !TileMapGeometry_NarrowF32(
             static_cast<f64>( box.centerX ) + box.halfExtentX,
             maxX ) ||
         !TileMapGeometry_NarrowF32(
             static_cast<f64>( box.centerY ) + box.halfExtentY,
             maxY ) ||
         !TileMapGeometry_NarrowF32(
             static_cast<f64>( box.centerZ ) + box.halfExtentZ,
             maxZ ) ) {
        return CY_FALSE;
    }
    if ( !geometry.bHasBounds ) {
        geometry.bHasBounds = CY_TRUE;
        geometry.boundsMinX = minX;
        geometry.boundsMinY = minY;
        geometry.boundsMinZ = minZ;
        geometry.boundsMaxX = maxX;
        geometry.boundsMaxY = maxY;
        geometry.boundsMaxZ = maxZ;
        return CY_TRUE;
    }
    if ( minX < geometry.boundsMinX ) geometry.boundsMinX = minX;
    if ( minY < geometry.boundsMinY ) geometry.boundsMinY = minY;
    if ( minZ < geometry.boundsMinZ ) geometry.boundsMinZ = minZ;
    if ( maxX > geometry.boundsMaxX ) geometry.boundsMaxX = maxX;
    if ( maxY > geometry.boundsMaxY ) geometry.boundsMaxY = maxY;
    if ( maxZ > geometry.boundsMaxZ ) geometry.boundsMaxZ = maxZ;
    return CY_TRUE;
}

bool_t TileMapGeometry_FloorBox(
    const tile_map_document_t &document,
    const tile_map_cell_t &cell,
    tile_map_grid_coord_t coordinate,
    f32 nFloorThickness,
    tile_map_geometry_box_t &boxOut ) noexcept
{
    const f64 nCellSize = document.nCellSize;
    const f64 floorZ = static_cast<f64>( cell.nFloorLevel ) *
                       document.nLevelHeight;
    tile_map_geometry_box_t box{};
    if ( !TileMapGeometry_NarrowF32(
             ( static_cast<f64>( coordinate.x ) + 0.5 ) * nCellSize,
             box.centerX ) ||
         !TileMapGeometry_NarrowF32(
             ( static_cast<f64>( coordinate.y ) + 0.5 ) * nCellSize,
             box.centerY ) ||
         !TileMapGeometry_NarrowF32(
             floorZ - static_cast<f64>( nFloorThickness ) * 0.5,
             box.centerZ ) ||
         !TileMapGeometry_NarrowF32( nCellSize * 0.5, box.halfExtentX ) ||
         !TileMapGeometry_NarrowF32( nCellSize * 0.5, box.halfExtentY ) ||
         !TileMapGeometry_NarrowF32(
             static_cast<f64>( nFloorThickness ) * 0.5,
             box.halfExtentZ ) ) {
        return CY_FALSE;
    }
    box.kind = tile_map_geometry_box_kind_t::FLOOR;
    box.side = tile_map_geometry_side_t::NONE;
    box.nMaterialSlot = cell.nMaterialSlot;
    box.sourceCell = coordinate;
    boxOut = box;
    return CY_TRUE;
}

bool_t TileMapGeometry_StairBox(
    const tile_map_document_t &document,
    const tile_map_cell_t &cell,
    tile_map_grid_coord_t coordinate,
    f32 nFloorThickness,
    u16 iStep,
    tile_map_geometry_box_t &box ) noexcept
{
    const f64 size = document.nCellSize;
    const f64 run = size / cell.nStairSteps;
    const f64 bottom = static_cast<f64>( cell.nFloorLevel ) *
                       document.nLevelHeight - nFloorThickness;
    const f64 top = ( static_cast<f64>( cell.nFloorLevel ) +
                     static_cast<f64>( iStep + 1u ) / cell.nStairSteps ) *
                    document.nLevelHeight;
    const bool_t bAlongX = cell.shape == tile_map_cell_shape_t::STAIRS_EAST ||
                          cell.shape == tile_map_cell_shape_t::STAIRS_WEST;
    const bool_t bReverse = cell.shape == tile_map_cell_shape_t::STAIRS_NORTH ||
                           cell.shape == tile_map_cell_shape_t::STAIRS_WEST;
    const f64 offset = bReverse
        ? size - ( iStep + 0.5 ) * run
        : ( iStep + 0.5 ) * run;
    box.kind = tile_map_geometry_box_kind_t::STAIR;
    box.nMaterialSlot = cell.nMaterialSlot;
    box.sourceCell = coordinate;
    return TileMapGeometry_NarrowF32(
               coordinate.x * size + ( bAlongX ? offset : size * 0.5 ), box.centerX ) &&
           TileMapGeometry_NarrowF32(
               coordinate.y * size + ( bAlongX ? size * 0.5 : offset ), box.centerY ) &&
           TileMapGeometry_NarrowF32( ( top + bottom ) * 0.5, box.centerZ ) &&
           TileMapGeometry_NarrowF32( ( bAlongX ? run : size ) * 0.5, box.halfExtentX ) &&
           TileMapGeometry_NarrowF32( ( bAlongX ? size : run ) * 0.5, box.halfExtentY ) &&
           TileMapGeometry_NarrowF32( ( top - bottom ) * 0.5, box.halfExtentZ );
}

bool_t TileMapGeometry_WallBox(
    const tile_map_document_t &document,
    const tile_map_cell_t &cell,
    tile_map_grid_coord_t coordinate,
    tile_map_geometry_side_t side,
    f32 nWallThickness,
    f64 zMin,
    f64 zMax,
    tile_map_geometry_box_t &boxOut ) noexcept
{
    if ( !std::isfinite( zMin ) || !std::isfinite( zMax ) ||
         zMax <= zMin ) {
        return CY_FALSE;
    }
    const f64 nCellSize = document.nCellSize;
    const f64 xMin = static_cast<f64>( coordinate.x ) * nCellSize;
    const f64 yMin = static_cast<f64>( coordinate.y ) * nCellSize;
    const f64 xMax = xMin + nCellSize;
    const f64 yMax = yMin + nCellSize;

    tile_map_geometry_box_t box{};
    if ( !TileMapGeometry_NarrowF32(
             ( zMin + zMax ) * 0.5,
             box.centerZ ) ||
         !TileMapGeometry_NarrowF32(
             ( zMax - zMin ) * 0.5,
             box.halfExtentZ ) ) {
        return CY_FALSE;
    }
    box.kind = tile_map_geometry_box_kind_t::WALL;
    box.side = side;
    box.nMaterialSlot = cell.nMaterialSlot;
    box.sourceCell = coordinate;

    if ( side == tile_map_geometry_side_t::NORTH ||
         side == tile_map_geometry_side_t::SOUTH ) {
        if ( !TileMapGeometry_NarrowF32(
                 ( xMin + xMax ) * 0.5,
                 box.centerX ) ||
             !TileMapGeometry_NarrowF32(
                 side == tile_map_geometry_side_t::NORTH
                     ? yMin + static_cast<f64>( nWallThickness ) * 0.5
                     : yMax - static_cast<f64>( nWallThickness ) * 0.5,
                 box.centerY ) ||
             !TileMapGeometry_NarrowF32(
                 nCellSize * 0.5,
                 box.halfExtentX ) ||
             !TileMapGeometry_NarrowF32(
                 static_cast<f64>( nWallThickness ) * 0.5,
                 box.halfExtentY ) ) {
            return CY_FALSE;
        }
    } else if ( side == tile_map_geometry_side_t::EAST ||
                side == tile_map_geometry_side_t::WEST ) {
        if ( !TileMapGeometry_NarrowF32(
                 side == tile_map_geometry_side_t::WEST
                     ? xMin + static_cast<f64>( nWallThickness ) * 0.5
                     : xMax - static_cast<f64>( nWallThickness ) * 0.5,
                 box.centerX ) ||
             !TileMapGeometry_NarrowF32(
                 ( yMin + yMax ) * 0.5,
                 box.centerY ) ||
             !TileMapGeometry_NarrowF32(
                 static_cast<f64>( nWallThickness ) * 0.5,
                 box.halfExtentX ) ||
             !TileMapGeometry_NarrowF32(
                 nCellSize * 0.5,
                 box.halfExtentY ) ) {
            return CY_FALSE;
        }
    } else {
        return CY_FALSE;
    }
    boxOut = box;
    return CY_TRUE;
}

bool_t TileMapGeometry_DoorBox(
    const tile_map_document_t &document,
    const tile_map_cell_t &cell,
    tile_map_grid_coord_t coordinate,
    tile_map_geometry_side_t side,
    f32 nWallThickness,
    tile_map_geometry_box_t &boxOut ) noexcept
{
    const f64 floorZ = static_cast<f64>( cell.nFloorLevel ) *
                       document.nLevelHeight;
    const f64 wallHeight =
        static_cast<f64>( cell.nWallHeightLevels ) *
        document.nLevelHeight;
    const f64 doorHeight = std::fmin(
        wallHeight,
        static_cast<f64>( document.nLevelHeight ) *
            TILE_MAP_DOOR_HEIGHT_RATIO );
    tile_map_geometry_box_t box{};
    if ( !TileMapGeometry_WallBox(
             document,
             cell,
             coordinate,
             side,
             nWallThickness,
             floorZ,
             floorZ + doorHeight,
             box ) ) {
        return CY_FALSE;
    }
    box.kind = tile_map_geometry_box_kind_t::DOOR;
    if ( side == tile_map_geometry_side_t::NORTH ||
         side == tile_map_geometry_side_t::SOUTH ) {
        box.halfExtentX *= TILE_MAP_DOOR_WIDTH_RATIO;
    } else {
        box.halfExtentY *= TILE_MAP_DOOR_WIDTH_RATIO;
    }
    boxOut = box;
    return CY_TRUE;
}

} // namespace

tile_map_document_status_t CypherTileMapGeometry_Init(
    tile_map_geometry_t *pGeometry,
    const allocator_t *pAllocator ) noexcept
{
    if ( pGeometry == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }
    if ( TileMapGeometry_IsInitialized( pGeometry ) ) {
        return tile_map_document_status_t::ALREADY_INITIALIZED;
    }
    if ( !TileMapGeometry_IsCanonicalEmpty( *pGeometry ) ) {
        return tile_map_document_status_t::INVALID_STATE;
    }
    if ( !Vector_Init( &pGeometry->boxes, pAllocator ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    pGeometry->pAllocator = pAllocator;
    return tile_map_document_status_t::OK;
}

void CypherTileMapGeometry_Shutdown(
    tile_map_geometry_t *pGeometry ) noexcept
{
    if ( pGeometry == nullptr ) {
        return;
    }
    if ( Vector_IsValid( &pGeometry->boxes ) ) {
        Vector_Shutdown( &pGeometry->boxes );
    }
    pGeometry->pAllocator = nullptr;
    pGeometry->bHasBounds = CY_FALSE;
    pGeometry->boundsMinX = 0.0f;
    pGeometry->boundsMinY = 0.0f;
    pGeometry->boundsMinZ = 0.0f;
    pGeometry->boundsMaxX = 0.0f;
    pGeometry->boundsMaxY = 0.0f;
    pGeometry->boundsMaxZ = 0.0f;
}

tile_map_document_status_t CypherTileMapGeometry_Build(
    const tile_map_document_t *pDocument,
    const tile_map_geometry_desc_t &desc,
    tile_map_geometry_t *pGeometry ) noexcept
{
    if ( !CypherTileMapDocument_IsInitialized( pDocument ) ) {
        return tile_map_document_status_t::NOT_INITIALIZED;
    }
    if ( !TileMapGeometry_IsInitialized( pGeometry ) ) {
        return tile_map_document_status_t::INVALID_ARGUMENT;
    }
    if ( !std::isfinite( desc.nFloorThickness ) ||
         !std::isfinite( desc.nWallThickness ) ||
         desc.nFloorThickness <= 0.0f ||
         desc.nWallThickness <= 0.0f ||
         desc.nFloorThickness > pDocument->nLevelHeight ||
         desc.nWallThickness > pDocument->nCellSize ) {
        return tile_map_document_status_t::INVALID_METRICS;
    }

    usize nBoxCount = 0u;
    for ( u32 y = 0u; y < pDocument->nHeight; ++y ) {
        for ( u32 x = 0u; x < pDocument->nWidth; ++x ) {
            const usize iCell = static_cast<usize>( y ) *
                                    pDocument->nWidth +
                                x;
            const tile_map_cell_t &cell = pDocument->cells.pData[iCell];
            if ( ( cell.flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) {
                continue;
            }
            if ( cell.nWallHeightLevels == 0u ||
                 cell.flags != TILE_MAP_CELL_FLAG_FLOOR ||
                 !CypherTileMapCellShape_IsValid( cell.shape ) ||
                 cell.nStairSteps < TILE_MAP_MIN_STAIR_STEPS ||
                 cell.nStairSteps > TILE_MAP_MAX_STAIR_STEPS ||
                 ( cell.shape != tile_map_cell_shape_t::FLAT &&
                   cell.nFloorLevel == CY_I16_MAX ) ) {
                return tile_map_document_status_t::INVALID_CELL;
            }
            const usize nCellBoxes =
                ( cell.shape == tile_map_cell_shape_t::FLAT ? 1u : cell.nStairSteps ) +
                TileMapGeometry_SideCount(
                    *pDocument,
                    cell,
                    static_cast<i32>( x ),
                    static_cast<i32>( y ) );
            if ( nCellBoxes > CY_USIZE_MAX - nBoxCount ) {
                return tile_map_document_status_t::ALLOCATION_FAILED;
            }
            nBoxCount += nCellBoxes;
        }
    }

    vector_t<tile_map_geometry_box_t> pendingBoxes{};
    if ( !Vector_Init(
             &pendingBoxes,
             pGeometry->pAllocator,
             nBoxCount ) ) {
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }
    if ( !Vector_Resize( &pendingBoxes, nBoxCount ) ) {
        Vector_Shutdown( &pendingBoxes );
        return tile_map_document_status_t::ALLOCATION_FAILED;
    }

    // pendingBoxes owns its allocation through vector_t's destructor. Every
    // validation return below therefore leaves the published geometry intact
    // and releases the unpublished allocation.

    tile_map_geometry_t pendingBounds{};
    usize iBox = 0u;
    for ( u32 y = 0u; y < pDocument->nHeight; ++y ) {
        for ( u32 x = 0u; x < pDocument->nWidth; ++x ) {
            const usize iCell = static_cast<usize>( y ) *
                                    pDocument->nWidth +
                                x;
            const tile_map_cell_t &cell = pDocument->cells.pData[iCell];
            if ( ( cell.flags & TILE_MAP_CELL_FLAG_FLOOR ) == 0u ) {
                continue;
            }
            const tile_map_grid_coord_t coordinate{
                static_cast<i32>( x ),
                static_cast<i32>( y )
            };
            if ( cell.shape != tile_map_cell_shape_t::FLAT ) {
                for ( u16 iStep = 0u; iStep < cell.nStairSteps; ++iStep ) {
                    tile_map_geometry_box_t stepBox{};
                    if ( !TileMapGeometry_StairBox( *pDocument, cell, coordinate,
                             desc.nFloorThickness, iStep, stepBox ) ||
                         !TileMapGeometry_IncludeBounds( pendingBounds, stepBox ) ) {
                        return tile_map_document_status_t::INVALID_METRICS;
                    }
                    pendingBoxes.pData[iBox++] = stepBox;
                }
                continue;
            }
            tile_map_geometry_box_t floorBox{};
            if ( !TileMapGeometry_FloorBox(
                     *pDocument,
                     cell,
                     coordinate,
                     desc.nFloorThickness,
                     floorBox ) ||
                 !TileMapGeometry_IncludeBounds(
                     pendingBounds,
                     floorBox ) ) {
                return tile_map_document_status_t::INVALID_METRICS;
            }
            pendingBoxes.pData[iBox++] = floorBox;

            struct edge_t {
                i32 dx;
                i32 dy;
                tile_map_geometry_side_t side;
            };
            constexpr edge_t edges[] = {
                { 0, -1, tile_map_geometry_side_t::NORTH },
                { 1, 0, tile_map_geometry_side_t::EAST },
                { 0, 1, tile_map_geometry_side_t::SOUTH },
                { -1, 0, tile_map_geometry_side_t::WEST }
            };
            for ( const edge_t &edge : edges ) {
                const tile_map_cell_t *pNeighbor =
                    TileMapGeometry_FloorCellAt(
                        *pDocument,
                        static_cast<i32>( x ) + edge.dx,
                        static_cast<i32>( y ) + edge.dy );
                if ( pNeighbor != nullptr &&
                     cell.nFloorLevel <= TileMapGeometry_NeighborFloorLevel(
                         *pNeighbor, edge.dx, edge.dy ) ) {
                    continue;
                }
                const f64 floorZ = static_cast<f64>( cell.nFloorLevel ) *
                                   pDocument->nLevelHeight;
                const f64 zMin = pNeighbor != nullptr
                    ? static_cast<f64>( TileMapGeometry_NeighborFloorLevel(
                          *pNeighbor, edge.dx, edge.dy ) ) *
                          pDocument->nLevelHeight
                    : floorZ;
                const f64 zMax = pNeighbor != nullptr
                    ? floorZ
                    : floorZ +
                          static_cast<f64>( cell.nWallHeightLevels ) *
                              pDocument->nLevelHeight;
                tile_map_geometry_box_t wallBox{};
                const bool_t bDoorEdge = pNeighbor == nullptr &&
                    TileMapGeometry_HasValidDoor(
                        *pDocument,
                        coordinate,
                        edge.side );
                const bool_t bBoxBuilt = bDoorEdge
                    ? TileMapGeometry_DoorBox(
                          *pDocument,
                          cell,
                          coordinate,
                          edge.side,
                          desc.nWallThickness,
                          wallBox )
                    : TileMapGeometry_WallBox(
                          *pDocument,
                          cell,
                          coordinate,
                          edge.side,
                          desc.nWallThickness,
                          zMin,
                          zMax,
                          wallBox );
                if ( !bBoxBuilt ||
                     !TileMapGeometry_IncludeBounds(
                         pendingBounds,
                         wallBox ) ) {
                    return tile_map_document_status_t::INVALID_METRICS;
                }
                pendingBoxes.pData[iBox++] = wallBox;
            }
        }
    }

    if ( iBox != nBoxCount ) {
        return tile_map_document_status_t::INVALID_STATE;
    }

    // All validation and allocation completed. Publish in one infallible swap.
    Vector_Shutdown( &pGeometry->boxes );
    Vector_Move( &pGeometry->boxes, &pendingBoxes );
    pGeometry->bHasBounds = pendingBounds.bHasBounds;
    pGeometry->boundsMinX = pendingBounds.boundsMinX;
    pGeometry->boundsMinY = pendingBounds.boundsMinY;
    pGeometry->boundsMinZ = pendingBounds.boundsMinZ;
    pGeometry->boundsMaxX = pendingBounds.boundsMaxX;
    pGeometry->boundsMaxY = pendingBounds.boundsMaxY;
    pGeometry->boundsMaxZ = pendingBounds.boundsMaxZ;
    return tile_map_document_status_t::OK;
}

usize CypherTileMapGeometry_CountKind(
    const tile_map_geometry_t *pGeometry,
    tile_map_geometry_box_kind_t kind ) noexcept
{
    if ( !TileMapGeometry_IsInitialized( pGeometry ) ) {
        return 0u;
    }
    usize nCount = 0u;
    for ( usize iBox = 0u;
          iBox < Vector_Count( &pGeometry->boxes );
          ++iBox ) {
        nCount += pGeometry->boxes.pData[iBox].kind == kind;
    }
    return nCount;
}

} // namespace cypher::tools::tile_editor
