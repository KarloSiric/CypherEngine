//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Overlay.h
//  Purpose: Declares regularized 2D Boolean overlay of two PlanarRegions:
//           union, intersection, difference, and symmetric difference.
//  Details: Pipeline (all in the shared frame's 2D space):
//
//             1. collect oriented boundary edges of A and B (interior on
//                the left: outers CCW, holes CW);
//             2. split every edge at every contact with every other edge,
//                classified exactly (Planar_ClassifySegments);
//             3. merge vertices — input points exactly, constructed
//                crossing points within policy.fAbsoluteDistanceTolerance;
//             4. build a half-edge arrangement and trace its faces;
//             5. classify each face as in/out of A and of B. A face that
//                borders an A edge takes its A-state from that edge's
//                direction (left of a forward A edge = inside A), which is
//                exact. Faces with no A edge inherit across non-A edges,
//                and any still-unknown face falls back to exact point
//                containment of one of its vertices;
//             6. select faces by operation and extract the boundary between
//                selected and unselected faces;
//             7. drop collinear pass-through vertices and assign holes to
//                outers by exact containment.
//
//           The result is regularized: zero-area slivers and dangling
//           edges cannot appear because only boundaries between faces of
//           positive area are emitted.
//
//           Robustness limits (documented, not hidden): sidedness and
//           contact decisions are exact, but crossing points are rounded
//           to binary64. After merging, two nearly coincident crossings
//           can in principle introduce a new contact that was not
//           re-examined (the classic snap-rounding gap). The output is
//           therefore validated with PlanarRegion_Validate before it is
//           published; a failure returns NUMERIC_FAILURE with no output.
//
//           Cost: O(E^2) pairwise contact search with bounding-box
//           pruning, O(V log V) merging, O(E log E) arrangement.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_OVERLAY_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_OVERLAY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PlanarRegion.h"
#include "CypherGeometry_Policy.h"
#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

enum class planar_boolean_op_t : common::u8 {
    UNION                = 0u, // A ∪ B
    INTERSECTION         = 1u, // A ∩ B
    DIFFERENCE           = 2u, // A \ B
    SYMMETRIC_DIFFERENCE = 3u  // (A \ B) ∪ (B \ A)
};

// Bound on total input edges (A + B). The contact search is quadratic.
inline constexpr common::usize kPlanarOverlayEdgesMax = 16384u;

// Statistics for diagnostics and tests.
struct planar_overlay_stats_t {
    common::u32 cInputEdges{ 0u };
    common::u32 cArrangementVertices{ 0u };
    common::u32 cArrangementEdges{ 0u };
    common::u32 cFaces{ 0u };
    common::u32 cSelectedFaces{ 0u };
    common::u32 cOutputPolygons{ 0u };
    common::u32 cOutputHoles{ 0u };
};

// Computes op(A, B) into pOut.
//
// Preconditions: A and B are initialized, valid regions (PlanarRegion_
// Validate) sharing a bit-identical frame; pOut is zero-initialized (it is
// initialized here with A's frame and resultRegionId). New polygon and
// contour source IDs come from pIdAllocator.
//
// Failure-atomic: on any failure pOut stays uninitialized and the ID
// allocator is unchanged. An empty result (e.g. disjoint intersection) is
// a success with zero polygons.
//
// Returns INVALID_ARGUMENT (null/mismatched frames/initialized output/bad
// ID), NOT_INITIALIZED, LIMIT_EXCEEDED, ALLOCATION_FAILED, or
// NUMERIC_FAILURE (see robustness note).
CYPHER_NODISCARD geometry_status_t Planar_TryOverlay(
    const planar_region_t *pA,
    const planar_region_t *pB,
    planar_boolean_op_t op,
    const geometry_policy_t &policy,
    geometry_source_id_t resultRegionId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    planar_region_t *pOut,
    planar_overlay_stats_t *pStatsOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_OVERLAY_H
