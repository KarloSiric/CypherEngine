//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_ConvexHull.h
//  Purpose: Declares 3D convex hull construction from a point cloud.
//  Details: Implements the incremental convex hull algorithm in f64.
//           The result is either a standalone hull (triangulated face
//           list with outward normals) or a brush_solid_t suitable for
//           direct use in the editor.
//
//           Points fewer than 4 or all-coplanar inputs are rejected
//           with DEGENERATE. Duplicate and interior points are handled
//           gracefully — they are simply ignored during construction.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_KERNEL_CONVEX_HULL_H
#define CYPHER_EDITOR_GEOMETRY_KERNEL_CONVEX_HULL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherCommon_Vector.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// A single triangular face of the convex hull. Indices reference the
// input point array. The normal points outward (away from the hull
// interior).
struct convex_hull_face_t {
    common::u32 indices[3]{ 0u, 0u, 0u };
    math::vec3d_t normal{};
};

// The raw convex hull result: a triangulated surface with outward
// normals. Does not own the input points — indices reference the
// original array.
struct convex_hull_t {
    common::vector_t<convex_hull_face_t> faces{};
};

// Lifecycle for the hull result container.
CYPHER_NODISCARD geometry_status_t ConvexHull_Init(
    convex_hull_t *pHull,
    const common::allocator_t *pAllocator ) noexcept;

void ConvexHull_Shutdown(
    convex_hull_t *pHull ) noexcept;

// Builds the convex hull of the input point set. The result is a
// triangle mesh with outward normals. Requires at least 4 non-coplanar
// points. Face indices use the same outward winding as the stored normal.
// Duplicate and interior points are ignored. Every complexity and numerical
// bound comes from policy; there are no hidden face or horizon capacities.
//
// Failure atomicity: validation, numerical, limit, and allocation failures
// preserve the hull's allocation, capacity, count, and face contents exactly.
CYPHER_NODISCARD geometry_status_t ConvexHull_TryBuild(
    convex_hull_t *pHull,
    const math::vec3d_t *pPoints,
    common::usize cPoints,
    const geometry_policy_t &policy ) noexcept;

// Number of triangular faces in the hull.
CYPHER_NODISCARD common::usize ConvexHull_FaceCount(
    const convex_hull_t *pHull ) noexcept;

// Convenience: builds a brush_solid_t directly from a point cloud.
// Merges coplanar hull faces into single brush sides, so the resulting
// brush has the minimum number of planes. The destination must be canonical
// empty. On failure it remains canonical empty and pIdAllocator is unchanged;
// the completed brush and advanced identity state are published together.
CYPHER_NODISCARD geometry_status_t ConvexHull_TryBuildBrush(
    brush_solid_t *pSolid,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    const math::vec3d_t *pPoints,
    common::usize cPoints ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_KERNEL_CONVEX_HULL_H
