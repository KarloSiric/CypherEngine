//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_HeightField.h
//  Purpose: Declares the HeightField source representation: a regular
//           grid of height samples over the XY plane (Z up), a per-cell
//           hole mask, and tiles that carry identity and dirty state.
//  Details: Samples sit on cell corners, so a field of cCellsX x cCellsY
//           cells has (cCellsX + 1) x (cCellsY + 1) samples, and samples on
//           tile borders are shared by the neighbouring tiles. Sharing is
//           what makes tile seams stitch exactly: both tiles read the same
//           stored height.
//
//           Why tiles: an edit touches a bounded region, and cooking or
//           re-tessellating the whole field for every brush stroke does not
//           scale. Each tile has a source ID (stable mapping from cooked
//           triangles back to authored data) and a revision that bumps on
//           every edit touching it. Consumers clear a tile's dirty flag
//           only for the revision they actually processed, so an edit that
//           lands during a cook is never lost.
//
//           Tile size is a power of two that divides the field in both
//           directions. That keeps every LOD step aligned on tile borders,
//           which PatchTessellation-style stitching relies on.
//
//           Triangulation rule: each cell is split along the diagonal whose
//           end heights differ least (the shorter 3D diagonal on square
//           cells), ties going to (x, y)-(x+1, y+1). HeightAt, Raycast, and
//           LOD-0 tessellation all use this one rule, so picking agrees
//           exactly with the rendered and collision surface.
//
//           Edits validate their whole result before writing, so every
//           mutation is failure-atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_HEIGHTFIELD_H
#define CYPHER_EDITOR_GEOMETRY_HEIGHTFIELD_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_Span.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kHeightFieldCellsPerAxisMax = 8192u;
inline constexpr common::u32 kHeightFieldSamplesMax = 1u << 24;
inline constexpr common::u32 kHeightFieldTileCellsMax = 256u;
// Same authoring range as brushes and patches.
inline constexpr common::f64 kHeightFieldCoordinateMax = 1048576.0;

struct heightfield_tile_t {
    geometry_source_id_t sourceId{};
    common::u64 revision{ 0u }; // bumps on every edit touching the tile
    bool bDirty{ true };
};

struct heightfield_t {
    common::vector_t<common::f64> heights{};      // row-major samples, z above origin.z
    common::vector_t<common::u8> holes{};         // row-major cells, 1 = hole
    common::vector_t<heightfield_tile_t> tiles{}; // row-major tiles
    math::vec3d_t origin{};                       // world position of sample (0, 0) at height 0
    common::f64 cellSize{ 1.0 };
    common::u32 cCellsX{ 0u };
    common::u32 cCellsY{ 0u };
    common::u32 tileCells{ 0u };
    common::u32 cTilesX{ 0u };
    common::u32 cTilesY{ 0u };
    common::u64 revision{ 0u }; // bumps on every successful edit
    geometry_source_id_t sourceId{};
};

