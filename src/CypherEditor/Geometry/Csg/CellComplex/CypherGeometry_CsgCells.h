//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCells.h
//  Purpose: Declares the CSG cell complex: the refined surfaces cut into
//           patches (cells) along the curves where the operands meet, each
//           cell one operand's connected piece of surface that is entirely
//           inside, outside, or on the other operand.
//  Details: A refined edge used by triangles of both operands lies on the
//           intersection (or on a shared coplanar piece); every other edge
//           joins two triangles of one operand whose inside/outside status
//           cannot differ. Cells are the connected components across those
//           other edges, so classifying one triangle classifies its cell.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_CELLS_H
#define CYPHER_EDITOR_GEOMETRY_CSG_CELLS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgTypes.h"

namespace cypher::editor::geometry
{

struct csg_cell_complex_t {
    common::vector_t<common::u32> triangleCell{};  // per refined triangle
    common::vector_t<common::u32> cellStart{};     // cCells + 1, into cellTriangles
    common::vector_t<common::u32> cellTriangles{}; // refined triangles grouped by cell
    common::vector_t<common::u32> cellOperand{};   // per cell
    common::vector_t<csg_label_t> cellLabel{};     // per cell (filled by Classification)
};

CYPHER_NODISCARD geometry_status_t CsgCells_Init( csg_cell_complex_t *pCells, const common::allocator_t *pAllocator ) noexcept;
void CsgCells_Shutdown( csg_cell_complex_t *pCells ) noexcept;
CYPHER_NODISCARD common::usize CsgCells_Count( const csg_cell_complex_t *pCells ) noexcept;

// Builds cells over the refined triangles (A's first, cTrianglesA of them).
// NON_MANIFOLD when one operand puts three triangles on an edge.
CYPHER_NODISCARD geometry_status_t CsgCells_TryBuild(
    const common::vector_t<csg_refined_triangle_t> &triangles,
    common::usize cTrianglesA,
    csg_cell_complex_t *pCells ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_CELLS_H
