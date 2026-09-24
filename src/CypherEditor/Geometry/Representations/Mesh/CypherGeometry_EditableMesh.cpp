//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_EditableMesh.cpp
//  Purpose: Implements the editable half-edge mesh lifecycle and queries.
//  Details: Each pool is initialized with the mesh's limits and shut down
//           in reverse order. Element access resolves generation-tagged
//           handles against the owning pool, returning nullptr for stale
//           or invalid handles.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_EditableMesh.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t EditableMesh_Init(
    editable_mesh_t *pMesh,
    const allocator_t *pAllocator,
    const editable_mesh_limits_t &limits ) noexcept
{
    if ( pMesh == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Init all six pools with their respective limits.
    auto initPool = [&]( auto *pPool, usize limit ) -> geometry_status_t {
        const auto status = GenerationPool_Init( pPool, pAllocator, limit );
        return GeometryStatus_FromGenerationPoolStatus( status );
    };

    geometry_status_t s = initPool( &pMesh->vertices, limits.cVertexMax );
    if ( s != geometry_status_t::OK ) { return s; }

    s = initPool( &pMesh->halfEdges, limits.cHalfEdgeMax );
    if ( s != geometry_status_t::OK ) {
        GenerationPool_Shutdown( &pMesh->vertices );
        return s;
    }

    s = initPool( &pMesh->edges, limits.cEdgeMax );
    if ( s != geometry_status_t::OK ) {
        GenerationPool_Shutdown( &pMesh->halfEdges );
        GenerationPool_Shutdown( &pMesh->vertices );
        return s;
    }

    s = initPool( &pMesh->loops, limits.cLoopMax );
    if ( s != geometry_status_t::OK ) {
        GenerationPool_Shutdown( &pMesh->edges );
        GenerationPool_Shutdown( &pMesh->halfEdges );
        GenerationPool_Shutdown( &pMesh->vertices );
        return s;
    }

    s = initPool( &pMesh->faces, limits.cFaceMax );
    if ( s != geometry_status_t::OK ) {
        GenerationPool_Shutdown( &pMesh->loops );
        GenerationPool_Shutdown( &pMesh->edges );
        GenerationPool_Shutdown( &pMesh->halfEdges );
        GenerationPool_Shutdown( &pMesh->vertices );
        return s;
    }

    s = initPool( &pMesh->shells, limits.cShellMax );
    if ( s != geometry_status_t::OK ) {
        GenerationPool_Shutdown( &pMesh->faces );
        GenerationPool_Shutdown( &pMesh->loops );
        GenerationPool_Shutdown( &pMesh->edges );
        GenerationPool_Shutdown( &pMesh->halfEdges );
        GenerationPool_Shutdown( &pMesh->vertices );
        return s;
    }

    pMesh->pAllocator = pAllocator;
    pMesh->limits = limits;
    return geometry_status_t::OK;
}

void EditableMesh_Shutdown(
    editable_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) {
        return;
    }

    GenerationPool_Shutdown( &pMesh->shells );
    GenerationPool_Shutdown( &pMesh->faces );
    GenerationPool_Shutdown( &pMesh->loops );
    GenerationPool_Shutdown( &pMesh->edges );
    GenerationPool_Shutdown( &pMesh->halfEdges );
    GenerationPool_Shutdown( &pMesh->vertices );
    pMesh->pAllocator = nullptr;
}

bool EditableMesh_IsInitialized(
    const editable_mesh_t *pMesh ) noexcept
{
    return pMesh != nullptr && pMesh->pAllocator != nullptr;
}

// ---------------------------------------------------------------------------
// Element counts
// ---------------------------------------------------------------------------

usize EditableMesh_VertexCount(
    const editable_mesh_t *pMesh ) noexcept
{
    return pMesh ? GenerationPool_Count( &pMesh->vertices ) : 0u;
}

usize EditableMesh_HalfEdgeCount(
    const editable_mesh_t *pMesh ) noexcept
{
    return pMesh ? GenerationPool_Count( &pMesh->halfEdges ) : 0u;
}

usize EditableMesh_EdgeCount(
    const editable_mesh_t *pMesh ) noexcept
{
    return pMesh ? GenerationPool_Count( &pMesh->edges ) : 0u;
}

