//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSolidify.h
//  Purpose: Declares bounded solidification of open polygonal mesh sources.
//  Details: Solidify converts every open, oriented source shell into a closed
//           volume. The authored source surface stays in place and keeps its
//           vertex/face identities. A second surface is displaced by the
//           signed thickness along angle-weighted vertex normals, and every
//           boundary loop is closed with material-inheriting wall faces.
//
//           The operation builds a separate mesh source rather than mutating
//           its input. This makes allocation, identity exhaustion, geometric
//           rejection, and cancellation at the caller failure-atomic by
//           construction. The caller can adopt the result in one document
//           transaction after preview/validation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOLIDIFY_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOLIDIFY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSource.h"
#include "CypherGeometry_Policy.h"

namespace cypher::editor::geometry
{

struct mesh_solidify_report_t {
    common::u32 cSourceVertices{ 0u };
    common::u32 cSourceFaces{ 0u };
    common::u32 cBoundaryEdges{ 0u };
    common::u32 cOffsetVertices{ 0u };
    common::u32 cOffsetFaces{ 0u };
    common::u32 cSideFaces{ 0u };
    common::u32 cTriangulatedSideWalls{ 0u };
    common::u32 cNewSourceIds{ 0u };
};

// Builds a closed solid from all open shells in pSource.
//
// Signed-thickness convention:
//   thickness > 0: the offset layer moves along the authored surface normal;
//                  the stationary layer is the back side of the volume.
//   thickness < 0: the offset layer moves opposite the authored normal;
//                  the stationary layer is the front side of the volume.
//
// Identity and surfacing:
//   - stationary vertices/faces keep their source IDs and attributes;
//   - offset vertices/faces and side faces receive fresh IDs;
//   - the offset layer copies face, corner, and non-default edge attributes;
//   - each side wall inherits its adjacent source material, uses smoothing
//     group 0 (a deliberately sharp rim), and copies endpoint corner data;
//   - a warped wall quad is split into two triangles along the safer diagonal.
//
// Every connected shell must be an open topological disk. Closed-only,
// mixed open/closed, annular, and higher-genus inputs return UNSUPPORTED:
// closed shells need a separate inside/outside policy, while the current
// editable-mesh validator deliberately admits genus-zero solids only.
// Zero/effectively-zero thickness is DEGENERATE. Invalid or non-manifold input
// is rejected before construction. The realized element counts are checked
// against the exact preflight, and every result shell is independently checked
// for closed two-manifold topology and positive signed volume. Degenerate
// faces and self-intersection are rejected before publication.
//
// pResultOut must be default initialized and must differ from pSource. Failure
// leaves it shut down and leaves pSource and *pIdAllocator unchanged. On
// success, pResultOut owns the result and *pIdAllocator advances past every
// new vertex and face identity in deterministic canonical-source order.
CYPHER_NODISCARD geometry_status_t MeshSolidify_TryBuild(
    const mesh_source_t *pSource,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    common::f64 thickness,
    mesh_source_t *pResultOut,
    mesh_solidify_report_t *pReportOut = nullptr ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOLIDIFY_H
