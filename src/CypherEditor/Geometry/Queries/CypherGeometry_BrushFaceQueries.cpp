//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushFaceQueries.cpp
//  Purpose: Implements the cross-brush coplanar flood fill.
//  Details: Candidates are found from side planes first (cheap), and only
//           brushes with a candidate side get their boundary reconstructed
//           for the face polygon. Polygons are flattened into the seed
//           plane's frame; the flood then links polygons whose separating-
//           axis gap is within the coplanar tolerance.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushFaceQueries.h"

#include "CypherGeometry_BrushBoundary.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::vec2d_t;
using math::vec3d_t;

namespace
{

struct candidate_t {
    brush_face_ref_t ref{};
    u32 iFirst{ 0u }; // into the flattened point array
    u32 cPoints{ 0u };
};

// Same orientation and plane: unit-normal chord within the angular
// tolerance (well conditioned at small angles, unlike comparing cosines)
// and plane offsets within the coplanar distance.
bool Coplanar( math::planed_t a, math::planed_t b, const geometry_numerical_policy_t &p ) noexcept
{
    const f64 chord = std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( a.normal, b.normal ) ) );
    return chord <= p.fAngularToleranceRadians && std::fabs( a.d - b.d ) <= p.fCoplanarDistanceTolerance;
}

// True when two convex polygons are no further apart than eps (touching or
// overlapping): no edge normal of either separates them by more than eps.
bool Touch( const vec2d_t *a, u32 na, const vec2d_t *b, u32 nb, f64 eps ) noexcept
{
    auto separated = [&]( const vec2d_t *p, u32 np, const vec2d_t *q, u32 nq ) noexcept {
        for ( u32 i = 0u; i < np; ++i ) {
            const vec2d_t e{ p[( i + 1u ) % np].x - p[i].x, p[( i + 1u ) % np].y - p[i].y };
            const f64 len = std::sqrt( e.x * e.x + e.y * e.y );
            if ( !( len > 0.0 ) ) { continue; }
            const vec2d_t axis{ e.y / len, -e.x / len };
            f64 pMin = 1e300, pMax = -1e300, qMin = 1e300, qMax = -1e300;
            for ( u32 k = 0u; k < np; ++k ) {
                const f64 s = p[k].x * axis.x + p[k].y * axis.y;
                pMin = std::min( pMin, s );
                pMax = std::max( pMax, s );
            }
            for ( u32 k = 0u; k < nq; ++k ) {
                const f64 s = q[k].x * axis.x + q[k].y * axis.y;
                qMin = std::min( qMin, s );
                qMax = std::max( qMax, s );
            }
            if ( qMin > pMax + eps || pMin > qMax + eps ) { return true; }
        }
        return false;
    };
    return !separated( a, na, b, nb ) && !separated( b, nb, a, na );
}

} // namespace

