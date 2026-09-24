//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCsg.cpp
//  Purpose: Implements convex-piece clipping, intersection, subtraction,
//           hollowing, hull construction, and convex merge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCsg.h"

#include "CypherCommon_Sort.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::usize;
using math::f64;
using math::planed_t;
using math::vec3d_t;

// Owns a piece for one scope.
struct scoped_piece_t {
    geometry_brush_piece_t piece{};
    scoped_piece_t() noexcept = default;
    scoped_piece_t( const scoped_piece_t & ) = delete;
    scoped_piece_t &operator=( const scoped_piece_t & ) = delete;
    ~scoped_piece_t() noexcept { BrushPiece_Shutdown( &piece ); }
    CYPHER_NODISCARD bool_t TryInit( const common::allocator_t *pAllocator ) noexcept
    {
        return BrushPiece_Init( &piece, pAllocator ) == geometry_status_t::OK;
    }
};

struct scoped_list_t {
    geometry_piece_list_t list{};
    scoped_list_t() noexcept = default;
    scoped_list_t( const scoped_list_t & ) = delete;
    scoped_list_t &operator=( const scoped_list_t & ) = delete;
    ~scoped_list_t() noexcept { PieceList_Shutdown( &list ); }
    CYPHER_NODISCARD bool_t TryInit( const common::allocator_t *pAllocator ) noexcept
    {
        return PieceList_Init( &list, pAllocator ) == geometry_status_t::OK;
    }
};

bool_t IsReady( const geometry_brush_piece_t *pPiece ) noexcept
{
    return pPiece != nullptr && pPiece->planes.pAllocator != nullptr;
}

const common::allocator_t *AllocatorOf( const geometry_brush_piece_t *pPiece ) noexcept
{
    return pPiece->planes.pAllocator;
}

// Copies pSource into pOut, appends one plane, and reduces.
geometry_status_t CopyAppendReduce(
    const geometry_brush_piece_t *pSource,
    const geometry_piece_plane_t &plane,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pOut,
    geometry_piece_extent_t *pExtentOut ) noexcept
{
    geometry_status_t status = BrushPiece_TryCopy( pOut, pSource );
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryAppendPlane( pOut, policy, plane );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryReduce( pOut, policy, pExtentOut, nullptr );
    }
    return status;
}

// Deterministic cutter-plane order: dominant axis, then its sign, then the
// full normal and distance. Groups axial planes first so fragments of an
// axis-aligned cutter come out as tidy slabs, independent of side order.
struct cutter_plane_less_t {
    CYPHER_NODISCARD static usize DominantAxis( vec3d_t n ) noexcept
    {
        const f64 ax = math::Scalar_Abs( n.x );
        const f64 ay = math::Scalar_Abs( n.y );
        const f64 az = math::Scalar_Abs( n.z );
        if ( az >= ax && az >= ay ) {
            return 0u; // Z first: floors and ceilings before walls.
        }
        return ax >= ay ? 1u : 2u;
    }
    CYPHER_NODISCARD static f64 DominantValue( vec3d_t n, usize axis ) noexcept
    {
        return axis == 0u ? n.z : ( axis == 1u ? n.x : n.y );
    }
    CYPHER_NODISCARD bool_t operator()(
        const geometry_piece_plane_t &a, const geometry_piece_plane_t &b ) const noexcept
    {
        const usize axisA = DominantAxis( a.plane.normal );
        const usize axisB = DominantAxis( b.plane.normal );
        if ( axisA != axisB ) {
            return axisA < axisB;
        }
        const f64 va = DominantValue( a.plane.normal, axisA );
        const f64 vb = DominantValue( b.plane.normal, axisB );
        if ( ( va < 0.0 ) != ( vb < 0.0 ) ) {
            return va < 0.0;
        }
        if ( a.plane.normal.x != b.plane.normal.x ) {
            return a.plane.normal.x < b.plane.normal.x;
        }
        if ( a.plane.normal.y != b.plane.normal.y ) {
            return a.plane.normal.y < b.plane.normal.y;
        }
        if ( a.plane.normal.z != b.plane.normal.z ) {
            return a.plane.normal.z < b.plane.normal.z;
        }
        return a.plane.d < b.plane.d;
    }
};

geometry_piece_plane_t Flipped( const geometry_piece_plane_t &plane ) noexcept
{
    geometry_piece_plane_t flipped = plane;
    flipped.plane = BrushCsg_FlipPlane( plane.plane );
    if ( plane.origin == geometry_piece_plane_origin_t::SOURCE_SIDE ) {
        flipped.origin = geometry_piece_plane_origin_t::FLIPPED_SIDE;
    } else if ( plane.origin == geometry_piece_plane_origin_t::FLIPPED_SIDE ) {
        flipped.origin = geometry_piece_plane_origin_t::SOURCE_SIDE;
    }
    return flipped;
}

