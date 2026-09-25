//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceModeling.h
//  Purpose: Declares identity-addressed modeling edits on a mesh source:
//           extrude, inset, bevel, loop cut, triangulation, transforms, and
//           direct attribute edits.
//  Details: Same contract as MeshSourceTopology.h: IDs in, the capture ->
//           op -> resolve bracket in between, fresh IDs for new elements at
//           commit. Modeling adds explicit identity continuity where a tool
//           semantically moves a face rather than replacing it:
//
//             - ExtrudeFace: the cap face keeps the original face's ID and
//               attributes; the side walls are new faces whose parent is the
//               old face they share the most vertices with, ties going to
//               the one whose plane is closest (on a box, the coplanar wall
//               they extend).
//             - InsetFace: the inner face keeps the original ID; the ring
//               faces are new, parented to the original face (coplanar).
//
//           This is what lets a selection of "the top face" survive an
//           extrude and what makes the undo record describe the edit as the
//           user sees it.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_MODELING_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_MODELING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSourceTopology.h"

namespace cypher::editor::geometry
{

CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryExtrudeFace(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    common::f64 distance,
    mesh_edit_report_t *pReportOut ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryInsetFace(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    common::f64 margin,
    mesh_edit_report_t *pReportOut ) noexcept;

// Bevels edge (a, b) into a strip of cSegments faces of the given width.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryBevelEdge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    common::f64 width,
    common::u32 cSegments,
    mesh_edit_report_t *pReportOut ) noexcept;

// Cuts an edge loop across the quad ring through edge (a, b) at t.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryLoopCut(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    common::f64 t,
    common::u32 *pFacesSplitOut,
    mesh_edit_report_t *pReportOut ) noexcept;

// Triangulates one face (faceId valid) or, with an invalid faceId, every
// face. Each face keeps its ID on one triangle; the other triangles are new
// faces with its attributes.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryTriangulate(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    common::u32 *pFacesCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryMoveVertex(
    mesh_source_t *pSource,
    geometry_source_id_t vertexId,
    math::vec3d_t position ) noexcept;

// Applies an orientation-preserving affine transform (determinant > 0) to
// every vertex. Mirroring transforms are UNSUPPORTED here: they must flip
// winding, which MeshSourceEdit_TryMirror does. Validates every transformed
// position before writing, so failure changes nothing. Face normals are
// recomputed.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryTransform(
    mesh_source_t *pSource,
    const math::affine3d_t &transform ) noexcept;

// Reflects the mesh across `plane`, flipping winding so faces stay
// outward. Corner attributes stay with their (face, vertex) pairs.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TryMirror(
    mesh_source_t *pSource,
    math::planed_t plane,
    mesh_edit_report_t *pReportOut ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySmooth(
    mesh_source_t *pSource,
    common::f64 factor,
    common::u32 cIterations ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySnapToGrid(
    mesh_source_t *pSource,
    common::f64 spacing ) noexcept;

// Direct attribute edits.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySetFaceAttributes(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    const mesh_face_attributes_t &attributes ) noexcept;

CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySetCornerAttributes(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    geometry_source_id_t vertexId,
    const mesh_corner_attributes_t &attributes ) noexcept;

// Crease weight must be finite and >= 0 (mesh ops clamp to [0, 1] when
// subdividing; storage keeps the authored value).
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySetEdgeAttributes(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    const mesh_edge_attributes_t &attributes,
    common::f64 creaseWeight ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_MODELING_H
