//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PatchTessellation.cpp
//  Purpose: Implements level selection, grid evaluation, and
//           triangulation of Patch surfaces.
//  Details: Second-partial bounds for a degree-n tensor Bézier patch:
//             |S_ss| <= n(n-1) max |P[i+2][j] - 2P[i+1][j] + P[i][j]|
//             |S_st| <= n^2    max |P[i+1][j+1] - P[i+1][j] - P[i][j+1] + P[i][j]|
//             |S_tt| <= n(n-1) max |P[i][j+2] - 2P[i][j+1] + P[i][j]|
//           because each partial is itself a Bernstein combination of
//           those differences, and Bernstein weights sum to one.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PatchTessellation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct second_bounds_t {
    f64 uu;
    f64 uv;
    f64 vv;
};

bool PositionInRange( math::vec3d_t p ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= kPatchCoordinateMax &&
           std::fabs( p.y ) <= kPatchCoordinateMax &&
           std::fabs( p.z ) <= kPatchCoordinateMax;
}

bool UvInRange( math::vec2d_t uv ) noexcept
{
    return math::Vec2d_IsFinite( uv ) && std::fabs( uv.x ) <= kPatchCoordinateMax &&
           std::fabs( uv.y ) <= kPatchCoordinateMax;
}

math::vec3d_t At( const patch_surface_t *pPatch, u32 iSubU, u32 iSubV, u32 i, u32 j ) noexcept
{
    return pPatch->controls.pData[Patch_SubPatchControlIndex( pPatch, iSubU, iSubV, i, j )].position;
}

f64 Length( math::vec3d_t v ) noexcept
{
    return std::sqrt( math::Vec3d_LengthSquared( v ) );
}

second_bounds_t SecondBounds( const patch_surface_t *pPatch, u32 iSubU, u32 iSubV ) noexcept
{
    const u32 n = Patch_Degree( pPatch->basis );
    f64 muu = 0.0, muv = 0.0, mvv = 0.0;
    for ( u32 j = 0u; j <= n; ++j ) {
        for ( u32 i = 0u; i + 2u <= n; ++i ) {
            const math::vec3d_t d = math::Vec3d_Add(
                math::Vec3d_Subtract( At( pPatch, iSubU, iSubV, i + 2u, j ),
                                      math::Vec3d_Scale( At( pPatch, iSubU, iSubV, i + 1u, j ), 2.0 ) ),
                At( pPatch, iSubU, iSubV, i, j ) );
            const f64 l = Length( d );
            if ( l > muu ) { muu = l; }
        }
    }
    for ( u32 i = 0u; i <= n; ++i ) {
        for ( u32 j = 0u; j + 2u <= n; ++j ) {
            const math::vec3d_t d = math::Vec3d_Add(
                math::Vec3d_Subtract( At( pPatch, iSubU, iSubV, i, j + 2u ),
                                      math::Vec3d_Scale( At( pPatch, iSubU, iSubV, i, j + 1u ), 2.0 ) ),
                At( pPatch, iSubU, iSubV, i, j ) );
            const f64 l = Length( d );
            if ( l > mvv ) { mvv = l; }
        }
    }
    for ( u32 j = 0u; j < n; ++j ) {
        for ( u32 i = 0u; i < n; ++i ) {
            const math::vec3d_t d = math::Vec3d_Add(
                math::Vec3d_Subtract( At( pPatch, iSubU, iSubV, i + 1u, j + 1u ), At( pPatch, iSubU, iSubV, i + 1u, j ) ),
                math::Vec3d_Subtract( At( pPatch, iSubU, iSubV, i, j ), At( pPatch, iSubU, iSubV, i, j + 1u ) ) );
            const f64 l = Length( d );
            if ( l > muv ) { muv = l; }
        }
    }
    const f64 nn1 = static_cast<f64>( n * ( n - 1u ) );
    return second_bounds_t{ nn1 * muu, static_cast<f64>( n * n ) * muv, nn1 * mvv };
}

f64 Bound( const second_bounds_t &m, u32 ku, u32 kv ) noexcept
{
    const f64 hu = 1.0 / static_cast<f64>( ku );
    const f64 hv = 1.0 / static_cast<f64>( kv );
    return 0.125 * ( m.uu * hu * hu + 2.0 * m.uv * hu * hv + m.vv * hv * hv );
}

