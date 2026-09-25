//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCoplanar.h
//  Purpose: Declares coplanar overlay resolution: refined triangles of A
//           and B that are the same piece of surface (the overlap of two
//           coplanar faces) are paired and marked as facing the same way or
//           opposite ways.
//  Details: Intersection construction splits each of two coplanar
//           triangles by the other's edges and Corefinement triangulates
//           each resulting piece canonically, so an overlap piece comes out
//           as identical triangles (same three points) on both sides.
//           Matching is then exact: same point set. Same cyclic order means
//           the two surfaces face the same way there.
//
//           This is what makes touching boxes union into one solid (the
//           touching faces face opposite ways and both go) and flush faces
//           keep a single copy (same way, one kept).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_COPLANAR_H
#define CYPHER_EDITOR_GEOMETRY_CSG_COPLANAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgTypes.h"

namespace cypher::editor::geometry
{

// *pLabels (initialized; resized to the triangle count) gets SHARED_SAME /
// SHARED_OPPOSITE for matched triangles of both operands and UNKNOWN for
// the rest. *pcSharedOut (optional) receives the number of matched pairs.
CYPHER_NODISCARD geometry_status_t CsgCoplanar_TryMatch(
    const common::vector_t<csg_refined_triangle_t> &triangles,
    common::usize cTrianglesA,
    common::vector_t<csg_label_t> *pLabels,
    common::usize *pcSharedOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_COPLANAR_H
