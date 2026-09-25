//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCorefine.h
//  Purpose: Declares corefinement: every input triangle of both operands is
//           split along the intersection segments and points recorded
//           against it, so the two refined surfaces share every vertex and
//           edge along the curves where they meet.
//  Details: Per triangle, the segments and points form a planar straight-
//           line graph inside it (the triangle's own edges subdivided by the
//           points recorded on them). Its faces are walked in the
//           triangle's projection, loops lying entirely inside a face
//           become holes of that face, and each face is triangulated. A
//           triangle nothing touches passes through unchanged.
//
//           Segments of one triangle can only meet at shared endpoints when
//           the other operand does not intersect itself; a proper crossing
//           is reported as INVALID_TOPOLOGY with the triangle as witness.
//
//           Coplanar overlap pieces: a face is triangulated from its
//           lowest-numbered point, counter-clockwise in a canonical frame,
//           so the same polygon arising in both operands (their overlap)
//           yields the same triangles, which CoplanarOverlay then matches.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_COREFINE_H
#define CYPHER_EDITOR_GEOMETRY_CSG_COREFINE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgIntersections.h"

namespace cypher::editor::geometry
{

// Refines both operands. *pTrianglesOut (initialized; replaced) receives
// the refined triangles of A then of B (iSource tells which input triangle
// each lies in, and thereby which operand). *pcTrianglesAOut receives how
// many belong to A.
CYPHER_NODISCARD geometry_status_t CsgCorefine_TryRefine(
    const csg_operand_t *pA,
    const csg_operand_t *pB,
    const csg_intersection_t *pX,
    common::vector_t<csg_refined_triangle_t> *pTrianglesOut,
    common::usize *pcTrianglesAOut,
    csg_diagnostics_t *pDiag ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_COREFINE_H
