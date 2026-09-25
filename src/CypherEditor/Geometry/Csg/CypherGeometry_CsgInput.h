//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgInput.h
//  Purpose: Declares CSG input preparation: an authored mesh becomes a
//           triangulated operand with source mapping, validated, optionally
//           quantised, and put in a canonical triangle order.
//  Details: Later stages work on triangles because the exact predicates
//           are defined on triangles, but every triangle remembers its
//           source face and carries that face's corner surface data, so the
//           result can be rebuilt into polygons with the authored materials
//           and UVs.
//
//           Canonical order: triangles sorted by their vertex positions
//           (lexicographically, smallest corner first), then by source
//           face, so two operands that differ only in face or vertex order
//           run through the pipeline identically.
//
//           Closedness: every edge must have exactly two triangles walking
//           it in opposite directions. Union, intersection and difference
//           need both operands closed; CLIP needs only the cutter closed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_INPUT_H
#define CYPHER_EDITOR_GEOMETRY_CSG_INPUT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgTypes.h"

namespace cypher::editor::geometry
{

CYPHER_NODISCARD geometry_status_t CsgOperand_Init( csg_operand_t *pOperand, const common::allocator_t *pAllocator ) noexcept;
void CsgOperand_Shutdown( csg_operand_t *pOperand ) noexcept;

// Triangulates every face in its own plane (ear clipping), copying corner
// data, face attributes and IDs, and records bounds and closedness.
// DEGENERATE for a face that cannot be triangulated, LIMIT_EXCEEDED past
// kCsgTrianglesMax. Replaces the operand's previous contents.
CYPHER_NODISCARD geometry_status_t CsgInput_TryFromMeshSource(
    const mesh_source_t *pSource,
    csg_operand_t *pOperand ) noexcept;

// Snaps every coordinate to the nearest multiple of `step` (> 0). Moving
// points can flatten a triangle; that is DEGENERATE and nothing changes.
CYPHER_NODISCARD geometry_status_t CsgInput_TryQuantize( csg_operand_t *pOperand, common::f64 step ) noexcept;

// Reorders triangles canonically (see file comment).
void CsgInput_Canonicalize( csg_operand_t *pOperand ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_INPUT_H
