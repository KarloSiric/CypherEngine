//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapGeometry.h
//  Purpose: Declares renderer-neutral blockout geometry generation.
//  Details: Active map cells become floor and exposed-boundary wall boxes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_TILEMAPGEOMETRY_H
#define CYPHER_TOOLS_TILEEDITOR_TILEMAPGEOMETRY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherTileMapDocument.h"

namespace cypher::tools::tile_editor
{

inline constexpr f32 TILE_MAP_DEFAULT_FLOOR_THICKNESS =
    TILE_MAP_MIN_LEVEL_HEIGHT;
inline constexpr f32 TILE_MAP_DEFAULT_WALL_THICKNESS =
    TILE_MAP_MIN_CELL_SIZE;
inline constexpr f32 TILE_MAP_DOOR_WIDTH_RATIO = 0.70f;
inline constexpr f32 TILE_MAP_DOOR_HEIGHT_RATIO = 0.80f;

enum class tile_map_geometry_box_kind_t : u8 {
    FLOOR = 0u,
    WALL,
    DOOR,
    STAIR
};

enum class tile_map_geometry_side_t : u8 {
    NONE = 0u,
    NORTH,
    EAST,
    SOUTH,
    WEST
};

// Cypher uses Z-up coordinates. Grid X maps to world X, grid Y maps to world
// Y, and world Z is vertical.
// Half extents are always positive. A renderer can draw every record using one
// shared unit cube and a scale/translation transform.
struct tile_map_geometry_box_t {
    f32 centerX{ 0.0f };
    f32 centerY{ 0.0f };
    f32 centerZ{ 0.0f };

    f32 halfExtentX{ 0.0f };
    f32 halfExtentY{ 0.0f };
    f32 halfExtentZ{ 0.0f };

    tile_map_geometry_box_kind_t kind{
        tile_map_geometry_box_kind_t::FLOOR
    };
    tile_map_geometry_side_t side{ tile_map_geometry_side_t::NONE };
    u16 nMaterialSlot{ 0u };
    tile_map_grid_coord_t sourceCell{};
};

struct tile_map_geometry_desc_t {
    f32 nFloorThickness{ TILE_MAP_DEFAULT_FLOOR_THICKNESS };
    f32 nWallThickness{ TILE_MAP_DEFAULT_WALL_THICKNESS };
};

struct tile_map_geometry_t {
    tile_map_geometry_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( tile_map_geometry_t );

    const allocator_t *pAllocator{ nullptr };
    vector_t<tile_map_geometry_box_t> boxes{};

    bool_t bHasBounds{ CY_FALSE };
    f32 boundsMinX{ 0.0f };
    f32 boundsMinY{ 0.0f };
    f32 boundsMinZ{ 0.0f };
    f32 boundsMaxX{ 0.0f };
    f32 boundsMaxY{ 0.0f };
    f32 boundsMaxZ{ 0.0f };
};

CYPHER_NODISCARD tile_map_document_status_t CypherTileMapGeometry_Init(
    tile_map_geometry_t *pGeometry,
    const allocator_t *pAllocator ) noexcept;

void CypherTileMapGeometry_Shutdown(
    tile_map_geometry_t *pGeometry ) noexcept;

// Build is transactional: allocation or validation failure leaves the previous
// geometry and its bounds untouched.
CYPHER_NODISCARD tile_map_document_status_t CypherTileMapGeometry_Build(
    const tile_map_document_t *pDocument,
    const tile_map_geometry_desc_t &desc,
    tile_map_geometry_t *pGeometry ) noexcept;

CYPHER_NODISCARD usize CypherTileMapGeometry_CountKind(
    const tile_map_geometry_t *pGeometry,
    tile_map_geometry_box_kind_t kind ) noexcept;

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_TILEMAPGEOMETRY_H
