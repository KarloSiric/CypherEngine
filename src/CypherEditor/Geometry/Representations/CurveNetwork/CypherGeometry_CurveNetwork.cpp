//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CurveNetwork.cpp
//  Purpose: Implements CurveNetwork storage, Catmull-Rom conversion,
//           shape-preserving splitting, evaluation, and arc length.
//  Details: Uniform Catmull-Rom -> Bézier: for the span p1 -> p2 with
//           neighbours p0, p3 the equivalent cubic Bézier handles are
//             h0 = p1 + (p2 - p0) / 6,   h1 = p2 - (p3 - p1) / 6,
//           which reproduces the Catmull-Rom curve exactly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CurveNetwork.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

math::vec3d_t Lerp( math::vec3d_t a, math::vec3d_t b, f64 t ) noexcept
{
    return math::Vec3d_Add( a, math::Vec3d_Scale( math::Vec3d_Subtract( b, a ), t ) );
}

f64 Clamp01( f64 t ) noexcept { return t < 0.0 ? 0.0 : ( t > 1.0 ? 1.0 : t ); }

// Control polygon of a segment (linear segments use their endpoints for all
// four so the same evaluation code applies).
void Controls( const curve_network_t *pNet, const curve_segment_t &s, math::vec3d_t c[4] ) noexcept
{
    c[0] = pNet->nodes.pData[s.iNode0].position;
    c[3] = pNet->nodes.pData[s.iNode1].position;
    if ( s.basis == curve_basis_t::CUBIC_BEZIER ) {
        c[1] = s.handle0;
        c[2] = s.handle1;
    } else {
        c[1] = Lerp( c[0], c[3], 1.0 / 3.0 );
        c[2] = Lerp( c[0], c[3], 2.0 / 3.0 );
    }
}

math::vec3d_t Bezier( const math::vec3d_t c[4], f64 t ) noexcept
{
    const f64 u = 1.0 - t;
    const f64 b0 = u * u * u, b1 = 3.0 * u * u * t, b2 = 3.0 * u * t * t, b3 = t * t * t;
    return math::Vec3d_Add( math::Vec3d_Add( math::Vec3d_Scale( c[0], b0 ), math::Vec3d_Scale( c[1], b1 ) ),
                            math::Vec3d_Add( math::Vec3d_Scale( c[2], b2 ), math::Vec3d_Scale( c[3], b3 ) ) );
}

math::vec3d_t BezierDerivative( const math::vec3d_t c[4], f64 t ) noexcept
{
    const f64 u = 1.0 - t;
    const math::vec3d_t d0 = math::Vec3d_Subtract( c[1], c[0] );
    const math::vec3d_t d1 = math::Vec3d_Subtract( c[2], c[1] );
    const math::vec3d_t d2 = math::Vec3d_Subtract( c[3], c[2] );
    return math::Vec3d_Scale(
        math::Vec3d_Add( math::Vec3d_Add( math::Vec3d_Scale( d0, u * u ), math::Vec3d_Scale( d1, 2.0 * u * t ) ),
                         math::Vec3d_Scale( d2, t * t ) ),
        3.0 );
}

// 5-point Gauss-Legendre nodes/weights on [-1, 1].
constexpr f64 kGLx[5] = { 0.0, -0.5384693101056831, 0.5384693101056831, -0.9061798459386640,
                          0.9061798459386640 };
constexpr f64 kGLw[5] = { 0.5688888888888889, 0.4786286704993665, 0.4786286704993665,
                          0.2369268850561891, 0.2369268850561891 };

f64 GaussLength( const math::vec3d_t c[4], f64 a, f64 b ) noexcept
{
    const f64 half = 0.5 * ( b - a ), mid = 0.5 * ( a + b );
    f64 sum = 0.0;
    for ( int i = 0; i < 5; ++i ) {
        sum += kGLw[i] * std::sqrt( math::Vec3d_LengthSquared( BezierDerivative( c, mid + half * kGLx[i] ) ) );
    }
    return sum * half;
}

