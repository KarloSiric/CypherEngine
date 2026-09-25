//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgMesh.h
//  Purpose: Declares the general mesh Boolean: union, intersection,
//           difference, symmetric difference and clip of two authored
//           meshes of any shape (convex or not, several shells, holes),
//           as a pipeline over the Csg stages.
//  Details: Stages, each in its own folder: InputNormalization (triangles
//           with source mapping) -> BroadPhase (candidate pairs) ->
//           Intersections (exact-sign segments with canonical points) ->
//           Corefinement (conforming refinement) -> CoplanarOverlay +
//           CellComplex + Classification (inside / outside / shared per
//           patch) -> Expression (the operator's keep table) ->
//           BoundaryExtraction (regularised, oriented boundary) ->
//           Reconstruction + Stitching + AttributeTransfer (polygons,
//           identity, surface data) -> Cleanup (redundant points, final
//           validation).
//
//           Failure is atomic and explained: the result stays empty, and
//           the diagnostics name the stage, a witness point, and the counts
//           reached, so the editor can show the user where a Boolean failed
//           instead of producing a broken mesh.
//
//           The existing convex-only MeshBool_* (Representations/Mesh) and
//           brush BrushCSG_* stay as fast paths; this handles everything
//           they reject.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_MESH_H
#define CYPHER_EDITOR_GEOMETRY_CSG_MESH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgReconstruct.h"

namespace cypher::editor::geometry
{

struct csg_mesh_options_t {
    csg_operator_t op{ csg_operator_t::DIFFERENCE };
    csg_attribute_policy_t attributes{};
    common::f64 quantizeStep{ 0.0 };         // 0 = no snapping
    common::usize cPairsMax{ 1u << 22 };     // broad-phase guard
    bool bRemoveRedundantVertices{ true };
};

// Evaluates A op B into *pResult (initialized; replaced). An empty result
// (e.g. the intersection of disjoint solids) is OK with no faces.
CYPHER_NODISCARD geometry_status_t CsgMesh_TryEvaluate(
    const mesh_source_t *pA,
    const mesh_source_t *pB,
    const csg_mesh_options_t &options,
    csg_mesh_result_t *pResult,
    csg_diagnostics_t *pDiag ) noexcept;

// Evaluates and builds an authored mesh in canonical-empty *pOut: the root
// keeps A's ID, input vertices and first faces keep theirs, everything else
// gets fresh IDs from *pIdAllocator. An empty result is DEGENERATE (a mesh
// needs faces). Failure leaves *pOut empty and the allocator unchanged.
CYPHER_NODISCARD geometry_status_t CsgMesh_TryBuild(
    const mesh_source_t *pA,
    const mesh_source_t *pB,
    const csg_mesh_options_t &options,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    mesh_source_t *pOut,
    csg_diagnostics_t *pDiag ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_MESH_H
