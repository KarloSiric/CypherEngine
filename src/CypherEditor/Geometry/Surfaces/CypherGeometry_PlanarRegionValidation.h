//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PlanarRegionValidation.h
//  Purpose: Declares structural and geometric validation for PlanarRegion.
//  Details: A valid region satisfies, in frame 2D space:
//             - finite coordinates within the policy magnitude limit;
//             - no repeated consecutive point (including last -> first);
//             - |area| of every contour above policy.fMinimumFaceArea;
//             - every contour simple (edges meet only at shared vertices);
//             - outer contours CCW, holes CW;
//             - holes inside their outer;
//             - holes of one polygon with disjoint interiors, not nested;
//             - polygons with disjoint interiors (a polygon may sit inside
//               another polygon's hole).
//
//           Distinct contours may touch at isolated points (the region is
//           "weakly simple"), because Boolean results routinely do: the
//           XOR of two overlapping squares is two L shapes meeting at two
//           corners. They may not share an edge segment, cross properly,
//           or cross *through* a contact point; the last is decided by an
//           exact local-wedge test at every contact.
//
//           Every sidedness decision uses exact Orient2D, so validation
//           never disagrees with the triangulator or overlay about whether
//           two edges touch.
//
//           Validation reports and never repairs (ARCHITECTURE.md). The
//           result names the first fault found with the indices needed to
//           highlight it; the scan order is deterministic (polygons, then
//           contours, then edges, ascending).
//
//           Cost: O(E^2) edge-pair tests per contour pair, pruned by
//           contour bounding boxes. A sweep-line pass is the planned
//           replacement once regions of many thousands of edges appear.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_REGION_VALIDATION_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_REGION_VALIDATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PlanarRegion.h"
#include "CypherGeometry_Policy.h"

namespace cypher::editor::geometry
{

enum class planar_region_fault_t : common::u8 {
    NONE               = 0u,
    NON_FINITE         = 1u,
    COORDINATE_LIMIT   = 2u,
    DUPLICATE_POINT    = 3u,  // iContour, iEdge = index of the repeated point
    ZERO_AREA          = 4u,  // iContour
    SELF_INTERSECTION  = 5u,  // iContour, iEdge, iEdgeOther
    OUTER_NOT_CCW      = 6u,  // iPolygon, iContour
    HOLE_NOT_CW        = 7u,  // iPolygon, iContour
    CONTOURS_INTERSECT = 8u,  // iPolygon, iContour, iContourOther, edges
    HOLE_OUTSIDE_OUTER = 9u,  // iPolygon, iContour (the hole)
    HOLES_NESTED       = 10u, // iPolygon, iContour, iContourOther
    POLYGONS_INTERSECT = 11u, // iPolygon, iPolygonOther, contours, edges
    POLYGONS_OVERLAP   = 12u  // iPolygon, iPolygonOther
};

struct planar_region_validation_t {
    geometry_status_t status{ geometry_status_t::OK };
    planar_region_fault_t fault{ planar_region_fault_t::NONE };
    common::u32 iPolygon{ CY_INVALID_INDEX };
    common::u32 iPolygonOther{ CY_INVALID_INDEX };
    common::u32 iContour{ CY_INVALID_INDEX };
    common::u32 iContourOther{ CY_INVALID_INDEX };
    common::u32 iEdge{ CY_INVALID_INDEX };       // edge i runs point i -> i+1
    common::u32 iEdgeOther{ CY_INVALID_INDEX };
};

// Status per fault: NON_FINITE -> NUMERIC_FAILURE, COORDINATE_LIMIT ->
// LIMIT_EXCEEDED, DUPLICATE_POINT / ZERO_AREA -> DEGENERATE, any
// intersection -> SELF_INTERSECTING, everything else -> INVALID_TOPOLOGY.
// NOT_INITIALIZED for an uninitialized region; an empty region is valid.
CYPHER_NODISCARD planar_region_validation_t PlanarRegion_Validate(
    const planar_region_t *pRegion,
    const geometry_policy_t &policy ) noexcept;

// Stable, non-localized name of a fault for logs and test output.
CYPHER_NODISCARD const char *PlanarRegionFault_Name(
    planar_region_fault_t fault ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_REGION_VALIDATION_H