usize EditableMesh_LoopCount(
    const editable_mesh_t *pMesh ) noexcept
{
    return pMesh ? GenerationPool_Count( &pMesh->loops ) : 0u;
}

usize EditableMesh_FaceCount(
    const editable_mesh_t *pMesh ) noexcept
{
    return pMesh ? GenerationPool_Count( &pMesh->faces ) : 0u;
}

usize EditableMesh_ShellCount(
    const editable_mesh_t *pMesh ) noexcept
{
    return pMesh ? GenerationPool_Count( &pMesh->shells ) : 0u;
}

// ---------------------------------------------------------------------------
// Element access
// ---------------------------------------------------------------------------

const mesh_vertex_record_t *EditableMesh_GetVertex(
    const editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hVertex ) noexcept
{
    if ( pMesh == nullptr ) { return nullptr; }
    return GenerationPool_Get( &pMesh->vertices, hVertex );
}

const mesh_half_edge_record_t *EditableMesh_GetHalfEdge(
    const editable_mesh_t *pMesh,
    geometry_mesh_half_edge_handle_t hHalfEdge ) noexcept
{
    if ( pMesh == nullptr ) { return nullptr; }
    return GenerationPool_Get( &pMesh->halfEdges, hHalfEdge );
}

const mesh_edge_record_t *EditableMesh_GetEdge(
    const editable_mesh_t *pMesh,
    geometry_mesh_edge_handle_t hEdge ) noexcept
{
    if ( pMesh == nullptr ) { return nullptr; }
    return GenerationPool_Get( &pMesh->edges, hEdge );
}

const mesh_loop_record_t *EditableMesh_GetLoop(
    const editable_mesh_t *pMesh,
    geometry_mesh_loop_handle_t hLoop ) noexcept
{
    if ( pMesh == nullptr ) { return nullptr; }
    return GenerationPool_Get( &pMesh->loops, hLoop );
}

const mesh_face_record_t *EditableMesh_GetFace(
    const editable_mesh_t *pMesh,
    geometry_mesh_face_handle_t hFace ) noexcept
{
    if ( pMesh == nullptr ) { return nullptr; }
    return GenerationPool_Get( &pMesh->faces, hFace );
}

const mesh_shell_record_t *EditableMesh_GetShell(
    const editable_mesh_t *pMesh,
    geometry_mesh_shell_handle_t hShell ) noexcept
{
    if ( pMesh == nullptr ) { return nullptr; }
    return GenerationPool_Get( &pMesh->shells, hShell );
}

// ---------------------------------------------------------------------------
// Topological queries
// ---------------------------------------------------------------------------

i32 EditableMesh_EulerCharacteristic(
    const editable_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) { return 0; }
    const i32 V = static_cast<i32>( EditableMesh_VertexCount( pMesh ) );
    const i32 E = static_cast<i32>( EditableMesh_EdgeCount( pMesh ) );
    const i32 F = static_cast<i32>( EditableMesh_FaceCount( pMesh ) );
    return V - E + F;
}

f64 EditableMesh_SignedVolume(
    const editable_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) { return 0.0; }

    // Signed volume via the divergence theorem: sum of signed tetrahedra
    // formed by each face triangle and the origin.
    f64 volume = 0.0;

    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t /*hFace*/,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                return true;
            }

            // Fan-triangulate the face from its first vertex.
            const mesh_half_edge_record_t *pFirst =
                GenerationPool_Get( &pMesh->halfEdges,
                                    pLoop->hFirstHalfEdge );
            if ( pFirst == nullptr ) { return true; }

            const mesh_vertex_record_t *pV0 =
                GenerationPool_Get( &pMesh->vertices, pFirst->hOrigin );
            if ( pV0 == nullptr ) { return true; }

            geometry_mesh_half_edge_handle_t hCurr = pFirst->hNext;
            for ( u32 i = 1u; i + 1u < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pCurr =
                    GenerationPool_Get( &pMesh->halfEdges, hCurr );
                if ( pCurr == nullptr ) { break; }

                const mesh_half_edge_record_t *pNext =
                    GenerationPool_Get( &pMesh->halfEdges, pCurr->hNext );
                if ( pNext == nullptr ) { break; }

                const mesh_vertex_record_t *pV1 =
                    GenerationPool_Get( &pMesh->vertices, pCurr->hOrigin );
                const mesh_vertex_record_t *pV2 =
                    GenerationPool_Get( &pMesh->vertices, pNext->hOrigin );
                if ( pV1 == nullptr || pV2 == nullptr ) { break; }

                // Signed volume of tetrahedron (origin, v0, v1, v2).
                const math::vec3d_t &a = pV0->position;
                const math::vec3d_t &b = pV1->position;
                const math::vec3d_t &c = pV2->position;
                volume += ( a.x * ( b.y * c.z - b.z * c.y )
                          + a.y * ( b.z * c.x - b.x * c.z )
                          + a.z * ( b.x * c.y - b.y * c.x ) );

                hCurr = pCurr->hNext;
            }
            return true;
        } );

    return volume / 6.0;
}