// Inclusive-exclusive rectangle in sample or cell coordinates.
struct heightfield_rect_t {
    common::u32 x0{ 0u };
    common::u32 y0{ 0u };
    common::u32 cx{ 0u };
    common::u32 cy{ 0u };
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Creates a flat field (all heights 0, no holes, every tile dirty). Tile IDs
// are allocated row-major from *pIdAllocator; failure-atomic including the
// allocator. cCellsX/cCellsY must be multiples of tileCells, and tileCells a
// power of two in [1, kHeightFieldTileCellsMax].
CYPHER_NODISCARD geometry_status_t HeightField_TryInit(
    heightfield_t *pField,
    const common::allocator_t *pAllocator,
    math::vec3d_t origin,
    common::f64 cellSize,
    common::u32 cCellsX,
    common::u32 cCellsY,
    common::u32 tileCells,
    geometry_source_id_t fieldId,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept;

void HeightField_Shutdown( heightfield_t *pField ) noexcept;

CYPHER_NODISCARD bool HeightField_IsInitialized( const heightfield_t *pField ) noexcept;

// Performs the constant-time storage/layout gate used before any public
// operation indexes the backing arrays. This is deliberately narrower than
// HeightField_Validate: it proves that dimensions, vector storage, coordinate
// metadata, and cached tile counts are safe to consume, but does not scan every
// sample, hole, or source ID. Returns NOT_INITIALIZED for a canonical empty
// object and CORRUPT_STATE for an initialized object whose layout is unusable.
CYPHER_NODISCARD geometry_status_t HeightField_ValidateStructure(
    const heightfield_t *pField ) noexcept;

CYPHER_NODISCARD common::u32 HeightField_SamplesX( const heightfield_t *pField ) noexcept;
CYPHER_NODISCARD common::u32 HeightField_SamplesY( const heightfield_t *pField ) noexcept;

// ---------------------------------------------------------------------------
// Access
// ---------------------------------------------------------------------------

// Stored height (relative to origin.z) of sample (ix, iy); 0 if out of range.
CYPHER_NODISCARD common::f64 HeightField_Height(
    const heightfield_t *pField, common::u32 ix, common::u32 iy ) noexcept;

// World position of sample (ix, iy).
CYPHER_NODISCARD math::vec3d_t HeightField_SamplePosition(
    const heightfield_t *pField, common::u32 ix, common::u32 iy ) noexcept;

// Unit normal at a sample from central differences (one-sided on the field
// border). Uses neighbours across tile borders, so shared border samples get
// the same normal from either tile.
CYPHER_NODISCARD math::vec3d_t HeightField_SampleNormal(
    const heightfield_t *pField, common::u32 ix, common::u32 iy ) noexcept;

CYPHER_NODISCARD bool HeightField_IsHole(
    const heightfield_t *pField, common::u32 cx, common::u32 cy ) noexcept;

// Tile containing cell (cx, cy), or CY_INVALID_INDEX.
CYPHER_NODISCARD common::u32 HeightField_TileOfCell(
    const heightfield_t *pField, common::u32 cx, common::u32 cy ) noexcept;

// True when the cell is split along (x, y)-(x+1, y+1); false for the other
// diagonal. The single rule shared by queries and tessellation.
CYPHER_NODISCARD bool HeightField_CellUsesMainDiagonal(
    const heightfield_t *pField, common::u32 cx, common::u32 cy ) noexcept;

// World-space surface height at world (x, y), interpolated on the cell's
// triangles. False outside the field or inside a hole.
CYPHER_NODISCARD bool HeightField_TryHeightAt(
    const heightfield_t *pField,
    common::f64 x,
    common::f64 y,
    common::f64 *pWorldZ ) noexcept;

// Copies a sample rectangle into `out` (row-major, rect.cx * rect.cy). Used
// for undo snapshots of a bounded edit.
CYPHER_NODISCARD geometry_status_t HeightField_TryReadHeights(
    const heightfield_t *pField,
    heightfield_rect_t rect,
    common::span_t<common::f64> out ) noexcept;

// ---------------------------------------------------------------------------
// Editing (each marks affected tiles dirty and bumps their revision)
// ---------------------------------------------------------------------------

// Writes a sample rectangle (row-major values). Every value must be finite
// and keep the sample inside kHeightFieldCoordinateMax.
CYPHER_NODISCARD geometry_status_t HeightField_TryWriteHeights(
    heightfield_t *pField,
    heightfield_rect_t rect,
    common::span_t<const common::f64> values ) noexcept;

CYPHER_NODISCARD geometry_status_t HeightField_TrySetHole(
    heightfield_t *pField,
    common::u32 cx,
    common::u32 cy,
    bool bHole ) noexcept;

enum class heightfield_brush_op_t : common::u8 {
    RAISE   = 0u, // add amount * falloff
    FLATTEN = 1u, // move towards targetHeight by strength * falloff
    SMOOTH  = 2u  // move towards the 4-neighbour mean by strength * falloff
};

struct heightfield_brush_t {
    heightfield_brush_op_t op{ heightfield_brush_op_t::RAISE };
    common::f64 centerX{ 0.0 };   // world
    common::f64 centerY{ 0.0 };   // world
    common::f64 radius{ 1.0 };    // world, > 0
    common::f64 amount{ 1.0 };    // RAISE: world height at the centre
    common::f64 strength{ 1.0 };  // FLATTEN/SMOOTH: [0, 1]
    common::f64 targetHeight{ 0.0 }; // FLATTEN: height relative to origin.z
};

// Applies a radial brush with smoothstep falloff (1 at the centre, 0 at the
// radius). SMOOTH reads a snapshot of the pre-edit heights (Jacobi), so the
// result does not depend on iteration order. *pTouchedOut (optional)
// receives the sample rectangle that may have changed, for undo capture.
CYPHER_NODISCARD geometry_status_t HeightField_TryApplyBrush(
    heightfield_t *pField,
    const heightfield_brush_t &brush,
    heightfield_rect_t *pTouchedOut ) noexcept;

// ---------------------------------------------------------------------------
// Dirty tracking
// ---------------------------------------------------------------------------

// Appends the indices of dirty tiles in ascending order.
CYPHER_NODISCARD geometry_status_t HeightField_TryCollectDirtyTiles(
    const heightfield_t *pField,
    common::vector_t<common::u32> *pOut ) noexcept;

// Clears the dirty flag only if the tile is still at `processedRevision`.
// Returns false when the tile changed since (it stays dirty).
bool HeightField_ClearDirty(
    heightfield_t *pField,
    common::u32 iTile,
    common::u64 processedRevision ) noexcept;

// ---------------------------------------------------------------------------
// Queries and validation
// ---------------------------------------------------------------------------

struct heightfield_ray_hit_t {
    common::f64 t{ 0.0 };
    math::vec3d_t position{};
    math::vec3d_t normal{}; // geometric normal of the hit triangle, facing +Z side
    common::u32 cx{ 0u };
    common::u32 cy{ 0u };
    common::u32 iTile{ CY_INVALID_INDEX };
};

// First intersection of origin + t*direction, t in [0, maxT], with the
// surface (holes excluded). Walks cells with a 2D DDA so cost is
// proportional to the cells the ray crosses, not the field size. The output is
// reset before validation and remains neutral on every miss or failure.
CYPHER_NODISCARD bool HeightField_TryRaycast(
    const heightfield_t *pField,
    math::vec3d_t origin,
    math::vec3d_t direction,
    common::f64 maxT,
    heightfield_ray_hit_t *pHitOut ) noexcept;

enum class heightfield_fault_t : common::u8 {
    NONE = 0u,
    NOT_INITIALIZED,
    INVALID_DIMENSIONS,
    INVALID_SOURCE_ID,
    DUPLICATE_SOURCE_ID,
    NON_FINITE,
    COORDINATE_RANGE,
    INVALID_HOLE_VALUE,
    VALIDATION_INCOMPLETE
};

struct heightfield_validation_result_t {
    heightfield_fault_t fault{ heightfield_fault_t::NONE };
    common::u32 index{ CY_INVALID_INDEX }; // offending sample / cell / tile
};

CYPHER_NODISCARD heightfield_validation_result_t HeightField_Validate(
    const heightfield_t *pField,
    const common::allocator_t *pScratchAllocator ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_HEIGHTFIELD_H
