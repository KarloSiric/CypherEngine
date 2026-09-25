//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Cook_RenderMesh.cpp
//  Purpose: Implements the EditableMesh render-mesh cook.
//  Details: Seam expansion is a sort over corner keys rather than a hash
//           map: sorting gives a canonical, platform-independent grouping,
//           and render vertices are then numbered by first appearance in
//           triangle order, so the output order is a pure function of the
//           source mesh.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Cook_RenderMesh.h"
#include "CypherGeometry_MeshTessellation.h"
#include "CypherGeometry_MeshCornerNormals.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename type_t>
struct buf_t {
    vector_t<type_t> v{};
    bool ok{ true };
    bool Init( const allocator_t *a, usize cap ) noexcept { return ok = Vector_Init( &v, a, cap ); }
    ~buf_t() { Vector_Shutdown( &v ); }
    void Push( const type_t &x ) noexcept { if ( ok && !Vector_PushBack( &v, x ) ) { ok = false; } }
    bool Resize( usize n ) noexcept { if ( ok && !Vector_Resize( &v, n ) ) { ok = false; } return ok; }
    usize Size() const noexcept { return v.nCount; }
    type_t &operator[]( usize i ) noexcept { return v.pData[i]; }
    type_t *Data() noexcept { return v.pData; }
};

struct corner_key_t {
    u32 vertex{ 0u };
    u32 group{ 0u };
    f64 u0{ 0.0 }, v0{ 0.0 }, u1{ 0.0 }, v1{ 0.0 };
    u32 color{ 0u };
    u32 pos{ 0u };       // position in triangle-corner order
};

bool KeyLess( const corner_key_t &a, const corner_key_t &b ) noexcept
{
    if ( a.vertex != b.vertex ) { return a.vertex < b.vertex; }
    if ( a.group != b.group ) { return a.group < b.group; }
    if ( a.u0 != b.u0 ) { return a.u0 < b.u0; }
    if ( a.v0 != b.v0 ) { return a.v0 < b.v0; }
    if ( a.u1 != b.u1 ) { return a.u1 < b.u1; }
    if ( a.v1 != b.v1 ) { return a.v1 < b.v1; }
    if ( a.color != b.color ) { return a.color < b.color; }
    return a.pos < b.pos;
}

bool KeyEqual( const corner_key_t &a, const corner_key_t &b ) noexcept
{
    return a.vertex == b.vertex && a.group == b.group && a.u0 == b.u0 && a.v0 == b.v0 &&
           a.u1 == b.u1 && a.v1 == b.v1 && a.color == b.color;
}

bool FitsF32( f64 x ) noexcept
{
    return std::isfinite( x ) && std::fabs( x ) <= static_cast<f64>( FLT_MAX );
}

void ClearAll( render_mesh_t *p ) noexcept
{
    Vector_Clear( &p->vertices );
    Vector_Clear( &p->indices );
    Vector_Clear( &p->batches );
    Vector_Clear( &p->triangleFace );
    for ( int i = 0; i < 3; ++i ) { p->boundsMin[i] = 0.0f; p->boundsMax[i] = 0.0f; }
    p->contentHash = content_hash_t{};
}

// Canonical little-endian byte writer for hashing.
struct le_writer_t {
    buf_t<byte> b;
    void U32( u32 x ) noexcept {
        for ( int i = 0; i < 4; ++i ) { b.Push( static_cast<byte>( ( x >> ( 8 * i ) ) & 0xFFu ) ); }
    }
    void U64( u64 x ) noexcept { U32( static_cast<u32>( x ) ); U32( static_cast<u32>( x >> 32 ) ); }
    void F32( f32 x ) noexcept {
        u32 bits = 0u;
        std::memcpy( &bits, &x, sizeof( bits ) );
        U32( bits );
    }
};

} // namespace

