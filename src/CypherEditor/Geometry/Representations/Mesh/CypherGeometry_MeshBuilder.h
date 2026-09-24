//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBuilder.h
//  Purpose: Declares the checked mesh builder for constructing half-edge
//           meshes from validated brush boundaries.
//  Details: The builder takes a reconstructed brush_boundary_t and populates
//           an editable_mesh_t with vertices, half-edges, edges, loops,
//           faces, and a single shell. It validates topological consistency
//           before committing: every edge must have exactly two half-edges,
//           every face loop must be closed, and twin pointers must be
//           reciprocal.
//
//           The builder does not retain any state after completion. It is a
//           one-shot conversion, not an incremental editing interface.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_BUILDER_H
#define CYPHER_EDITOR_GEOMETRY_MESH_BUILDER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_BrushBoundary.h"

namespace cypher::editor::geometry
{

// Builds a half-edge mesh from a reconstructed brush boundary. The output
// mesh must be initialized but empty. On failure the mesh is left empty.
//
// The boundary must have been successfully reconstructed (non-empty
// vertices, edges, and faces). The builder:
//   1. Inserts all boundary vertices into the vertex pool.
//   2. For each boundary face, creates half-edges in CCW order, a loop,
//      and a face record.
//   3. Pairs opposing half-edges as twins and creates edge records.
//   4. Creates a single shell covering all faces.
//   5. Validates reciprocal adjacency before returning.
CYPHER_NODISCARD geometry_status_t MeshBuilder_TryBuildFromBoundary(
    editable_mesh_t *pMeshOut,
    const brush_boundary_t *pBoundary ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_BUILDER_H
