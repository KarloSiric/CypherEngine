//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushPiece.cpp
//  Purpose: Implements convex working pieces and piece lists.
//  Details: Reduce borrows the brush reconstruction path by building a
//           throwaway brush with synthetic identities. Pruned planes are
//           gathered into scratch first and written back only once the
//           result is known, so every error leaves the piece untouched.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushPiece.h"

#include "CypherGeometry_BrushValidation.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::usize;

bool_t IsReady( const geometry_brush_piece_t *pPiece ) noexcept
{
    return pPiece != nullptr && pPiece->planes.pAllocator != nullptr;
}

bool_t IsListReady( const geometry_piece_list_t *pList ) noexcept
{
    return pList != nullptr && pList->planes.pAllocator != nullptr;
}

// Builds a brush over the given planes with synthetic identities 1..n+1.
geometry_status_t BuildScratchBrush(
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    const geometry_piece_plane_t *pPlanes,
    usize cPlanes,
    brush_solid_t *pBrushOut ) noexcept
{
    geometry_status_t status =
        BrushSolid_Init( pBrushOut, pAllocator, geometry_source_id_t{ 1u } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushSolid_TryReserve( pBrushOut, policy.limits, cPlanes );
    for ( usize i = 0u; status == geometry_status_t::OK && i < cPlanes; ++i ) {
        brush_solid_side_t side{};
        side.plane = pPlanes[i].plane;
        side.sourceId = geometry_source_id_t{ 2u + i };
        side.iAttributeIndex = static_cast<common::u32>( i );
        status = BrushSolid_TryAddSide( pBrushOut, policy.limits, side, nullptr );
    }
    if ( status != geometry_status_t::OK ) {
        BrushSolid_Shutdown( pBrushOut );
    }
    return status;
}

} // namespace

// ---------------------------------------------------------------------------
// Piece
// ---------------------------------------------------------------------------

