//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushExtrude.cpp
//  Purpose: Implements face extrusion.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushExtrude.h"

#include "CypherGeometry_BrushCutting.h"
#include "CypherGeometry_BrushTransform.h"

namespace cypher::editor::geometry
{

namespace
{

using common::usize;

struct piece_holder_t {
    geometry_brush_piece_t piece{};
    ~piece_holder_t() noexcept { BrushPiece_Shutdown( &piece ); }
};

struct value_pin_t {
    const geometry_brush_value_t *p{ nullptr };
    ~value_pin_t() noexcept { BrushValue_Release( p ); }
};

} // namespace

geometry_status_t BrushExtrude_TryExtrudeSide(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    geometry_source_id_t sideId,
    math::f64 distance,
    geometry_extrude_mode_t mode,
    geometry_texture_lock_t lock,
    geometry_source_id_t *pNewBrushIdOut ) noexcept
{
    if ( pNewBrushIdOut != nullptr ) {
        *pNewBrushIdOut = GEOMETRY_SOURCE_ID_INVALID;
    }
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Scalar_IsFinite( distance ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( mode == geometry_extrude_mode_t::GROW ) {
        return BrushTransform_TryMoveSide( pTransaction, brushId, sideId, distance, lock );
    }
    if ( ( mode != geometry_extrude_mode_t::SPLIT_OUT && mode != geometry_extrude_mode_t::SPLIT_IN ) ||
         !( distance > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    value_pin_t source{};
    const geometry_brush_value_t *pPreview = nullptr;
    if ( GeometryTransaction_TryGetPreview( pTransaction, brushId, &pPreview ) != geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    BrushValue_AddRef( pPreview );
    source.p = pPreview;
    const brush_solid_side_t *pSide = nullptr;
    for ( usize i = 0u; i < BrushSolid_SideCount( &pPreview->brush ); ++i ) {
        if ( pPreview->brush.sides.pData[i].sourceId.value == sideId.value ) {
            pSide = &pPreview->brush.sides.pData[i];
        }
    }
    if ( pSide == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( mode == geometry_extrude_mode_t::SPLIT_IN ) {
        // Cut `distance` inside the face; the slab next to the face becomes
        // a new brush and the original keeps the rest.
        math::planed_t cut = pSide->plane;
        cut.d += distance;
        geometry_clip_result_t result{};
        const geometry_status_t status =
            BrushCutting_TryClip( pTransaction, brushId, cut, geometry_clip_keep_t::BOTH, &result );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
        if ( !result.bCut ) {
            return geometry_status_t::DEGENERATE;
        }
        if ( pNewBrushIdOut != nullptr ) {
            *pNewBrushIdOut = result.frontBrushId;
        }
        return geometry_status_t::OK;
    }

    // SPLIT_OUT: the slab between the face and the face moved outward.
    const geometry_policy_t &policy = pTransaction->pDocument->policy;
    piece_holder_t slab{};
    geometry_status_t status = BrushPiece_Init( &slab.piece, pTransaction->pDocument->pAllocator );
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryFromBrush( &slab.piece, &pPreview->brush );
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    for ( usize i = 0u; i < BrushPiece_PlaneCount( &slab.piece ); ++i ) {
        geometry_piece_plane_t &plane = slab.piece.planes.pData[i];
        if ( plane.sourceSideId.value == sideId.value ) {
            plane.plane.d -= distance;
        }
    }
    geometry_piece_plane_t back{};
    back.plane = math::Planed_Make( math::Vec3d_Negate( pSide->plane.normal ), -pSide->plane.d );
    back.origin = geometry_piece_plane_origin_t::FLIPPED_SIDE;
    back.sourceBrushId = brushId;
    back.sourceSideId = sideId;
    status = BrushPiece_TryAppendPlane( &slab.piece, policy, back );
    geometry_piece_extent_t extent = geometry_piece_extent_t::EMPTY;
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryReduce( &slab.piece, policy, &extent, nullptr );
    }
    if ( status == geometry_status_t::OK && extent != geometry_piece_extent_t::SOLID ) {
        status = geometry_status_t::DEGENERATE;
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const geometry_brush_value_t *donors[1] = { pPreview };
    geometry_materialize_desc_t desc{};
    desc.pPiece = &slab.piece;
    desc.donors = { donors, 1u };
    const geometry_brush_value_t *pValue = nullptr;
    status = GeometryMaterialize_TryPiece( pTransaction, desc, &pValue );
    if ( status == geometry_status_t::OK ) {
        status = GeometryTransaction_TryPreviewInsert( pTransaction, pValue );
    }
    if ( status == geometry_status_t::OK && pNewBrushIdOut != nullptr ) {
        *pNewBrushIdOut = pValue->brush.sourceId;
    }
    BrushValue_Release( pValue );
    return status;
}

} // namespace cypher::editor::geometry
