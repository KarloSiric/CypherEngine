//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SpatialIndex.h
//  Purpose: Declares a deterministic bounding-volume hierarchy over the
//           brushes of one document snapshot, with overlap and pick queries.
//  Details: The index is derived data. It is built from an immutable
//           snapshot and keeps a reference to that snapshot, so leaf values
//           stay alive and picking can ray-cast the exact brush planes
//           rather than approximate proxies.
//
//           Sync is the incremental path: when a new snapshot contains
//           exactly the same brush IDs, only leaf bounds change and the
//           tree is refit bottom-up in O(n); otherwise it is rebuilt.
//           Either way query results equal a brute-force scan of the
//           snapshot, which is the index's correctness contract.
//
//           Construction is a median split on the longest centroid axis,
//           ordered by (centroid, brush ID), so the tree shape is a pure
//           function of the snapshot and never of allocation or insertion
//           order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SPATIAL_INDEX_H
#define CYPHER_EDITOR_GEOMETRY_SPATIAL_INDEX_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_Snapshot.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 SPATIAL_NODE_NONE = common::CY_U32_MAX;
inline constexpr common::u32 SPATIAL_LEAF_SIZE_MAX = 4u;

// A node is a leaf when cLeaves > 0; its leaves are leafOrder[iFirst,
// iFirst + cLeaves). Interior nodes have two children.
struct geometry_spatial_node_t {
    math::aabbd_t bounds;
    common::u32 iFirst;
    common::u32 cLeaves;
    common::u32 iLeft;
    common::u32 iRight;
};

struct geometry_spatial_index_t {
    geometry_spatial_index_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( geometry_spatial_index_t );
    ~geometry_spatial_index_t() noexcept;

    const common::allocator_t *pAllocator{ nullptr };
    // Referenced snapshot; leaf i is snapshot brush entry i.
    const geometry_document_snapshot_t *pSnapshot{ nullptr };
    common::vector_t<geometry_spatial_node_t> nodes{};
    // Permutation of snapshot entry indices in tree order.
    common::vector_t<common::u32> leafOrder{};
    common::bool_t bInitialized{ false };
};

CYPHER_NODISCARD geometry_status_t SpatialIndex_Init(
    geometry_spatial_index_t *pIndex,
    const common::allocator_t *pAllocator ) noexcept;

// Drops the snapshot reference and all nodes. Safe when uninitialized.
void SpatialIndex_Shutdown( geometry_spatial_index_t *pIndex ) noexcept;

// Builds a fresh tree for the snapshot. Failure-atomic: on any error the
// previous tree and snapshot reference are kept unchanged.
CYPHER_NODISCARD geometry_status_t SpatialIndex_TryRebuild(
    geometry_spatial_index_t *pIndex,
    const geometry_document_snapshot_t *pSnapshot ) noexcept;

// Brings the index to the given snapshot, refitting in place when the
// brush ID set is unchanged and rebuilding otherwise. *pRebuiltOut
// (optional) reports which path ran. Failure-atomic like Rebuild.
CYPHER_NODISCARD geometry_status_t SpatialIndex_TrySync(
    geometry_spatial_index_t *pIndex,
    const geometry_document_snapshot_t *pSnapshot,
    common::bool_t *pRebuiltOut ) noexcept;

// Zero when no snapshot is indexed.
CYPHER_NODISCARD common::u64 SpatialIndex_Revision(
    const geometry_spatial_index_t *pIndex ) noexcept;

// Appends to *pBrushIdsOut (cleared first; must be initialized) the IDs of
// every brush whose bounds overlap the query box, in ascending ID order.
// Touching bounds count as overlapping.
CYPHER_NODISCARD geometry_status_t SpatialIndex_TryQueryAabb(
    const geometry_spatial_index_t *pIndex,
    math::aabbd_t query,
    common::vector_t<geometry_source_id_t> *pBrushIdsOut ) noexcept;

struct geometry_spatial_pick_t {
    common::bool_t bHit{ false };
    geometry_source_id_t brushId{};
    geometry_source_id_t sideId{};
    math::f64 t{ 0.0 };
    math::vec3d_t point{};
    math::vec3d_t normal{};
};

// Nearest brush surface hit by the ray within [0, maxT]. Brushes that
// contain the ray origin are skipped, matching the editor convention that a
// camera inside a brush picks what it sees, not the brush around it. Equal
// t resolves to the lower brush ID.
CYPHER_NODISCARD geometry_status_t SpatialIndex_TryPick(
    const geometry_spatial_index_t *pIndex,
    const geometry_numerical_policy_t &policy,
    math::vec3d_t origin,
    math::vec3d_t direction,
    math::f64 maxT,
    geometry_spatial_pick_t *pPickOut ) noexcept;

// Structural audit: every snapshot entry appears in exactly one leaf, and
// every node's bounds enclose its children or leaves.
CYPHER_NODISCARD common::bool_t SpatialIndex_ValidateDeep(
    const geometry_spatial_index_t *pIndex ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SPATIAL_INDEX_H