u32 EditableMesh_VertexValence(
    const editable_mesh_t *pMesh,
    geometry_mesh_vertex_handle_t hVertex ) noexcept
{
    if ( pMesh == nullptr ) { return 0u; }

    const mesh_vertex_record_t *pVert =
        GenerationPool_Get( &pMesh->vertices, hVertex );
    if ( pVert == nullptr ) { return 0u; }

    if ( !GenerationHandle_IsValid( pVert->hOutHalfEdge ) ) {
        return 0u;
    }

    // Walk the fan: follow twin→next around the vertex.
    u32 valence = 0u;
    geometry_mesh_half_edge_handle_t hStart = pVert->hOutHalfEdge;
    geometry_mesh_half_edge_handle_t hCurr = hStart;
    do {
        ++valence;
        const mesh_half_edge_record_t *pHe =
            GenerationPool_Get( &pMesh->halfEdges, hCurr );
        if ( pHe == nullptr ) { break; }

        const mesh_half_edge_record_t *pTwin =
            GenerationPool_Get( &pMesh->halfEdges, pHe->hTwin );
        if ( pTwin == nullptr ) { break; }

        hCurr = pTwin->hNext;

        if ( valence > 10000u ) { break; }
    } while ( hCurr.nSlot != hStart.nSlot ||
              hCurr.nGeneration != hStart.nGeneration );

    return valence;
}

// ---------------------------------------------------------------------------
// Geometric analysis
// ---------------------------------------------------------------------------

f64 EditableMesh_SurfaceArea(
    const editable_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) { return 0.0; }

    f64 totalArea = 0.0;

    // Fan-triangulate each face and sum triangle areas.
    (void)GenerationPool_ForEach( &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get( &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                return true;
            }

            const mesh_half_edge_record_t *pFirst =
                GenerationPool_Get( &pMesh->halfEdges,
                                    pLoop->hFirstHalfEdge );
            if ( pFirst == nullptr ) { return true; }

            const mesh_vertex_record_t *pV0 =
                GenerationPool_Get( &pMesh->vertices, pFirst->hOrigin );
            if ( pV0 == nullptr ) { return true; }

            geometry_mesh_half_edge_handle_t hCurr = pFirst->hNext;
            for ( u32 i = 1u; i + 1u < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pCurr =
                    GenerationPool_Get( &pMesh->halfEdges, hCurr );
                if ( pCurr == nullptr ) { break; }

                const mesh_half_edge_record_t *pNext =
                    GenerationPool_Get( &pMesh->halfEdges, pCurr->hNext );
                if ( pNext == nullptr ) { break; }

                const mesh_vertex_record_t *pV1 =
                    GenerationPool_Get( &pMesh->vertices, pCurr->hOrigin );
                const mesh_vertex_record_t *pV2 =
                    GenerationPool_Get( &pMesh->vertices, pNext->hOrigin );
                if ( pV1 == nullptr || pV2 == nullptr ) { break; }

                const math::vec3d_t e1 = math::Vec3d_Subtract(
                    pV1->position, pV0->position );
                const math::vec3d_t e2 = math::Vec3d_Subtract(
                    pV2->position, pV0->position );
                const math::vec3d_t cross = math::Vec3d_Cross( e1, e2 );
                totalArea += 0.5 * std::sqrt(
                    math::Vec3d_LengthSquared( cross ) );

                hCurr = pCurr->hNext;
            }
            return true;
        } );

    return totalArea;
}

