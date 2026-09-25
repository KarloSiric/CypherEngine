//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSlice.h
//  Purpose: Declares cutting an editable mesh with a plane - Hammer's
//           Clipping tool on meshes: slice (cut every face the plane
//           crosses and keep both sides) or clip (keep one side, optionally
//           capping the opening with faces in the plane).
//  Details: Vertices within `onPlaneTolerance` of the plane count as on it;
//           they are used as cut points as they are (not moved). Every edge
//           whose ends lie strictly on opposite sides gets one new vertex,
//           shared by both of its faces, so the cut is watertight.
//
//           A crossed face is cut along the line where the plane meets it.
//           Faces may be concave: the line can pass through a face several
//           times, and each stretch inside the face becomes a new edge, so a
//           face can fall into more than two pieces. Which stretches are
//           inside is decided by walking the face's crossings in order
//           along the line (an inside/outside parity), not by testing
//           constructed midpoints, so a vertex that only touches the line is
//           handled exactly. A face whose crossings do not pair up (a badly
//           non-planar or nearly edge-on face) is DEGENERATE.
//
//           Clip sides: "front" is where n.x + d > 0. A face lying in the
//           plane belongs to the side its normal points away from - it is
//           the boundary of the solid behind it - so clipping a box at its
//           own top face keeps the whole box when keeping the back side.
//
//           Caps: the new boundary a clip leaves in the plane is closed
//           with faces facing the removed side. A cap outline without
//           holes becomes one face; an outline with holes (clipping a
//           hollow room's walls) is triangulated, because faces have a
//           single loop. Outline vertices where the cap boundary branches
//           (kept parts touching at a point in the plane) are
//           NON_MANIFOLD.
//
//           The whole result is planned first and applied with one
//           MeshBoundary_ReplaceFaces call, so the cut is failure-atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SLICE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SLICE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

enum class mesh_slice_keep_t : common::u8 {
    BOTH = 0u, // slice: cut the crossed faces, keep everything
    FRONT,     // clip: keep the side n.x + d > 0
    BACK       // clip: keep the side n.x + d < 0
};

struct mesh_slice_params_t {
    math::planed_t plane{ { 0.0, 0.0, 1.0 }, 0.0 }; // need not be unit
    mesh_slice_keep_t keep{ mesh_slice_keep_t::BOTH };
    bool bCap{ false };                // clip only: close the opening
    common::f64 onPlaneTolerance{ 1e-9 };
};

enum class mesh_slice_face_role_t : common::u8 {
    PIECE = 0u, // part of a face the plane cut
    CAP         // closes a clip's opening
};

// A new face. PIECE: hSource is the face it was cut from (removed by the
// cut); bLargestPiece marks the largest kept piece of that face, the one
// that should keep the face's identity. CAP: hSource is invalid.
struct mesh_slice_face_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_face_handle_t hSource{};
    mesh_slice_face_role_t role{ mesh_slice_face_role_t::PIECE };
    bool bLargestPiece{ false };
};

struct mesh_slice_result_t {
    common::u32 cFacesCut{ 0u };      // faces the plane crossed
    common::u32 cFacesDiscarded{ 0u }; // uncut faces removed by a clip
    common::u32 cPieces{ 0u };        // kept pieces of cut faces
    common::u32 cCapFaces{ 0u };
    common::u32 cVerticesCreated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Cuts the mesh (see the file comment). Nothing to cut - the plane misses
// every face, or a clip keeps everything - is OK with no change. Rejected
// (mesh unchanged): a non-finite plane or tolerance -> NUMERIC_FAILURE; a
// zero plane normal, a negative tolerance, or bCap with keep BOTH ->
// INVALID_ARGUMENT / DEGENERATE; a clip that would remove every face, a
// face that cannot be cut consistently, or a piece or cap too small to
// describe, or an opening that does not close (a capped clip of an open
// mesh) -> DEGENERATE; branching cap outlines or a result with a pinched
// vertex -> NON_MANIFOLD; a piece over the source's corner limit ->
// LIMIT_EXCEEDED (a cap outline over it is triangulated instead).
// pFacesOut (optional, initialized) receives every new face.
CYPHER_NODISCARD mesh_slice_result_t MeshSlice_ByPlane(
    editable_mesh_t *pMesh,
    const mesh_slice_params_t &params,
    common::vector_t<mesh_slice_face_t> *pFacesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SLICE_H
