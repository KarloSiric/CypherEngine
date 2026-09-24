//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshGeometricValidation.h
//  Purpose: Declares geometric (as opposed to structural) validation of an
//           editable mesh: short edges, small faces, warped faces,
//           coincident vertices, and self-intersection.
//  Details: MeshValidation_Validate proves the half-edge structure is
//           consistent. A structurally perfect mesh can still be unusable:
//           a face folded through its neighbour, a 1e-12 sliver, or two
//           shells passing through each other. This pass finds those.
//
//           Findings are bounded (kMeshGeometricIssuesMax recorded, all
//           counted) and never repaired — Repair consumes them
//           (ARCHITECTURE.md: Validation reports, Repair plans).
//
//           Severity: NON_FINITE, SELF_INTERSECTION, DEGENERATE_FACE are
//           errors (status != OK). SHORT_EDGE, NON_PLANAR_FACE, and
//           COINCIDENT_VERTICES are warnings: legal but suspicious, and
//           reported with status OK.
//
//           Self-intersection cost: faces are triangulated in their own
//           plane (so concave faces are handled), triangles are sorted by
//           bounding box on x, and candidate pairs are tested with the
//           exact Kernel predicates. Adjacency-implied contact (shared
//           mesh vertices) is excluded.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_GEOMETRIC_VALIDATION_H
#define CYPHER_EDITOR_GEOMETRY_MESH_GEOMETRIC_VALIDATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_Policy.h"

namespace cypher::editor::geometry
{

inline constexpr common::usize kMeshGeometricIssuesMax = 64u;

enum class mesh_geometric_issue_kind_t : common::u8 {
    NON_FINITE_VERTEX   = 0u, // hVertexA
    SHORT_EDGE          = 1u, // hEdge, fValue = length
    DEGENERATE_FACE     = 2u, // hFaceA, fValue = area
    NON_PLANAR_FACE     = 3u, // hFaceA, fValue = max deviation
    COINCIDENT_VERTICES = 4u, // hVertexA, hVertexB, fValue = distance
    SELF_INTERSECTION   = 5u  // hFaceA, hFaceB
};

struct mesh_geometric_issue_t {
    mesh_geometric_issue_kind_t kind{ mesh_geometric_issue_kind_t::NON_FINITE_VERTEX };
    geometry_mesh_vertex_handle_t hVertexA{};
    geometry_mesh_vertex_handle_t hVertexB{};
    geometry_mesh_edge_handle_t hEdge{};
    geometry_mesh_face_handle_t hFaceA{};
    geometry_mesh_face_handle_t hFaceB{};
    common::f64 fValue{ 0.0 };
};

struct mesh_geometric_options_t {
    bool bCheckSelfIntersection{ true };
    bool bCheckCoincidentVertices{ true };
};

struct mesh_geometric_validation_t {
    geometry_status_t status{ geometry_status_t::OK };
    mesh_geometric_issue_t issues[kMeshGeometricIssuesMax]{};
    common::u32 cIssues{ 0u };      // recorded (<= max)
    common::u32 cTotalIssues{ 0u }; // found
    common::u32 cNonFinite{ 0u };
    common::u32 cShortEdges{ 0u };
    common::u32 cDegenerateFaces{ 0u };
    common::u32 cNonPlanarFaces{ 0u };
    common::u32 cCoincidentPairs{ 0u };
    common::u32 cSelfIntersections{ 0u };
    common::u32 cCandidatePairsTested{ 0u };
};

// Runs every enabled check with thresholds from policy.numerical:
// fMinimumEdgeLength, fMinimumFaceArea, fPlanarityTolerance, fWeldDistance
// (coincidence). Status: NUMERIC_FAILURE, SELF_INTERSECTING, or DEGENERATE
// for the first error class present (in that priority), OK otherwise;
// NOT_INITIALIZED / ALLOCATION_FAILED for infrastructure failures.
CYPHER_NODISCARD mesh_geometric_validation_t MeshValidation_ValidateGeometry(
    const editable_mesh_t *pMesh,
    const geometry_policy_t &policy,
    const mesh_geometric_options_t &options ) noexcept;

CYPHER_NODISCARD const char *MeshGeometricIssue_Name( mesh_geometric_issue_kind_t kind ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_GEOMETRIC_VALIDATION_H
