//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgStitch.h
//  Purpose: Declares CSG stitching: the points the result boundary uses
//           become its vertex table, joined by identity only.
//  Details: Two result faces share a vertex exactly when they use the same
//           canonical point - never because two distinct points happen to
//           be close. Closeness is still worth knowing (it marks a sliver
//           the next edit may trip over), so near-duplicate distinct points
//           are counted for diagnostics but left alone: welding them would
//           silently change topology the exact stages decided.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_STITCH_H
#define CYPHER_EDITOR_GEOMETRY_CSG_STITCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgBoundary.h"

namespace cypher::editor::geometry
{

// Distinct points closer than this fraction of the result's extent are
// reported as near duplicates.
inline constexpr common::f64 kCsgNearDuplicateRelative = 1e-9;

// *pPointToVertex (size = points, CY_U32_MAX for unused points) and
// *pVertexToPoint (used points in first-use order) are initialized and
// replaced. *pcNearDuplicatesOut (optional) receives the report count.
CYPHER_NODISCARD geometry_status_t CsgStitch_TryCompact(
    const common::vector_t<csg_boundary_triangle_t> &triangles,
    const common::vector_t<math::vec3d_t> &points,
    common::vector_t<common::u32> *pPointToVertex,
    common::vector_t<common::u32> *pVertexToPoint,
    common::usize *pcNearDuplicatesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_STITCH_H
