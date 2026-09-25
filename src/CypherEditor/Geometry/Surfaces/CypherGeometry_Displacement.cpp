//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Displacement.cpp
//  Purpose: Implements displacement surfaces.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Displacement.h"

#include <cmath>
#include <utility>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

f64 Len( math::vec3d_t v ) noexcept { return std::sqrt( math::Vec3d_LengthSquared( v ) ); }

math::vec3d_t QuadNewell( const displacement_quad_t &q ) noexcept
{
    math::vec3d_t s{};
    for ( u32 i = 0u; i < 4u; ++i ) {
        const math::vec3d_t a = q.corners[i], b = q.corners[( i + 1u ) % 4u];
        s.x += ( a.y - b.y ) * ( a.z + b.z );
        s.y += ( a.z - b.z ) * ( a.x + b.x );
        s.z += ( a.x - b.x ) * ( a.y + b.y );
    }
    return s;
}

math::vec3d_t Direction( const displacement_t &d, const displacement_quad_t &q ) noexcept
{
    if ( d.bUseAxis ) { return d.axis; }
    const math::vec3d_t n = QuadNewell( q );
    return math::Vec3d_Scale( n, 1.0 / Len( n ) );
}

// A point k/n of the way along the edge p -> q, computed from the
// lexicographically smaller end so both faces on a shared edge get the
// same bits.
math::vec3d_t EdgePoint( math::vec3d_t p, math::vec3d_t q, u32 k, u32 n ) noexcept
{
    if ( math::Vec3d_LexicographicLess( q, p ) ) { return math::Vec3d_Lerp( q, p, static_cast<f64>( n - k ) / n ); }
    return math::Vec3d_Lerp( p, q, static_cast<f64>( k ) / n );
}

math::vec3d_t BasePoint( const displacement_quad_t &q, u32 i, u32 j, u32 n ) noexcept
{
    const math::vec3d_t *c = q.corners;
    if ( j == 0u ) { return i == 0u ? c[0] : i == n ? c[1] : EdgePoint( c[0], c[1], i, n ); }
    if ( j == n ) { return i == 0u ? c[3] : i == n ? c[2] : EdgePoint( c[3], c[2], i, n ); }
    if ( i == 0u ) { return EdgePoint( c[0], c[3], j, n ); }
    if ( i == n ) { return EdgePoint( c[1], c[2], j, n ); }
    const f64 u = static_cast<f64>( i ) / n, v = static_cast<f64>( j ) / n;
    return math::Vec3d_Lerp( math::Vec3d_Lerp( c[0], c[1], u ), math::Vec3d_Lerp( c[3], c[2], u ), v );
}

math::vec2d_t BaseUv( const displacement_quad_t &q, u32 i, u32 j, u32 n ) noexcept
{
    const f64 u = static_cast<f64>( i ) / n, v = static_cast<f64>( j ) / n;
    const math::vec2d_t a{ q.uvs[0].x + ( q.uvs[1].x - q.uvs[0].x ) * u, q.uvs[0].y + ( q.uvs[1].y - q.uvs[0].y ) * u };
    const math::vec2d_t b{ q.uvs[3].x + ( q.uvs[2].x - q.uvs[3].x ) * u, q.uvs[3].y + ( q.uvs[2].y - q.uvs[3].y ) * u };
    return math::vec2d_t{ a.x + ( b.x - a.x ) * v, a.y + ( b.y - a.y ) * v };
}

// Integer hash (murmur3 finaliser over a mixed lattice key): the same
// lattice point and seed give the same value everywhere.
u32 Hash( u32 x, u32 y, u32 z, u32 seed ) noexcept
{
    u32 h = seed * 0x9E3779B1u ^ x * 0x85EBCA77u ^ y * 0xC2B2AE3Du ^ z * 0x27D4EB2Fu;
    h ^= h >> 16u;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13u;
    h *= 0xC2B2AE35u;
    h ^= h >> 16u;
    return h;
}

f64 Lattice( i64 x, i64 y, i64 z, u32 seed ) noexcept
{
    const u32 h = Hash( static_cast<u32>( x ), static_cast<u32>( y ), static_cast<u32>( z ), seed );
    return static_cast<f64>( h ) / 4294967295.0 * 2.0 - 1.0;
}

f64 Smooth( f64 t ) noexcept { return t * t * ( 3.0 - 2.0 * t ); }