// Smallest (ku, kv) meeting the tolerance by greedy refinement of whichever
// axis currently contributes more error. Greedy is not globally minimal in
// ku * kv, but it is deterministic and never over-refines an axis whose own
// term is already negligible (a cylinder stays 1 segment along its length).
bool ChooseLevels( const second_bounds_t &m, f64 tol, u32 cap, u32 *pKu, u32 *pKv ) noexcept
{
    u32 ku = 1u, kv = 1u;
    while ( Bound( m, ku, kv ) > tol ) {
        const f64 hu = 1.0 / static_cast<f64>( ku ), hv = 1.0 / static_cast<f64>( kv );
        // Split the mixed term evenly between the axes when comparing.
        const f64 eu = m.uu * hu * hu + m.uv * hu * hv;
        const f64 ev = m.vv * hv * hv + m.uv * hu * hv;
        const bool bCanU = ku < cap, bCanV = kv < cap;
        if ( !bCanU && !bCanV ) {
            *pKu = ku;
            *pKv = kv;
            return false;
        }
        if ( bCanU && ( eu >= ev || !bCanV ) ) {
            ++ku;
        } else {
            ++kv;
        }
    }
    *pKu = ku;
    *pKv = kv;
    return true;
}

void ClearAll( patch_tessellation_t *pOut ) noexcept
{
    Vector_Clear( &pOut->positions );
    Vector_Clear( &pOut->normals );
    Vector_Clear( &pOut->uvs );
    Vector_Clear( &pOut->params );
    Vector_Clear( &pOut->vertexControl );
    Vector_Clear( &pOut->indices );
    Vector_Clear( &pOut->triangleSubPatch );
    Vector_Clear( &pOut->columnSubdivisions );
    Vector_Clear( &pOut->rowSubdivisions );
    pOut->cGridColumns = 0u;
    pOut->cGridRows = 0u;
    pOut->cCollapsedTriangles = 0u;
    pOut->cNormalFallbacks = 0u;
    pOut->cUnresolvedNormals = 0u;
    pOut->bToleranceMet = true;
}

template <typename value_t>
bool IsCanonicalEmptyVector( const vector_t<value_t> &values ) noexcept
{
    return values.pData == nullptr && values.nCount == 0u &&
           values.nCapacity == 0u && values.pAllocator == nullptr;
}

bool IsCanonicalEmpty( const patch_tessellation_t &out ) noexcept
{
    return IsCanonicalEmptyVector( out.positions ) &&
           IsCanonicalEmptyVector( out.normals ) &&
           IsCanonicalEmptyVector( out.uvs ) &&
           IsCanonicalEmptyVector( out.params ) &&
           IsCanonicalEmptyVector( out.vertexControl ) &&
           IsCanonicalEmptyVector( out.indices ) &&
           IsCanonicalEmptyVector( out.triangleSubPatch ) &&
           IsCanonicalEmptyVector( out.columnSubdivisions ) &&
           IsCanonicalEmptyVector( out.rowSubdivisions ) &&
           out.cGridColumns == 0u && out.cGridRows == 0u &&
           out.cCollapsedTriangles == 0u && out.cNormalFallbacks == 0u &&
           out.cUnresolvedNormals == 0u && out.bToleranceMet;
}

template <typename value_t>
bool IsBoundVector( const vector_t<value_t> &values, const allocator_t *pAllocator ) noexcept
{
    return Vector_IsValid( &values ) && values.pAllocator == pAllocator;
}

bool IsInitialized( const patch_tessellation_t *pOut ) noexcept
{
    if ( pOut == nullptr || !Allocator_IsValid( pOut->positions.pAllocator ) ) { return false; }
    const allocator_t *pAllocator = pOut->positions.pAllocator;
    return IsBoundVector( pOut->positions, pAllocator ) &&
           IsBoundVector( pOut->normals, pAllocator ) &&
           IsBoundVector( pOut->uvs, pAllocator ) &&
           IsBoundVector( pOut->params, pAllocator ) &&
           IsBoundVector( pOut->vertexControl, pAllocator ) &&
           IsBoundVector( pOut->indices, pAllocator ) &&
           IsBoundVector( pOut->triangleSubPatch, pAllocator ) &&
           IsBoundVector( pOut->columnSubdivisions, pAllocator ) &&
           IsBoundVector( pOut->rowSubdivisions, pAllocator );
}

