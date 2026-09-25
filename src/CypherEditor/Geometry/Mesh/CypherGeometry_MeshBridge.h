//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshBridge.h
//  Purpose: Declares Hammer's Bridge on the editable mesh for the two cases
//           MeshBoundary_Bridge (whole open loops) does not cover: two faces
//           (removed and joined by a tube - a tunnel through a wall, or a
//           connector between two parts) and two open edge chains (a strip
//           across a gap between two borders).
//  Details: Pairing. Two faces that face each other run their loops in
//           opposite directions as seen along the tube, so face A's loop
//           forward pairs with face B's loop backward; of the n rotations
//           the one with the smallest summed squared distance between
//           paired corners is used, which is the untwisted tube for any
//           reasonable pair. Edge chains pair the same way: A's chain
//           forward with B's backward, because two borders across a gap run
//           in opposite directions when their faces agree in orientation.
//
//           The tube or strip can be split into `cSegments` rings of quads
//           (new vertices on the straight lines between paired corners).
//           Everything is one MeshBoundary_ReplaceFaces call - the removed
//           faces and the new quads - so a bridge is failure-atomic, and a
//           result that would be non-manifold (faces that share an edge, a
//           chain that runs into the other) is rejected by its checks.
//
//           Each new quad reports a source face for attribute transfer: the
//           face across the paired edge of A (for a face bridge, the wall
//           the tube continues - its texture carries on along the tube;
//           face A itself if that edge was open), or the face owning A's
//           chain edge (for a chain bridge).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_BRIDGE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_BRIDGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kMeshBridgeSegmentsMax = 64u;

// A new bridge quad and the face it takes its attributes from (a handle
// from before the bridge; face A itself no longer exists afterwards).
struct mesh_bridge_face_t {
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_face_handle_t hSource{};
};

struct mesh_bridge_result_t {
    common::u32 cFacesRemoved{ 0u };
    common::u32 cFacesCreated{ 0u };
    common::u32 cVerticesCreated{ 0u };
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
};

// Removes faces A and B and joins their loops with a tube. The faces must
// be distinct, share no vertex, and have the same number of corners
// (INVALID_ARGUMENT otherwise); cSegments in [1, kMeshBridgeSegmentsMax].
// Stale faces -> INVALID_HANDLE; a tube quad too small to describe ->
// DEGENERATE; a non-manifold result -> NON_MANIFOLD. pFacesOut (optional,
// initialized) receives the new quads.
CYPHER_NODISCARD mesh_bridge_result_t MeshBridge_Faces(
    editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFaceA,
    geometry_mesh_face_handle_t hFaceB,
    common::u32 cSegments,
    common::vector_t<mesh_bridge_face_t> *pFacesOut ) noexcept;

// Joins two open edge chains with a strip. Each chain lists boundary edges
// in order along the border (either direction: each is normalized to the
// border's own direction); consecutive edges must share a vertex. The
// chains must be the same length and share no vertex; a chain that closes
// on itself is a loop - use MeshBoundary_Bridge. Rejections as
// MeshBridge_Faces, plus INVALID_ARGUMENT for a non-boundary edge or a
// broken chain.
CYPHER_NODISCARD mesh_bridge_result_t MeshBridge_EdgeChains(
    editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_edge_handle_t> chainA,
    common::span_t<const geometry_mesh_edge_handle_t> chainB,
    common::u32 cSegments,
    common::vector_t<mesh_bridge_face_t> *pFacesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_BRIDGE_H