f64 ValueNoise( math::vec3d_t p, u32 seed ) noexcept
{
    const f64 fx = std::floor( p.x ), fy = std::floor( p.y ), fz = std::floor( p.z );
    const i64 x = static_cast<i64>( fx ), y = static_cast<i64>( fy ), z = static_cast<i64>( fz );
    const f64 tx = Smooth( p.x - fx ), ty = Smooth( p.y - fy ), tz = Smooth( p.z - fz );
    auto lerp = []( f64 a, f64 b, f64 t ) noexcept { return a + ( b - a ) * t; };
    const f64 x00 = lerp( Lattice( x, y, z, seed ), Lattice( x + 1, y, z, seed ), tx );
    const f64 x10 = lerp( Lattice( x, y + 1, z, seed ), Lattice( x + 1, y + 1, z, seed ), tx );
    const f64 x01 = lerp( Lattice( x, y, z + 1, seed ), Lattice( x + 1, y, z + 1, seed ), tx );
    const f64 x11 = lerp( Lattice( x, y + 1, z + 1, seed ), Lattice( x + 1, y + 1, z + 1, seed ), tx );
    return lerp( lerp( x00, x10, ty ), lerp( x01, x11, ty ), tz );
}

// fBm: octaves at doubling frequency and halving weight, normalised so
// the result stays within [-amplitude, amplitude].
f64 Noise( const displacement_noise_t &n, math::vec3d_t p ) noexcept
{
    if ( n.amplitude == 0.0 ) { return 0.0; }
    f64 sum = 0.0, weight = 1.0, total = 0.0, freq = n.frequency;
    for ( u32 o = 0u; o < n.cOctaves; ++o ) {
        sum += weight * ValueNoise( math::Vec3d_Scale( p, freq ), n.seed + o );
        total += weight;
        weight *= 0.5;
        freq *= 2.0;
    }
    return n.amplitude * sum / total;
}

u32 SideOf( u32 power ) noexcept { return ( 1u << power ) + 1u; }

// Grid indices along quad edge e (0: c0->c1, 1: c1->c2, 2: c2->c3,
// 3: c3->c0), from the edge's first corner to its second.
u32 EdgeIndex( u32 e, u32 t, u32 n ) noexcept
{
    const u32 side = n + 1u;
    switch ( e ) {
    case 0u: return t;                      // (t, 0)
    case 1u: return t * side + n;           // (n, t)
    case 2u: return n * side + ( n - t );   // (n - t, n)
    default: return ( n - t ) * side;       // (0, n - t)
    }
}

bool Near( math::vec3d_t a, math::vec3d_t b ) noexcept { return Len( math::Vec3d_Subtract( a, b ) ) <= kDisplacementSewEpsilon; }

} // namespace

u32 Displacement_Side( u32 power ) noexcept
{
    return power >= kDisplacementPowerMin && power <= kDisplacementPowerMax ? SideOf( power ) : 0u;
}