void Publish( patch_tessellation_t *pOut, patch_tessellation_t *pNext ) noexcept
{
    Vector_Move( &pOut->positions, &pNext->positions );
    Vector_Move( &pOut->normals, &pNext->normals );
    Vector_Move( &pOut->uvs, &pNext->uvs );
    Vector_Move( &pOut->params, &pNext->params );
    Vector_Move( &pOut->vertexControl, &pNext->vertexControl );
    Vector_Move( &pOut->indices, &pNext->indices );
    Vector_Move( &pOut->triangleSubPatch, &pNext->triangleSubPatch );
    Vector_Move( &pOut->columnSubdivisions, &pNext->columnSubdivisions );
    Vector_Move( &pOut->rowSubdivisions, &pNext->rowSubdivisions );
}

// One grid line along an axis: which sub-patch evaluates it, at which local
// parameter, and whether it sits on a sub-patch corner (local 0 or 1).
struct axis_sample_t {
    u32 iSub;
    f64 local;
    u32 iControl; // control index along this axis when on a corner, else invalid
};

// Builds the grid-line list for one axis. Interior boundaries belong to the
// higher-index sub-patch at local 0 (matching Patch_Evaluate); the final
// line belongs to the last sub-patch at local 1.
bool BuildAxis( const vector_t<u32> &levels, u32 degree, vector_t<axis_sample_t> *pOut ) noexcept
{
    if ( levels.nCount == 0u || levels.nCount > static_cast<usize>( CY_U32_MAX ) ) { return false; }
    for ( usize a = 0u; a < levels.nCount; ++a ) {
        const u32 k = levels.pData[a];
        if ( k == 0u ) { return false; }
        for ( u32 i = 0u; i < k; ++i ) {
            axis_sample_t s{};
            s.iSub = static_cast<u32>( a );
            // i / k computed directly (not accumulated) so the same level
            // always yields bit-identical parameters.
            s.local = static_cast<f64>( i ) / static_cast<f64>( k );
            s.iControl = i == 0u ? static_cast<u32>( a ) * degree : CY_INVALID_INDEX;
            if ( !Vector_PushBack( pOut, s ) ) { return false; }
        }
    }
    const u32 last = static_cast<u32>( levels.nCount ) - 1u;
    return Vector_PushBack( pOut, axis_sample_t{ last, 1.0, ( last + 1u ) * degree } );
}

} // namespace