bool_t SamePlane( planed_t a, planed_t b, f64 distanceTolerance ) noexcept
{
    return math::Vec3d_Dot( a.normal, b.normal ) > 1.0 - 1.0e-9 &&
           math::Scalar_Abs( a.d - b.d ) <= distanceTolerance;
}

} // namespace

planed_t BrushCsg_FlipPlane( planed_t plane ) noexcept
{
    return math::Planed_Make( math::Vec3d_Negate( plane.normal ), -plane.d );
}

// ---------------------------------------------------------------------------
// Split / intersect
// ---------------------------------------------------------------------------

geometry_status_t BrushCsg_TrySplit(
    const geometry_brush_piece_t *pPiece,
    const geometry_piece_plane_t &cut,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pBackOut,
    geometry_piece_extent_t *pBackExtentOut,
    geometry_brush_piece_t *pFrontOut,
    geometry_piece_extent_t *pFrontExtentOut ) noexcept
{
    if ( pBackExtentOut == nullptr || pFrontExtentOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pBackExtentOut = geometry_piece_extent_t::EMPTY;
    *pFrontExtentOut = geometry_piece_extent_t::EMPTY;
    if ( !IsReady( pPiece ) || !IsReady( pBackOut ) || !IsReady( pFrontOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pBackOut == pPiece || pFrontOut == pPiece || pBackOut == pFrontOut ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    geometry_status_t status =
        CopyAppendReduce( pPiece, cut, policy, pBackOut, pBackExtentOut );
    if ( status == geometry_status_t::OK ) {
        status = CopyAppendReduce( pPiece, Flipped( cut ), policy, pFrontOut, pFrontExtentOut );
    }
    if ( status != geometry_status_t::OK ) {
        BrushPiece_Clear( pBackOut );
        BrushPiece_Clear( pFrontOut );
        *pBackExtentOut = geometry_piece_extent_t::EMPTY;
        *pFrontExtentOut = geometry_piece_extent_t::EMPTY;
    }
    return status;
}

geometry_status_t BrushCsg_TryIntersect(
    const geometry_brush_piece_t *pA,
    const geometry_brush_piece_t *pB,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pOut,
    geometry_piece_extent_t *pExtentOut ) noexcept
{
    if ( pExtentOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pExtentOut = geometry_piece_extent_t::EMPTY;
    if ( !IsReady( pA ) || !IsReady( pB ) || !IsReady( pOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pOut == pA || pOut == pB ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_status_t status = BrushPiece_TryCopy( pOut, pA );
    const usize cB = common::Vector_Count( &pB->planes );
    for ( usize i = 0u; status == geometry_status_t::OK && i < cB; ++i ) {
        status = BrushPiece_TryAppendPlane( pOut, policy, pB->planes.pData[i] );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryReduce( pOut, policy, pExtentOut, nullptr );
    }
    if ( status != geometry_status_t::OK ) {
        BrushPiece_Clear( pOut );
    }
    return status;
}

// ---------------------------------------------------------------------------
// Subtraction
// ---------------------------------------------------------------------------

geometry_status_t BrushCsg_TrySubtract(
    const geometry_brush_piece_t *pMinuend,
    const geometry_brush_piece_t *pCutter,
    const geometry_policy_t &policy,
    geometry_piece_list_t *pFragmentsOut,
    geometry_csg_overlap_t *pOverlapOut ) noexcept
{
    if ( pOverlapOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pOverlapOut = geometry_csg_overlap_t::DISJOINT;
    if ( !IsReady( pMinuend ) || !IsReady( pCutter ) || pFragmentsOut == nullptr ||
         pFragmentsOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::allocator_t *pAllocator = AllocatorOf( pMinuend );
    const usize cEntry = PieceList_Count( pFragmentsOut );

    scoped_piece_t remaining{};
    scoped_piece_t fragment{};
    scoped_piece_t probe{};
    if ( !remaining.TryInit( pAllocator ) || !fragment.TryInit( pAllocator ) ||
         !probe.TryInit( pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // Quick trim: no volumetric overlap means the minuend is unchanged.
    geometry_piece_extent_t extent = geometry_piece_extent_t::EMPTY;
    geometry_status_t status =
        BrushCsg_TryIntersect( pMinuend, pCutter, policy, &probe.piece, &extent );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushPiece_TryCopy( &remaining.piece, pMinuend );
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryReduce( &remaining.piece, policy, &extent, nullptr );
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( extent == geometry_piece_extent_t::EMPTY ) {
        // An empty minuend has nothing to subtract from.
        *pOverlapOut = geometry_csg_overlap_t::CONSUMED;
        return geometry_status_t::OK;
    }
    if ( BrushPiece_PlaneCount( &probe.piece ) == 0u ) {
        status = PieceList_TryAppend( pFragmentsOut, &remaining.piece );
        return status;
    }

    // Order the cutter planes deterministically.
    common::vector_t<geometry_piece_plane_t> cutterPlanes{};
    const usize cCutter = common::Vector_Count( &pCutter->planes );
    if ( !common::Vector_Init( &cutterPlanes, pAllocator, cCutter ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cCutter; ++i ) {
        ( void )common::Vector_PushBack( &cutterPlanes, pCutter->planes.pData[i] );
    }
    common::Sort_Unstable(
        common::span_t<geometry_piece_plane_t>{ cutterPlanes.pData, cCutter },
        cutter_plane_less_t{} );

    *pOverlapOut = geometry_csg_overlap_t::CONSUMED;
    for ( usize i = 0u; i < cCutter; ++i ) {
        const geometry_piece_plane_t &plane = cutterPlanes.pData[i];

        // Outside this cutter plane: a finished fragment.
        status = CopyAppendReduce( &remaining.piece, Flipped( plane ), policy,
                                   &fragment.piece, &extent );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        if ( extent == geometry_piece_extent_t::SOLID ) {
            status = PieceList_TryAppend( pFragmentsOut, &fragment.piece );
            if ( status != geometry_status_t::OK ) {
                break;
            }
            *pOverlapOut = geometry_csg_overlap_t::PARTIAL;
        }

        // Inside it: keep carving.
        status = BrushPiece_TryAppendPlane( &remaining.piece, policy, plane );
        if ( status == geometry_status_t::OK ) {
            status = BrushPiece_TryReduce( &remaining.piece, policy, &extent, nullptr );
        }
        if ( status != geometry_status_t::OK ) {
            break;
        }
        if ( extent == geometry_piece_extent_t::EMPTY ) {
            break;
        }
    }

    if ( status != geometry_status_t::OK ) {
        PieceList_Truncate( pFragmentsOut, cEntry );
        *pOverlapOut = geometry_csg_overlap_t::DISJOINT;
    }
    return status;
}

geometry_status_t BrushCsg_TrySubtractAll(
    const geometry_brush_piece_t *pMinuend,
    common::span_t<const geometry_brush_piece_t *const> cutters,
    const geometry_policy_t &policy,
    usize cFragmentsMax,
    geometry_piece_list_t *pFragmentsOut,
    bool_t *pChangedOut ) noexcept
{
    if ( pChangedOut == nullptr || !common::Span_IsValid( cutters ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pChangedOut = false;
    if ( !IsReady( pMinuend ) || pFragmentsOut == nullptr ||
         pFragmentsOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::allocator_t *pAllocator = AllocatorOf( pMinuend );
    const usize cEntry = PieceList_Count( pFragmentsOut );

    scoped_list_t current{};
    scoped_list_t next{};
    scoped_piece_t piece{};
    if ( !current.TryInit( pAllocator ) || !next.TryInit( pAllocator ) ||
         !piece.TryInit( pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = PieceList_TryAppend( &current.list, pMinuend );

    for ( usize c = 0u; status == geometry_status_t::OK && c < cutters.nCount; ++c ) {
        PieceList_Clear( &next.list );
        const usize cCurrent = PieceList_Count( &current.list );
        for ( usize i = 0u; status == geometry_status_t::OK && i < cCurrent; ++i ) {
            status = PieceList_TryGet( &current.list, i, &piece.piece );
            geometry_csg_overlap_t overlap = geometry_csg_overlap_t::DISJOINT;
            if ( status == geometry_status_t::OK ) {
                status = BrushCsg_TrySubtract( &piece.piece, cutters.pData[c], policy,
                                               &next.list, &overlap );
            }
            if ( overlap != geometry_csg_overlap_t::DISJOINT ) {
                *pChangedOut = true;
            }
            if ( status == geometry_status_t::OK &&
                 PieceList_Count( &next.list ) > cFragmentsMax ) {
                status = geometry_status_t::LIMIT_EXCEEDED;
            }
        }
        // Swap the lists' roles.
        if ( status == geometry_status_t::OK ) {
            PieceList_Clear( &current.list );
            const usize cNext = PieceList_Count( &next.list );
            for ( usize i = 0u; status == geometry_status_t::OK && i < cNext; ++i ) {
                status = PieceList_TryGet( &next.list, i, &piece.piece );
                if ( status == geometry_status_t::OK ) {
                    status = PieceList_TryAppend( &current.list, &piece.piece );
                }
            }
        }
    }

    const usize cResult = PieceList_Count( &current.list );
    for ( usize i = 0u; status == geometry_status_t::OK && i < cResult; ++i ) {
        status = PieceList_TryGet( &current.list, i, &piece.piece );
        if ( status == geometry_status_t::OK ) {
            status = PieceList_TryAppend( pFragmentsOut, &piece.piece );
        }
    }
    if ( status != geometry_status_t::OK ) {
        PieceList_Truncate( pFragmentsOut, cEntry );
        *pChangedOut = false;
    }
    return status;
}

// ---------------------------------------------------------------------------
// Hollow
// ---------------------------------------------------------------------------

geometry_status_t BrushCsg_TryHollow(
    const geometry_brush_piece_t *pPiece,
    f64 thickness,
    const geometry_policy_t &policy,
    geometry_piece_list_t *pWallsOut ) noexcept
{
    if ( !IsReady( pPiece ) || pWallsOut == nullptr || pWallsOut->planes.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Scalar_IsFinite( thickness ) || !( thickness > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    scoped_piece_t inner{};
    if ( !inner.TryInit( AllocatorOf( pPiece ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = BrushPiece_TryCopy( &inner.piece, pPiece );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    BrushPiece_OffsetPlanes( &inner.piece, -thickness );
    geometry_piece_extent_t extent = geometry_piece_extent_t::EMPTY;
    status = BrushPiece_TryReduce( &inner.piece, policy, &extent, nullptr );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( extent == geometry_piece_extent_t::EMPTY ) {
        return geometry_status_t::DEGENERATE;
    }
    geometry_csg_overlap_t overlap = geometry_csg_overlap_t::DISJOINT;
    return BrushCsg_TrySubtract( pPiece, &inner.piece, policy, pWallsOut, &overlap );
}

// ---------------------------------------------------------------------------
// Hull and merge
// ---------------------------------------------------------------------------

geometry_status_t BrushCsg_TryConvexHull(
    common::span_t<const vec3d_t> points,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pOut ) noexcept
{
    return BrushPiece_TryFromPoints( pOut, points, policy );
}

geometry_status_t BrushCsg_TryMerge(
    common::span_t<const geometry_brush_piece_t *const> pieces,
    const geometry_policy_t &policy,
    geometry_brush_piece_t *pOut ) noexcept
{
    if ( !IsReady( pOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Span_IsValid( pieces ) || pieces.nCount == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::allocator_t *pAllocator = AllocatorOf( pOut );

    common::vector_t<vec3d_t> points{};
    if ( !common::Vector_Init( &points, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    brush_boundary_t boundary{};
    if ( BrushBoundary_Init( &boundary, pAllocator ) != geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = geometry_status_t::OK;
    for ( usize i = 0u; status == geometry_status_t::OK && i < pieces.nCount; ++i ) {
        if ( !IsReady( pieces.pData[i] ) ) {
            status = geometry_status_t::NOT_INITIALIZED;
            break;
        }
        status = BrushPiece_TryBuildBoundary( pieces.pData[i], policy, &boundary );
        const usize cVertices = common::Vector_Count( &boundary.vertices );
        for ( usize v = 0u; status == geometry_status_t::OK && v < cVertices; ++v ) {
            if ( !common::Vector_PushBack( &points, boundary.vertices.pData[v] ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }
    BrushBoundary_Shutdown( &boundary );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    status = BrushCsg_TryConvexHull(
        common::span_t<const vec3d_t>{ points.pData, common::Vector_Count( &points ) },
        policy, pOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    // Inherit provenance from coplanar input planes.
    const f64 tolerance = policy.numerical.fCoplanarDistanceTolerance;
    const usize cHull = BrushPiece_PlaneCount( pOut );
    for ( usize h = 0u; h < cHull; ++h ) {
        geometry_piece_plane_t &facet = pOut->planes.pData[h];
        for ( usize i = 0u; i < pieces.nCount && facet.origin == geometry_piece_plane_origin_t::NONE; ++i ) {
            const geometry_brush_piece_t *pSource = pieces.pData[i];
            for ( usize p = 0u; p < BrushPiece_PlaneCount( pSource ); ++p ) {
                const geometry_piece_plane_t &source = pSource->planes.pData[p];
                if ( SamePlane( source.plane, facet.plane, tolerance ) ) {
                    facet.origin = source.origin;
                    facet.sourceBrushId = source.sourceBrushId;
                    facet.sourceSideId = source.sourceSideId;
                    break;
                }
            }
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
