//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Sweep.cpp
//  Purpose: Implements sweep and lathe via an indexed soup and Sanitation.
//  Details: Wall orientation for a sweep: the quad (ring i, k) -> (i, k+1)
//           -> (i+1, k+1) -> (i+1, k) has normal ~ profileDir x T. With
//           (T, N, B) right-handed, that is the outward side of a CCW profile
//           in (N, B) coordinates, so CW profiles simply reverse the order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Sweep.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"
#include "CypherGeometry_Planar_Triangulate.h"
#include "CypherGeometry_PlanarRegion.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr f64 kTwoPi = 6.283185307179586;

sweep_report_t Fail( sweep_report_t r, geometry_status_t s ) noexcept { r.status = s; return r; }

struct soup_guard_t {
    polygon_soup_t *p;
    ~soup_guard_t() { PolygonSoup_Shutdown( p ); }
};

// Signed volume of a closed soup by the divergence theorem (fan per face).
f64 SoupVolume( const polygon_soup_t *s ) noexcept
{
    f64 vol = 0.0;
    for ( usize f = 0u; f < s->faces.nCount; ++f ) {
        const span_t<const u32> c = PolygonSoup_FaceCorners( s, f );
        const math::vec3d_t o = s->positions.pData[c.pData[0]];
        for ( usize k = 1u; k + 1u < c.nCount; ++k ) {
            vol += math::Vec3d_Dot( o, math::Vec3d_Cross( s->positions.pData[c.pData[k]],
                                                          s->positions.pData[c.pData[k + 1u]] ) );
        }
    }
    return vol / 6.0;
}

void ReverseAllFaces( polygon_soup_t *s ) noexcept
{
    for ( usize f = 0u; f < s->faces.nCount; ++f ) {
        const polygon_soup_face_t &fc = s->faces.pData[f];
        u32 *c = s->corners.pData + fc.iFirstCorner;
        for ( u32 a = 0u, b = fc.cCorners - 1u; a < b; ++a, --b ) {
            const u32 t = c[a]; c[a] = c[b]; c[b] = t;
        }
    }
}

// Adds a face after dropping consecutive duplicate indices (pole collapse);
// faces left with fewer than three corners are skipped (returns OK).
geometry_status_t AddFaceDedup( polygon_soup_t *s, const u32 *idx, u32 n, u32 group, u32 *pAdded ) noexcept
{
    u32 tmp[8];
    u32 m = 0u;
    for ( u32 i = 0u; i < n; ++i ) {
        if ( m > 0u && tmp[m - 1u] == idx[i] ) { continue; }
        tmp[m++] = idx[i];
    }
    while ( m > 1u && tmp[m - 1u] == tmp[0] ) { --m; }
    if ( m < 3u ) { return geometry_status_t::OK; }
    const geometry_status_t st = PolygonSoup_TryAddFace( s, span_t<const u32>{ tmp, m }, {}, group, nullptr );
    if ( st == geometry_status_t::OK && pAdded ) { ++*pAdded; }
    return st;
}

} // namespace

