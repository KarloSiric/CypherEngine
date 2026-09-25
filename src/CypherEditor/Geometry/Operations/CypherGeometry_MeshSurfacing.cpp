//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSurfacing.cpp
//  Purpose: Implements mesh UV projection, material/smoothing assignment,
//           and hard-edge marking.
//  Details: Every multi-element write is two-phase: compute all values
//           first, then write. The writes can only fail on allocation, and
//           the store is pre-grown to the mesh's pool capacity before the
//           first write so even that cannot happen mid-way.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSurfacing.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr f64 kPi = 3.14159265358979323846;
constexpr f64 kMinUvScale = 1.0e-12;

// Visits the selected faces (all when the span is empty). Stale handles in
// the selection are reported instead of skipped: a stale selection is a
// host bug that would otherwise silently surface nothing.
template <typename fn_t>
geometry_status_t ForFaces( const editable_mesh_t *pMesh,
                            span_t<const geometry_mesh_face_handle_t> faces, fn_t &&fn ) noexcept
{
    if ( faces.nCount == 0u ) {
        bool ok = true;
        (void)GenerationPool_ForEach( &pMesh->faces,
            [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t &f ) noexcept -> bool_t {
                ok = fn( h, f );
                return ok;
            } );
        return ok ? geometry_status_t::OK : geometry_status_t::CORRUPT_STATE;
    }
    if ( faces.pData == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, faces.pData[i] );
        if ( pF == nullptr ) { return geometry_status_t::STALE_HANDLE; }
        if ( !fn( faces.pData[i], *pF ) ) { return geometry_status_t::CORRUPT_STATE; }
    }
    return geometry_status_t::OK;
}

// Visits each corner (half-edge, position) of a face in loop order.
template <typename fn_t>
bool ForCorners( const editable_mesh_t *pMesh, const mesh_face_record_t &f, fn_t &&fn ) noexcept
{
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
    if ( pL == nullptr ) { return false; }
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
        const mesh_vertex_record_t *pV = pH ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin ) : nullptr;
        if ( pV == nullptr ) { return false; }
        fn( k, h, pV->position );
        h = pH->hNext;
    }
    return true;
}

