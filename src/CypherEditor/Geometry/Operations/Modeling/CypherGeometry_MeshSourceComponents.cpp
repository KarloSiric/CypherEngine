//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceComponents.cpp
//  Purpose: Implements component transform and offset on mesh sources.
//  Details: Both compute every target position first (checking the source
//           coordinate domain), then make one MeshVertices_TryMove call,
//           which validates all affected faces before writing anything.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceComponents.h"

#include "CypherGeometry_MeshSelectionQueries.h"
#include "CypherGeometry_MeshVertexMove.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// A new vertex position the source can store.
bool InSourceDomain( math::vec3d_t p ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= kMeshSourceCoordinateMax &&
           std::fabs( p.y ) <= kMeshSourceCoordinateMax && std::fabs( p.z ) <= kMeshSourceCoordinateMax;
}

// Moves `verts` to `targets` (parallel), after the domain check.
geometry_status_t MoveAll(
    mesh_source_t *pSource,
    const vector_t<geometry_mesh_vertex_handle_t> &verts,
    const vector_t<math::vec3d_t> &targets ) noexcept
{
    for ( usize i = 0u; i < targets.nCount; ++i ) {
        if ( !InSourceDomain( targets.pData[i] ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    return MeshVertices_TryMove( &pSource->mesh, span_t<const geometry_mesh_vertex_handle_t>{ verts.pData, verts.nCount },
                                 span_t<const math::vec3d_t>{ targets.pData, targets.nCount }, nullptr );
}

// Unit eigenvector of the smallest eigenvalue of a symmetric 3x3 matrix
// (row-major a[3][3]), by cyclic Jacobi rotations - a handful of sweeps is
// exact to rounding for 3x3. Returns false for a non-finite matrix.
bool SmallestEigenvector( f64 a[3][3], math::vec3d_t *pOut ) noexcept
{
    f64 v[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    for ( int sweep = 0; sweep < 32; ++sweep ) {
        const f64 off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
        if ( !std::isfinite( off ) ) { return false; }
        if ( off == 0.0 ) { break; }
        for ( int p = 0; p < 2; ++p ) {
            for ( int q = p + 1; q < 3; ++q ) {
                if ( a[p][q] == 0.0 ) { continue; }
                const f64 theta = ( a[q][q] - a[p][p] ) / ( 2.0 * a[p][q] );
                const f64 t = ( theta >= 0.0 ? 1.0 : -1.0 ) / ( std::fabs( theta ) + std::sqrt( theta * theta + 1.0 ) );
                const f64 c = 1.0 / std::sqrt( t * t + 1.0 ), s = t * c;
                for ( int k = 0; k < 3; ++k ) { // A = J^T A J
                    const f64 akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for ( int k = 0; k < 3; ++k ) {
                    const f64 apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for ( int k = 0; k < 3; ++k ) {
                    const f64 vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    int iMin = 0;
    for ( int i = 1; i < 3; ++i ) {
        if ( a[i][i] < a[iMin][iMin] ) { iMin = i; }
    }
    *pOut = math::Vec3d_Make( v[0][iMin], v[1][iMin], v[2][iMin] );
    return true;
}

} // namespace

geometry_status_t MeshSourceEdit_TryTransformComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    const math::affine3d_t &transform ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) || pSelection == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    for ( f64 v : transform.m ) {
        if ( !std::isfinite( v ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    const math::vec3d_t c0 = math::Affine3d_Column( transform, 0u ), c1 = math::Affine3d_Column( transform, 1u ),
                        c2 = math::Affine3d_Column( transform, 2u );
    if ( !( math::Vec3d_Dot( c0, math::Vec3d_Cross( c1, c2 ) ) > 0.0 ) ) { return geometry_status_t::UNSUPPORTED; }
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    vector_t<geometry_mesh_vertex_handle_t> verts{};
    vector_t<math::vec3d_t> targets{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &verts );
        Vector_Shutdown( &targets );
    };
    if ( !Vector_Init( &verts, pA ) || !Vector_Init( &targets, pA ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t st = MeshSelection_TryGatherVertices( pSelection, pSource, mode, &verts );
    if ( st == geometry_status_t::OK && verts.nCount > 0u ) {
        if ( !Vector_Resize( &targets, verts.nCount ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
        } else {
            for ( usize i = 0u; i < verts.nCount; ++i ) {
                targets.pData[i] =
                    math::Affine3d_TransformPoint( transform, GenerationPool_Get( &pSource->mesh.vertices, verts.pData[i] )->position );
            }
            st = MoveAll( pSource, verts, targets );
        }
    }
    cleanup();
    return st;
}

geometry_status_t MeshSourceEdit_TryOffsetComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    f64 distance ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) || pSelection == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !std::isfinite( distance ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    const editable_mesh_t *pM = &pSource->mesh;
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    vector_t<geometry_mesh_vertex_handle_t> verts{};
    vector_t<math::vec3d_t> targets{}, normalSum{};
    vector_t<u32> slotToIndex{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &verts );
        Vector_Shutdown( &targets );
        Vector_Shutdown( &normalSum );
        Vector_Shutdown( &slotToIndex );
    };
    if ( !Vector_Init( &verts, pA ) || !Vector_Init( &targets, pA ) || !Vector_Init( &normalSum, pA ) ||
         !Vector_Init( &slotToIndex, pA ) || !Vector_Resize( &slotToIndex, pM->vertices.cSlots ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t st = MeshSelection_TryGatherVertices( pSelection, pSource, mode, &verts );
    if ( st != geometry_status_t::OK || verts.nCount == 0u ) {
        cleanup();
        return st;
    }
    if ( !Vector_Resize( &targets, verts.nCount ) || !Vector_Resize( &normalSum, verts.nCount ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < slotToIndex.nCount; ++i ) { slotToIndex.pData[i] = CY_INVALID_INDEX; }
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        slotToIndex.pData[verts.pData[i].nSlot] = static_cast<u32>( i );
        normalSum.pData[i] = math::vec3d_t{};
    }
    // Sum the normals of the contributing faces at each gathered vertex.
    (void)GenerationPool_ForEach( &pM->faces,
        [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
            if ( mode == mesh_selection_mode_t::FACE && !MeshSelection_HasFace( pSelection, MeshSource_FaceId( pSource, hF ) ) ) {
                return true;
            }
            const mesh_loop_record_t *pL = GenerationPool_Get( &pM->loops, f.hOuterLoop );
            geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
            for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
                const mesh_half_edge_record_t *pH = GenerationPool_Get( &pM->halfEdges, h );
                const u32 idx = slotToIndex.pData[pH->hOrigin.nSlot];
                if ( idx != CY_INVALID_INDEX && verts.pData[idx].nGeneration == pH->hOrigin.nGeneration ) {
                    normalSum.pData[idx] = math::Vec3d_Add( normalSum.pData[idx], f.normal );
                }
                h = pH->hNext;
            }
            return true;
        } );
    for ( usize i = 0u; i < verts.nCount && st == geometry_status_t::OK; ++i ) {
        math::vec3d_t dir{};
        if ( !math::Vec3d_TryNormalize( normalSum.pData[i], 0.0, &dir, nullptr ) ) {
            st = geometry_status_t::DEGENERATE;
            break;
        }
        targets.pData[i] = math::Vec3d_Add( GenerationPool_Get( &pM->vertices, verts.pData[i] )->position, math::Vec3d_Scale( dir, distance ) );
    }
    if ( st == geometry_status_t::OK ) { st = MoveAll( pSource, verts, targets ); }
    cleanup();
    return st;
}

geometry_status_t MeshSourceEdit_TryFlattenComponents(
    mesh_source_t *pSource,
    const mesh_selection_t *pSelection,
    mesh_selection_mode_t mode,
    mesh_flatten_plane_t plane,
    math::planed_t *pPlaneOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) || pSelection == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( plane > mesh_flatten_plane_t::AXIS_Z ) { return geometry_status_t::INVALID_ARGUMENT; }
    const allocator_t *pA = pSource->attributes.faces.pAllocator;
    const editable_mesh_t *pMesh = &pSource->mesh;
    vector_t<geometry_mesh_vertex_handle_t> verts{};
    vector_t<math::vec3d_t> targets{};
    if ( !Vector_Init( &verts, pA ) || !Vector_Init( &targets, pA ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    geometry_status_t st = MeshSelection_TryGatherVertices( pSelection, pSource, mode, &verts );
    if ( st != geometry_status_t::OK || verts.nCount == 0u ) { return st; }

    // Centroid, relative to the first vertex to limit cancellation.
    const math::vec3d_t base = GenerationPool_Get( &pMesh->vertices, verts.pData[0] )->position;
    math::vec3d_t sum{};
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        sum = math::Vec3d_Add( sum, math::Vec3d_Subtract( GenerationPool_Get( &pMesh->vertices, verts.pData[i] )->position, base ) );
    }
    const math::vec3d_t centroid = math::Vec3d_Add( base, math::Vec3d_Scale( sum, 1.0 / static_cast<f64>( verts.nCount ) ) );

    math::vec3d_t normal{};
    switch ( plane ) {
        case mesh_flatten_plane_t::AXIS_X: normal = math::Vec3d_Make( 1, 0, 0 ); break;
        case mesh_flatten_plane_t::AXIS_Y: normal = math::Vec3d_Make( 0, 1, 0 ); break;
        case mesh_flatten_plane_t::AXIS_Z: normal = math::Vec3d_Make( 0, 0, 1 ); break;
        case mesh_flatten_plane_t::BEST_FIT: {
            // Smallest principal axis of the covariance, scaled to the
            // selection's size so the flatness test is relative.
            f64 cov[3][3] = {};
            f64 extent = 0.0;
            for ( usize i = 0u; i < verts.nCount; ++i ) {
                const math::vec3d_t d = math::Vec3d_Subtract( GenerationPool_Get( &pMesh->vertices, verts.pData[i] )->position, centroid );
                const f64 c[3] = { d.x, d.y, d.z };
                for ( int r = 0; r < 3; ++r ) {
                    for ( int k = 0; k < 3; ++k ) { cov[r][k] += c[r] * c[k]; }
                }
                extent += math::Vec3d_LengthSquared( d );
            }
            if ( verts.nCount < 3u || !( extent > 0.0 ) ) { return geometry_status_t::DEGENERATE; }
            f64 work[3][3];
            for ( int r = 0; r < 3; ++r ) {
                for ( int k = 0; k < 3; ++k ) { work[r][k] = cov[r][k]; }
            }
            if ( !SmallestEigenvector( work, &normal ) ) { return geometry_status_t::NUMERIC_FAILURE; }
            // Collinear points: the two smallest eigenvalues are both ~0 and
            // the plane is not determined.
            f64 eig[3] = { work[0][0], work[1][1], work[2][2] };
            std::sort( eig, eig + 3 );
            if ( !( eig[1] > 1.0e-12 * extent ) ) { return geometry_status_t::DEGENERATE; }
            break;
        }
        case mesh_flatten_plane_t::AVERAGE_NORMAL: {
            // The selected faces in face mode, else every face at a vertex.
            vector_t<u8> isVert{};
            if ( !Vector_Init( &isVert, pA ) || !Vector_Resize( &isVert, pMesh->vertices.cSlots ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            for ( usize i = 0u; i < isVert.nCount; ++i ) { isVert.pData[i] = 0u; }
            for ( usize i = 0u; i < verts.nCount; ++i ) { isVert.pData[verts.pData[i].nSlot] = 1u; }
            (void)GenerationPool_ForEach( &pMesh->faces, [&]( geometry_mesh_face_handle_t hF, const mesh_face_record_t &f ) noexcept -> bool_t {
                bool bUse = false;
                if ( mode == mesh_selection_mode_t::FACE ) {
                    bUse = MeshSelection_HasFace( pSelection, MeshSource_FaceId( pSource, hF ) );
                } else {
                    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
                    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
                    for ( u32 k = 0u; k < pL->cHalfEdges && !bUse; ++k ) {
                        const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
                        bUse = isVert.pData[pH->hOrigin.nSlot] != 0u;
                        h = pH->hNext;
                    }
                }
                if ( bUse ) { normal = math::Vec3d_Add( normal, f.normal ); }
                return true;
            } );
            break;
        }
    }
    math::vec3d_t unit{};
    if ( !math::Vec3d_TryNormalize( normal, 1.0e-9, &unit, nullptr ) ) { return geometry_status_t::DEGENERATE; }
    const math::planed_t result{ unit, -math::Vec3d_Dot( unit, centroid ) };
    if ( !Vector_Resize( &targets, verts.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < verts.nCount; ++i ) {
        const math::vec3d_t p = GenerationPool_Get( &pMesh->vertices, verts.pData[i] )->position;
        targets.pData[i] = math::Vec3d_Subtract( p, math::Vec3d_Scale( unit, math::Vec3d_Dot( unit, p ) + result.d ) );
    }
    st = MoveAll( pSource, verts, targets );
    if ( st == geometry_status_t::OK && pPlaneOut ) { *pPlaneOut = result; }
    return st;
}

} // namespace cypher::editor::geometry
