//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_TriangleSoup.cpp
//  Purpose: Implements TriangleSoup storage and explicit welding.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_TriangleSoup.h"
#include "CypherGeometry_PointWeld.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename type_t>
struct scratch_t {
    vector_t<type_t> v{};
    ~scratch_t() { Vector_Shutdown( &v ); }
};

} // namespace

bool TriangleSoup_IsInitialized( const triangle_soup_t *pSoup ) noexcept
{
    return pSoup != nullptr && pSoup->triangles.pAllocator != nullptr;
}

geometry_status_t TriangleSoup_Init(
    triangle_soup_t *pSoup,
    const allocator_t *pAllocator ) noexcept
{
    if ( pSoup == nullptr || !Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( TriangleSoup_IsInitialized( pSoup ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return Vector_Init( &pSoup->triangles, pAllocator ) ? geometry_status_t::OK
                                                        : geometry_status_t::ALLOCATION_FAILED;
}

void TriangleSoup_Shutdown( triangle_soup_t *pSoup ) noexcept
{
    if ( pSoup != nullptr ) { Vector_Shutdown( &pSoup->triangles ); }
}

geometry_status_t TriangleSoup_TryAdd(
    triangle_soup_t *pSoup,
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c,
    geometry_source_id_t sourceId,
    u32 iGroup ) noexcept
{
    if ( !TriangleSoup_IsInitialized( pSoup ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSoup->triangles.nCount >= kTriangleSoupTrianglesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    triangle_soup_triangle_t t{};
    t.p[0] = a;
    t.p[1] = b;
    t.p[2] = c;
    t.sourceId = sourceId;
    t.iGroup = iGroup;
    return Vector_PushBack( &pSoup->triangles, t ) ? geometry_status_t::OK
                                                  : geometry_status_t::ALLOCATION_FAILED;
}

usize TriangleSoup_Count( const triangle_soup_t *pSoup ) noexcept
{
    return TriangleSoup_IsInitialized( pSoup ) ? pSoup->triangles.nCount : 0u;
}

triangle_weld_result_t TriangleSoup_TryWeldToPolygonSoup(
    const triangle_soup_t *pSoup,
    f64 fWeldDistance,
    polygon_soup_t *pOut ) noexcept
{
    triangle_weld_result_t r{};
    if ( pSoup == nullptr || pOut == nullptr || !( fWeldDistance >= 0.0 ) ) {
        return r; // INVALID_ARGUMENT
    }
    if ( !TriangleSoup_IsInitialized( pSoup ) || !PolygonSoup_IsInitialized( pOut ) ) {
        r.status = geometry_status_t::NOT_INITIALIZED;
        return r;
    }
    if ( PolygonSoup_VertexCount( pOut ) != 0u || PolygonSoup_FaceCount( pOut ) != 0u ) {
        return r; // must be empty
    }
    const allocator_t *pAlloc = pSoup->triangles.pAllocator;
    const usize cTri = pSoup->triangles.nCount;

    scratch_t<math::vec3d_t> corners;
    if ( !Vector_Init( &corners.v, pAlloc, cTri * 3u ) ) {
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    for ( usize t = 0u; t < cTri; ++t ) {
        for ( int k = 0; k < 3; ++k ) {
            const math::vec3d_t p = pSoup->triangles.pData[t].p[k];
            if ( !math::Vec3d_IsFinite( p ) ) {
                r.status = geometry_status_t::NUMERIC_FAILURE;
                return r;
            }
            (void)Vector_PushBack( &corners.v, p );
        }
    }

    scratch_t<u32> remap, reps;
    if ( !Vector_Init( &remap.v, pAlloc ) || !Vector_Init( &reps.v, pAlloc ) ) {
        r.status = geometry_status_t::ALLOCATION_FAILED;
        return r;
    }
    const point_weld_result_t w = PointWeld_BuildRemap(
        span_t<const math::vec3d_t>{ corners.v.pData, corners.v.nCount }, fWeldDistance, pAlloc,
        &remap.v, &reps.v );
    if ( w.status != geometry_status_t::OK ) {
        r.status = w.status;
        return r;
    }
    if ( w.cUnique > kPolygonSoupVerticesMax ) {
        r.status = geometry_status_t::LIMIT_EXCEEDED;
        return r;
    }

    // Build into the output; on any failure clear it back to empty.
    geometry_status_t s = geometry_status_t::OK;
    for ( usize c = 0u; c < reps.v.nCount && s == geometry_status_t::OK; ++c ) {
        s = PolygonSoup_TryAddVertex( pOut, corners.v.pData[reps.v.pData[c]], nullptr );
    }
    for ( usize t = 0u; t < cTri && s == geometry_status_t::OK; ++t ) {
        const u32 idx[3] = { remap.v.pData[t * 3u + 0u], remap.v.pData[t * 3u + 1u],
                             remap.v.pData[t * 3u + 2u] };
        if ( idx[0] == idx[1] || idx[1] == idx[2] || idx[0] == idx[2] ) {
            ++r.cTrianglesCollapsed;
            continue;
        }
        s = PolygonSoup_TryAddFace( pOut, span_t<const u32>{ idx, 3u },
                                    pSoup->triangles.pData[t].sourceId,
                                    pSoup->triangles.pData[t].iGroup, nullptr );
        if ( s == geometry_status_t::OK ) { ++r.cTrianglesKept; }
    }
    if ( s != geometry_status_t::OK ) {
        PolygonSoup_Clear( pOut );
        r = triangle_weld_result_t{};
        r.status = s;
        return r;
    }
    r.cVertices = w.cUnique;
    r.fMaxDisplacement = w.fMaxDisplacement;
    r.status = geometry_status_t::OK;
    return r;
}

} // namespace cypher::editor::geometry
