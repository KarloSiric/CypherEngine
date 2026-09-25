//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_EditableMesh.h
//  Purpose: Declares the editable half-edge mesh and its element records.
//  Details: The mesh is an oriented polygonal two-manifold stored as six
//           generation-pool families: vertex, half-edge, edge, loop, face,
//           and shell. Every element is addressed by a generation-tagged
//           handle that rejects stale references after removal or reuse.
//
//           Each edge carries exactly two opposing half-edges whose twin
//           pointers are reciprocal. Each face owns one outer loop (a CCW
//           ring of half-edges viewed from the outward normal). Each shell
//           is a connected component of faces. The mesh may contain one or
//           more shells.
//
//           This representation does not own or replicate the originating
//           brush plane set. Conversion from brush_boundary_t is performed
//           by the checked MeshBuilder, which validates the input topology
//           before populating the pools.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_EDITABLE_MESH_H
#define CYPHER_EDITOR_GEOMETRY_EDITABLE_MESH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherCommon_GenerationPool.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Element records — one per pool slot
// ---------------------------------------------------------------------------

// A mesh vertex stores its world position and a handle to any one of its
// outgoing half-edges. The full fan of outgoing half-edges is reachable
// by following twin→next around the vertex.
struct mesh_vertex_record_t {
    math::vec3d_t position{};
    geometry_mesh_half_edge_handle_t hOutHalfEdge{};
};

// A half-edge is the fundamental traversal primitive. It stores the origin
// vertex, its twin on the opposite side of the shared edge, the next and
// previous half-edges in its face loop (CCW winding), its parent edge,
// and its parent loop.
struct mesh_half_edge_record_t {
    geometry_mesh_vertex_handle_t hOrigin{};
    geometry_mesh_half_edge_handle_t hTwin{};
    geometry_mesh_half_edge_handle_t hNext{};
    geometry_mesh_half_edge_handle_t hPrev{};
    geometry_mesh_edge_handle_t hEdge{};
    geometry_mesh_loop_handle_t hLoop{};
};

// An edge is a pair of twin half-edges. The handle points to one of the
// two; the other is reachable via its twin pointer. The crease weight
// controls sharpness during subdivision: 0.0 = fully smooth (default),
// 1.0 = fully sharp (the edge stays perfectly creased through any
// number of subdivision iterations).
struct mesh_edge_record_t {
    geometry_mesh_half_edge_handle_t hHalfEdge{};
    common::f64 creaseWeight{ 0.0 };
};

// A loop is a closed ring of half-edges bounding a face. For the initial
// convex mesh each face has exactly one outer loop and no inner loops.
// The half-edge count is cached to avoid traversal for simple queries.
struct mesh_loop_record_t {
    geometry_mesh_half_edge_handle_t hFirstHalfEdge{};
    geometry_mesh_face_handle_t hFace{};
    common::u32 cHalfEdges{ 0u };
};

// A face owns one outer boundary loop. The normal is the outward-facing
// direction, consistent with the CCW winding of the outer loop. The
// source side index traces this face back to the brush side that produced
// it (CY_INVALID_INDEX if the face was created by a mesh operation).
struct mesh_face_record_t {
    geometry_mesh_loop_handle_t hOuterLoop{};
    geometry_mesh_shell_handle_t hShell{};
    math::vec3d_t normal{};
    common::u32 iSourceSide{ CY_INVALID_INDEX };
};

// A shell is a maximal connected component of faces. A closed convex
// solid has exactly one shell. The face count is cached.
struct mesh_shell_record_t {
    geometry_mesh_face_handle_t hAnyFace{};
    common::u32 cFaces{ 0u };
};

// ---------------------------------------------------------------------------
// Pool type aliases — shorthand for the verbose template instantiations
// ---------------------------------------------------------------------------

using mesh_vertex_pool_t =
    common::generation_pool_t<mesh_vertex_record_t,
                              geometry_mesh_vertex_tag_t>;
using mesh_half_edge_pool_t =
    common::generation_pool_t<mesh_half_edge_record_t,
                              geometry_mesh_half_edge_tag_t>;
using mesh_edge_pool_t =
    common::generation_pool_t<mesh_edge_record_t,
                              geometry_mesh_edge_tag_t>;
using mesh_loop_pool_t =
    common::generation_pool_t<mesh_loop_record_t,
                              geometry_mesh_loop_tag_t>;
using mesh_face_pool_t =
    common::generation_pool_t<mesh_face_record_t,
                              geometry_mesh_face_tag_t>;
using mesh_shell_pool_t =
    common::generation_pool_t<mesh_shell_record_t,
                              geometry_mesh_shell_tag_t>;

// ---------------------------------------------------------------------------
// Editable mesh — owns all six element pools
// ---------------------------------------------------------------------------

// Mesh limits bound pool growth. Set before Init; the builder and edit
// operations check these rather than allowing unbounded allocation.
struct editable_mesh_limits_t {
    common::usize cVertexMax{ 65536u };
    common::usize cHalfEdgeMax{ 262144u };
    common::usize cEdgeMax{ 131072u };
    common::usize cLoopMax{ 65536u };
    common::usize cFaceMax{ 65536u };
    common::usize cShellMax{ 256u };
};