math::vec3d_t EditableMesh_Centroid(
    const editable_mesh_t *pMesh ) noexcept
{
    math::vec3d_t sum = math::Vec3d_Make( 0.0, 0.0, 0.0 );
    if ( pMesh == nullptr ) { return sum; }

    usize count = 0u;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> bool_t {
            sum = math::Vec3d_Add( sum, v.position );
            ++count;
            return true;
        } );

    if ( count > 0u ) {
        sum = math::Vec3d_Scale( sum, 1.0 / static_cast<f64>( count ) );
    }
    return sum;
}

bool EditableMesh_BoundingSphere(
    const editable_mesh_t *pMesh,
    mesh_bounding_sphere_t *pSphereOut ) noexcept
{
    if ( pMesh == nullptr || pSphereOut == nullptr ) { return false; }
    if ( EditableMesh_VertexCount( pMesh ) == 0u ) { return false; }

    const math::vec3d_t center = EditableMesh_Centroid( pMesh );
    f64 maxDistSq = 0.0;

    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> bool_t {
            const math::vec3d_t diff = math::Vec3d_Subtract(
                v.position, center );
            const f64 distSq = math::Vec3d_LengthSquared( diff );
            if ( distSq > maxDistSq ) {
                maxDistSq = distSq;
            }
            return true;
        } );

    pSphereOut->center = center;
    pSphereOut->radius = std::sqrt( maxDistSq );
    return true;
}

bool EditableMesh_BoundingBox(
    const editable_mesh_t *pMesh,
    math::vec3d_t *pMinOut,
    math::vec3d_t *pMaxOut ) noexcept
{
    if ( pMesh == nullptr || pMinOut == nullptr || pMaxOut == nullptr ) {
        return false;
    }
    if ( EditableMesh_VertexCount( pMesh ) == 0u ) { return false; }

    constexpr f64 kHuge = 1.0e18;
    math::vec3d_t vMin = math::Vec3d_Make( kHuge, kHuge, kHuge );
    math::vec3d_t vMax = math::Vec3d_Make( -kHuge, -kHuge, -kHuge );

    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &v ) noexcept -> bool_t {
            if ( v.position.x < vMin.x ) { vMin.x = v.position.x; }
            if ( v.position.y < vMin.y ) { vMin.y = v.position.y; }
            if ( v.position.z < vMin.z ) { vMin.z = v.position.z; }
            if ( v.position.x > vMax.x ) { vMax.x = v.position.x; }
            if ( v.position.y > vMax.y ) { vMax.y = v.position.y; }
            if ( v.position.z > vMax.z ) { vMax.z = v.position.z; }
            return true;
        } );

    *pMinOut = vMin;
    *pMaxOut = vMax;
    return true;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

const char *EditableMesh_StatusName(
    geometry_status_t status ) noexcept
{
    switch ( status ) {
        case geometry_status_t::OK:                   return "OK";
        case geometry_status_t::INVALID_ARGUMENT:     return "INVALID_ARGUMENT";
        case geometry_status_t::NOT_INITIALIZED:      return "NOT_INITIALIZED";
        case geometry_status_t::ALREADY_INITIALIZED:  return "ALREADY_INITIALIZED";
        case geometry_status_t::CORRUPT_STATE:        return "CORRUPT_STATE";
        case geometry_status_t::INVALID_HANDLE:       return "INVALID_HANDLE";
        case geometry_status_t::STALE_HANDLE:         return "STALE_HANDLE";
        case geometry_status_t::INVALID_TOPOLOGY:     return "INVALID_TOPOLOGY";
        case geometry_status_t::NON_MANIFOLD:         return "NON_MANIFOLD";
        case geometry_status_t::DEGENERATE:           return "DEGENERATE";
        default:                                      return "UNKNOWN";
    }
}

} // namespace cypher::editor::geometry