geometry_status_t Precheck( mesh_attribute_store_t *pStore, const editable_mesh_t *pMesh ) noexcept
{
    if ( pStore == nullptr || pMesh == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !MeshAttributeStore_IsInitialized( pStore ) || !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    // Pre-grow every domain to pool capacity so later writes cannot fail.
    auto grow = [&]( auto *pV, usize cap ) noexcept {
        return cap == 0u || ( Vector_Reserve( pV, cap ) && ( pV->nCount >= cap || Vector_Resize( pV, cap ) ) );
    };
    if ( !grow( &pStore->corners, GenerationPool_Capacity( &pMesh->halfEdges ) ) ||
         !grow( &pStore->faces, GenerationPool_Capacity( &pMesh->faces ) ) ||
         !grow( &pStore->edges, GenerationPool_Capacity( &pMesh->edges ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void WriteUv( mesh_attribute_store_t *pStore, const editable_mesh_t *pMesh,
              geometry_mesh_half_edge_handle_t h, math::vec2d_t uv, mesh_uv_set_t set ) noexcept
{
    mesh_corner_attributes_t c = MeshAttributeStore_GetCorner( pStore, h );
    if ( set == mesh_uv_set_t::MATERIAL ) { c.uv0 = uv; } else { c.uv1 = uv; }
    (void)MeshAttributeStore_TrySetCorner( pStore, h, c, GenerationPool_Capacity( &pMesh->halfEdges ) );
}

} // namespace

geometry_status_t MeshSurfacing_TryProjectPlanar(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    const math::planar_uv_mappingd_t &mapping,
    mesh_uv_set_t uvSet ) noexcept
{
    geometry_status_t s = Precheck( pStore, pMesh );
    if ( s != geometry_status_t::OK ) { return s; }
    // Validate the mapping once by projecting its own origin.
    math::vec2d_t probe{};
    if ( !math::Uvd_TryProjectPlanarPoint( mapping, mapping.origin, kMinUvScale, &probe ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return ForFaces( pMesh, faces, [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept {
        return ForCorners( pMesh, f, [&]( u32, geometry_mesh_half_edge_handle_t h, math::vec3d_t p ) noexcept {
            math::vec2d_t uv{};
            (void)math::Uvd_TryProjectPlanarPoint( mapping, p, kMinUvScale, &uv );
            WriteUv( pStore, pMesh, h, uv, uvSet );
        } );
    } );
}

geometry_status_t MeshSurfacing_TryProjectBox(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    math::vec3d_t origin,
    math::vec2d_t worldUnitsPerUv,
    mesh_uv_set_t uvSet ) noexcept
{
    if ( !math::Vec3d_IsFinite( origin ) || !( worldUnitsPerUv.x > kMinUvScale ) ||
         !( worldUnitsPerUv.y > kMinUvScale ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_status_t s = Precheck( pStore, pMesh );
    if ( s != geometry_status_t::OK ) { return s; }

    // Six mappings, one per signed axis. Up hint: world Z for side faces,
    // world Y for top/bottom, the conventional box-mapping frame.
    math::planar_uv_mappingd_t maps[6]{};
    const math::vec3d_t normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                                       { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for ( int i = 0; i < 6; ++i ) {
        const math::vec3d_t up = i < 4 ? math::Vec3d_Make( 0, 0, 1 ) : math::Vec3d_Make( 0, 1, 0 );
        if ( !math::Uvd_TryBuildPlanarMapping( origin, normals[i], up, worldUnitsPerUv, 0.0,
                                               math::Vec2d_Make( 0, 0 ), 1.0e-12, &maps[i] ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    return ForFaces( pMesh, faces, [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept {
        const f64 ax = std::fabs( f.normal.x ), ay = std::fabs( f.normal.y ), az = std::fabs( f.normal.z );
        int m = 0;
        if ( ax >= ay && ax >= az ) { m = f.normal.x >= 0.0 ? 0 : 1; }
        else if ( ay >= az ) { m = f.normal.y >= 0.0 ? 2 : 3; }
        else { m = f.normal.z >= 0.0 ? 4 : 5; }
        return ForCorners( pMesh, f, [&]( u32, geometry_mesh_half_edge_handle_t h, math::vec3d_t p ) noexcept {
            math::vec2d_t uv{};
            (void)math::Uvd_TryProjectPlanarPoint( maps[m], p, kMinUvScale, &uv );
            WriteUv( pStore, pMesh, h, uv, uvSet );
        } );
    } );
}

geometry_status_t MeshSurfacing_TryProjectCylindrical(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    math::vec3d_t axisOrigin,
    math::vec3d_t axisDir,
    f64 radius,
    math::vec2d_t worldUnitsPerUv,
    mesh_uv_set_t uvSet ) noexcept
{
    math::vec3d_t axis{};
    if ( !math::Vec3d_IsFinite( axisOrigin ) ||
         !math::Vec3d_TryNormalize( axisDir, 1.0e-300, &axis, nullptr ) || !( radius > 0.0 ) ||
         !( worldUnitsPerUv.x > kMinUvScale ) || !( worldUnitsPerUv.y > kMinUvScale ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_status_t s = Precheck( pStore, pMesh );
    if ( s != geometry_status_t::OK ) { return s; }

    // Reference frame around the axis: e1 from the least-aligned world axis.
    const f64 ax = std::fabs( axis.x ), ay = std::fabs( axis.y ), az = std::fabs( axis.z );
    const math::vec3d_t helper = ( ax <= ay && ax <= az ) ? math::Vec3d_Make( 1, 0, 0 )
                               : ( ay <= az ) ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
    math::vec3d_t e1{};
    (void)math::Vec3d_TryNormalize( math::Vec3d_Cross( axis, helper ), 1.0e-300, &e1, nullptr );
    const math::vec3d_t e2 = math::Vec3d_Cross( axis, e1 );
    const f64 circumferenceScale = radius / worldUnitsPerUv.x; // UV per radian

    return ForFaces( pMesh, faces, [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept {
        f64 firstAngle = 0.0;
        return ForCorners( pMesh, f, [&]( u32 k, geometry_mesh_half_edge_handle_t h, math::vec3d_t p ) noexcept {
            const math::vec3d_t r = math::Vec3d_Subtract( p, axisOrigin );
            f64 angle = std::atan2( math::Vec3d_Dot( r, e2 ), math::Vec3d_Dot( r, e1 ) );
            if ( k == 0u ) {
                firstAngle = angle;
            } else {
                // Unwrap relative to the face's first corner (see header).
                while ( angle - firstAngle > kPi ) { angle -= 2.0 * kPi; }
                while ( angle - firstAngle < -kPi ) { angle += 2.0 * kPi; }
            }
            const math::vec2d_t uv = math::Vec2d_Make(
                angle * circumferenceScale, math::Vec3d_Dot( r, axis ) / worldUnitsPerUv.y );
            WriteUv( pStore, pMesh, h, uv, uvSet );
        } );
    } );
}

geometry_status_t MeshSurfacing_TryAssignMaterial(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    geometry_material_ref_t material ) noexcept
{
    geometry_status_t s = Precheck( pStore, pMesh );
    if ( s != geometry_status_t::OK ) { return s; }
    return ForFaces( pMesh, faces, [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept {
        mesh_face_attributes_t a = MeshAttributeStore_GetFace( pStore, h );
        a.material = material;
        return MeshAttributeStore_TrySetFace( pStore, h, a, GenerationPool_Capacity( &pMesh->faces ) ) ==
               geometry_status_t::OK;
    } );
}

geometry_status_t MeshSurfacing_TrySetSmoothingGroups(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    u32 smoothingGroups ) noexcept
{
    geometry_status_t s = Precheck( pStore, pMesh );
    if ( s != geometry_status_t::OK ) { return s; }
    return ForFaces( pMesh, faces, [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t & ) noexcept {
        mesh_face_attributes_t a = MeshAttributeStore_GetFace( pStore, h );
        a.smoothingGroups = smoothingGroups;
        return MeshAttributeStore_TrySetFace( pStore, h, a, GenerationPool_Capacity( &pMesh->faces ) ) ==
               geometry_status_t::OK;
    } );
}

geometry_status_t MeshSurfacing_TryMarkHardEdgesByAngle(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    f64 fAngleRadians,
    u32 *pcHardOut ) noexcept
{
    if ( !( fAngleRadians >= 0.0 ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t s = Precheck( pStore, pMesh );
    if ( s != geometry_status_t::OK ) { return s; }
    const f64 cosLimit = std::cos( fAngleRadians );
    u32 cHard = 0u;
    bool ok = true;
    (void)GenerationPool_ForEach( &pMesh->edges,
        [&]( geometry_mesh_edge_handle_t hE, const mesh_edge_record_t &e ) noexcept -> bool_t {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, e.hHalfEdge );
            if ( pH == nullptr ) { ok = false; return false; }
            const mesh_half_edge_record_t *pT = GenerationPool_Get( &pMesh->halfEdges, pH->hTwin );
            bool hard = true; // boundary edges are always hard
            if ( pT != nullptr ) {
                const mesh_loop_record_t *pLa = GenerationPool_Get( &pMesh->loops, pH->hLoop );
                const mesh_loop_record_t *pLb = GenerationPool_Get( &pMesh->loops, pT->hLoop );
                const mesh_face_record_t *pFa = pLa ? GenerationPool_Get( &pMesh->faces, pLa->hFace ) : nullptr;
                const mesh_face_record_t *pFb = pLb ? GenerationPool_Get( &pMesh->faces, pLb->hFace ) : nullptr;
                if ( pFa == nullptr || pFb == nullptr ) { ok = false; return false; }
                // Dihedral angle between normals: hard when it exceeds the limit,
                // i.e. when the cosine drops below cos(limit).
                hard = math::Vec3d_Dot( pFa->normal, pFb->normal ) < cosLimit;
            }
            mesh_edge_attributes_t a = MeshAttributeStore_GetEdge( pStore, hE );
            a.flags = hard ? static_cast<u8>( a.flags | MESH_EDGE_FLAG_HARD )
                           : static_cast<u8>( a.flags & ~MESH_EDGE_FLAG_HARD );
            (void)MeshAttributeStore_TrySetEdge( pStore, hE, a, GenerationPool_Capacity( &pMesh->edges ) );
            cHard += hard ? 1u : 0u;
            return true;
        } );
    if ( !ok ) { return geometry_status_t::CORRUPT_STATE; }
    if ( pcHardOut ) { *pcHardOut = cHard; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