geometry_status_t RenderMesh_Init( render_mesh_t *pOut, const allocator_t *pAllocator ) noexcept
{
    if ( pOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pOut->vertices.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !Vector_Init( &pOut->vertices, pAllocator ) || !Vector_Init( &pOut->indices, pAllocator ) ||
         !Vector_Init( &pOut->batches, pAllocator ) || !Vector_Init( &pOut->triangleFace, pAllocator ) ) {
        RenderMesh_Shutdown( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void RenderMesh_Shutdown( render_mesh_t *pOut ) noexcept
{
    if ( pOut == nullptr ) { return; }
    Vector_Shutdown( &pOut->vertices );
    Vector_Shutdown( &pOut->indices );
    Vector_Shutdown( &pOut->batches );
    Vector_Shutdown( &pOut->triangleFace );
}

geometry_status_t RenderMesh_TryCook(
    const editable_mesh_t *pMesh,
    const mesh_attribute_store_t *pStore,
    render_mesh_t *pOut ) noexcept
{
    if ( pMesh == nullptr || pOut == nullptr || pOut->vertices.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !EditableMesh_IsInitialized( pMesh ) ||
         ( pStore != nullptr && !MeshAttributeStore_IsInitialized( pStore ) ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    ClearAll( pOut );
    const allocator_t *pAlloc = pOut->vertices.pAllocator;
    auto fail = [&]( geometry_status_t s ) noexcept { ClearAll( pOut ); return s; };

    // ---- 0. Binary32 representability first ------------------------------------
    // Checked before tessellation: coordinates beyond float range also
    // overflow the tessellator's predicates, which would misreport the
    // problem as a degenerate face.
    {
        bool fits = true;
        (void)GenerationPool_ForEach( &pMesh->vertices,
            [&]( geometry_mesh_vertex_handle_t, const mesh_vertex_record_t &v ) noexcept -> bool_t {
                fits = FitsF32( v.position.x ) && FitsF32( v.position.y ) && FitsF32( v.position.z );
                return fits;
            } );
        if ( !fits ) { return fail( geometry_status_t::NUMERIC_FAILURE ); }
    }

    // ---- 1. Tessellation and split normals -------------------------------------
    mesh_tessellation_t tess{};
    vector_t<mesh_corner_normal_record_t> normals{};
    struct guard_t {
        mesh_tessellation_t *t; vector_t<mesh_corner_normal_record_t> *n;
        ~guard_t() { MeshTessellation_Shutdown( t ); Vector_Shutdown( n ); }
    } guard{ &tess, &normals };
    geometry_status_t s = MeshTessellation_Init( &tess, pAlloc );
    if ( s != geometry_status_t::OK ) { return fail( s ); }
    s = MeshTessellation_TryBuild( pMesh, &tess, nullptr );
    if ( s != geometry_status_t::OK ) { return fail( s ); }
    if ( !Vector_Init( &normals, pAlloc ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    s = MeshNormals_TryComputeCornerNormals( pMesh, pStore, &normals );
    if ( s != geometry_status_t::OK ) { return fail( s ); }

    const usize cHeSlots = GenerationPool_Capacity( &pMesh->halfEdges );
    buf_t<u32> normalOfSlot;
    if ( !normalOfSlot.Init( pAlloc, cHeSlots ) || !normalOfSlot.Resize( cHeSlots ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < normals.nCount; ++i ) {
        normalOfSlot[normals.pData[i].hCorner.nSlot] = static_cast<u32>( i );
    }

    // ---- 2. Seam expansion -------------------------------------------------------
    const usize cCorners = tess.indices.nCount;
    const usize cTris = cCorners / 3u;
    buf_t<corner_key_t> keys;
    if ( !keys.Init( pAlloc, cCorners ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    for ( usize k = 0u; k < cCorners; ++k ) {
        const geometry_mesh_half_edge_handle_t hc = tess.triangleCorners.pData[k];
        const mesh_corner_attributes_t a = MeshAttributeStore_GetCorner( pStore, hc );
        if ( !FitsF32( a.uv0.x ) || !FitsF32( a.uv0.y ) || !FitsF32( a.uv1.x ) || !FitsF32( a.uv1.y ) ) {
            return fail( geometry_status_t::NUMERIC_FAILURE );
        }
        corner_key_t key{};
        key.vertex = tess.indices.pData[k];
        key.group = normals.pData[normalOfSlot[hc.nSlot]].iSmoothGroupId;
        key.u0 = a.uv0.x; key.v0 = a.uv0.y; key.u1 = a.uv1.x; key.v1 = a.uv1.y;
        key.color = a.colorRgba;
        key.pos = static_cast<u32>( k );
        keys.Push( key );
    }
    if ( !keys.ok ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    buf_t<corner_key_t> sorted;
    buf_t<u32> repOf, renderOf;
    if ( !sorted.Init( pAlloc, cCorners ) || !repOf.Init( pAlloc, cCorners ) || !repOf.Resize( cCorners ) ||
         !renderOf.Init( pAlloc, cCorners ) || !renderOf.Resize( cCorners ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize k = 0u; k < cCorners; ++k ) { sorted.Push( keys[k] ); }
    std::sort( sorted.Data(), sorted.Data() + cCorners, KeyLess );
    for ( usize i = 0u; i < cCorners; ) {
        usize j = i + 1u;
        while ( j < cCorners && KeyEqual( sorted[i], sorted[j] ) ) { ++j; }
        // pos is the final sort key, so sorted[i] holds the smallest pos.
        for ( usize m = i; m < j; ++m ) { repOf[sorted[m].pos] = sorted[i].pos; }
        i = j;
    }

    // Render vertices in order of first appearance.
    if ( !Vector_Reserve( &pOut->vertices, cCorners ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    buf_t<math::vec3d_t> tanAcc, bitAcc;
    buf_t<math::vec3d_t> posD;
    buf_t<math::vec2d_t> uvD;
    if ( !tanAcc.Init( pAlloc, cCorners ) || !bitAcc.Init( pAlloc, cCorners ) ||
         !posD.Init( pAlloc, cCorners ) || !uvD.Init( pAlloc, cCorners ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize k = 0u; k < cCorners; ++k ) {
        if ( repOf[k] != k ) { renderOf[k] = renderOf[repOf[k]]; continue; }
        const corner_key_t &key = keys[k];
        const math::vec3d_t p = tess.positions.pData[key.vertex];
        if ( !FitsF32( p.x ) || !FitsF32( p.y ) || !FitsF32( p.z ) ) {
            return fail( geometry_status_t::NUMERIC_FAILURE );
        }
        const math::vec3d_t n = normals.pData[normalOfSlot[tess.triangleCorners.pData[k].nSlot]].normal;
        render_vertex_t rv{};
        rv.position[0] = static_cast<f32>( p.x );
        rv.position[1] = static_cast<f32>( p.y );
        rv.position[2] = static_cast<f32>( p.z );
        rv.normal[0] = static_cast<f32>( n.x );
        rv.normal[1] = static_cast<f32>( n.y );
        rv.normal[2] = static_cast<f32>( n.z );
        rv.uv0[0] = static_cast<f32>( key.u0 );
        rv.uv0[1] = static_cast<f32>( key.v0 );
        rv.uv1[0] = static_cast<f32>( key.u1 );
        rv.uv1[1] = static_cast<f32>( key.v1 );
        rv.colorRgba = key.color;
        renderOf[k] = static_cast<u32>( pOut->vertices.nCount );
        (void)Vector_PushBack( &pOut->vertices, rv );
        tanAcc.Push( math::Vec3d_Make( 0, 0, 0 ) );
        bitAcc.Push( math::Vec3d_Make( 0, 0, 0 ) );
        posD.Push( p );
        uvD.Push( math::Vec2d_Make( key.u0, key.v0 ) );
    }
    if ( !tanAcc.ok || !bitAcc.ok || !posD.ok || !uvD.ok ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }

    // ---- 3. Tangents ------------------------------------------------------------------
    for ( usize t = 0u; t < cTris; ++t ) {
        const u32 r0 = renderOf[3u * t], r1 = renderOf[3u * t + 1u], r2 = renderOf[3u * t + 2u];
        const math::vec3d_t e1 = math::Vec3d_Subtract( posD[r1], posD[r0] );
        const math::vec3d_t e2 = math::Vec3d_Subtract( posD[r2], posD[r0] );
        const math::vec2d_t d1 = math::Vec2d_Subtract( uvD[r1], uvD[r0] );
        const math::vec2d_t d2 = math::Vec2d_Subtract( uvD[r2], uvD[r0] );
        const f64 det = d1.x * d2.y - d2.x * d1.y;
        if ( !( std::fabs( det ) > 1.0e-30 ) ) { continue; } // no UV parameterization on this triangle
        const f64 inv = 1.0 / det;
        const math::vec3d_t tg = math::Vec3d_Scale(
            math::Vec3d_Subtract( math::Vec3d_Scale( e1, d2.y ), math::Vec3d_Scale( e2, d1.y ) ), inv );
        const math::vec3d_t bt = math::Vec3d_Scale(
            math::Vec3d_Subtract( math::Vec3d_Scale( e2, d1.x ), math::Vec3d_Scale( e1, d2.x ) ), inv );
        const u32 rs[3] = { r0, r1, r2 };
        for ( u32 r : rs ) {
            tanAcc[r] = math::Vec3d_Add( tanAcc[r], tg );
            bitAcc[r] = math::Vec3d_Add( bitAcc[r], bt );
        }
    }
    for ( usize r = 0u; r < pOut->vertices.nCount; ++r ) {
        render_vertex_t &rv = pOut->vertices.pData[r];
        const math::vec3d_t n = math::Vec3d_Make( rv.normal[0], rv.normal[1], rv.normal[2] );
        math::vec3d_t t = math::Vec3d_Subtract( tanAcc[r], math::Vec3d_Scale( n, math::Vec3d_Dot( n, tanAcc[r] ) ) );
        math::vec3d_t tu{};
        if ( !math::Vec3d_TryNormalize( t, 1.0e-20, &tu, nullptr ) ) {
            // No usable UV gradient: any unit vector perpendicular to n,
            // chosen deterministically from the least-aligned world axis.
            const f64 ax = std::fabs( n.x ), ay = std::fabs( n.y ), az = std::fabs( n.z );
            const math::vec3d_t h = ( ax <= ay && ax <= az ) ? math::Vec3d_Make( 1, 0, 0 )
                                  : ( ay <= az ) ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
            (void)math::Vec3d_TryNormalize( math::Vec3d_Cross( h, n ), 1.0e-300, &tu, nullptr );
        }
        const f64 w = math::Vec3d_Dot( math::Vec3d_Cross( n, tu ), bitAcc[r] ) < 0.0 ? -1.0 : 1.0;
        rv.tangent[0] = static_cast<f32>( tu.x );
        rv.tangent[1] = static_cast<f32>( tu.y );
        rv.tangent[2] = static_cast<f32>( tu.z );
        rv.tangent[3] = static_cast<f32>( w );
    }

    // ---- 4. Material batches ---------------------------------------------------------
    buf_t<u32> triOrder;
    if ( !triOrder.Init( pAlloc, cTris ) || !triOrder.Resize( cTris ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    buf_t<u64> triMaterial;
    if ( !triMaterial.Init( pAlloc, cTris ) || !triMaterial.Resize( cTris ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize t = 0u; t < cTris; ++t ) {
        triOrder[t] = static_cast<u32>( t );
        triMaterial[t] = MeshAttributeStore_GetFace( pStore, tess.triangleFace.pData[t] ).material.value;
    }
    u64 *tm = triMaterial.Data();
    std::stable_sort( triOrder.Data(), triOrder.Data() + cTris,
                      [tm]( u32 a, u32 b ) { return tm[a] < tm[b]; } );
    if ( !Vector_Reserve( &pOut->indices, cCorners ) || !Vector_Reserve( &pOut->triangleFace, cTris ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    for ( usize i = 0u; i < cTris; ++i ) {
        const u32 t = triOrder[i];
        const u64 mat = triMaterial[t];
        if ( pOut->batches.nCount == 0u ||
             pOut->batches.pData[pOut->batches.nCount - 1u].material.value != mat ) {
            render_batch_t b{};
            b.material = geometry_material_ref_t{ mat };
            b.iFirstIndex = static_cast<u32>( pOut->indices.nCount );
            if ( !Vector_PushBack( &pOut->batches, b ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
        }
        for ( u32 c = 0u; c < 3u; ++c ) { (void)Vector_PushBack( &pOut->indices, renderOf[3u * t + c] ); }
        (void)Vector_PushBack( &pOut->triangleFace, tess.triangleFace.pData[t] );
        pOut->batches.pData[pOut->batches.nCount - 1u].cIndices += 3u;
    }

    // ---- 5. Bounds and hash ---------------------------------------------------------------
    for ( usize r = 0u; r < pOut->vertices.nCount; ++r ) {
        for ( int a = 0; a < 3; ++a ) {
            const f32 v = pOut->vertices.pData[r].position[a];
            if ( r == 0u || v < pOut->boundsMin[a] ) { pOut->boundsMin[a] = v; }
            if ( r == 0u || v > pOut->boundsMax[a] ) { pOut->boundsMax[a] = v; }
        }
    }
    le_writer_t w;
    if ( !w.b.Init( pAlloc, 64u + pOut->vertices.nCount * 64u + pOut->indices.nCount * 4u ) ) {
        return fail( geometry_status_t::ALLOCATION_FAILED );
    }
    w.U32( kRenderMeshCookVersion );
    w.U32( static_cast<u32>( pOut->vertices.nCount ) );
    w.U32( static_cast<u32>( pOut->indices.nCount ) );
    w.U32( static_cast<u32>( pOut->batches.nCount ) );
    for ( usize r = 0u; r < pOut->vertices.nCount; ++r ) {
        const render_vertex_t &v = pOut->vertices.pData[r];
        for ( f32 x : v.position ) { w.F32( x ); }
        for ( f32 x : v.normal ) { w.F32( x ); }
        for ( f32 x : v.tangent ) { w.F32( x ); }
        for ( f32 x : v.uv0 ) { w.F32( x ); }
        for ( f32 x : v.uv1 ) { w.F32( x ); }
        w.U32( v.colorRgba );
    }
    for ( usize i = 0u; i < pOut->indices.nCount; ++i ) { w.U32( pOut->indices.pData[i] ); }
    for ( usize i = 0u; i < pOut->batches.nCount; ++i ) {
        w.U64( pOut->batches.pData[i].material.value );
        w.U32( pOut->batches.pData[i].iFirstIndex );
        w.U32( pOut->batches.pData[i].cIndices );
    }
    for ( int a = 0; a < 3; ++a ) { w.F32( pOut->boundsMin[a] ); w.F32( pOut->boundsMax[a] ); }
    if ( !w.b.ok ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    pOut->contentHash = ContentHash_Data( binary_block_t{ w.b.Data(), w.b.Size() } );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
