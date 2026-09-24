//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshCornerNormals.h
//  Purpose: Declares attribute-driven split normals for an EditableMesh:
//           one normal per corner, smooth across edges that are neither
//           HARD nor separating disjoint smoothing groups.
//  Details: For each corner (half-edge h at vertex v in face F) the normal
//           averages the face normals of every face reachable from F by
//           rotating around v without crossing a sharp edge. An edge is
//           sharp when it is flagged HARD, lies on an open boundary, or
//           separates faces whose smoothing-group masks do not intersect
//           (a face with mask 0 is flat and shares with nobody).
//
//           Face normals are weighted by the face's interior angle at v
//           (Thürmer & Wüthrich, "Computing Vertex Normals from Polygonal
//           Facets", 1998), so a vertex normal does not tilt toward a side
//           that merely has more, thinner triangles.
//
//           This is the source-level answer Cook/RenderMesh needs to decide
//           where to split vertices; it does not modify the mesh or store.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_CORNER_NORMALS_H
#define CYPHER_EDITOR_GEOMETRY_MESH_CORNER_NORMALS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_MeshStore.h"

namespace cypher::editor::geometry
{

struct mesh_corner_normal_record_t {
    geometry_mesh_half_edge_handle_t hCorner{};
    math::vec3d_t normal{};
    common::u32 iSmoothGroupId{ 0u }; // corners at one vertex sharing this id share the normal
};

// Computes one record per half-edge into pOut (initialized; cleared here).
// pStore may be null, which means "all faces in smoothing group 1, no hard
// edges" (fully smooth). iSmoothGroupId is a dense id per (vertex, smooth
// fan) so a cook can merge corners with equal ids into one render vertex.
//
// Returns NOT_INITIALIZED, INVALID_ARGUMENT, CORRUPT_STATE, or
// ALLOCATION_FAILED (pOut cleared on failure).
CYPHER_NODISCARD geometry_status_t MeshNormals_TryComputeCornerNormals(
    const editable_mesh_t *pMesh,
    const mesh_attribute_store_t *pStore,
    common::vector_t<mesh_corner_normal_record_t> *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_CORNER_NORMALS_H
