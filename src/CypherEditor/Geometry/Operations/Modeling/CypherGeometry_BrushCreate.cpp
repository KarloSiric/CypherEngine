//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushCreate.cpp
//  Purpose: Implements brush creation from generated pieces.
//  Details: Surfacing is supplied through a synthetic donor brush whose
//           single side carries the requested record, so creation reuses
//           the materializer's attribute path unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushCreate.h"

#include "CypherGeometry_PreviewPlan.h"

namespace cypher::editor::geometry
{

namespace
{

using common::usize;

// Materializes every piece and previews them atomically.
geometry_status_t CreateAll(
    geometry_transaction_t *pTransaction,
    const geometry_brush_piece_t *const *ppPieces,
    usize cPieces,
    const geometry_create_surfacing_t &surfacing,
    geometry_source_id_t *pIdsOut ) noexcept
{
    geometry_document_t *pDocument = pTransaction->pDocument;
    const common::allocator_t *pAllocator = pDocument->pAllocator;
    const geometry_policy_t &policy = pDocument->policy;

    geometry_status_t status = BrushSideAttributes_Validate( policy.numerical, surfacing.attributes );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    geometry_preview_plan_t plan{};
    if ( PreviewPlan_Init( &plan, pAllocator ) != geometry_status_t::OK ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // A donor candidate list with one record and no preferred side: every
    // face selects it and re-bases it onto its own normal.
    const geometry_attribute_candidate_t candidate{
        GEOMETRY_SOURCE_ID_INVALID, math::CY_VEC3D_UP, &surfacing.attributes };

    for ( usize i = 0u; status == geometry_status_t::OK && i < cPieces; ++i ) {
        const geometry_brush_piece_t *pPiece = ppPieces[i];
        const usize cPlanes = BrushPiece_PlaneCount( pPiece );
        if ( cPlanes < 4u ) {
            status = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        geometry_source_id_t *pIds = static_cast<geometry_source_id_t *>( common::Allocator_Allocate(
            pAllocator, sizeof( geometry_source_id_t ) * ( cPlanes + 1u ),
            alignof( geometry_source_id_t ) ) );
        if ( pIds == nullptr ) {
            status = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        status = GeometryTransaction_TryAllocateSourceIds( pTransaction, { pIds, cPlanes + 1u } );

        brush_solid_t brush{};
        geometry_brush_side_attribute_store_t attributes{};
        if ( status == geometry_status_t::OK ) {
            status = BrushSolid_Init( &brush, pAllocator, pIds[0] );
        }
        if ( status == geometry_status_t::OK ) {
            status = BrushSideAttributeStore_Init( &attributes, pAllocator );
        }
        for ( usize p = 0u; status == geometry_status_t::OK && p < cPlanes; ++p ) {
            const math::planed_t plane = pPiece->planes.pData[p].plane;
            geometry_brush_side_attributes_t record{};
            usize iRecord = 0u;
            status = AttributePropagation_TrySelectForFace(
                policy.numerical, &candidate, 1u, GEOMETRY_SOURCE_ID_INVALID, false,
                plane.normal, &record );
            if ( status == geometry_status_t::OK ) {
                status = BrushSideAttributeStore_TryAppend( &attributes, policy, record, &iRecord );
            }
            if ( status == geometry_status_t::OK ) {
                brush_solid_side_t side{};
                side.plane = plane;
                side.sourceId = pIds[p + 1u];
                side.iAttributeIndex = static_cast<common::u32>( iRecord );
                status = BrushSolid_TryAddSide( &brush, policy.limits, side, nullptr );
            }
        }
        const geometry_brush_value_t *pValue = nullptr;
        if ( status == geometry_status_t::OK ) {
            status = GeometryDocument_TryCreateBrushValue( pDocument, &brush, &attributes, &pValue,
                                                           nullptr );
        }
        if ( status == geometry_status_t::OK ) {
            status = PreviewPlan_TryAddValue( &plan, geometry_preview_step_kind_t::INSERT, pValue );
            if ( status != geometry_status_t::OK ) {
                BrushValue_Release( pValue );
            }
        }
        if ( status == geometry_status_t::OK && pIdsOut != nullptr ) {
            pIdsOut[i] = pIds[0];
        }
        BrushSideAttributeStore_Shutdown( &attributes );
        BrushSolid_Shutdown( &brush );
        common::Allocator_Free( pAllocator, pIds, sizeof( geometry_source_id_t ) * ( cPlanes + 1u ),
                                alignof( geometry_source_id_t ) );
    }
    if ( status == geometry_status_t::OK ) {
        status = PreviewPlan_TryApply( &plan, pTransaction );
    }
    return status;
}

} // namespace

geometry_status_t BrushCreate_TryFromPieces(
    geometry_transaction_t *pTransaction,
    const geometry_piece_list_t *pPieces,
    const geometry_create_surfacing_t &surfacing,
    common::span_t<geometry_source_id_t> brushIdsOut ) noexcept
{
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pPieces == nullptr || pPieces->planes.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const usize cPieces = PieceList_Count( pPieces );
    if ( cPieces == 0u || !common::Span_IsValid( brushIdsOut ) ||
         ( brushIdsOut.nCount != 0u && brushIdsOut.nCount < cPieces ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::allocator_t *pAllocator = pTransaction->pDocument->pAllocator;

    // Expand the list into owned pieces.
    common::vector_t<geometry_brush_piece_t *> pieces{};
    if ( !common::Vector_Init( &pieces, pAllocator, cPieces ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = geometry_status_t::OK;
    for ( usize i = 0u; status == geometry_status_t::OK && i < cPieces; ++i ) {
        void *pBlock = common::Allocator_Allocate( pAllocator, sizeof( geometry_brush_piece_t ),
                                                   alignof( geometry_brush_piece_t ) );
        if ( pBlock == nullptr ) {
            status = geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        geometry_brush_piece_t *pPiece = new ( pBlock ) geometry_brush_piece_t{};
        ( void )common::Vector_PushBack( &pieces, pPiece );
        status = BrushPiece_Init( pPiece, pAllocator );
        if ( status == geometry_status_t::OK ) {
            status = PieceList_TryGet( pPieces, i, pPiece );
        }
    }
    if ( status == geometry_status_t::OK ) {
        status = CreateAll( pTransaction, pieces.pData, cPieces, surfacing,
                            brushIdsOut.nCount != 0u ? brushIdsOut.pData : nullptr );
    }
    for ( usize i = 0u; i < common::Vector_Count( &pieces ); ++i ) {
        BrushPiece_Shutdown( pieces.pData[i] );
        pieces.pData[i]->~geometry_brush_piece_t();
        common::Allocator_Free( pAllocator, pieces.pData[i], sizeof( geometry_brush_piece_t ),
                                alignof( geometry_brush_piece_t ) );
    }
    return status;
}

geometry_status_t BrushCreate_TryFromPiece(
    geometry_transaction_t *pTransaction,
    const geometry_brush_piece_t *pPiece,
    const geometry_create_surfacing_t &surfacing,
    geometry_source_id_t *pBrushIdOut ) noexcept
{
    if ( pBrushIdOut != nullptr ) {
        *pBrushIdOut = GEOMETRY_SOURCE_ID_INVALID;
    }
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pPiece == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return CreateAll( pTransaction, &pPiece, 1u, surfacing, pBrushIdOut );
}

} // namespace cypher::editor::geometry
