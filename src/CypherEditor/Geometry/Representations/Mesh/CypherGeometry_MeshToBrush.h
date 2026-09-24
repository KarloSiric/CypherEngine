//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshToBrush.h
//  Purpose: Declares the mesh-to-brush conversion that closes the
//           editing round-trip: brush → boundary → mesh → edit → brush.
//  Details: Each mesh face is converted to a brush side by computing a
//           plane from the face's vertices. The resulting brush_solid_t
//           represents the same convex solid as the input mesh.
//
//           The mesh must be a single closed convex shell. Non-convex
//           meshes are rejected with INVALID_TOPOLOGY.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_TO_BRUSH_H
#define CYPHER_EDITOR_GEOMETRY_MESH_TO_BRUSH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

// Converts a closed convex mesh back to a brush solid. Each distinct face
// plane becomes one brush side; coplanar faces (e.g. a triangulated quad)
// share a single side.
//
// Preconditions are checked, not assumed, because a brush built from a
// non-convex or warped face set silently describes a different shape:
//   NON_PLANAR  — a face corner lies off its face plane by more than
//                 policy.numerical.fPlanarityTolerance;
//   UNSUPPORTED — the mesh is concave (a vertex lies in front of some face
//                 plane by more than fCoplanarDistanceTolerance).
//
// The input must be one structurally valid, closed, outward-wound shell and
// policy must be valid. The output brush must be canonical zero-initialized;
// an already initialized output is rejected with ALREADY_INITIALIZED.
//
// The conversion is failure-atomic. It builds a private brush and advances a
// private source-ID allocator copy, then publishes both only after every
// allocation and side insertion succeeds. On any failure the output and the
// caller's source-ID allocator are unchanged.
//
// Fresh source IDs are allocated from pIdAllocator (1 for the brush +
// 1 per emitted side).
CYPHER_NODISCARD geometry_status_t MeshToBrush_TryConvert(
    const editable_mesh_t *pMeshIn,
    brush_solid_t *pBrushOut,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_TO_BRUSH_H
