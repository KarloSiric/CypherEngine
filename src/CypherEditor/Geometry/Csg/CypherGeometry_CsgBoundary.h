//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgBoundary.h
//  Purpose: Declares CSG boundary extraction: the refined triangles the
//           region decision keeps, oriented outward, minus any closed
//           component that encloses no volume.
//  Details: Regularisation: a Boolean can leave a closed sheet with zero
//           volume (two coincident faces kept back to back, e.g. where
//           solids only touch in an intersection). That is not a solid and
//           is dropped rather than published. Open results (CLIP) have no
//           volume to test and keep every component.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_BOUNDARY_H
#define CYPHER_EDITOR_GEOMETRY_CSG_BOUNDARY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgCells.h"
#include "CypherGeometry_CsgExpression.h"

namespace cypher::editor::geometry
{

struct csg_boundary_triangle_t {
    common::u32 v[3]{ 0u, 0u, 0u }; // global points, outward winding
    common::u32 iSource{ 0u };      // global input triangle
    bool bFlipped{ false };         // kept inside out (a cut face)
};

// *pOut (initialized; replaced). *pcDroppedOut (optional) counts the
// triangles removed as zero-volume components.
CYPHER_NODISCARD geometry_status_t CsgBoundary_TryExtract(
    const common::vector_t<csg_refined_triangle_t> &triangles,
    const csg_cell_complex_t *pCells,
    const common::vector_t<math::vec3d_t> &points,
    csg_operator_t op,
    common::vector_t<csg_boundary_triangle_t> *pOut,
    common::usize *pcDroppedOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_BOUNDARY_H