geometry_status_t PatchTessellation_Init( patch_tessellation_t *pOut, const allocator_t *pAllocator ) noexcept
{
    if ( pOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( IsInitialized( pOut ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !IsCanonicalEmpty( *pOut ) ) { return geometry_status_t::CORRUPT_STATE; }

    patch_tessellation_t next{};
    if ( !Vector_Init( &next.positions, pAllocator ) || !Vector_Init( &next.normals, pAllocator ) ||
         !Vector_Init( &next.uvs, pAllocator ) || !Vector_Init( &next.params, pAllocator ) ||
         !Vector_Init( &next.vertexControl, pAllocator ) || !Vector_Init( &next.indices, pAllocator ) ||
         !Vector_Init( &next.triangleSubPatch, pAllocator ) ||
         !Vector_Init( &next.columnSubdivisions, pAllocator ) ||
         !Vector_Init( &next.rowSubdivisions, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    Publish( pOut, &next );
    return geometry_status_t::OK;
}

void PatchTessellation_Shutdown( patch_tessellation_t *pOut ) noexcept
{
    if ( pOut == nullptr ) { return; }
    Vector_Shutdown( &pOut->positions );
    Vector_Shutdown( &pOut->normals );
    Vector_Shutdown( &pOut->uvs );
    Vector_Shutdown( &pOut->params );
    Vector_Shutdown( &pOut->vertexControl );
    Vector_Shutdown( &pOut->indices );
    Vector_Shutdown( &pOut->triangleSubPatch );
    Vector_Shutdown( &pOut->columnSubdivisions );
    Vector_Shutdown( &pOut->rowSubdivisions );
    pOut->cGridColumns = 0u;
    pOut->cGridRows = 0u;
    pOut->cCollapsedTriangles = 0u;
    pOut->cNormalFallbacks = 0u;
    pOut->cUnresolvedNormals = 0u;
    pOut->bToleranceMet = true;
}

f64 PatchTessellation_ErrorBound( const patch_surface_t *pPatch, u32 iSubU, u32 iSubV, u32 ku, u32 kv ) noexcept
{
    if ( !Patch_IsInitialized( pPatch ) || iSubU >= Patch_SubPatchColumns( pPatch ) ||
         iSubV >= Patch_SubPatchRows( pPatch ) || ku == 0u || kv == 0u ||
         ku > kPatchTessellationSubdivisionsMax || kv > kPatchTessellationSubdivisionsMax ) {
        return std::numeric_limits<f64>::infinity();
    }
    const u32 n = Patch_Degree( pPatch->basis );
    for ( u32 j = 0u; j <= n; ++j ) {
        for ( u32 i = 0u; i <= n; ++i ) {
            const u32 iControl = Patch_SubPatchControlIndex( pPatch, iSubU, iSubV, i, j );
            if ( iControl == CY_INVALID_INDEX ||
                 !PositionInRange( pPatch->controls.pData[iControl].position ) ) {
                return std::numeric_limits<f64>::infinity();
            }
        }
    }
    const f64 bound = Bound( SecondBounds( pPatch, iSubU, iSubV ), ku, kv );
    return std::isfinite( bound ) ? bound : std::numeric_limits<f64>::infinity();
}

geometry_status_t PatchTessellation_TryBuild(
    const patch_surface_t *pPatch,
    const patch_tessellation_options_t &options,
    patch_tessellation_t *pOut ) noexcept
{
    if ( pOut == nullptr || IsCanonicalEmpty( *pOut ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !IsInitialized( pOut ) ) { return geometry_status_t::CORRUPT_STATE; }
    ClearAll( pOut );
    if ( !Patch_IsInitialized( pPatch ) ) {
        return pPatch != nullptr && pPatch->controls.pAllocator != nullptr
                   ? geometry_status_t::CORRUPT_STATE
                   : geometry_status_t::NOT_INITIALIZED;
    }

    // IDs are deliberately irrelevant here: previews may tessellate a patch
    // whose control identities have not been authored yet. Numeric data and
    // storage structure are still checked independently of Patch_Validate's
    // identity-first fault order.
    const math::vec3d_t first = pPatch->controls.pData[0].position;
    bool bAllSame = true;
    for ( usize i = 0u; i < pPatch->controls.nCount; ++i ) {
        const patch_control_t &control = pPatch->controls.pData[i];
        if ( !PositionInRange( control.position ) || !UvInRange( control.uv ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( !math::Vec3d_EqualsExact( control.position, first ) ) { bAllSame = false; }
    }
    if ( bAllSame ) { return geometry_status_t::DEGENERATE; }

    const bool bFixed = options.fixedSubdivisions > 0u;
    if ( bFixed ) {
        if ( options.fixedSubdivisions > kPatchTessellationSubdivisionsMax ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    } else if ( !( options.fMaxChordError > 0.0 ) || !std::isfinite( options.fMaxChordError ) ||
                options.maxSubdivisions == 0u || options.maxSubdivisions > kPatchTessellationSubdivisionsMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const u32 n = Patch_Degree( pPatch->basis );
    const u32 cSubU = Patch_SubPatchColumns( pPatch );
    const u32 cSubV = Patch_SubPatchRows( pPatch );
    if ( !Vector_Resize( &pOut->columnSubdivisions, cSubU ) || !Vector_Resize( &pOut->rowSubdivisions, cSubV ) ) {
        ClearAll( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 a = 0u; a < cSubU; ++a ) { pOut->columnSubdivisions.pData[a] = bFixed ? options.fixedSubdivisions : 1u; }
    for ( u32 b = 0u; b < cSubV; ++b ) { pOut->rowSubdivisions.pData[b] = bFixed ? options.fixedSubdivisions : 1u; }

    if ( !bFixed ) {
        // Per-sub-patch levels, then max-reduce along columns and rows. The
        // bound decreases monotonically in both ku and kv, so raising a
        // sub-patch's levels to the shared maximum keeps it within tolerance.
        for ( u32 b = 0u; b < cSubV; ++b ) {
            for ( u32 a = 0u; a < cSubU; ++a ) {
                u32 ku = 1u, kv = 1u;
                if ( !ChooseLevels( SecondBounds( pPatch, a, b ), options.fMaxChordError, options.maxSubdivisions,
                                    &ku, &kv ) ) {
                    pOut->bToleranceMet = false;
                }
                if ( ku > pOut->columnSubdivisions.pData[a] ) { pOut->columnSubdivisions.pData[a] = ku; }
                if ( kv > pOut->rowSubdivisions.pData[b] ) { pOut->rowSubdivisions.pData[b] = kv; }
            }
        }
    }

    u64 cGridU64 = 1u, cGridV64 = 1u;
    for ( u32 a = 0u; a < cSubU; ++a ) {
        cGridU64 += static_cast<u64>( pOut->columnSubdivisions.pData[a] );
    }
    for ( u32 b = 0u; b < cSubV; ++b ) {
        cGridV64 += static_cast<u64>( pOut->rowSubdivisions.pData[b] );
    }
    if ( cGridU64 > static_cast<u64>( CY_U32_MAX ) ||
         cGridV64 > static_cast<u64>( CY_U32_MAX ) ||
         ( cGridV64 != 0u &&
           cGridU64 > static_cast<u64>( kPatchTessellationVerticesMax ) / cGridV64 ) ) {
        ClearAll( pOut );
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const u32 cGridU = static_cast<u32>( cGridU64 );
    const u32 cGridV = static_cast<u32>( cGridV64 );

    vector_t<axis_sample_t> axisU{}, axisV{};
    const bool bAxes = Vector_Init( &axisU, pOut->positions.pAllocator ) &&
                       Vector_Init( &axisV, pOut->positions.pAllocator ) &&
                       Vector_Reserve( &axisU, static_cast<usize>( cGridU ) ) &&
                       Vector_Reserve( &axisV, static_cast<usize>( cGridV ) ) &&
                       BuildAxis( pOut->columnSubdivisions, n, &axisU ) &&
                       BuildAxis( pOut->rowSubdivisions, n, &axisV );
    const usize cVerts = static_cast<usize>( cGridU ) * static_cast<usize>( cGridV );
    const usize cCells = static_cast<usize>( cGridU - 1u ) * static_cast<usize>( cGridV - 1u );
    if ( cCells > CY_USIZE_MAX / 6u || cCells > CY_USIZE_MAX / 2u ) {
        Vector_Shutdown( &axisU );
        Vector_Shutdown( &axisV );
        ClearAll( pOut );
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !bAxes || !Vector_Reserve( &pOut->positions, cVerts ) || !Vector_Reserve( &pOut->normals, cVerts ) ||
         !Vector_Reserve( &pOut->uvs, cVerts ) || !Vector_Reserve( &pOut->params, cVerts ) ||
         !Vector_Reserve( &pOut->vertexControl, cVerts ) || !Vector_Reserve( &pOut->indices, cCells * 6u ) ||
         !Vector_Reserve( &pOut->triangleSubPatch, cCells * 2u ) ) {
        Vector_Shutdown( &axisU );
        Vector_Shutdown( &axisV );
        ClearAll( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // Capacity is reserved above, so the pushes below cannot fail; the
    // results are still checked because PushBack is nodiscard and a silent
    // ignore would hide a future change to the reservation.
    bool bPushed = true;
    for ( usize gv = 0u; gv < axisV.nCount; ++gv ) {
        const axis_sample_t sv = axisV.pData[gv];
        for ( usize gu = 0u; gu < axisU.nCount; ++gu ) {
            const axis_sample_t su = axisU.pData[gu];
            const patch_sample_t s = Patch_EvaluateSubPatch( pPatch, su.iSub, sv.iSub, su.local, sv.local );
            if ( s.bNormalFromNeighbour ) { ++pOut->cNormalFallbacks; }
            if ( !s.bNormalValid ) { ++pOut->cUnresolvedNormals; }
            const math::vec2d_t param{ ( static_cast<f64>( su.iSub ) + su.local ) / static_cast<f64>( cSubU ),
                                       ( static_cast<f64>( sv.iSub ) + sv.local ) / static_cast<f64>( cSubV ) };
            const u32 iControl = ( su.iControl != CY_INVALID_INDEX && sv.iControl != CY_INVALID_INDEX )
                                     ? sv.iControl * pPatch->cColumns + su.iControl
                                     : CY_INVALID_INDEX;
            bPushed = bPushed && Vector_PushBack( &pOut->positions, s.position ) &&
                      Vector_PushBack( &pOut->normals, s.normal ) && Vector_PushBack( &pOut->uvs, s.uv ) &&
                      Vector_PushBack( &pOut->params, param ) && Vector_PushBack( &pOut->vertexControl, iControl );
        }
    }

    // The reserves above make this unreachable for the current vector
    // implementation. Keep the guard before index generation so a future
    // container-contract change cannot make the triangle loop read vertices
    // that were never appended.
    if ( !bPushed ) {
        Vector_Shutdown( &axisU );
        Vector_Shutdown( &axisV );
        ClearAll( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const u32 cols = cGridU;
    for ( u32 gv = 0u; gv + 1u < cGridV; ++gv ) {
        for ( u32 gu = 0u; gu + 1u < cGridU; ++gu ) {
            const u32 a = gv * cols + gu, b = a + 1u, c = a + cols + 1u, d = a + cols;
            const math::vec3d_t *p = pOut->positions.pData;
            // Shorter diagonal gives better-shaped triangles on sheared
            // cells; the tie goes to a-c so the choice is deterministic.
            const bool bAc = math::Vec3d_DistanceSquared( p[a], p[c] ) <= math::Vec3d_DistanceSquared( p[b], p[d] );
            const u32 tris[2][3] = { { a, b, bAc ? c : d }, { bAc ? a : b, c, d } };
            const u32 iSub = axisV.pData[gv].iSub * cSubU + axisU.pData[gu].iSub;
            for ( const auto &t : tris ) {
                const math::vec3d_t edge0 = math::Vec3d_Subtract( p[t[1]], p[t[0]] );
                const math::vec3d_t edge1 = math::Vec3d_Subtract( p[t[2]], p[t[0]] );
                const math::vec3d_t edge2 = math::Vec3d_Subtract( p[t[2]], p[t[1]] );
                const f64 areaSquared = math::Vec3d_LengthSquared( math::Vec3d_Cross( edge0, edge1 ) );
                const f64 maxEdgeSquared = std::max(
                    math::Vec3d_LengthSquared( edge0 ),
                    std::max( math::Vec3d_LengthSquared( edge1 ),
                              math::Vec3d_LengthSquared( edge2 ) ) );
                // A collapsed parametric boundary (for example a sphere or
                // cone pole) should evaluate to one point, but Bernstein
                // arithmetic can leave a few ulps between nominally equal
                // vertices. An exact area == 0 test consequently leaks tiny
                // sliver triangles. Compare the cross-product magnitude to
                // the local edge scale so the decision is translation- and
                // uniformly-scale-invariant without rejecting ordinary thin
                // triangles at authoring scale.
                constexpr f64 kCollapsedRelative =
                    64.0 * std::numeric_limits<f64>::epsilon();
                const f64 collapsedAreaSquared =
                    kCollapsedRelative * kCollapsedRelative *
                    maxEdgeSquared * maxEdgeSquared;
                const bool bCollapsed =
                    !std::isfinite( areaSquared ) ||
                    !std::isfinite( maxEdgeSquared ) ||
                    !( areaSquared > collapsedAreaSquared );
                if ( bCollapsed ) {
                    ++pOut->cCollapsedTriangles;
                    if ( options.bDropCollapsedTriangles ) { continue; }
                }
                bPushed = bPushed && Vector_PushBack( &pOut->indices, t[0] ) &&
                          Vector_PushBack( &pOut->indices, t[1] ) && Vector_PushBack( &pOut->indices, t[2] ) &&
                          Vector_PushBack( &pOut->triangleSubPatch, iSub );
            }
        }
    }
    Vector_Shutdown( &axisU );
    Vector_Shutdown( &axisV );
    if ( !bPushed ) {
        ClearAll( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pOut->cGridColumns = cols;
    pOut->cGridRows = cGridV;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
