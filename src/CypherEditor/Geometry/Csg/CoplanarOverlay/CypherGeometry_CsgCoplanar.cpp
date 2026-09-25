//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgCoplanar.cpp
//  Purpose: Implements coplanar overlay resolution.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CsgCoplanar.h"

#include <algorithm>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct tri_key_t {
    u32 s[3]; // sorted points
    u32 tri;
};

bool Less( const tri_key_t &x, const tri_key_t &y ) noexcept
{
    for ( u32 i = 0u; i < 3u; ++i ) {
        if ( x.s[i] != y.s[i] ) { return x.s[i] < y.s[i]; }
    }
    return x.tri < y.tri;
}

bool SameSet( const tri_key_t &x, const tri_key_t &y ) noexcept { return x.s[0] == y.s[0] && x.s[1] == y.s[1] && x.s[2] == y.s[2]; }

// Same cyclic order: b is a rotation of a.
bool SameWinding( const csg_refined_triangle_t &a, const csg_refined_triangle_t &b ) noexcept
{
    for ( u32 r = 0u; r < 3u; ++r ) {
        if ( a.v[0] == b.v[r] && a.v[1] == b.v[( r + 1u ) % 3u] && a.v[2] == b.v[( r + 2u ) % 3u] ) { return true; }
    }
    return false;
}

} // namespace

geometry_status_t CsgCoplanar_TryMatch( const vector_t<csg_refined_triangle_t> &tris, usize cTrianglesA, vector_t<csg_label_t> *pLabels,
                                        usize *pcSharedOut ) noexcept
{
    if ( pLabels == nullptr || pLabels->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Resize( pLabels, tris.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize t = 0u; t < tris.nCount; ++t ) { pLabels->pData[t] = csg_label_t::UNKNOWN; }
    vector_t<tri_key_t> keys{};
    if ( !Vector_Init( &keys, pLabels->pAllocator ) || !Vector_Resize( &keys, tris.nCount ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize t = 0u; t < tris.nCount; ++t ) {
        tri_key_t &k = keys.pData[t];
        k.s[0] = tris.pData[t].v[0];
        k.s[1] = tris.pData[t].v[1];
        k.s[2] = tris.pData[t].v[2];
        std::sort( k.s, k.s + 3 );
        k.tri = static_cast<u32>( t );
    }
    std::sort( keys.pData, keys.pData + keys.nCount, Less );
    usize cShared = 0u;
    for ( usize i = 0u; i < keys.nCount; ) {
        usize j = i + 1u;
        while ( j < keys.nCount && SameSet( keys.pData[j], keys.pData[i] ) ) { ++j; }
        // A run of one A triangle and one B triangle is an overlap piece;
        // anything else in a run (a duplicate within one operand) is not
        // valid refined input.
        if ( j - i == 2u ) {
            const u32 t0 = keys.pData[i].tri, t1 = keys.pData[i + 1u].tri;
            const bool bA0 = t0 < cTrianglesA, bA1 = t1 < cTrianglesA;
            if ( bA0 != bA1 ) {
                const csg_label_t l = SameWinding( tris.pData[t0], tris.pData[t1] ) ? csg_label_t::SHARED_SAME : csg_label_t::SHARED_OPPOSITE;
                pLabels->pData[t0] = l;
                pLabels->pData[t1] = l;
                ++cShared;
            } else {
                return geometry_status_t::NON_MANIFOLD;
            }
        } else if ( j - i > 2u ) {
            return geometry_status_t::NON_MANIFOLD;
        }
        i = j;
    }
    if ( pcSharedOut != nullptr ) { *pcSharedOut = cShared; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