f64 AdaptiveLength( const math::vec3d_t c[4], f64 a, f64 b, f64 whole, f64 tol, int depth ) noexcept
{
    const f64 m = 0.5 * ( a + b );
    const f64 left = GaussLength( c, a, m ), right = GaussLength( c, m, b );
    if ( depth <= 0 || std::fabs( left + right - whole ) <= tol * ( left + right ) ) {
        return left + right;
    }
    return AdaptiveLength( c, a, m, left, tol, depth - 1 ) + AdaptiveLength( c, m, b, right, tol, depth - 1 );
}

bool SegmentOk( const curve_network_t *pNet, u32 i ) noexcept
{
    return CurveNetwork_IsInitialized( pNet ) && i < pNet->segments.nCount;
}

} // namespace

bool CurveNetwork_IsInitialized( const curve_network_t *pNet ) noexcept
{
    return pNet != nullptr && pNet->nodes.pAllocator != nullptr;
}

geometry_status_t CurveNetwork_Init(
    curve_network_t *pNet,
    const allocator_t *pAllocator,
    geometry_source_id_t networkId ) noexcept
{
    if ( pNet == nullptr || !Allocator_IsValid( pAllocator ) || !GeometrySourceId_IsValid( networkId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !Vector_Init( &pNet->nodes, pAllocator ) || !Vector_Init( &pNet->segments, pAllocator ) ||
         !Vector_Init( &pNet->steps, pAllocator ) || !Vector_Init( &pNet->paths, pAllocator ) ) {
        CurveNetwork_Shutdown( pNet );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pNet->sourceId = networkId;
    return geometry_status_t::OK;
}

void CurveNetwork_Shutdown( curve_network_t *pNet ) noexcept
{
    if ( pNet == nullptr ) { return; }
    Vector_Shutdown( &pNet->nodes );
    Vector_Shutdown( &pNet->segments );
    Vector_Shutdown( &pNet->steps );
    Vector_Shutdown( &pNet->paths );
    pNet->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

geometry_status_t CurveNetwork_TryAddNode(
    curve_network_t *pNet,
    math::vec3d_t position,
    geometry_source_id_t nodeId,
    u32 *pIndexOut ) noexcept
{
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( nodeId ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec3d_IsFinite( position ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    if ( pNet->nodes.nCount >= kCurveNetworkNodesMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    if ( !Vector_PushBack( &pNet->nodes, curve_node_t{ position, nodeId } ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( pIndexOut ) { *pIndexOut = static_cast<u32>( pNet->nodes.nCount - 1u ); }
    return geometry_status_t::OK;
}

geometry_status_t CurveNetwork_TryAddSegment(
    curve_network_t *pNet,
    u32 iNode0,
    u32 iNode1,
    curve_basis_t basis,
    math::vec3d_t handle0,
    math::vec3d_t handle1,
    geometry_source_id_t segmentId,
    u32 *pIndexOut ) noexcept
{
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( segmentId ) || iNode0 == iNode1 ||
         ( basis != curve_basis_t::LINEAR && basis != curve_basis_t::CUBIC_BEZIER ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( iNode0 >= pNet->nodes.nCount || iNode1 >= pNet->nodes.nCount ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    if ( basis == curve_basis_t::CUBIC_BEZIER &&
         ( !math::Vec3d_IsFinite( handle0 ) || !math::Vec3d_IsFinite( handle1 ) ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( pNet->segments.nCount >= kCurveNetworkSegmentsMax ) { return geometry_status_t::LIMIT_EXCEEDED; }
    curve_segment_t s{};
    s.iNode0 = iNode0;
    s.iNode1 = iNode1;
    s.basis = basis;
    s.handle0 = basis == curve_basis_t::CUBIC_BEZIER ? handle0 : pNet->nodes.pData[iNode0].position;
    s.handle1 = basis == curve_basis_t::CUBIC_BEZIER ? handle1 : pNet->nodes.pData[iNode1].position;
    s.sourceId = segmentId;
    if ( !Vector_PushBack( &pNet->segments, s ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    if ( pIndexOut ) { *pIndexOut = static_cast<u32>( pNet->segments.nCount - 1u ); }
    return geometry_status_t::OK;
}

u32 CurveNetwork_StepStartNode( const curve_network_t *pNet, curve_path_step_t step ) noexcept
{
    if ( !SegmentOk( pNet, step.iSegment ) ) { return CY_INVALID_INDEX; }
    const curve_segment_t &s = pNet->segments.pData[step.iSegment];
    return step.bReversed ? s.iNode1 : s.iNode0;
}

u32 CurveNetwork_StepEndNode( const curve_network_t *pNet, curve_path_step_t step ) noexcept
{
    if ( !SegmentOk( pNet, step.iSegment ) ) { return CY_INVALID_INDEX; }
    const curve_segment_t &s = pNet->segments.pData[step.iSegment];
    return step.bReversed ? s.iNode0 : s.iNode1;
}

geometry_status_t CurveNetwork_TryAddPath(
    curve_network_t *pNet,
    span_t<const curve_path_step_t> steps,
    bool bClosed,
    geometry_source_id_t pathId,
    u32 *pIndexOut ) noexcept
{
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySourceId_IsValid( pathId ) || steps.pData == nullptr || steps.nCount == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pNet->paths.nCount >= kCurveNetworkPathsMax ||
         pNet->steps.nCount + steps.nCount > kCurveNetworkPathStepsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    for ( usize i = 0u; i < steps.nCount; ++i ) {
        if ( !SegmentOk( pNet, steps.pData[i].iSegment ) ) { return geometry_status_t::INVALID_HANDLE; }
        if ( i > 0u && CurveNetwork_StepEndNode( pNet, steps.pData[i - 1u] ) !=
                           CurveNetwork_StepStartNode( pNet, steps.pData[i] ) ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
    }
    if ( bClosed && CurveNetwork_StepEndNode( pNet, steps.pData[steps.nCount - 1u] ) !=
                        CurveNetwork_StepStartNode( pNet, steps.pData[0] ) ) {
        return geometry_status_t::INVALID_TOPOLOGY;
    }
    if ( !Vector_Reserve( &pNet->steps, pNet->steps.nCount + steps.nCount ) ||
         !Vector_Reserve( &pNet->paths, pNet->paths.nCount + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    curve_path_t p{};
    p.iFirstStep = static_cast<u32>( pNet->steps.nCount );
    p.cSteps = static_cast<u32>( steps.nCount );
    p.bClosed = bClosed;
    p.sourceId = pathId;
    for ( usize i = 0u; i < steps.nCount; ++i ) { (void)Vector_PushBack( &pNet->steps, steps.pData[i] ); }
    (void)Vector_PushBack( &pNet->paths, p );
    if ( pIndexOut ) { *pIndexOut = static_cast<u32>( pNet->paths.nCount - 1u ); }
    return geometry_status_t::OK;
}

geometry_status_t CurveNetwork_TryAddCatmullRomPath(
    curve_network_t *pNet,
    span_t<const math::vec3d_t> points,
    bool bClosed,
    geometry_source_id_allocator_t *pIdAllocator,
    u32 *pPathIndexOut ) noexcept
{
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pIdAllocator == nullptr || points.pData == nullptr || points.nCount < 2u ||
         ( bClosed && points.nCount < 3u ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const usize n = points.nCount;
    const usize cSeg = bClosed ? n : n - 1u;
    for ( usize i = 0u; i < n; ++i ) {
        if ( !math::Vec3d_IsFinite( points.pData[i] ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    if ( pNet->nodes.nCount + n > kCurveNetworkNodesMax ||
         pNet->segments.nCount + cSeg > kCurveNetworkSegmentsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const usize nodesBefore = pNet->nodes.nCount, segsBefore = pNet->segments.nCount;
    const usize stepsBefore = pNet->steps.nCount, pathsBefore = pNet->paths.nCount;
    geometry_source_id_allocator_t ids = *pIdAllocator;
    auto rollback = [&]( geometry_status_t s ) noexcept {
        (void)Vector_Resize( &pNet->nodes, nodesBefore );
        (void)Vector_Resize( &pNet->segments, segsBefore );
        (void)Vector_Resize( &pNet->steps, stepsBefore );
        (void)Vector_Resize( &pNet->paths, pathsBefore );
        return s;
    };
    for ( usize i = 0u; i < n; ++i ) {
        const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( &ids );
        if ( id.status != geometry_status_t::OK ) { return rollback( id.status ); }
        const geometry_status_t s = CurveNetwork_TryAddNode( pNet, points.pData[i], id.id, nullptr );
        if ( s != geometry_status_t::OK ) { return rollback( s ); }
    }
    auto P = [&]( i64 i ) noexcept {
        if ( bClosed ) { return points.pData[static_cast<usize>( ( i % static_cast<i64>( n ) + static_cast<i64>( n ) ) % static_cast<i64>( n ) )]; }
        if ( i < 0 ) { return points.pData[0]; }
        if ( i >= static_cast<i64>( n ) ) { return points.pData[n - 1u]; }
        return points.pData[static_cast<usize>( i )];
    };
    vector_t<curve_path_step_t> pathSteps{};
    if ( !Vector_Init( &pathSteps, pNet->nodes.pAllocator, cSeg ) ) { return rollback( geometry_status_t::ALLOCATION_FAILED ); }
    for ( usize i = 0u; i < cSeg; ++i ) {
        const i64 k = static_cast<i64>( i );
        const math::vec3d_t p0 = P( k - 1 ), p1 = P( k ), p2 = P( k + 1 ), p3 = P( k + 2 );
        const math::vec3d_t h0 = math::Vec3d_Add( p1, math::Vec3d_Scale( math::Vec3d_Subtract( p2, p0 ), 1.0 / 6.0 ) );
        const math::vec3d_t h1 = math::Vec3d_Subtract( p2, math::Vec3d_Scale( math::Vec3d_Subtract( p3, p1 ), 1.0 / 6.0 ) );
        const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( &ids );
        u32 iSeg = 0u;
        const geometry_status_t s = id.status != geometry_status_t::OK
            ? id.status
            : CurveNetwork_TryAddSegment( pNet, static_cast<u32>( nodesBefore + i ),
                                          static_cast<u32>( nodesBefore + ( i + 1u ) % n ),
                                          curve_basis_t::CUBIC_BEZIER, h0, h1, id.id, &iSeg );
        if ( s != geometry_status_t::OK ) { Vector_Shutdown( &pathSteps ); return rollback( s ); }
        (void)Vector_PushBack( &pathSteps, curve_path_step_t{ iSeg, false } );
    }
    const geometry_source_id_result_t pid = GeometrySourceIdAllocator_Allocate( &ids );
    geometry_status_t s = pid.status;
    if ( s == geometry_status_t::OK ) {
        s = CurveNetwork_TryAddPath( pNet, span_t<const curve_path_step_t>{ pathSteps.pData, pathSteps.nCount },
                                     bClosed, pid.id, pPathIndexOut );
    }
    Vector_Shutdown( &pathSteps );
    if ( s != geometry_status_t::OK ) { return rollback( s ); }
    *pIdAllocator = ids;
    return geometry_status_t::OK;
}

geometry_status_t CurveNetwork_TrySplitSegment(
    curve_network_t *pNet,
    u32 iSegment,
    f64 t,
    geometry_source_id_t newNodeId,
    geometry_source_id_t newSegmentId,
    u32 *pNewNodeOut,
    u32 *pNewSegmentOut ) noexcept
{
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !SegmentOk( pNet, iSegment ) ) { return geometry_status_t::INVALID_HANDLE; }
    if ( !( t > 0.0 && t < 1.0 ) || !GeometrySourceId_IsValid( newNodeId ) ||
         !GeometrySourceId_IsValid( newSegmentId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Count path references so capacity is reserved before any mutation.
    usize cRefs = 0u;
    for ( usize i = 0u; i < pNet->steps.nCount; ++i ) {
        cRefs += pNet->steps.pData[i].iSegment == iSegment ? 1u : 0u;
    }
    if ( pNet->nodes.nCount >= kCurveNetworkNodesMax || pNet->segments.nCount >= kCurveNetworkSegmentsMax ||
         pNet->steps.nCount + cRefs > kCurveNetworkPathStepsMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !Vector_Reserve( &pNet->nodes, pNet->nodes.nCount + 1u ) ||
         !Vector_Reserve( &pNet->segments, pNet->segments.nCount + 1u ) ||
         !Vector_Reserve( &pNet->steps, pNet->steps.nCount + cRefs ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const curve_segment_t orig = pNet->segments.pData[iSegment];
    math::vec3d_t c[4];
    Controls( pNet, orig, c );
    // de Casteljau.
    const math::vec3d_t q0 = Lerp( c[0], c[1], t ), q1 = Lerp( c[1], c[2], t ), q2 = Lerp( c[2], c[3], t );
    const math::vec3d_t r0 = Lerp( q0, q1, t ), r1 = Lerp( q1, q2, t );
    const math::vec3d_t m = Lerp( r0, r1, t );

    const u32 iNew = static_cast<u32>( pNet->nodes.nCount );
    (void)Vector_PushBack( &pNet->nodes, curve_node_t{ m, newNodeId } );
    curve_segment_t first = orig, second = orig;
    first.iNode1 = iNew;
    second.iNode0 = iNew;
    second.sourceId = newSegmentId;
    if ( orig.basis == curve_basis_t::CUBIC_BEZIER ) {
        first.handle0 = q0;
        first.handle1 = r0;
        second.handle0 = r1;
        second.handle1 = q2;
    } else {
        first.handle0 = c[0];
        first.handle1 = m;
        second.handle0 = m;
        second.handle1 = c[3];
    }
    pNet->segments.pData[iSegment] = first;
    const u32 iSecond = static_cast<u32>( pNet->segments.nCount );
    (void)Vector_PushBack( &pNet->segments, second );

    // Insert a step for the second half next to every reference. Forward:
    // [first, second]; reversed: [second(rev), first(rev)]. Walk backwards so
    // insertion does not disturb unvisited indices, and fix path ranges.
    for ( usize i = pNet->steps.nCount; i-- > 0u; ) {
        const curve_path_step_t st = pNet->steps.pData[i];
        if ( st.iSegment != iSegment ) { continue; }
        const usize insertAt = st.bReversed ? i : i + 1u;
        (void)Vector_Insert( &pNet->steps, insertAt, curve_path_step_t{ iSecond, st.bReversed } );
        for ( usize p = 0u; p < pNet->paths.nCount; ++p ) {
            curve_path_t &path = pNet->paths.pData[p];
            if ( i >= path.iFirstStep && i < path.iFirstStep + path.cSteps ) {
                ++path.cSteps;
            } else if ( path.iFirstStep >= insertAt ) {
                ++path.iFirstStep;
            }
        }
    }
    if ( pNewNodeOut ) { *pNewNodeOut = iNew; }
    if ( pNewSegmentOut ) { *pNewSegmentOut = iSecond; }
    return geometry_status_t::OK;
}

math::vec3d_t CurveNetwork_EvaluateSegment( const curve_network_t *pNet, u32 iSegment, f64 t ) noexcept
{
    if ( !SegmentOk( pNet, iSegment ) ) { return math::Vec3d_Make( 0, 0, 0 ); }
    math::vec3d_t c[4];
    Controls( pNet, pNet->segments.pData[iSegment], c );
    return Bezier( c, Clamp01( t ) );
}

math::vec3d_t CurveNetwork_EvaluateSegmentDerivative( const curve_network_t *pNet, u32 iSegment, f64 t ) noexcept
{
    if ( !SegmentOk( pNet, iSegment ) ) { return math::Vec3d_Make( 0, 0, 0 ); }
    math::vec3d_t c[4];
    Controls( pNet, pNet->segments.pData[iSegment], c );
    return BezierDerivative( c, Clamp01( t ) );
}

f64 CurveNetwork_SegmentLength( const curve_network_t *pNet, u32 iSegment, f64 fTolerance ) noexcept
{
    if ( !SegmentOk( pNet, iSegment ) ) { return 0.0; }
    const curve_segment_t &s = pNet->segments.pData[iSegment];
    if ( s.basis == curve_basis_t::LINEAR ) {
        return std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract(
            pNet->nodes.pData[s.iNode1].position, pNet->nodes.pData[s.iNode0].position ) ) );
    }
    math::vec3d_t c[4];
    Controls( pNet, s, c );
    const f64 tol = fTolerance > 0.0 ? fTolerance : 1e-9;
    return AdaptiveLength( c, 0.0, 1.0, GaussLength( c, 0.0, 1.0 ), tol, 12 );
}

f64 CurveNetwork_PathLength( const curve_network_t *pNet, u32 iPath, f64 fTolerance ) noexcept
{
    if ( !CurveNetwork_IsInitialized( pNet ) || iPath >= pNet->paths.nCount ) { return 0.0; }
    const curve_path_t &p = pNet->paths.pData[iPath];
    f64 len = 0.0;
    for ( u32 i = 0u; i < p.cSteps; ++i ) {
        len += CurveNetwork_SegmentLength( pNet, pNet->steps.pData[p.iFirstStep + i].iSegment, fTolerance );
    }
    return len;
}

math::vec3d_t CurveNetwork_EvaluateStep( const curve_network_t *pNet, curve_path_step_t step, f64 t ) noexcept
{
    return CurveNetwork_EvaluateSegment( pNet, step.iSegment, step.bReversed ? 1.0 - t : t );
}

math::vec3d_t CurveNetwork_EvaluateStepDerivative( const curve_network_t *pNet, curve_path_step_t step, f64 t ) noexcept
{
    const math::vec3d_t d = CurveNetwork_EvaluateSegmentDerivative( pNet, step.iSegment, step.bReversed ? 1.0 - t : t );
    return step.bReversed ? math::Vec3d_Negate( d ) : d;
}

geometry_status_t CurveNetwork_Validate( const curve_network_t *pNet ) noexcept
{
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return geometry_status_t::NOT_INITIALIZED; }
    for ( usize i = 0u; i < pNet->nodes.nCount; ++i ) {
        if ( !math::Vec3d_IsFinite( pNet->nodes.pData[i].position ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    for ( usize i = 0u; i < pNet->segments.nCount; ++i ) {
        const curve_segment_t &s = pNet->segments.pData[i];
        if ( s.iNode0 >= pNet->nodes.nCount || s.iNode1 >= pNet->nodes.nCount ) {
            return geometry_status_t::INVALID_HANDLE;
        }
        if ( s.iNode0 == s.iNode1 ) { return geometry_status_t::INVALID_TOPOLOGY; }
        if ( !math::Vec3d_IsFinite( s.handle0 ) || !math::Vec3d_IsFinite( s.handle1 ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    for ( usize p = 0u; p < pNet->paths.nCount; ++p ) {
        const curve_path_t &path = pNet->paths.pData[p];
        if ( path.iFirstStep + path.cSteps > pNet->steps.nCount || path.cSteps == 0u ) {
            return geometry_status_t::INVALID_HANDLE;
        }
        for ( u32 i = 0u; i < path.cSteps; ++i ) {
            const curve_path_step_t st = pNet->steps.pData[path.iFirstStep + i];
            if ( st.iSegment >= pNet->segments.nCount ) { return geometry_status_t::INVALID_HANDLE; }
            if ( i > 0u && CurveNetwork_StepEndNode( pNet, pNet->steps.pData[path.iFirstStep + i - 1u] ) !=
                               CurveNetwork_StepStartNode( pNet, st ) ) {
                return geometry_status_t::INVALID_TOPOLOGY;
            }
        }
        if ( path.bClosed &&
             CurveNetwork_StepEndNode( pNet, pNet->steps.pData[path.iFirstStep + path.cSteps - 1u] ) !=
                 CurveNetwork_StepStartNode( pNet, pNet->steps.pData[path.iFirstStep] ) ) {
            return geometry_status_t::INVALID_TOPOLOGY;
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
