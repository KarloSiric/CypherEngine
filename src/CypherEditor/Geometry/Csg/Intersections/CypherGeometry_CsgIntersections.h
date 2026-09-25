//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgIntersections.h
//  Purpose: Declares CSG intersection construction: for every candidate
//           triangle pair, the points and segments where the two surfaces
//           meet, recorded against the triangles that must be split there.
//  Details: One point per canonical key (see CypherGeometry_CsgTypes.h):
//           the first pair to need a point constructs it, every later pair
//           finds it by key. Input vertices of B that coincide exactly with
//           input vertices of A are aliased to A's, so the operands also
//           share coincident corners.
//
//           Per pair (non-coplanar): the intersection is the part of the
//           line where the two planes meet that lies in both triangles, so
//           it is found from the edges of each triangle crossing (or lying
//           in) the other's plane, decided by exact side tests; the
//           resulting points are ordered along that line and the extreme
//           two span the segment. Coplanar pairs: each triangle is split by
//           the other's edges clipped to it, so matching overlap pieces
//           arise on both sides.
//
//           Points on an operand edge are also recorded against that edge,
//           so both triangles sharing the edge split it identically even if
//           only one of them met the other surface.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_INTERSECTIONS_H
#define CYPHER_EDITOR_GEOMETRY_CSG_INTERSECTIONS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgBroadPhase.h"

#include "CypherCommon_HashMap.h"

namespace cypher::editor::geometry
{

// Constructed points closer than this fraction of the operands' extent
// are one point (see the rounding step in CsgIntersection_TryCompute).
inline constexpr common::f64 kCsgMergeRelative = 1e-10;

struct csg_point_key_hash_t {
    common::hash64_t operator()( const csg_point_key_t &k ) const noexcept;
};
struct csg_point_key_equal_t {
    common::bool_t operator()( const csg_point_key_t &x, const csg_point_key_t &y ) const noexcept;
};

// A point recorded as lying on an operand edge (local, v0 < v1).
struct csg_edge_point_t {
    common::u32 iOperand{ 0u };
    common::u32 v0{ 0u }, v1{ 0u };
    common::u32 p{ 0u };
};

struct csg_intersection_t {
    common::vector_t<math::vec3d_t> positions{};  // global point table
    common::vector_t<csg_point_key_t> keys{};     // parallel
    common::vector_t<common::u32> alias{};        // global vertex -> the point it resolves to (B corners on A corners)
    common::hash_map_t<csg_point_key_t, common::u32, csg_point_key_hash_t, csg_point_key_equal_t> lookup{};
    common::u32 cVerticesA{ 0u }, cVerticesB{ 0u };
    common::u32 cTrianglesA{ 0u }, cTrianglesB{ 0u };
    common::vector_t<csg_constraint_t> constraints{};
    common::vector_t<csg_triangle_point_t> trianglePoints{};
    common::vector_t<csg_edge_point_t> edgePoints{};
};

CYPHER_NODISCARD geometry_status_t CsgIntersection_Init( csg_intersection_t *pOut, const common::allocator_t *pAllocator ) noexcept;
void CsgIntersection_Shutdown( csg_intersection_t *pOut ) noexcept;

// Global point index of an operand's local vertex (after aliasing).
CYPHER_NODISCARD common::u32 CsgIntersection_VertexPoint( const csg_intersection_t *pX, common::u32 iOperand, common::u32 iLocal ) noexcept;

// Computes every pair's intersection into *pOut (initialized; replaced).
// LIMIT_EXCEEDED past kCsgPointsMax points. *pDiag (optional) receives the
// point and segment counts, or the failing pair's triangle as witness.
CYPHER_NODISCARD geometry_status_t CsgIntersection_TryCompute(
    const csg_operand_t *pA,
    const csg_operand_t *pB,
    const common::vector_t<csg_pair_t> &pairs,
    csg_intersection_t *pOut,
    csg_diagnostics_t *pDiag ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_INTERSECTIONS_H
