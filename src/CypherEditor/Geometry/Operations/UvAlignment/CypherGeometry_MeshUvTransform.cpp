//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshUvTransform.cpp
//  Purpose: Implements the face-edit UV tools.
//  Details: Every tool runs the same three steps: gather the selected
//           faces' corners (face by face, so each face is one contiguous
//           run), compute every new UV into a buffer, then write. The store
//           is grown to the mesh's capacity before the writes, so the write
//           step cannot fail and a call is all-or-nothing.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshUvTransform.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::vec2d_t;

namespace
{

constexpr f64 kMinUvExtent = 1.0e-12;

struct corner_t {
    geometry_mesh_half_edge_handle_t h{};
    math::vec3d_t position{};
    vec2d_t uv{};
};

struct face_run_t {
    geometry_mesh_face_handle_t hFace{};
    math::vec3d_t normal{};
    u32 iFirst{ 0u };
    u32 cCorners{ 0u };
};

struct gathered_t {
    vector_t<corner_t> corners{};
    vector_t<face_run_t> runs{};
};

struct bounds_t {
    vec2d_t lo{ 0.0, 0.0 };
    vec2d_t hi{ 0.0, 0.0 };
};

vec2d_t UvOf( const mesh_corner_attributes_t &c, mesh_uv_set_t set ) noexcept
{
    return set == mesh_uv_set_t::MATERIAL ? c.uv0 : c.uv1;
}

// Checks inputs, grows the store so later writes cannot fail, and gathers
// the corners of the selected faces (every face for an empty span).
geometry_status_t Gather(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    gathered_t *pOut ) noexcept
{
    if ( pStore == nullptr || pMesh == nullptr || uvSet > mesh_uv_set_t::LIGHTMAP || ( faces.nCount > 0u && faces.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !MeshAttributeStore_IsInitialized( pStore ) || !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const allocator_t *pA = pMesh->pAllocator;
    const usize cCornerCapacity = GenerationPool_Capacity( &pMesh->halfEdges );
    if ( !Vector_Init( &pOut->corners, pA ) || !Vector_Init( &pOut->runs, pA ) ||
         ( cCornerCapacity > 0u && ( !Vector_Reserve( &pStore->corners, cCornerCapacity ) ||
                                     ( pStore->corners.nCount < cCornerCapacity && !Vector_Resize( &pStore->corners, cCornerCapacity ) ) ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    vector_t<u8> seen{};
    if ( !Vector_Init( &seen, pA ) || !Vector_Resize( &seen, pMesh->faces.cSlots ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < seen.nCount; ++i ) { seen.pData[i] = 0u; }

    auto addFace = [&]( geometry_mesh_face_handle_t hFace, const mesh_face_record_t &f ) noexcept -> geometry_status_t {
        if ( seen.pData[hFace.nSlot] != 0u ) { return geometry_status_t::INVALID_ARGUMENT; }
        seen.pData[hFace.nSlot] = 1u;
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
        if ( pL == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        face_run_t run{ hFace, f.normal, static_cast<u32>( pOut->corners.nCount ), pL->cHalfEdges };
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
            const mesh_vertex_record_t *pV = pH ? GenerationPool_Get( &pMesh->vertices, pH->hOrigin ) : nullptr;
            if ( pV == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
            const corner_t c{ h, pV->position, UvOf( MeshAttributeStore_GetCorner( pStore, h ), uvSet ) };
            if ( !Vector_PushBack( &pOut->corners, c ) ) { return geometry_status_t::ALLOCATION_FAILED; }
            h = pH->hNext;
        }
        return Vector_PushBack( &pOut->runs, run ) ? geometry_status_t::OK : geometry_status_t::ALLOCATION_FAILED;
    };
    geometry_status_t st = geometry_status_t::OK;
    if ( faces.nCount == 0u ) {
        (void)GenerationPool_ForEach( &pMesh->faces,
            [&]( geometry_mesh_face_handle_t h, const mesh_face_record_t &f ) noexcept -> bool_t {
                st = addFace( h, f );
                return st == geometry_status_t::OK;
            } );
        return st;
    }
    for ( usize i = 0u; i < faces.nCount && st == geometry_status_t::OK; ++i ) {
        const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, faces.pData[i] );
        st = pF == nullptr ? geometry_status_t::STALE_HANDLE : addFace( faces.pData[i], *pF );
    }
    return st;
}

bounds_t BoundsOf( const gathered_t &g, u32 iFirst, u32 cCorners ) noexcept
{
    bounds_t b{ g.corners.pData[iFirst].uv, g.corners.pData[iFirst].uv };
    for ( u32 k = 1u; k < cCorners; ++k ) {
        const vec2d_t uv = g.corners.pData[iFirst + k].uv;
        b.lo = vec2d_t{ std::min( b.lo.x, uv.x ), std::min( b.lo.y, uv.y ) };
        b.hi = vec2d_t{ std::max( b.hi.x, uv.x ), std::max( b.hi.y, uv.y ) };
    }
    return b;
}

vec2d_t CenterOf( const bounds_t &b ) noexcept
{
    return vec2d_t{ 0.5 * ( b.lo.x + b.hi.x ), 0.5 * ( b.lo.y + b.hi.y ) };
}

// Writes the computed UVs; cannot fail once Gather has grown the store.
geometry_status_t Publish( mesh_attribute_store_t *pStore, const editable_mesh_t *pMesh, const gathered_t &g, mesh_uv_set_t uvSet ) noexcept
{
    for ( usize i = 0u; i < g.corners.nCount; ++i ) {
        if ( !std::isfinite( g.corners.pData[i].uv.x ) || !std::isfinite( g.corners.pData[i].uv.y ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    const usize cLimit = GenerationPool_Capacity( &pMesh->halfEdges );
    for ( usize i = 0u; i < g.corners.nCount; ++i ) {
        mesh_corner_attributes_t c = MeshAttributeStore_GetCorner( pStore, g.corners.pData[i].h );
        ( uvSet == mesh_uv_set_t::MATERIAL ? c.uv0 : c.uv1 ) = g.corners.pData[i].uv;
        (void)MeshAttributeStore_TrySetCorner( pStore, g.corners.pData[i].h, c, cLimit );
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t MeshUv_TryTransform(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    const mesh_uv_transform_t &transform,
    mesh_uv_pivot_t pivot,
    vec2d_t pivotPoint ) noexcept
{
    if ( !std::isfinite( transform.scale.x ) || !std::isfinite( transform.scale.y ) || !std::isfinite( transform.rotationRadians ) ||
         !std::isfinite( transform.shift.x ) || !std::isfinite( transform.shift.y ) ||
         ( pivot == mesh_uv_pivot_t::POINT && ( !std::isfinite( pivotPoint.x ) || !std::isfinite( pivotPoint.y ) ) ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !( std::fabs( transform.scale.x ) >= 1.0e-12 ) || !( std::fabs( transform.scale.y ) >= 1.0e-12 ) ||
         pivot > mesh_uv_pivot_t::POINT ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    gathered_t g{};
    const geometry_status_t st = Gather( pStore, pMesh, faces, uvSet, &g );
    if ( st != geometry_status_t::OK || g.corners.nCount == 0u ) { return st; }
    const f64 c = std::cos( transform.rotationRadians ), s = std::sin( transform.rotationRadians );
    const vec2d_t selectionCenter = CenterOf( BoundsOf( g, 0u, static_cast<u32>( g.corners.nCount ) ) );
    for ( usize r = 0u; r < g.runs.nCount; ++r ) {
        const face_run_t &run = g.runs.pData[r];
        const vec2d_t p = pivot == mesh_uv_pivot_t::POINT    ? pivotPoint
                          : pivot == mesh_uv_pivot_t::FACE_CENTER ? CenterOf( BoundsOf( g, run.iFirst, run.cCorners ) )
                                                                : selectionCenter;
        for ( u32 k = 0u; k < run.cCorners; ++k ) {
            vec2d_t &uv = g.corners.pData[run.iFirst + k].uv;
            const f64 x = transform.scale.x * ( uv.x - p.x ), y = transform.scale.y * ( uv.y - p.y );
            uv = vec2d_t{ p.x + c * x - s * y + transform.shift.x, p.y + s * x + c * y + transform.shift.y };
        }
    }
    return Publish( pStore, pMesh, g, uvSet );
}

geometry_status_t MeshUv_TryJustify(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    mesh_uv_justify_t mode,
    bool bTreatAsOne ) noexcept
{
    if ( mode > mesh_uv_justify_t::FIT ) { return geometry_status_t::INVALID_ARGUMENT; }
    gathered_t g{};
    const geometry_status_t st = Gather( pStore, pMesh, faces, uvSet, &g );
    if ( st != geometry_status_t::OK || g.corners.nCount == 0u ) { return st; }
    // Groups: the whole selection, or each face run.
    const usize cGroups = bTreatAsOne ? 1u : g.runs.nCount;
    for ( usize grp = 0u; grp < cGroups; ++grp ) {
        const u32 iFirst = bTreatAsOne ? 0u : g.runs.pData[grp].iFirst;
        const u32 cCorners = bTreatAsOne ? static_cast<u32>( g.corners.nCount ) : g.runs.pData[grp].cCorners;
        const bounds_t b = BoundsOf( g, iFirst, cCorners );
        const vec2d_t extent{ b.hi.x - b.lo.x, b.hi.y - b.lo.y };
        if ( mode == mesh_uv_justify_t::FIT && ( !( extent.x > kMinUvExtent ) || !( extent.y > kMinUvExtent ) ) ) {
            return geometry_status_t::DEGENERATE;
        }
        vec2d_t shift{ 0.0, 0.0 };
        switch ( mode ) {
            case mesh_uv_justify_t::MIN_U: shift.x = -b.lo.x; break;
            case mesh_uv_justify_t::MAX_U: shift.x = 1.0 - b.hi.x; break;
            case mesh_uv_justify_t::MIN_V: shift.y = -b.lo.y; break;
            case mesh_uv_justify_t::MAX_V: shift.y = 1.0 - b.hi.y; break;
            case mesh_uv_justify_t::CENTER: shift = vec2d_t{ 0.5 - CenterOf( b ).x, 0.5 - CenterOf( b ).y }; break;
            case mesh_uv_justify_t::FIT: break;
        }
        for ( u32 k = 0u; k < cCorners; ++k ) {
            vec2d_t &uv = g.corners.pData[iFirst + k].uv;
            uv = mode == mesh_uv_justify_t::FIT ? vec2d_t{ ( uv.x - b.lo.x ) / extent.x, ( uv.y - b.lo.y ) / extent.y }
                                                : vec2d_t{ uv.x + shift.x, uv.y + shift.y };
        }
    }
    return Publish( pStore, pMesh, g, uvSet );
}

geometry_status_t MeshUv_TryAlignToFace(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    vec2d_t worldUnitsPerUv ) noexcept
{
    if ( !( worldUnitsPerUv.x > 0.0 ) || !( worldUnitsPerUv.y > 0.0 ) || !std::isfinite( worldUnitsPerUv.x ) ||
         !std::isfinite( worldUnitsPerUv.y ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    gathered_t g{};
    const geometry_status_t st = Gather( pStore, pMesh, faces, uvSet, &g );
    if ( st != geometry_status_t::OK ) { return st; }
    for ( usize r = 0u; r < g.runs.nCount; ++r ) {
        const face_run_t &run = g.runs.pData[r];
        // Faces facing mostly up or down take world Y as "up"; walls take Z.
        const math::vec3d_t up = std::fabs( run.normal.z ) > 0.7071067811865476 ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
        math::planar_uv_mappingd_t mapping{};
        if ( !math::Uvd_TryBuildPlanarMapping( math::Vec3d_Make( 0, 0, 0 ), run.normal, up, worldUnitsPerUv, 0.0, vec2d_t{ 0.0, 0.0 },
                                               1.0e-12, &mapping ) ) {
            return geometry_status_t::DEGENERATE; // a face without a usable normal
        }
        for ( u32 k = 0u; k < run.cCorners; ++k ) {
            corner_t &c = g.corners.pData[run.iFirst + k];
            if ( !math::Uvd_TryProjectPlanarPoint( mapping, c.position, 1.0e-12, &c.uv ) ) { return geometry_status_t::NUMERIC_FAILURE; }
        }
    }
    return Publish( pStore, pMesh, g, uvSet );
}

geometry_status_t MeshUv_TryApplyHotspots(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    mesh_uv_set_t uvSet,
    span_t<const mesh_hotspot_rect_t> rects,
    vector_t<u32> *pChosenOut ) noexcept
{
    if ( rects.pData == nullptr || rects.nCount == 0u || rects.nCount > kMeshHotspotRectsMax ||
         ( pChosenOut != nullptr && pChosenOut->pAllocator == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( usize i = 0u; i < rects.nCount; ++i ) {
        const mesh_hotspot_rect_t &r = rects.pData[i];
        const bool bOk = r.uvMax.x - r.uvMin.x > kMinUvExtent && r.uvMax.y - r.uvMin.y > kMinUvExtent && r.worldSize.x > 0.0 &&
                         r.worldSize.y > 0.0 && std::isfinite( r.uvMin.x ) && std::isfinite( r.uvMin.y ) && std::isfinite( r.uvMax.x ) &&
                         std::isfinite( r.uvMax.y ) && std::isfinite( r.worldSize.x ) && std::isfinite( r.worldSize.y );
        if ( !bOk ) { return geometry_status_t::INVALID_ARGUMENT; }
    }
    gathered_t g{};
    geometry_status_t st = Gather( pStore, pMesh, faces, uvSet, &g );
    if ( st != geometry_status_t::OK ) { return st; }
    vector_t<u32> chosen{};
    if ( !Vector_Init( &chosen, pMesh->pAllocator ) || !Vector_Reserve( &chosen, g.runs.nCount ) ||
         ( pChosenOut != nullptr && !Vector_Reserve( pChosenOut, pChosenOut->nCount + g.runs.nCount ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize r = 0u; r < g.runs.nCount; ++r ) {
        const face_run_t &run = g.runs.pData[r];
        // The face's own frame: U along its longest edge, V = N x U.
        math::vec3d_t uAxis{};
        f64 longest = -1.0;
        for ( u32 k = 0u; k < run.cCorners; ++k ) {
            const math::vec3d_t e = math::Vec3d_Subtract( g.corners.pData[run.iFirst + ( k + 1u ) % run.cCorners].position,
                                                          g.corners.pData[run.iFirst + k].position );
            const math::vec3d_t inPlane = math::Vec3d_Subtract( e, math::Vec3d_Scale( run.normal, math::Vec3d_Dot( e, run.normal ) ) );
            const f64 len = math::Vec3d_LengthSquared( inPlane );
            if ( len > longest ) {
                longest = len;
                uAxis = inPlane;
            }
        }
        if ( !math::Vec3d_TryNormalize( uAxis, 1.0e-300, &uAxis, nullptr ) ) { return geometry_status_t::DEGENERATE; }
        const math::vec3d_t vAxis = math::Vec3d_Cross( run.normal, uAxis );
        vec2d_t lo{}, hi{};
        for ( u32 k = 0u; k < run.cCorners; ++k ) {
            const math::vec3d_t p = g.corners.pData[run.iFirst + k].position;
            const vec2d_t st2{ math::Vec3d_Dot( p, uAxis ), math::Vec3d_Dot( p, vAxis ) };
            g.corners.pData[run.iFirst + k].uv = st2; // plane coordinates for now
            lo = k == 0u ? st2 : vec2d_t{ std::min( lo.x, st2.x ), std::min( lo.y, st2.y ) };
            hi = k == 0u ? st2 : vec2d_t{ std::max( hi.x, st2.x ), std::max( hi.y, st2.y ) };
        }
        const vec2d_t size{ hi.x - lo.x, hi.y - lo.y };
        if ( !( size.x > kMinUvExtent ) || !( size.y > kMinUvExtent ) ) { return geometry_status_t::DEGENERATE; }

        // Best region and orientation.
        u32 iBest = 0u;
        bool bBestRotated = false;
        f64 bestCost = 0.0;
        bool bHaveBest = false;
        for ( u32 i = 0u; i < rects.nCount; ++i ) {
            const mesh_hotspot_rect_t &rc = rects.pData[i];
            for ( int rot = 0; rot < ( rc.bAllowRotation ? 2 : 1 ); ++rot ) {
                // Rotated: the face's U runs along the region's V.
                const f64 alongU = rot ? rc.worldSize.y : rc.worldSize.x, alongV = rot ? rc.worldSize.x : rc.worldSize.y;
                const bool bTileAlongU = rot ? rc.bTileV : rc.bTileU, bTileAlongV = rot ? rc.bTileU : rc.bTileV;
                const f64 cost = ( bTileAlongU ? 0.0 : std::fabs( std::log( size.x / alongU ) ) ) +
                                 ( bTileAlongV ? 0.0 : std::fabs( std::log( size.y / alongV ) ) );
                if ( !bHaveBest || cost < bestCost ) {
                    bHaveBest = true;
                    bestCost = cost;
                    iBest = i;
                    bBestRotated = rot != 0;
                }
            }
        }
        const mesh_hotspot_rect_t &rc = rects.pData[iBest];
        const vec2d_t extent{ rc.uvMax.x - rc.uvMin.x, rc.uvMax.y - rc.uvMin.y };
        for ( u32 k = 0u; k < run.cCorners; ++k ) {
            vec2d_t &uv = g.corners.pData[run.iFirst + k].uv;
            // Position along the face's axes: 0..1 of the face when fitted,
            // or in region repeats when tiling.
            const f64 tileAlongU = bBestRotated ? rc.worldSize.y : rc.worldSize.x;
            const f64 tileAlongV = bBestRotated ? rc.worldSize.x : rc.worldSize.y;
            const bool bTileAlongU = bBestRotated ? rc.bTileV : rc.bTileU, bTileAlongV = bBestRotated ? rc.bTileU : rc.bTileV;
            const f64 a = ( uv.x - lo.x ) / ( bTileAlongU ? tileAlongU : size.x );
            const f64 b = ( uv.y - lo.y ) / ( bTileAlongV ? tileAlongV : size.y );
            // Unrotated: face U -> region U. Rotated a quarter turn: face U
            // -> region V and face V -> region U running backwards, which
            // keeps the winding (no mirror).
            uv = bBestRotated ? vec2d_t{ rc.uvMin.x + ( 1.0 - b ) * extent.x, rc.uvMin.y + a * extent.y }
                              : vec2d_t{ rc.uvMin.x + a * extent.x, rc.uvMin.y + b * extent.y };
        }
        (void)Vector_PushBack( &chosen, iBest );
    }
    st = Publish( pStore, pMesh, g, uvSet );
    if ( st == geometry_status_t::OK && pChosenOut != nullptr ) {
        for ( usize i = 0u; i < chosen.nCount; ++i ) { (void)Vector_PushBack( pChosenOut, chosen.pData[i] ); }
    }
    return st;
}

} // namespace cypher::editor::geometry