sweep_report_t Sweep_TryAlongPath(
    const curve_network_t *pNet,
    u32 iPath,
    span_t<const math::vec2d_t> profile,
    const sweep_options_t &options,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept
{
    sweep_report_t r{};
    if ( pMeshOut == nullptr || !Allocator_IsValid( pAllocator ) || profile.pData == nullptr ||
         profile.nCount < 2u || ( options.bProfileClosed && profile.nCount < 3u ) ||
         profile.nCount > kSweepProfilePointsMax || !std::isfinite( options.fScaleStart ) ||
         !std::isfinite( options.fScaleEnd ) ) {
        return r;
    }
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return Fail( r, geometry_status_t::NOT_INITIALIZED ); }
    if ( iPath >= pNet->paths.nCount ) { return r; }
    for ( usize k = 0u; k < profile.nCount; ++k ) {
        if ( !math::Vec2d_IsFinite( profile.pData[k] ) ) { return Fail( r, geometry_status_t::NUMERIC_FAILURE ); }
    }
    const bool pathClosed = pNet->paths.pData[iPath].bClosed;

    vector_t<curve_sample_t> samples{};
    if ( !Vector_Init( &samples, pAllocator ) ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
    struct sg_t { vector_t<curve_sample_t> *v; ~sg_t() { Vector_Shutdown( v ); } } sg{ &samples };
    geometry_status_t s = CurveSampling_TrySamplePath( pNet, iPath, options.sampling, &samples );
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    const usize n = samples.nCount;
    const usize m = profile.nCount;
    if ( n < 2u ) { return Fail( r, geometry_status_t::DEGENERATE ); }
    const f64 total = samples.pData[n - 1u].distance > 0.0 ? samples.pData[n - 1u].distance : 1.0;

    polygon_soup_t soup{};
    soup_guard_t guard{ &soup };
    s = PolygonSoup_Init( &soup, pAllocator );
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    for ( usize i = 0u; i < n && s == geometry_status_t::OK; ++i ) {
        const curve_sample_t &cs = samples.pData[i];
        const f64 scale = options.fScaleStart + ( options.fScaleEnd - options.fScaleStart ) * ( cs.distance / total );
        for ( usize k = 0u; k < m && s == geometry_status_t::OK; ++k ) {
            const math::vec2d_t q = profile.pData[k];
            const math::vec3d_t p = math::Vec3d_Add(
                cs.position, math::Vec3d_Add( math::Vec3d_Scale( cs.normal, q.x * scale ),
                                              math::Vec3d_Scale( cs.binormal, q.y * scale ) ) );
            s = PolygonSoup_TryAddVertex( &soup, p, nullptr );
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    r.cRings = static_cast<u32>( n );

    const bool ccw = !options.bProfileClosed ||
                     Planar_RingSignedArea( span_t<const math::vec2d_t>{ profile.pData, m } ) >= 0.0;
    const usize ringSpans = pathClosed ? n : n - 1u;
    const usize profileSpans = options.bProfileClosed ? m : m - 1u;
    auto V = [&]( usize i, usize k ) noexcept { return static_cast<u32>( ( i % n ) * m + ( k % m ) ); };
    for ( usize i = 0u; i < ringSpans && s == geometry_status_t::OK; ++i ) {
        for ( usize k = 0u; k < profileSpans && s == geometry_status_t::OK; ++k ) {
            u32 q[4] = { V( i, k ), V( i, k + 1u ), V( i + 1u, k + 1u ), V( i + 1u, k ) };
            if ( !ccw ) { const u32 t = q[1]; q[1] = q[3]; q[3] = t; }
            s = AddFaceDedup( &soup, q, 4u, 0u, &r.cWallFaces );
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    const bool caps = !pathClosed && options.bProfileClosed && options.bCapEnds;
    if ( caps ) {
        vector_t<planar_ring_triangle_t> tris{};
        if ( !Vector_Init( &tris, pAllocator ) ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
        s = Planar_TryTriangulateRing( span_t<const math::vec2d_t>{ profile.pData, m }, pAllocator, &tris );
        // Ring triangles share the profile's winding. A CCW profile's
        // triangles face +T: correct for the end cap, reversed for the start.
        for ( usize t = 0u; t < tris.nCount && s == geometry_status_t::OK; ++t ) {
            const planar_ring_triangle_t &tr = tris.pData[t];
            u32 endTri[3] = { V( n - 1u, tr.a ), V( n - 1u, tr.b ), V( n - 1u, tr.c ) };
            u32 startTri[3] = { V( 0u, tr.a ), V( 0u, tr.c ), V( 0u, tr.b ) };
            if ( !ccw ) {
                const u32 t0 = endTri[1]; endTri[1] = endTri[2]; endTri[2] = t0;
                const u32 t1 = startTri[1]; startTri[1] = startTri[2]; startTri[2] = t1;
            }
            s = AddFaceDedup( &soup, startTri, 3u, 1u, &r.cCapFaces );
            if ( s == geometry_status_t::OK ) { s = AddFaceDedup( &soup, endTri, 3u, 2u, &r.cCapFaces ); }
        }
        Vector_Shutdown( &tris );
        if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    }

    r.bClosed = options.bProfileClosed && ( pathClosed || caps );
    sanitation_policy_t sp{};
    sp.bRequireClosed = r.bClosed;
    const sanitation_report_t sr = Sanitation_TryPolygonSoupToMesh( &soup, sp, pAllocator, pMeshOut, nullptr );
    r.status = sr.status;
    return r;
}

sweep_report_t Sweep_TryLathe(
    span_t<const math::vec2d_t> profile,
    math::vec3d_t axisOrigin,
    math::vec3d_t axisDir,
    const lathe_options_t &options,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept
{
    sweep_report_t r{};
    math::vec3d_t A{};
    if ( pMeshOut == nullptr || !Allocator_IsValid( pAllocator ) || profile.pData == nullptr ||
         profile.nCount < 2u || profile.nCount > kSweepProfilePointsMax || options.segments < 3u ||
         !( options.angle > 0.0 ) || !math::Vec3d_IsFinite( axisOrigin ) ||
         !math::Vec3d_TryNormalize( axisDir, 1.0e-300, &A, nullptr ) ) {
        return r;
    }
    for ( usize k = 0u; k < profile.nCount; ++k ) {
        if ( !math::Vec2d_IsFinite( profile.pData[k] ) ) { return Fail( r, geometry_status_t::NUMERIC_FAILURE ); }
        if ( profile.pData[k].x < 0.0 ) { return r; }
    }
    const bool full = options.angle >= kTwoPi - 1.0e-9;
    const u32 segs = options.segments;
    const u32 rings = full ? segs : segs + 1u;
    const usize m = profile.nCount;

    const f64 ax = std::fabs( A.x ), ay = std::fabs( A.y ), az = std::fabs( A.z );
    const math::vec3d_t h = ( ax <= ay && ax <= az ) ? math::Vec3d_Make( 1, 0, 0 )
                          : ( ay <= az ) ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
    math::vec3d_t e1{};
    (void)math::Vec3d_TryNormalize( math::Vec3d_Cross( A, h ), 1.0e-300, &e1, nullptr );
    const math::vec3d_t e2 = math::Vec3d_Cross( A, e1 );

    polygon_soup_t soup{};
    soup_guard_t guard{ &soup };
    geometry_status_t s = PolygonSoup_Init( &soup, pAllocator );
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    // Vertex index table: poles (x == 0) get one shared vertex per profile point.
    vector_t<u32> index{};
    if ( !Vector_Init( &index, pAllocator ) || !Vector_Resize( &index, static_cast<usize>( rings ) * m ) ) {
        Vector_Shutdown( &index );
        return Fail( r, geometry_status_t::ALLOCATION_FAILED );
    }
    struct ig_t { vector_t<u32> *v; ~ig_t() { Vector_Shutdown( v ); } } ig{ &index };
    for ( usize k = 0u; k < m && s == geometry_status_t::OK; ++k ) {
        const math::vec2d_t q = profile.pData[k];
        if ( q.x == 0.0 ) {
            u32 vi = 0u;
            s = PolygonSoup_TryAddVertex( &soup, math::Vec3d_Add( axisOrigin, math::Vec3d_Scale( A, q.y ) ), &vi );
            for ( u32 j = 0u; j < rings; ++j ) { index.pData[j * m + k] = vi; }
            continue;
        }
        for ( u32 j = 0u; j < rings && s == geometry_status_t::OK; ++j ) {
            const f64 phi = options.angle * static_cast<f64>( j ) / static_cast<f64>( segs );
            const math::vec3d_t radial = math::Vec3d_Add( math::Vec3d_Scale( e1, std::cos( phi ) ),
                                                          math::Vec3d_Scale( e2, std::sin( phi ) ) );
            const math::vec3d_t p = math::Vec3d_Add( axisOrigin, math::Vec3d_Add( math::Vec3d_Scale( A, q.y ),
                                                                                  math::Vec3d_Scale( radial, q.x ) ) );
            u32 vi = 0u;
            s = PolygonSoup_TryAddVertex( &soup, p, &vi );
            index.pData[j * m + k] = vi;
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    r.cRings = rings;
    auto I = [&]( u32 j, usize k ) noexcept { return index.pData[( j % rings ) * m + k]; };
    for ( u32 j = 0u; j < segs && s == geometry_status_t::OK; ++j ) {
        for ( usize k = 0u; k + 1u < m && s == geometry_status_t::OK; ++k ) {
            // Profile up + revolution CCW about A: this order faces away from
            // the axis for an upward profile (see header).
            const u32 q[4] = { I( j, k ), I( j + 1u, k ), I( j + 1u, k + 1u ), I( j, k + 1u ) };
            s = AddFaceDedup( &soup, q, 4u, 0u, &r.cWallFaces );
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    // Closed iff full turn and both profile ends sit on the axis.
    r.bClosed = full && profile.pData[0].x == 0.0 && profile.pData[m - 1u].x == 0.0;
    if ( r.bClosed && SoupVolume( &soup ) < 0.0 ) { ReverseAllFaces( &soup ); }
    sanitation_policy_t sp{};
    sp.bRequireClosed = r.bClosed;
    const sanitation_report_t sr = Sanitation_TryPolygonSoupToMesh( &soup, sp, pAllocator, pMeshOut, nullptr );
    r.status = sr.status;
    return r;
}

} // namespace cypher::editor::geometry
