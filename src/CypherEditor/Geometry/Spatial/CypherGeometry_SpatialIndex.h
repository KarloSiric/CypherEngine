//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SpatialIndex.h
//  Purpose: Declares a flat AABB broad-phase spatial index for brushes.
//  Details: Stores one axis-aligned bounding box per brush, keyed by
//           source ID. Supports insertion, removal, refit (update bounds),
//           overlap queries (which brushes does a given AABB intersect),
//           and point containment queries. The flat linear scan is
//           appropriate for editor brush counts (hundreds to low thousands).
//           A hierarchical acceleration structure would replace this only
//           when profiling shows the linear scan is a bottleneck.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SPATIAL_INDEX_H
#define CYPHER_EDITOR_GEOMETRY_SPATIAL_INDEX_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherCommon_Vector.h"
#include "CypherMath_Bounds.h"

namespace cypher::editor::geometry
{

// One entry in the spatial index: an AABB associated with a brush source ID.
struct spatial_index_entry_t {
    geometry_source_id_t brushId{};
    math::aabbd_t bounds{ math::CY_AABBD_EMPTY };
};

// Flat linear spatial index over brush AABBs. All entries share one
// contiguous array — insertion, removal, and queries are O(N) in the
// number of indexed brushes.
struct geometry_spatial_index_t {
    common::vector_t<spatial_index_entry_t> entries{};
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_status_t GeometrySpatialIndex_Init(
    geometry_spatial_index_t *pIndex,
    const common::allocator_t *pAllocator ) noexcept;

void GeometrySpatialIndex_Shutdown(
    geometry_spatial_index_t *pIndex ) noexcept;

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

// Inserts a brush with a finite, non-empty bounding box. Returns
// IDENTITY_CONFLICT if the brush is already indexed.
CYPHER_NODISCARD geometry_status_t GeometrySpatialIndex_TryInsert(
    geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId,
    math::aabbd_t bounds ) noexcept;

// Removes a brush from the index. Returns INVALID_ARGUMENT if the brush
// is not indexed.
CYPHER_NODISCARD geometry_status_t GeometrySpatialIndex_TryRemove(
    geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId ) noexcept;

// Updates the finite, non-empty bounding box for an already-indexed brush.
// Returns INVALID_ARGUMENT if the brush is not indexed or bounds are invalid.
CYPHER_NODISCARD geometry_status_t GeometrySpatialIndex_TryRefit(
    geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId,
    math::aabbd_t newBounds ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize GeometrySpatialIndex_Count(
    const geometry_spatial_index_t *pIndex ) noexcept;

// Finds all brushes whose AABB overlaps the query AABB. Writes their
// source IDs to pResultsOut (up to nCapacity). Reports actual hit count
// in *pCountOut. Returns INSUFFICIENT_CAPACITY if more hits exist than
// the output can hold (results are still written up to capacity).
CYPHER_NODISCARD geometry_status_t GeometrySpatialIndex_QueryOverlap(
    const geometry_spatial_index_t *pIndex,
    math::aabbd_t queryBounds,
    CY_OUT_WRITES( nCapacity ) geometry_source_id_t *pResultsOut,
    common::usize nCapacity,
    common::usize *pCountOut ) noexcept;

// Finds all brushes whose AABB contains the query point. Same output
// semantics as QueryOverlap.
CYPHER_NODISCARD geometry_status_t GeometrySpatialIndex_QueryPoint(
    const geometry_spatial_index_t *pIndex,
    math::vec3d_t queryPoint,
    CY_OUT_WRITES( nCapacity ) geometry_source_id_t *pResultsOut,
    common::usize nCapacity,
    common::usize *pCountOut ) noexcept;

// Retrieves the stored AABB for one brush. Returns INVALID_ARGUMENT if
// the brush is not indexed.
CYPHER_NODISCARD geometry_status_t GeometrySpatialIndex_TryGetBounds(
    const geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId,
    math::aabbd_t *pBoundsOut ) noexcept;

static_assert( std::is_trivially_copyable_v<spatial_index_entry_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SPATIAL_INDEX_H
