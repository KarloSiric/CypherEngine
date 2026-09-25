//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshValidation.h
//  Purpose: Declares structural validation for editable half-edge meshes.
//  Details: Validates that the mesh is a closed, oriented two-manifold:
//           - Every half-edge has a valid twin, and twin pointers are
//             reciprocal (he→twin→twin == he).
//           - Every half-edge loop is closed (following next returns to
//             the starting half-edge in exactly cHalfEdges steps).
//           - Face/loop and face/shell ownership links are reciprocal, and
//             each shell's cached face count matches its live members.
//           - Every vertex is reachable from at least one half-edge.
//           - Euler characteristic V - E + F equals 2 per shell.
//           - All face normals are consistent with outward winding.
//           - Signed volume is positive (outward-facing convention).
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_VALIDATION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_VALIDATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

// Result of a full structural validation pass. Each field is independently
// computed so that a caller can diagnose exactly which invariant failed.
struct mesh_validation_result_t {
    geometry_status_t status{ geometry_status_t::OK };

    // True when every half-edge's twin pointer is reciprocal.
    bool bReciprocalTwins{ false };

    // True when every half-edge loop is closed and has the correct count.
    bool bClosedLoops{ false };

    // True when every vertex's outgoing half-edge is live and originates
    // at that vertex.
    bool bAllVerticesReferenced{ false };

    // True when every half-edge names a live edge whose representative is
    // that half-edge or its twin, twins name the same edge, and no edge
    // record is orphaned.
    bool bEdgeLinks{ false };

    // True when every face names a live shell, every shell names one of its
    // faces, and each shell's cached face count is exact.
    bool bShellLinks{ false };

    // True when Euler characteristic equals 2 × shell count.
    bool bEulerValid{ false };

    // True when all face normals agree with CCW winding direction.
    bool bConsistentWinding{ false };

    // True when the signed volume is positive.
    bool bPositiveVolume{ false };

    // Computed Euler characteristic (V - E + F).
    common::i32 nEulerCharacteristic{ 0 };

    // Computed signed volume.
    common::f64 fSignedVolume{ 0.0 };
};

// Performs a complete structural validation of the mesh topology.
// All checks run unconditionally so the result captures every failure
// in a single pass.
CYPHER_NODISCARD mesh_validation_result_t MeshValidation_Validate(
    const editable_mesh_t *pMesh ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_VALIDATION_H