geometry_status_t BrushFaces_TrySelectCoplanar(
    span_t<const brush_solid_t *const> brushes, brush_face_ref_t seed, const geometry_policy_t &policy,
    const allocator_t *pAllocator, vector_t<brush_face_ref_t> *pFacesOut ) noexcept
{
    if ( pFacesOut == nullptr || pFacesOut->pAllocator == nullptr || pAllocator == nullptr || !GeometryPolicy_IsValid( policy ) ||
         ( brushes.nCount > 0u && brushes.pData == nullptr ) || seed.iBrush >= brushes.nCount ||
         brushes.pData[seed.iBrush] == nullptr || seed.iSide >= BrushSolid_SideCount( brushes.pData[seed.iBrush] ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const math::planed_t seedPlane = brushes.pData[seed.iBrush]->sides.pData[seed.iSide].plane;
    // Frame of the seed plane.
    const vec3d_t n = seedPlane.normal;
    const vec3d_t helper = std::fabs( n.x ) < 0.9 ? math::Vec3d_Make( 1, 0, 0 ) : math::Vec3d_Make( 0, 1, 0 );
    vec3d_t u{}, v{};
    (void)math::Vec3d_TryNormalize( math::Vec3d_Cross( helper, n ), 0.0, &u, nullptr );
    v = math::Vec3d_Cross( n, u );

    vector_t<candidate_t> cand{};
    vector_t<vec2d_t> pts{};
    vector_t<u32> queue{};
    vector_t<u8> reached{};
    brush_boundary_t bd{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &cand );
        Vector_Shutdown( &pts );
        Vector_Shutdown( &queue );
        Vector_Shutdown( &reached );
        BrushBoundary_Shutdown( &bd );
    };
    geometry_status_t st = BrushBoundary_Init( &bd, pAllocator );
    if ( st != geometry_status_t::OK || !Vector_Init( &cand, pAllocator ) || !Vector_Init( &pts, pAllocator ) ||
         !Vector_Init( &queue, pAllocator ) || !Vector_Init( &reached, pAllocator ) ) {
        cleanup();
        return st != geometry_status_t::OK ? st : geometry_status_t::ALLOCATION_FAILED;
    }
    u32 iSeedCandidate = CY_INVALID_INDEX;
    for ( usize ib = 0u; st == geometry_status_t::OK && ib < brushes.nCount; ++ib ) {
        const brush_solid_t *pB = brushes.pData[ib];
        if ( pB == nullptr ) { continue; }
        bool bAny = false;
        for ( usize is = 0u; is < BrushSolid_SideCount( pB ) && !bAny; ++is ) { bAny = Coplanar( pB->sides.pData[is].plane, seedPlane, policy.numerical ); }
        if ( !bAny ) { continue; }
        st = BrushBoundary_TryReconstruct( &bd, pB, policy );
        if ( st != geometry_status_t::OK ) { break; }
        for ( usize f = 0u; f < BrushBoundary_FaceCount( &bd ); ++f ) {
            const u32 iSide = bd.faces.pData[f].iSide;
            if ( !Coplanar( pB->sides.pData[iSide].plane, seedPlane, policy.numerical ) ) { continue; }
            u32 idx[256];
            usize cIdx = 0u;
            st = BrushBoundary_TryGetFaceVertexIndices( &bd, f, idx, 256u, &cIdx );
            if ( st != geometry_status_t::OK ) { break; }
            candidate_t c{};
            c.ref = brush_face_ref_t{ static_cast<u32>( ib ), iSide };
            c.iFirst = static_cast<u32>( pts.nCount );
            c.cPoints = static_cast<u32>( cIdx );
            for ( usize k = 0u; k < cIdx; ++k ) {
                const vec3d_t p = bd.vertices.pData[idx[k]];
                if ( !Vector_PushBack( &pts, vec2d_t{ math::Vec3d_Dot( p, u ), math::Vec3d_Dot( p, v ) } ) ) {
                    st = geometry_status_t::ALLOCATION_FAILED;
                }
            }
            if ( ib == seed.iBrush && iSide == seed.iSide ) { iSeedCandidate = static_cast<u32>( cand.nCount ); }
            if ( !Vector_PushBack( &cand, c ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
        }
    }
    if ( st == geometry_status_t::OK && iSeedCandidate == CY_INVALID_INDEX ) { st = geometry_status_t::INVALID_ARGUMENT; }
    if ( st == geometry_status_t::OK && !Vector_Resize( &reached, cand.nCount ) ) { st = geometry_status_t::ALLOCATION_FAILED; }
    if ( st != geometry_status_t::OK ) {
        cleanup();
        return st;
    }
    for ( usize i = 0u; i < reached.nCount; ++i ) { reached.pData[i] = 0u; }
    reached.pData[iSeedCandidate] = 1u;
    bool bOk = Vector_PushBack( &queue, iSeedCandidate );
    const f64 eps = policy.numerical.fCoplanarDistanceTolerance;
    for ( usize q = 0u; bOk && q < queue.nCount; ++q ) {
        const candidate_t &a = cand.pData[queue.pData[q]];
        for ( usize j = 0u; bOk && j < cand.nCount; ++j ) {
            if ( reached.pData[j] != 0u ) { continue; }
            const candidate_t &b = cand.pData[j];
            if ( Touch( pts.pData + a.iFirst, a.cPoints, pts.pData + b.iFirst, b.cPoints, eps ) ) {
                reached.pData[j] = 1u;
                bOk = Vector_PushBack( &queue, static_cast<u32>( j ) );
            }
        }
    }
    // Publish: reserve first so the output is replaced all at once.
    if ( bOk && Vector_Reserve( pFacesOut, queue.nCount ) ) {
        Vector_Clear( pFacesOut );
        for ( usize q = 0u; q < queue.nCount; ++q ) { (void)Vector_PushBack( pFacesOut, cand.pData[queue.pData[q]].ref ); }
        std::sort( pFacesOut->pData, pFacesOut->pData + pFacesOut->nCount, []( brush_face_ref_t x, brush_face_ref_t y ) {
            return x.iBrush != y.iBrush ? x.iBrush < y.iBrush : x.iSide < y.iSide;
        } );
        cleanup();
        return geometry_status_t::OK;
    }
    cleanup();
    return geometry_status_t::ALLOCATION_FAILED;
}

} // namespace cypher::editor::geometry
