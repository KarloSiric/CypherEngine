//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshLoopTraversal.h
//  Purpose: Declares a dynamically sized traversal for a regular closed
//           edge loop in an editable quad mesh.
//  Details: A regular loop is the conservative topology domain needed by
//           the first loop-slide operation: every loop vertex has valence
//           four, every selected edge is interior, and both incident faces
//           are quads. The returned half-edges are directed head-to-tail,
//           so their orientation also names the two slide rails.
//
//           This query does not truncate. Storage grows to the actual loop
//           size (bounded only by the mesh limits), and failure leaves a
//           previously populated output unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_LOOP_TRAVERSAL_H
#define CYPHER_EDITOR_GEOMETRY_MESH_LOOP_TRAVERSAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

struct mesh_regular_edge_loop_t {
    common::vector_t<geometry_mesh_half_edge_handle_t> directedHalfEdges{};
    const common::allocator_t *pAllocator{ nullptr };
    bool bClosed{ false };
};

CYPHER_NODISCARD geometry_status_t MeshRegularEdgeLoop_Init(
    mesh_regular_edge_loop_t *pLoop,
    const common::allocator_t *pAllocator ) noexcept;

void MeshRegularEdgeLoop_Shutdown(
    mesh_regular_edge_loop_t *pLoop ) noexcept;

CYPHER_NODISCARD bool MeshRegularEdgeLoop_IsInitialized(
    const mesh_regular_edge_loop_t *pLoop ) noexcept;

// Traces the regular closed edge loop containing hDirectedSeed. Success
// returns the seed first and directs every following half-edge from the
// previous edge's destination. Reversing the seed reverses the traversal
// and swaps the two rails.
//
// Valid open loops, boundary edges, poles, non-quad incident faces, repeated
// vertices, and rail vertices that re-enter the selected loop are currently
// UNSUPPORTED. Broken half-edge adjacency is CORRUPT_STATE. Allocation
// failure and every rejection leave *pLoopOut unchanged.
CYPHER_NODISCARD geometry_status_t MeshOps_TryTraceRegularClosedEdgeLoop(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hDirectedSeed,
    mesh_regular_edge_loop_t *pLoopOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_LOOP_TRAVERSAL_H
