//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgClassify.h
//  Purpose: Declares CSG classification: every cell is labelled inside,
//           outside, or shared (same / opposite facing) with respect to the
//           other operand.
//  Details: Shared cells come from CoplanarOverlay. Every other cell lies
//           entirely inside or entirely outside, so one sample decides it:
//           the generalised winding number of the other (closed) operand at
//           the centroid of the cell's largest triangle - the sum of the
//           solid angles its triangles subtend, divided by 4 pi, which is 1
//           inside and 0 outside and handles nested shells and several
//           components without ray-casting special cases. A value near 1/2
//           means the sample is too close to the other surface to trust;
//           the next largest triangle is tried, and a cell with no
//           decisive sample is NUMERIC_FAILURE with its centroid as witness
//           rather than a guess.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_CLASSIFY_H
#define CYPHER_EDITOR_GEOMETRY_CSG_CLASSIFY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgCells.h"
#include "CypherGeometry_CsgIntersections.h"

namespace cypher::editor::geometry
{

// Generalised winding number of the operand's input triangles at p.
CYPHER_NODISCARD common::f64 CsgClassify_WindingNumber( const csg_operand_t *pOperand, math::vec3d_t p ) noexcept;

// Labels the cells of operands whose flag is set (a cell of the other
// operand is left UNKNOWN). triangleLabels come from CsgCoplanar_TryMatch.
CYPHER_NODISCARD geometry_status_t CsgClassify_TryLabel(
    const common::vector_t<csg_refined_triangle_t> &triangles,
    const common::vector_t<csg_label_t> &triangleLabels,
    const csg_intersection_t *pX,
    const csg_operand_t *pA,
    const csg_operand_t *pB,
    bool bClassifyA,
    bool bClassifyB,
    csg_cell_complex_t *pCells,
    csg_diagnostics_t *pDiag ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_CLASSIFY_H
