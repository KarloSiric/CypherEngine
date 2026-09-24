//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCutting.cpp
//  Purpose: Implements brush clip and split.
//  Details: Both halves are materialized before anything is previewed, so
//           a failure in either leaves the transaction untouched.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCutting.h"

#include "CypherGeometry_BrushCsg.h"

namespace cypher::editor::geometry
{

namespace
{

struct piece_holder_t {
    geometry_brush_piece_t piece{};
    ~piece_holder_t() noexcept { BrushPiece_Shutdown( &piece ); }
};

struct value_holder_t {
    const geometry_brush_value_t *p{ nullptr };
    ~value_holder_t() noexcept { BrushValue_Release( p ); }
};

} // namespace

geometry_status_t BrushCutting_TryPlaneFromPoints(
    const geometry_numerical_policy_t &policy,
    math::vec3d_t a,
    math::vec3d_t b,
    math::vec3d_t c,
    math::planed_t *pPlaneOut ) noexcept
{
    if ( pPlaneOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pPlaneOut = {};
    if ( !math::Vec3d_IsFinite( a ) || !math::Vec3d_IsFinite( b ) || !math::Vec3d_IsFinite( c ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    const math::vec3d_t cross = math::Vec3d_Cross(
        math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, a ) );
    math::vec3d_t normal{};
    if ( !( math::Vec3d_LengthSquared( cross ) >
            policy.fMinimumFaceArea * policy.fMinimumFaceArea ) ||
         !math::Vec3d_TryNormalize( cross, 0.0, &normal, nullptr ) ) {
        return geometry_status_t::DEGENERATE;
    }
    *pPlaneOut = math::Planed_Make( normal, -math::Vec3d_Dot( normal, a ) );
    return geometry_status_t::OK;
}

geometry_status_t BrushCutting_TryClip(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    math::planed_t plane,
    geometry_clip_keep_t keep,
    geometry_clip_result_t *pResultOut ) noexcept
{
    if ( pResultOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pResultOut = {};
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( keep != geometry_clip_keep_t::BACK && keep != geometry_clip_keep_t::FRONT &&
         keep != geometry_clip_keep_t::BOTH ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_policy_t &policy = pTransaction->pDocument->policy;
    const common::allocator_t *pAllocator = pTransaction->pDocument->pAllocator;

    const geometry_brush_value_t *pSource = nullptr;
    geometry_status_t status = GeometryTransaction_TryGetPreview( pTransaction, brushId, &pSource );
    if ( status != geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    // The preview pointer is borrowed from the transaction and would be
    // freed by our own replace below; hold a reference for rollback.
    value_holder_t sourceRef{};
    BrushValue_AddRef( pSource );
    sourceRef.p = pSource;

    piece_holder_t source{};
    piece_holder_t back{};
    piece_holder_t front{};
    if ( BrushPiece_Init( &source.piece, pAllocator ) != geometry_status_t::OK ||
         BrushPiece_Init( &back.piece, pAllocator ) != geometry_status_t::OK ||
         BrushPiece_Init( &front.piece, pAllocator ) != geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = GeometryMaterialize_TryPieceFromValue( pSource, &source.piece );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    geometry_piece_plane_t cut{};
    cut.plane = plane;
    cut.origin = geometry_piece_plane_origin_t::CUT_PLANE;
    cut.sourceBrushId = brushId;
    geometry_piece_extent_t backExtent{};
    geometry_piece_extent_t frontExtent{};
    status = BrushCsg_TrySplit( &source.piece, cut, policy, &back.piece, &backExtent,
                                &front.piece, &frontExtent );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    // The plane misses the brush: one half is the whole brush.
    if ( backExtent == geometry_piece_extent_t::EMPTY ||
         frontExtent == geometry_piece_extent_t::EMPTY ) {
        const bool bWholeIsBack = frontExtent == geometry_piece_extent_t::EMPTY;
        const bool bKeepsWhole = keep == geometry_clip_keep_t::BOTH ||
                                 ( keep == geometry_clip_keep_t::BACK ) == bWholeIsBack;
        if ( !bKeepsWhole ) {
            return geometry_status_t::DEGENERATE;
        }
        ( bWholeIsBack ? pResultOut->backBrushId : pResultOut->frontBrushId ) = brushId;
        return geometry_status_t::OK;
    }

    const geometry_brush_value_t *donors[1] = { pSource };
    const common::span_t<const geometry_brush_value_t *const> donorSpan{ donors, 1u };
    value_holder_t kept{};
    value_holder_t extra{};

    // The half that stays in place keeps the brush's identity.
    const bool bBackStays = keep != geometry_clip_keep_t::FRONT;
    geometry_materialize_desc_t desc{};
    desc.pPiece = bBackStays ? &back.piece : &front.piece;
    desc.brushId = brushId;
    desc.bReuseSideIds = true;
    desc.donors = donorSpan;
    status = GeometryMaterialize_TryPiece( pTransaction, desc, &kept.p );
    if ( status == geometry_status_t::OK && keep == geometry_clip_keep_t::BOTH ) {
        geometry_materialize_desc_t other{};
        other.pPiece = &front.piece;
        other.donors = donorSpan;
        status = GeometryMaterialize_TryPiece( pTransaction, other, &extra.p );
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    status = GeometryTransaction_TryPreviewReplace( pTransaction, kept.p );
    if ( status == geometry_status_t::OK && extra.p != nullptr ) {
        status = GeometryTransaction_TryPreviewInsert( pTransaction, extra.p );
        if ( status != geometry_status_t::OK ) {
            // Roll the replacement back to the original preview.
            ( void )GeometryTransaction_TryPreviewReplace( pTransaction, pSource );
        }
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    pResultOut->bCut = true;
    if ( bBackStays ) {
        pResultOut->backBrushId = brushId;
        if ( extra.p != nullptr ) {
            pResultOut->frontBrushId = extra.p->brush.sourceId;
        }
    } else {
        pResultOut->frontBrushId = brushId;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
