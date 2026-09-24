//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushQueries.h
//  Purpose: Declares read-only measurement, containment, and ray queries
//           over plane-defined brushes.
//  Details: Measurements read the reconstructed boundary; containment and
//           ray casting read the plane set directly, because for a convex
//           solid the half-spaces answer both exactly without touching
//           derived topology. All queries are pure, allocate nothing, and
//           report failure through geometry_status_t rather than NaN.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_QUERIES_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_QUERIES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_Kernel_Classification.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 BRUSH_QUERY_SIDE_NONE = common::CY_U32_MAX;

// ---------------------------------------------------------------------------
// Measurements (boundary)
// ---------------------------------------------------------------------------

// Area of one boundary face, from its Newell vector.
CYPHER_NODISCARD geometry_status_t BrushQuery_TryFaceArea(
    const brush_boundary_t *pBoundary,
    common::usize iFace,
    math::f64 *pAreaOut ) noexcept;

// Sum of all face areas. DEGENERATE for an empty boundary.
CYPHER_NODISCARD geometry_status_t BrushQuery_TrySurfaceArea(
    const brush_boundary_t *pBoundary,
    math::f64 *pAreaOut ) noexcept;

// Enclosed volume and volume centroid by the divergence theorem, summing
// signed tetrahedra from the vertex average to every fan triangle. The
// vertex average is a deterministic function of the canonical vertex
// order, which keeps the result permutation-invariant, and sits inside
// the solid, which keeps cancellation low. DEGENERATE when the volume is
// not positive (inward winding or a flat solid).
CYPHER_NODISCARD geometry_status_t BrushQuery_TryVolumeCentroid(
    const brush_boundary_t *pBoundary,
    math::f64 *pVolumeOut,
    math::vec3d_t *pCentroidOut ) noexcept;

// ---------------------------------------------------------------------------
// Containment (planes)
// ---------------------------------------------------------------------------

enum class geometry_containment_t : common::u8 {
    OUTSIDE = 0u,     // strictly outside at least one side beyond tolerance
    ON_BOUNDARY,      // within tolerance of at least one side, outside none
    INSIDE,           // strictly inside every side
    COUNT
};

// Classifies a point against every side with Kernel_ClassifyPoint, so the
// single tolerance is policy.fCoplanarDistanceTolerance. Returns the
// Kernel's failure status (non-finite input, out-of-range coordinates,
// unnormalized plane) without classifying.
CYPHER_NODISCARD geometry_status_t BrushQuery_TryClassifyPoint(
    const brush_solid_t *pBrush,
    const geometry_numerical_policy_t &policy,
    math::vec3d_t point,
    geometry_containment_t *pContainmentOut ) noexcept;

// ---------------------------------------------------------------------------
// Ray casting (planes)
// ---------------------------------------------------------------------------

struct geometry_brush_ray_hit_t {
    common::bool_t bHit{ false };
    // True when the origin is already inside or on the solid; the hit then
    // reports t = 0 and iSide = BRUSH_QUERY_SIDE_NONE.
    common::bool_t bStartsInside{ false };
    // Parameter along the (unnormalized) direction: point = origin + t*dir.
    math::f64 t{ 0.0 };
    math::vec3d_t point{};
    // Side whose plane the ray enters through, and that side's outward normal.
    common::u32 iSide{ BRUSH_QUERY_SIDE_NONE };
    math::vec3d_t normal{};
};

// Clips the ray [origin, origin + maxT*direction] against every half-space.
// Direction must be finite and non-zero; maxT must be finite and
// non-negative (INVALID_ARGUMENT otherwise). A miss is OK with bHit false.
// Ties between sides entered at exactly the same t resolve to the lowest
// side index, so the reported side is deterministic.
CYPHER_NODISCARD geometry_status_t BrushQuery_TryRaycast(
    const brush_solid_t *pBrush,
    const geometry_numerical_policy_t &policy,
    math::vec3d_t origin,
    math::vec3d_t direction,
    math::f64 maxT,
    geometry_brush_ray_hit_t *pHitOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_QUERIES_H