geometry_status_t Displacement_Init( displacement_t *pDisp, const allocator_t *pAllocator, u32 power ) noexcept
{
    if ( pDisp == nullptr || !Allocator_IsValid( pAllocator ) || Displacement_Side( power ) == 0u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pDisp->distances.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    const usize c = static_cast<usize>( SideOf( power ) ) * SideOf( power );
    if ( !Vector_Init( &pDisp->distances, pAllocator ) || !Vector_Init( &pDisp->offsets, pAllocator ) || !Vector_Init( &pDisp->alphas, pAllocator ) ||
         !Vector_Resize( &pDisp->distances, c ) || !Vector_Resize( &pDisp->offsets, c ) || !Vector_Resize( &pDisp->alphas, c ) ) {
        Displacement_Shutdown( pDisp );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < c; ++i ) {
        pDisp->distances.pData[i] = 0.0;
        pDisp->offsets.pData[i] = math::vec3d_t{};
        pDisp->alphas.pData[i] = 0.0;
    }
    pDisp->power = power;
    return geometry_status_t::OK;
}

void Displacement_Shutdown( displacement_t *pDisp ) noexcept
{
    if ( pDisp == nullptr ) { return; }
    Vector_Shutdown( &pDisp->distances );
    Vector_Shutdown( &pDisp->offsets );
    Vector_Shutdown( &pDisp->alphas );
}

geometry_status_t Displacement_TryClone( const displacement_t *pSource, const allocator_t *pAllocator, displacement_t *pOut ) noexcept
{
    if ( pSource == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t st = Displacement_Validate( pSource );
    if ( st != geometry_status_t::OK ) { return st; }
    st = Displacement_Init( pOut, pAllocator, pSource->power );
    if ( st != geometry_status_t::OK ) { return st; }
    for ( usize i = 0u; i < pSource->distances.nCount; ++i ) {
        pOut->distances.pData[i] = pSource->distances.pData[i];
        pOut->offsets.pData[i] = pSource->offsets.pData[i];
        pOut->alphas.pData[i] = pSource->alphas.pData[i];
    }
    pOut->elevation = pSource->elevation;
    pOut->noise = pSource->noise;
    pOut->bUseAxis = pSource->bUseAxis;
    pOut->axis = pSource->axis;
    return geometry_status_t::OK;
}

geometry_status_t Displacement_Validate( const displacement_t *pDisp ) noexcept
{
    if ( pDisp == nullptr || pDisp->distances.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    const u32 side = Displacement_Side( pDisp->power );
    const usize c = static_cast<usize>( side ) * side;
    if ( side == 0u || pDisp->distances.nCount != c || pDisp->offsets.nCount != c || pDisp->alphas.nCount != c ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    for ( usize i = 0u; i < c; ++i ) {
        const f64 a = pDisp->alphas.pData[i];
        if ( !std::isfinite( pDisp->distances.pData[i] ) || !math::Vec3d_IsFinite( pDisp->offsets.pData[i] ) || !std::isfinite( a ) || a < 0.0 ||
             a > 1.0 ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    const displacement_noise_t &n = pDisp->noise;
    if ( !std::isfinite( pDisp->elevation ) || !std::isfinite( n.amplitude ) || n.amplitude < 0.0 ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( n.amplitude > 0.0 && ( !std::isfinite( n.frequency ) || !( n.frequency > 0.0 ) || n.cOctaves < 1u || n.cOctaves > kDisplacementOctavesMax ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pDisp->bUseAxis && ( !math::Vec3d_IsFinite( pDisp->axis ) || std::fabs( Len( pDisp->axis ) - 1.0 ) > 1e-9 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

geometry_status_t Displacement_ValidateQuad( const displacement_quad_t &quad ) noexcept
{
    f64 perimeter = 0.0;
    for ( u32 i = 0u; i < 4u; ++i ) {
        if ( !math::Vec3d_IsFinite( quad.corners[i] ) || !std::isfinite( quad.uvs[i].x ) || !std::isfinite( quad.uvs[i].y ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        perimeter += Len( math::Vec3d_Subtract( quad.corners[( i + 1u ) % 4u], quad.corners[i] ) );
    }
    const f64 area2 = Len( QuadNewell( quad ) );
    return std::isfinite( area2 ) && area2 > 1e-12 * perimeter * perimeter ? geometry_status_t::OK : geometry_status_t::DEGENERATE;
}

geometry_status_t DisplacementGrid_Init( displacement_grid_t *pGrid, const allocator_t *pAllocator ) noexcept
{
    if ( pGrid == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &pGrid->positions, pAllocator ) || !Vector_Init( &pGrid->normals, pAllocator ) || !Vector_Init( &pGrid->uvs, pAllocator ) ||
         !Vector_Init( &pGrid->alphas, pAllocator ) ) {
        DisplacementGrid_Shutdown( pGrid );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pGrid->side = 0u;
    return geometry_status_t::OK;
}

void DisplacementGrid_Shutdown( displacement_grid_t *pGrid ) noexcept
{
    if ( pGrid == nullptr ) { return; }
    Vector_Shutdown( &pGrid->positions );
    Vector_Shutdown( &pGrid->normals );
    Vector_Shutdown( &pGrid->uvs );
    Vector_Shutdown( &pGrid->alphas );
    pGrid->side = 0u;
}

geometry_status_t Displacement_TryEvaluate( const displacement_t *pDisp, const displacement_quad_t &quad, displacement_grid_t *pGrid ) noexcept
{
    if ( pGrid == nullptr || pGrid->positions.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    pGrid->side = 0u;
    Vector_Clear( &pGrid->positions );
    Vector_Clear( &pGrid->normals );
    Vector_Clear( &pGrid->uvs );
    Vector_Clear( &pGrid->alphas );
    geometry_status_t st = Displacement_Validate( pDisp );
    if ( st == geometry_status_t::OK ) { st = Displacement_ValidateQuad( quad ); }
    if ( st != geometry_status_t::OK ) { return st; }
    const u32 side = SideOf( pDisp->power ), n = side - 1u;
    const usize c = static_cast<usize>( side ) * side;
    if ( !Vector_Resize( &pGrid->positions, c ) || !Vector_Resize( &pGrid->normals, c ) || !Vector_Resize( &pGrid->uvs, c ) ||
         !Vector_Resize( &pGrid->alphas, c ) ) {
        Vector_Clear( &pGrid->positions );
        Vector_Clear( &pGrid->normals );
        Vector_Clear( &pGrid->uvs );
        Vector_Clear( &pGrid->alphas );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const math::vec3d_t dir = Direction( *pDisp, quad );
    for ( u32 j = 0u; j < side; ++j ) {
        for ( u32 i = 0u; i < side; ++i ) {
            const usize k = static_cast<usize>( j ) * side + i;
            const math::vec3d_t base = BasePoint( quad, i, j, n );
            const f64 push = pDisp->distances.pData[k] + pDisp->elevation + Noise( pDisp->noise, base );
            pGrid->positions.pData[k] = math::Vec3d_Add( math::Vec3d_Add( base, math::Vec3d_Scale( dir, push ) ), pDisp->offsets.pData[k] );
            pGrid->uvs.pData[k] = BaseUv( quad, i, j, n );
            pGrid->alphas.pData[k] = pDisp->alphas.pData[k];
        }
    }
    // Normals by differences across the grid (one-sided on the border);
    // du x dv faces out of a counter-clockwise quad.
    for ( u32 j = 0u; j < side; ++j ) {
        for ( u32 i = 0u; i < side; ++i ) {
            const u32 i0 = i > 0u ? i - 1u : i, i1 = i < n ? i + 1u : i;
            const u32 j0 = j > 0u ? j - 1u : j, j1 = j < n ? j + 1u : j;
            const math::vec3d_t du = math::Vec3d_Subtract( pGrid->positions.pData[j * side + i1], pGrid->positions.pData[j * side + i0] );
            const math::vec3d_t dv = math::Vec3d_Subtract( pGrid->positions.pData[j1 * side + i], pGrid->positions.pData[j0 * side + i] );
            const math::vec3d_t nv = math::Vec3d_Cross( du, dv );
            const f64 len = Len( nv );
            pGrid->normals.pData[j * side + i] = len > 0.0 ? math::Vec3d_Scale( nv, 1.0 / len ) : dir;
        }
    }
    pGrid->side = side;
    return geometry_status_t::OK;
}

geometry_status_t Displacement_TryResample( displacement_t *pDisp, u32 newPower ) noexcept
{
    geometry_status_t st = Displacement_Validate( pDisp );
    if ( st != geometry_status_t::OK ) { return st; }
    if ( Displacement_Side( newPower ) == 0u ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( newPower == pDisp->power ) { return geometry_status_t::OK; }
    displacement_t next{};
    st = Displacement_Init( &next, pDisp->distances.pAllocator, newPower );
    if ( st != geometry_status_t::OK ) { return st; }
    const u32 oldSide = SideOf( pDisp->power ), newSide = SideOf( newPower );
    const f64 scale = static_cast<f64>( oldSide - 1u ) / ( newSide - 1u );
    for ( u32 j = 0u; j < newSide; ++j ) {
        for ( u32 i = 0u; i < newSide; ++i ) {
            // Position in the old grid; integer when it lands on a sample.
            const f64 x = i * scale, y = j * scale;
            const u32 x0 = static_cast<u32>( std::floor( x ) ), y0 = static_cast<u32>( std::floor( y ) );
            const u32 x1 = x0 + 1u < oldSide ? x0 + 1u : x0, y1 = y0 + 1u < oldSide ? y0 + 1u : y0;
            const f64 tx = x - x0, ty = y - y0;
            const usize a = static_cast<usize>( y0 ) * oldSide + x0, b = static_cast<usize>( y0 ) * oldSide + x1;
            const usize c = static_cast<usize>( y1 ) * oldSide + x0, d = static_cast<usize>( y1 ) * oldSide + x1;
            auto mix = [&]( f64 va, f64 vb, f64 vc, f64 vd ) noexcept {
                return ( va + ( vb - va ) * tx ) + ( ( vc + ( vd - vc ) * tx ) - ( va + ( vb - va ) * tx ) ) * ty;
            };
            const usize k = static_cast<usize>( j ) * newSide + i;
            next.distances.pData[k] = mix( pDisp->distances.pData[a], pDisp->distances.pData[b], pDisp->distances.pData[c], pDisp->distances.pData[d] );
            next.alphas.pData[k] = mix( pDisp->alphas.pData[a], pDisp->alphas.pData[b], pDisp->alphas.pData[c], pDisp->alphas.pData[d] );
            const math::vec3d_t oa = pDisp->offsets.pData[a], ob = pDisp->offsets.pData[b], oc = pDisp->offsets.pData[c], od = pDisp->offsets.pData[d];
            next.offsets.pData[k] = math::Vec3d_Make( mix( oa.x, ob.x, oc.x, od.x ), mix( oa.y, ob.y, oc.y, od.y ), mix( oa.z, ob.z, oc.z, od.z ) );
        }
    }
    // Swap the arrays in (allocation-free); the old ones die with `next`.
    std::swap( pDisp->distances.pData, next.distances.pData );
    std::swap( pDisp->distances.nCount, next.distances.nCount );
    std::swap( pDisp->distances.nCapacity, next.distances.nCapacity );
    std::swap( pDisp->offsets.pData, next.offsets.pData );
    std::swap( pDisp->offsets.nCount, next.offsets.nCount );
    std::swap( pDisp->offsets.nCapacity, next.offsets.nCapacity );
    std::swap( pDisp->alphas.pData, next.alphas.pData );
    std::swap( pDisp->alphas.nCount, next.alphas.nCount );
    std::swap( pDisp->alphas.nCapacity, next.alphas.nCapacity );
    pDisp->power = newPower;
    Displacement_Shutdown( &next );
    return geometry_status_t::OK;
}

geometry_status_t Displacement_TrySew( displacement_t *pA, const displacement_quad_t &quadA, displacement_t *pB, const displacement_quad_t &quadB ) noexcept
{
    if ( pA == nullptr || pB == nullptr || pA == pB ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_status_t st = Displacement_Validate( pA );
    if ( st == geometry_status_t::OK ) { st = Displacement_Validate( pB ); }
    if ( st == geometry_status_t::OK ) { st = Displacement_ValidateQuad( quadA ); }
    if ( st == geometry_status_t::OK ) { st = Displacement_ValidateQuad( quadB ); }
    if ( st != geometry_status_t::OK ) { return st; }
    if ( pA->power != pB->power ) { return geometry_status_t::UNSUPPORTED; }
    // Find the one shared edge (either direction).
    u32 cShared = 0u, ea = 0u, eb = 0u;
    bool bReversed = false;
    for ( u32 i = 0u; i < 4u; ++i ) {
        for ( u32 k = 0u; k < 4u; ++k ) {
            const math::vec3d_t a0 = quadA.corners[i], a1 = quadA.corners[( i + 1u ) % 4u];
            const math::vec3d_t b0 = quadB.corners[k], b1 = quadB.corners[( k + 1u ) % 4u];
            const bool bRev = Near( a0, b1 ) && Near( a1, b0 ), bSame = Near( a0, b0 ) && Near( a1, b1 );
            if ( bRev || bSame ) {
                ++cShared;
                ea = i;
                eb = k;
                bReversed = bRev;
            }
        }
    }
    if ( cShared != 1u ) { return geometry_status_t::INVALID_ARGUMENT; }

    const allocator_t *pAlloc = pA->distances.pAllocator;
    displacement_grid_t ga{}, gb{};
    st = DisplacementGrid_Init( &ga, pAlloc );
    if ( st == geometry_status_t::OK ) { st = DisplacementGrid_Init( &gb, pAlloc ); }
    if ( st == geometry_status_t::OK ) { st = Displacement_TryEvaluate( pA, quadA, &ga ); }
    if ( st == geometry_status_t::OK ) { st = Displacement_TryEvaluate( pB, quadB, &gb ); }
    if ( st != geometry_status_t::OK ) {
        DisplacementGrid_Shutdown( &ga );
        DisplacementGrid_Shutdown( &gb );
        return st;
    }
    // Nothing below allocates, so the writes are all-or-nothing.
    const u32 side = ga.side, n = side - 1u;
    const math::vec3d_t dirA = Direction( *pA, quadA ), dirB = Direction( *pB, quadB );
    for ( u32 t = 0u; t <= n; ++t ) {
        const u32 ia = EdgeIndex( ea, t, n ), ib = EdgeIndex( eb, bReversed ? n - t : t, n );
        const math::vec3d_t target = math::Vec3d_Scale( math::Vec3d_Add( ga.positions.pData[ia], gb.positions.pData[ib] ), 0.5 );
        // Encode the whole move as offset: base + dir * (elevation + noise) + offset = target.
        const math::vec3d_t baseA = BasePoint( quadA, ia % side, ia / side, n ), baseB = BasePoint( quadB, ib % side, ib / side, n );
        const f64 pushA = pA->elevation + Noise( pA->noise, baseA ), pushB = pB->elevation + Noise( pB->noise, baseB );
        pA->distances.pData[ia] = 0.0;
        pA->offsets.pData[ia] = math::Vec3d_Subtract( target, math::Vec3d_Add( baseA, math::Vec3d_Scale( dirA, pushA ) ) );
        pB->distances.pData[ib] = 0.0;
        pB->offsets.pData[ib] = math::Vec3d_Subtract( target, math::Vec3d_Add( baseB, math::Vec3d_Scale( dirB, pushB ) ) );
    }
    DisplacementGrid_Shutdown( &ga );
    DisplacementGrid_Shutdown( &gb );
    return geometry_status_t::OK;
}

geometry_status_t Displacement_TryBuildMesh( const displacement_t *pDisp, const displacement_quad_t &quad, geometry_material_ref_t material,
                                             const allocator_t *pAllocator, geometry_source_id_allocator_t *pIdAllocator, mesh_source_t *pOut ) noexcept
{
    if ( pIdAllocator == nullptr || pOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    displacement_grid_t grid{};
    geometry_status_t st = DisplacementGrid_Init( &grid, pAllocator );
    if ( st == geometry_status_t::OK ) { st = Displacement_TryEvaluate( pDisp, quad, &grid ); }
    geometry_source_id_allocator_t ids = *pIdAllocator;
    mesh_source_description_t desc{};
    geometry_source_id_result_t root = GeometrySourceIdAllocator_Allocate( &ids );
    if ( st == geometry_status_t::OK ) { st = root.status; }
    if ( st == geometry_status_t::OK ) { st = MeshSourceDescription_Init( &desc, pAllocator, root.id ); }
    const u32 side = grid.side;
    for ( u32 k = 0u; st == geometry_status_t::OK && k < side * side; ++k ) {
        const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( &ids );
        st = id.status;
        if ( st == geometry_status_t::OK ) { st = MeshSourceDescription_TryAddVertex( &desc, grid.positions.pData[k], id.id, nullptr ); }
    }
    for ( u32 j = 0u; st == geometry_status_t::OK && j + 1u < side; ++j ) {
        for ( u32 i = 0u; st == geometry_status_t::OK && i + 1u < side; ++i ) {
            const u32 q[4] = { j * side + i, j * side + i + 1u, ( j + 1u ) * side + i + 1u, ( j + 1u ) * side + i };
            const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( &ids );
            st = id.status;
            mesh_face_attributes_t attributes{};
            attributes.material = material;
            attributes.smoothingGroups = 1u;
            u32 iFace = 0u;
            if ( st == geometry_status_t::OK ) { st = MeshSourceDescription_TryAddFace( &desc, span_t<const u32>{ q, 4u }, id.id, attributes, &iFace ); }
            if ( st == geometry_status_t::OK ) {
                // Per-corner UVs, and alpha in the colour's alpha channel for blend materials.
                const mesh_source_face_t &face = desc.faces.pData[iFace];
                for ( u32 c = 0u; c < 4u; ++c ) {
                    mesh_corner_attributes_t &a = desc.corners.pData[face.iFirstCorner + c].attributes;
                    a.uv0 = grid.uvs.pData[q[c]];
                    a.colorRgba = 0xFFFFFF00u | static_cast<u32>( std::lround( grid.alphas.pData[q[c]] * 255.0 ) );
                }
            }
        }
    }
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryBuild( &desc, pAllocator, pOut ); }
    if ( st == geometry_status_t::OK ) { *pIdAllocator = ids; }
    MeshSourceDescription_Shutdown( &desc );
    DisplacementGrid_Shutdown( &grid );
    return st;
}

} // namespace cypher::editor::geometry