struct editable_mesh_t {
    mesh_vertex_pool_t vertices;
    mesh_half_edge_pool_t halfEdges;
    mesh_edge_pool_t edges;
    mesh_loop_pool_t loops;
    mesh_face_pool_t faces;
    mesh_shell_pool_t shells;

    const common::allocator_t *pAllocator{ nullptr };
    editable_mesh_limits_t limits{};
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_status_t EditableMesh_Init(
    editable_mesh_t *pMesh,
    const common::allocator_t *pAllocator,
    const editable_mesh_limits_t &limits = {} ) noexcept;

void EditableMesh_Shutdown(
    editable_mesh_t *pMesh ) noexcept;

CYPHER_NODISCARD bool EditableMesh_IsInitialized(
    const editable_mesh_t *pMesh ) noexcept;

// ---------------------------------------------------------------------------
// Element counts
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize EditableMesh_VertexCount(
    const editable_mesh_t *pMesh ) noexcept;

CYPHER_NODISCARD common::usize EditableMesh_HalfEdgeCount(
    const editable_mesh_t *pMesh ) noexcept;

CYPHER_NODISCARD common::usize EditableMesh_EdgeCount(
    const editable_mesh_t *pMesh ) noexcept;

CYPHER_NODISCARD common::usize EditableMesh_LoopCount(
    const editable_mesh_t *pMesh ) noexcept;

CYPHER_NODISCARD common::usize EditableMesh_FaceCount(
    const editable_mesh_t *pMesh ) noexcept;

CYPHER_NODISCARD common::usize EditableMesh_ShellCount(
    const editable_mesh_t *pMesh ) noexcept;

// ---------------------------------------------------------------------------
// Element access — resolve a handle to its record
// ---------------------------------------------------------------------------

CYPHER_NODISCARD const mesh_vertex_record_t *EditableMesh_GetVertex(
    const editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hVertex ) noexcept;

CYPHER_NODISCARD const mesh_half_edge_record_t *EditableMesh_GetHalfEdge(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hHalfEdge ) noexcept;

CYPHER_NODISCARD const mesh_edge_record_t *EditableMesh_GetEdge(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept;

CYPHER_NODISCARD const mesh_loop_record_t *EditableMesh_GetLoop(
    const editable_mesh_t *pMesh,
    geometry_mesh_loop_handle_t hLoop ) noexcept;

CYPHER_NODISCARD const mesh_face_record_t *EditableMesh_GetFace(
    const editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace ) noexcept;

CYPHER_NODISCARD const mesh_shell_record_t *EditableMesh_GetShell(
    const editable_mesh_t *pMesh,
    geometry_mesh_shell_handle_t hShell ) noexcept;

// ---------------------------------------------------------------------------
// Topological queries
// ---------------------------------------------------------------------------

// Euler characteristic V - E + F for the entire mesh.
CYPHER_NODISCARD common::i32 EditableMesh_EulerCharacteristic(
    const editable_mesh_t *pMesh ) noexcept;

// Signed volume of the mesh. Positive for outward-facing normals.
CYPHER_NODISCARD common::f64 EditableMesh_SignedVolume(
    const editable_mesh_t *pMesh ) noexcept;

// Number of half-edges in the fan around a vertex.
CYPHER_NODISCARD common::u32 EditableMesh_VertexValence(
    const editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hVertex ) noexcept;

// ---------------------------------------------------------------------------
// Geometric analysis
// ---------------------------------------------------------------------------

// Total surface area of the mesh (sum of all face areas).
CYPHER_NODISCARD common::f64 EditableMesh_SurfaceArea(
    const editable_mesh_t *pMesh ) noexcept;

// Centroid of the mesh (average of all vertex positions).
CYPHER_NODISCARD math::vec3d_t EditableMesh_Centroid(
    const editable_mesh_t *pMesh ) noexcept;

// Bounding sphere: center is the centroid, radius is the maximum distance
// from the centroid to any vertex. Returns false if the mesh is empty.
struct mesh_bounding_sphere_t {
    math::vec3d_t center{};
    common::f64 radius{ 0.0 };
};

CYPHER_NODISCARD bool EditableMesh_BoundingSphere(
    const editable_mesh_t *pMesh,
    mesh_bounding_sphere_t *pSphereOut ) noexcept;

// Axis-aligned bounding box of the mesh.
CYPHER_NODISCARD bool EditableMesh_BoundingBox(
    const editable_mesh_t *pMesh,
    math::vec3d_t *pMinOut,
    math::vec3d_t *pMaxOut ) noexcept;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

CYPHER_NODISCARD const char *EditableMesh_StatusName(
    geometry_status_t status ) noexcept;

// Static assertions on element record layout.
static_assert( std::is_trivially_copyable_v<mesh_vertex_record_t> );
static_assert( std::is_trivially_copyable_v<mesh_half_edge_record_t> );
static_assert( std::is_trivially_copyable_v<mesh_edge_record_t> );
static_assert( std::is_trivially_copyable_v<mesh_loop_record_t> );
static_assert( std::is_trivially_copyable_v<mesh_face_record_t> );
static_assert( std::is_trivially_copyable_v<mesh_shell_record_t> );

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_EDITABLE_MESH_H
