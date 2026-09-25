//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PatchResize.cpp
//  Purpose: Implements sub-patch merging with least-squares refitting.
//  Details: Controls are treated as 5-vectors (position, UV); the fit is
//           the same linear problem for every component. With endpoints
//           fixed, degree d has d - 1 unknown interior controls (1 or 2), so
//           the normal equations are at most 2x2 and solved directly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PatchResize.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr u32 kSamples = 32u;

struct v5_t {
    f64 c[5]{};
};

v5_t Of( const patch_control_t &p ) noexcept { return v5_t{ { p.position.x, p.position.y, p.position.z, p.uv.x, p.uv.y } }; }

// Bezier segment of degree d (controls p[0..d]) at s (de Casteljau).
v5_t Eval( const v5_t *p, u32 d, f64 s ) noexcept
{
    v5_t w[4];
    for ( u32 i = 0u; i <= d; ++i ) { w[i] = p[i]; }
    for ( u32 r = 1u; r <= d; ++r ) {
        for ( u32 i = 0u; i + r <= d; ++i ) {
            for ( int k = 0; k < 5; ++k ) { w[i].c[k] = ( 1.0 - s ) * w[i].c[k] + s * w[i + 1u].c[k]; }
        }
    }
    return w[0];
}

f64 Bernstein( u32 d, u32 k, f64 t ) noexcept
{
    const f64 binom = ( d == 2u ) ? ( k == 1u ? 2.0 : 1.0 ) : ( ( k == 1u || k == 2u ) ? 3.0 : 1.0 );
    return binom * std::pow( t, static_cast<f64>( k ) ) * std::pow( 1.0 - t, static_cast<f64>( d - k ) );
}

// Merges two consecutive degree-d segments (left[0..d], right[0..d], sharing
// left[d] == right[0]) into one: out[0..d].
bool Merge( const v5_t *left, const v5_t *right, u32 d, v5_t *out ) noexcept
{
    out[0] = left[0];
    out[d] = right[d];
    // Normal equations for the interior controls (m = d - 1 unknowns).
    const u32 m = d - 1u;
    f64 ata[2][2] = { { 0, 0 }, { 0, 0 } };
    f64 aty[2][5] = {};
    for ( u32 i = 1u; i < kSamples; ++i ) {
        const f64 t = static_cast<f64>( i ) / kSamples;
        const v5_t s = t < 0.5 ? Eval( left, d, 2.0 * t ) : Eval( right, d, 2.0 * t - 1.0 );
        f64 b[2];
        for ( u32 k = 0u; k < m; ++k ) { b[k] = Bernstein( d, k + 1u, t ); }
        const f64 b0 = Bernstein( d, 0u, t ), bd = Bernstein( d, d, t );
        for ( u32 k = 0u; k < m; ++k ) {
            for ( u32 l = 0u; l < m; ++l ) { ata[k][l] += b[k] * b[l]; }
            for ( int c = 0; c < 5; ++c ) { aty[k][c] += b[k] * ( s.c[c] - b0 * out[0].c[c] - bd * out[d].c[c] ); }
        }
    }
    if ( m == 1u ) {
        if ( !( ata[0][0] > 0.0 ) ) { return false; }
        for ( int c = 0; c < 5; ++c ) { out[1].c[c] = aty[0][c] / ata[0][0]; }
        return true;
    }
    const f64 det = ata[0][0] * ata[1][1] - ata[0][1] * ata[1][0];
    if ( !( std::fabs( det ) > 0.0 ) ) { return false; }
    for ( int c = 0; c < 5; ++c ) {
        out[1].c[c] = ( aty[0][c] * ata[1][1] - ata[0][1] * aty[1][c] ) / det;
        out[2].c[c] = ( ata[0][0] * aty[1][c] - aty[0][c] * ata[1][0] ) / det;
    }
    return true;
}

geometry_status_t RemoveAlong( patch_surface_t *pPatch, bool bColumns, u32 iSub ) noexcept
{
    if ( pPatch == nullptr || !Patch_IsInitialized( pPatch ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const u32 d = Patch_Degree( pPatch->basis );
    const u32 cSub = bColumns ? Patch_SubPatchColumns( pPatch ) : Patch_SubPatchRows( pPatch );
    if ( d < 2u || d > 3u || cSub < 2u || iSub + 1u >= cSub ) { return geometry_status_t::INVALID_ARGUMENT; }
    const u32 cCols = pPatch->cColumns, cRows = pPatch->cRows;
    const u32 cAlong = bColumns ? cCols : cRows, cAcross = bColumns ? cRows : cCols;
    const u32 cAlongNew = cAlong - d;
    auto at = [&]( u32 along, u32 across ) noexcept -> const patch_control_t & {
        return bColumns ? pPatch->controls.pData[across * cCols + along] : pPatch->controls.pData[along * cCols + across];
    };
    vector_t<patch_control_t> next{};
    if ( !Vector_Init( &next, pPatch->controls.pAllocator ) || !Vector_Resize( &next, static_cast<usize>( cAlongNew ) * cAcross ) ) {
        Vector_Shutdown( &next );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const u32 cNewCols = bColumns ? cAlongNew : cCols;
    auto put = [&]( u32 along, u32 across, const patch_control_t &c ) noexcept {
        if ( bColumns ) {
            next.pData[across * cNewCols + along] = c;
        } else {
            next.pData[along * cNewCols + across] = c;
        }
    };
    const u32 first = iSub * d; // first control of the merged span
    for ( u32 a = 0u; a < cAcross; ++a ) {
        v5_t left[4], right[4], merged[4];
        for ( u32 k = 0u; k <= d; ++k ) {
            left[k] = Of( at( first + k, a ) );
            right[k] = Of( at( first + d + k, a ) );
        }
        if ( !Merge( left, right, d, merged ) ) {
            Vector_Shutdown( &next );
            return geometry_status_t::NUMERIC_FAILURE;
        }
        for ( u32 i = 0u; i < first; ++i ) { put( i, a, at( i, a ) ); }
        for ( u32 k = 0u; k <= d; ++k ) {
            // IDs: the first sub-patch's controls, and the second's last.
            patch_control_t c = k < d ? at( first + k, a ) : at( first + 2u * d, a );
            c.position = math::Vec3d_Make( merged[k].c[0], merged[k].c[1], merged[k].c[2] );
            c.uv = math::vec2d_t{ merged[k].c[3], merged[k].c[4] };
            if ( !math::Vec3d_IsFinite( c.position ) || std::fabs( c.position.x ) > kPatchCoordinateMax ||
                 std::fabs( c.position.y ) > kPatchCoordinateMax || std::fabs( c.position.z ) > kPatchCoordinateMax ) {
                Vector_Shutdown( &next );
                return geometry_status_t::NUMERIC_FAILURE;
            }
            put( first + k, a, c );
        }
        for ( u32 i = first + 2u * d + 1u; i < cAlong; ++i ) { put( i - d, a, at( i, a ) ); }
    }
    Vector_Shutdown( &pPatch->controls );
    Vector_Move( &pPatch->controls, &next );
    if ( bColumns ) {
        pPatch->cColumns = cAlongNew;
    } else {
        pPatch->cRows = cAlongNew;
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t Patch_TryRemoveColumn( patch_surface_t *pPatch, u32 iSubU ) noexcept { return RemoveAlong( pPatch, true, iSubU ); }
geometry_status_t Patch_TryRemoveRow( patch_surface_t *pPatch, u32 iSubV ) noexcept { return RemoveAlong( pPatch, false, iSubV ); }

} // namespace cypher::editor::geometry