geometry_status_t BrushPiece_Init(
    geometry_brush_piece_t *pPiece, const common::allocator_t *pAllocator ) noexcept
{
    if ( pPiece == nullptr || !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( IsReady( pPiece ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return common::Vector_Init( &pPiece->planes, pAllocator, 0u )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

void BrushPiece_Shutdown( geometry_brush_piece_t *pPiece ) noexcept
{
    if ( IsReady( pPiece ) ) {
        common::Vector_Shutdown( &pPiece->planes );
    }
}

usize BrushPiece_PlaneCount( const geometry_brush_piece_t *pPiece ) noexcept
{
    return IsReady( pPiece ) ? common::Vector_Count( &pPiece->planes ) : 0u;
}

void BrushPiece_Clear( geometry_brush_piece_t *pPiece ) noexcept
{
    if ( IsReady( pPiece ) ) {
        common::Vector_Clear( &pPiece->planes );
    }
}

geometry_status_t BrushPiece_TryAppendPlane(
    geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    const geometry_piece_plane_t &plane ) noexcept
{
    if ( !IsReady( pPiece ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Planed_IsFinite( plane.plane ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !math::Planed_IsNormalized( plane.plane, policy.numerical.fUnitNormalTolerance ) ) {
        return geometry_status_t::DEGENERATE;
    }
    if ( static_cast<common::u64>( common::Vector_Count( &pPiece->planes ) ) >=
         policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    return common::Vector_PushBack( &pPiece->planes, plane )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t BrushPiece_TryCopy(
    geometry_brush_piece_t *pDestination,
    const geometry_brush_piece_t *pSource ) noexcept
{
    if ( !IsReady( pDestination ) || !IsReady( pSource ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pDestination == pSource ) {
        return geometry_status_t::OK;
    }
    const usize cPlanes = common::Vector_Count( &pSource->planes );
    if ( !common::Vector_Reserve( &pDestination->planes, cPlanes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    common::Vector_Clear( &pDestination->planes );
    for ( usize i = 0u; i < cPlanes; ++i ) {
        ( void )common::Vector_PushBack( &pDestination->planes, pSource->planes.pData[i] );
    }
    return geometry_status_t::OK;
}

geometry_status_t BrushPiece_TryFromBrush(
    geometry_brush_piece_t *pPiece,
    const brush_solid_t *pBrush ) noexcept
{
    if ( !IsReady( pPiece ) || pBrush == nullptr || pBrush->sides.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const usize cSides = common::Vector_Count( &pBrush->sides );
    if ( !common::Vector_Reserve( &pPiece->planes, cSides ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    common::Vector_Clear( &pPiece->planes );
    for ( usize i = 0u; i < cSides; ++i ) {
        const brush_solid_side_t &side = pBrush->sides.pData[i];
        const geometry_piece_plane_t plane{
            side.plane, geometry_piece_plane_origin_t::SOURCE_SIDE,
            pBrush->sourceId, side.sourceId };
        ( void )common::Vector_PushBack( &pPiece->planes, plane );
    }
    return geometry_status_t::OK;
}

void BrushPiece_OffsetPlanes( geometry_brush_piece_t *pPiece, math::f64 distance ) noexcept
{
    if ( !IsReady( pPiece ) ) {
        return;
    }
    // n·p + d = 0 moved outward by `distance` along n: d' = d - distance.
    const usize cPlanes = common::Vector_Count( &pPiece->planes );
    for ( usize i = 0u; i < cPlanes; ++i ) {
        pPiece->planes.pData[i].plane.d -= distance;
    }
}

geometry_status_t BrushPiece_TryBuildBoundary(
    const geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    brush_boundary_t *pBoundaryOut ) noexcept
{
    if ( !IsReady( pPiece ) || pBoundaryOut == nullptr ||
         pBoundaryOut->vertices.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    brush_solid_t scratch{};
    geometry_status_t status = BuildScratchBrush(
        pPiece->planes.pAllocator, policy, pPiece->planes.pData,
        common::Vector_Count( &pPiece->planes ), &scratch );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushBoundary_TryReconstruct( pBoundaryOut, &scratch, policy );
    BrushSolid_Shutdown( &scratch );
    return status;
}

geometry_status_t BrushPiece_TryReduce(
    geometry_brush_piece_t *pPiece,
    const geometry_policy_t &policy,
    geometry_piece_extent_t *pExtentOut,
    math::aabbd_t *pBoundsOut ) noexcept
{
    if ( pExtentOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pExtentOut = geometry_piece_extent_t::EMPTY;
    if ( pBoundsOut != nullptr ) {
        *pBoundsOut = math::CY_AABBD_EMPTY;
    }
    if ( !IsReady( pPiece ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::allocator_t *pAllocator = pPiece->planes.pAllocator;
    const usize cPlanes = common::Vector_Count( &pPiece->planes );
    if ( cPlanes < 4u ) {
        common::Vector_Clear( &pPiece->planes );
        return geometry_status_t::OK;
    }

    brush_boundary_t boundary{};
    if ( BrushBoundary_Init( &boundary, pAllocator ) != geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // ---- Pass 1: which planes contribute a face -------------------------
    geometry_status_t status = BrushPiece_TryBuildBoundary( pPiece, policy, &boundary );
    if ( status == geometry_status_t::DEGENERATE ) {
        BrushBoundary_Shutdown( &boundary );
        common::Vector_Clear( &pPiece->planes );
        return geometry_status_t::OK;
    }
    if ( status != geometry_status_t::OK ) {
        BrushBoundary_Shutdown( &boundary );
        return status;
    }

    common::vector_t<geometry_piece_plane_t> kept{};
    if ( !common::Vector_Init( &kept, pAllocator, cPlanes ) ) {
        BrushBoundary_Shutdown( &boundary );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const usize cFaces = common::Vector_Count( &boundary.faces );
    for ( usize iPlane = 0u; iPlane < cPlanes; ++iPlane ) {
        // Faces are emitted in ascending side order; a linear probe is fine
        // at authoring scale and avoids depending on that detail.
        for ( usize iFace = 0u; iFace < cFaces; ++iFace ) {
            if ( boundary.faces.pData[iFace].iSide == iPlane ) {
                ( void )common::Vector_PushBack( &kept, pPiece->planes.pData[iPlane] );
                break;
            }
        }
    }

    // ---- Pass 2: the pruned set must be a closed solid ------------------
    common::bool_t bSolid = common::Vector_Count( &kept ) >= 4u;
    if ( bSolid ) {
        brush_solid_t scratch{};
        status = BuildScratchBrush(
            pAllocator, policy, kept.pData, common::Vector_Count( &kept ), &scratch );
        if ( status == geometry_status_t::OK ) {
            status = BrushBoundary_TryReconstruct( &boundary, &scratch, policy );
            if ( status == geometry_status_t::OK ) {
                const brush_validation_result_t check =
                    BrushValidation_CheckBoundary( &scratch, &boundary, policy );
                bSolid = check.status == geometry_status_t::OK;
            } else if ( status == geometry_status_t::DEGENERATE ) {
                bSolid = false;
                status = geometry_status_t::OK;
            }
            BrushSolid_Shutdown( &scratch );
        }
        if ( status != geometry_status_t::OK ) {
            BrushBoundary_Shutdown( &boundary );
            return status;
        }
    }

    // ---- Commit (cannot fail: kept fits in the piece's capacity) ----------
    common::Vector_Clear( &pPiece->planes );
    if ( bSolid ) {
        for ( usize i = 0u; i < common::Vector_Count( &kept ); ++i ) {
            ( void )common::Vector_PushBack( &pPiece->planes, kept.pData[i] );
        }
        *pExtentOut = geometry_piece_extent_t::SOLID;
        if ( pBoundsOut != nullptr ) {
            ( void )BrushBoundary_TryGetBounds( &boundary, pBoundsOut );
        }
    }
    BrushBoundary_Shutdown( &boundary );
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Piece lists
// ---------------------------------------------------------------------------

geometry_status_t PieceList_Init(
    geometry_piece_list_t *pList, const common::allocator_t *pAllocator ) noexcept
{
    if ( pList == nullptr || !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( IsListReady( pList ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !common::Vector_Init( &pList->planes, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Init( &pList->pieces, pAllocator, 0u ) ) {
        common::Vector_Shutdown( &pList->planes );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void PieceList_Shutdown( geometry_piece_list_t *pList ) noexcept
{
    if ( !IsListReady( pList ) ) {
        return;
    }
    common::Vector_Shutdown( &pList->pieces );
    common::Vector_Shutdown( &pList->planes );
}

void PieceList_Clear( geometry_piece_list_t *pList ) noexcept
{
    if ( IsListReady( pList ) ) {
        common::Vector_Clear( &pList->planes );
        common::Vector_Clear( &pList->pieces );
    }
}

usize PieceList_Count( const geometry_piece_list_t *pList ) noexcept
{
    return IsListReady( pList ) ? common::Vector_Count( &pList->pieces ) : 0u;
}

geometry_status_t PieceList_TryAppend(
    geometry_piece_list_t *pList, const geometry_brush_piece_t *pPiece ) noexcept
{
    if ( !IsListReady( pList ) || !IsReady( pPiece ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const usize cExisting = common::Vector_Count( &pList->planes );
    const usize cPlanes = common::Vector_Count( &pPiece->planes );
    if ( cExisting + cPlanes > static_cast<usize>( common::CY_U32_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !common::Vector_Reserve( &pList->planes, cExisting + cPlanes ) ||
         !common::Vector_Reserve( &pList->pieces, common::Vector_Count( &pList->pieces ) + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cPlanes; ++i ) {
        ( void )common::Vector_PushBack( &pList->planes, pPiece->planes.pData[i] );
    }
    const geometry_piece_range_t range{
        static_cast<common::u32>( cExisting ), static_cast<common::u32>( cPlanes ) };
    ( void )common::Vector_PushBack( &pList->pieces, range );
    return geometry_status_t::OK;
}

geometry_status_t PieceList_TryGet(
    const geometry_piece_list_t *pList,
    usize iPiece,
    geometry_brush_piece_t *pPieceOut ) noexcept
{
    if ( !IsListReady( pList ) || !IsReady( pPieceOut ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iPiece >= common::Vector_Count( &pList->pieces ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_piece_range_t range = pList->pieces.pData[iPiece];
    if ( !common::Vector_Reserve( &pPieceOut->planes, range.cPlanes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    common::Vector_Clear( &pPieceOut->planes );
    for ( common::u32 i = 0u; i < range.cPlanes; ++i ) {
        ( void )common::Vector_PushBack( &pPieceOut->planes,
                                         pList->planes.pData[range.iFirst + i] );
    }
    return geometry_status_t::OK;
}

void PieceList_Truncate( geometry_piece_list_t *pList, usize cPieces ) noexcept
{
    if ( !IsListReady( pList ) || cPieces >= common::Vector_Count( &pList->pieces ) ) {
        return;
    }
    const geometry_piece_range_t first = pList->pieces.pData[cPieces];
    while ( common::Vector_Count( &pList->pieces ) > cPieces ) {
        common::Vector_PopBack( &pList->pieces );
    }
    while ( common::Vector_Count( &pList->planes ) > first.iFirst ) {
        common::Vector_PopBack( &pList->planes );
    }
}

} // namespace cypher::editor::geometry
