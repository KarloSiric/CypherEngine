//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_TriangleIntersection.h
//  Purpose: Declares exact segment/triangle and triangle/triangle
//           intersection *tests* built only from exact Orient2D/Orient3D.
//  Details: These are predicates, not constructions: they decide whether
//           geometry meets without computing where. Because every decision
//           is an exact sign, two queries about the same input can never
//           disagree, which is what self-intersection validation and mesh
//           CSG broad phase need.
//
//           Non-coplanar triangles intersect iff some edge of one meets the
//           other triangle (an intersection of two non-coplanar triangles
//           is a segment whose endpoints lie on edges). Coplanar pairs are
//           resolved in 2D by dropping the dominant axis, which is an exact
//           projection.
//
//           Shared-vertex handling: validation must not report the contact
//           that mesh adjacency already implies. The "excluding shared"
//           variant ignores contact that happens only at vertices the two
//           triangles have in common (identified by index, not position).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_KERNEL_TRIANGLE_INTERSECTION_H
#define CYPHER_EDITOR_GEOMETRY_KERNEL_TRIANGLE_INTERSECTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// Does closed segment [p, q] meet closed triangle (a, b, c)? Exact.
// Degenerate triangles (collinear corners) never report an intersection.
CYPHER_NODISCARD bool Kernel_SegmentIntersectsTriangle(
    math::vec3d_t p,
    math::vec3d_t q,
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c ) noexcept;

// Do closed triangles T0 and T1 meet at all (including touching)? Exact.
CYPHER_NODISCARD bool Kernel_TrianglesIntersect(
    const math::vec3d_t t0[3],
    const math::vec3d_t t1[3] ) noexcept;

// Like TrianglesIntersect, but contact consisting only of shared corners is
// not an intersection. i0/i1 are vertex identities (e.g. mesh vertex
// slots); corners with equal identity are "shared". Triangles sharing an
// edge (two corners) intersect only if they overlap beyond that edge,
// i.e. they are coplanar and fold onto each other.
CYPHER_NODISCARD bool Kernel_TrianglesIntersectExcludingShared(
    const math::vec3d_t t0[3],
    const common::u32 i0[3],
    const math::vec3d_t t1[3],
    const common::u32 i1[3] ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_KERNEL_TRIANGLE_INTERSECTION_H
