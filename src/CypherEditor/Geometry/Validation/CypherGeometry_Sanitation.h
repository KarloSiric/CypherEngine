//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Sanitation.h
//  Purpose: Declares the checked conversion from PolygonSoup to
//           EditableMesh, and the reverse export.
//  Details: This is the only door from neutral soup into the canonical
//           half-edge representation. The half-edge structure is never
//           weakened to admit bad input; instead each defect is either a
//           reported failure or — only when the caller's policy explicitly
//           asks for it — a counted, deterministic sanitation step:
//
//             weld        points within fWeldDistance (0 = exact only);
//             degenerate  faces dropped only if bDropDegenerateFaces;
//             boundary    open edges allowed only if !bRequireClosed.
//
//           Never "fixed", always reported:
//             an edge used by 3+ faces              (NON_MANIFOLD);
//             two faces using an edge in one direction (INVALID_TOPOLOGY:
//               inconsistent orientation or duplicate face);
//             a vertex whose faces form 2+ separate fans (NON_MANIFOLD).
//
//           Connected components become separate shells. Face normals are
//           the normalized Newell normals of the (welded) input loops.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SANITATION_H
#define CYPHER_EDITOR_GEOMETRY_SANITATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

struct sanitation_policy_t {
    // Welding is opt-in. With bWeld == false the soup's vertex indices are
    // authoritative: two soup vertices at bit-identical positions stay two
    // mesh vertices, because even exact coincidence must not imply a weld
    // (ARCHITECTURE.md). With bWeld == true, points within fWeldDistance
    // merge (0 = bit-identical only).
    bool bWeld{ false };
    common::f64 fWeldDistance{ 0.0 };
    common::f64 fMinimumFaceArea{ 1.0e-12 };
    bool bDropDegenerateFaces{ false };
    bool bRequireClosed{ true };
};

enum class sanitation_fault_t : common::u8 {
    NONE                     = 0u,
    INVALID_INPUT            = 1u, // soup validation failure (non-finite, bad index)
    DEGENERATE_FACE          = 2u, // iFace (input index)
    NON_MANIFOLD_EDGE        = 3u, // iVertexA, iVertexB (input indices)
    INCONSISTENT_ORIENTATION = 4u, // iVertexA, iVertexB
    OPEN_BOUNDARY            = 5u, // iVertexA, iVertexB
    NON_MANIFOLD_VERTEX      = 6u, // iVertexA
    EMPTY                    = 7u  // no faces survive
};

struct sanitation_report_t {
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
    sanitation_fault_t fault{ sanitation_fault_t::NONE };
    common::u32 iFace{ CY_INVALID_INDEX };
    common::u32 iVertexA{ CY_INVALID_INDEX };
    common::u32 iVertexB{ CY_INVALID_INDEX };

    common::u32 cInputFaces{ 0u };
    common::u32 cOutputFaces{ 0u };
    common::u32 cDroppedFaces{ 0u };
    common::u32 cWeldedVertices{ 0u };      // input vertices merged into others
    common::u32 cUnreferencedVertices{ 0u };
    common::u32 cBoundaryEdges{ 0u };
    common::u32 cShells{ 0u };
    common::f64 fMaxWeldDisplacement{ 0.0 };
};

CYPHER_NODISCARD const char *SanitationFault_Name( sanitation_fault_t fault ) noexcept;

// Converts pSoup into pMeshOut, which must be zero-initialized (it is
// initialized here with pAllocator). On failure pMeshOut is left
// uninitialized and the report names the first defect. On success, if
// pFaceMapOut is non-null (and initialized) it receives one mesh face
// handle per input face, invalid for dropped faces — the provenance link
// from soup source IDs to mesh faces.
CYPHER_NODISCARD sanitation_report_t Sanitation_TryPolygonSoupToMesh(
    const polygon_soup_t *pSoup,
    const sanitation_policy_t &policy,
    const common::allocator_t *pAllocator,
    editable_mesh_t *pMeshOut,
    common::vector_t<geometry_mesh_face_handle_t> *pFaceMapOut ) noexcept;

// Exports every face of pMesh as a polygon in loop order into pSoupOut,
// which must be initialized and empty. Vertices are numbered densely in
// pool order; open-boundary and closed meshes both export. Failure-atomic.
CYPHER_NODISCARD geometry_status_t Sanitation_TryMeshToPolygonSoup(
    const editable_mesh_t *pMesh,
    polygon_soup_t *pSoupOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SANITATION_H
