//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PieceMaterialize.cpp
//  Purpose: Implements piece-to-brush-value conversion.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PieceMaterialize.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::usize;

bool_t IsReusable(
    const geometry_piece_plane_t &plane,
    const geometry_materialize_desc_t &desc ) noexcept
{
    return desc.bReuseSideIds && GeometrySourceId_IsValid( desc.brushId ) &&
           plane.origin == geometry_piece_plane_origin_t::SOURCE_SIDE &&
           plane.sourceBrushId.value == desc.brushId.value &&
           GeometrySourceId_IsValid( plane.sourceSideId );
}

} // namespace

geometry_status_t GeometryMaterialize_TryPieceFromValue(
    const geometry_brush_value_t *pValue,
    geometry_brush_piece_t *pPiece ) noexcept
{
    if ( pValue == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return BrushPiece_TryFromBrush( pPiece, &pValue->brush );
}

geometry_status_t GeometryMaterialize_TryPiece(
    geometry_transaction_t *pTransaction,
    const geometry_materialize_desc_t &desc,
    const geometry_brush_value_t **ppValueOut ) noexcept
{
    if ( ppValueOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppValueOut = nullptr;
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const usize cPlanes = BrushPiece_PlaneCount( desc.pPiece );
    if ( cPlanes < 4u || !common::Span_IsValid( desc.donors ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_document_t *pDocument = pTransaction->pDocument;
    const common::allocator_t *pAllocator = pDocument->pAllocator;
    const geometry_policy_t &policy = pDocument->policy;

    // ---- Identities ---------------------------------------------------------
    common::vector_t<geometry_source_id_t> sideIds{};
    if ( !common::Vector_Init( &sideIds, pAllocator, cPlanes ) ||
         !common::Vector_Resize( &sideIds, cPlanes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    usize cFresh = GeometrySourceId_IsValid( desc.brushId ) ? 0u : 1u;
    for ( usize i = 0u; i < cPlanes; ++i ) {
        const geometry_piece_plane_t &plane = desc.pPiece->planes.pData[i];
        sideIds.pData[i] = GEOMETRY_SOURCE_ID_INVALID;
        if ( !IsReusable( plane, desc ) ) {
            ++cFresh;
            continue;
        }
        // A side ID may appear on at most one face of the result.
        bool_t bTaken = false;
        for ( usize j = 0u; j < i && !bTaken; ++j ) {
            bTaken = sideIds.pData[j].value == plane.sourceSideId.value;
        }
        if ( bTaken ) {
            ++cFresh;
        } else {
            sideIds.pData[i] = plane.sourceSideId;
        }
    }
    common::vector_t<geometry_source_id_t> fresh{};
    if ( !common::Vector_Init( &fresh, pAllocator, cFresh ) ||
         !common::Vector_Resize( &fresh, cFresh ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = GeometryTransaction_TryAllocateSourceIds(
        pTransaction, common::span_t<geometry_source_id_t>{ fresh.pData, cFresh } );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    usize iFresh = 0u;
    const geometry_source_id_t brushId =
        GeometrySourceId_IsValid( desc.brushId ) ? desc.brushId : fresh.pData[iFresh++];
    for ( usize i = 0u; i < cPlanes; ++i ) {
        if ( !GeometrySourceId_IsValid( sideIds.pData[i] ) ) {
            sideIds.pData[i] = fresh.pData[iFresh++];
        }
    }

    // ---- Donor candidates -------------------------------------------------
    common::vector_t<geometry_attribute_candidate_t> candidates{};
    if ( !common::Vector_Init( &candidates, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize d = 0u; d < desc.donors.nCount; ++d ) {
        const geometry_brush_value_t *pDonor = desc.donors.pData[d];
        if ( pDonor == nullptr ) {
            continue;
        }
        const usize cSides = BrushSolid_SideCount( &pDonor->brush );
        for ( usize s = 0u; s < cSides; ++s ) {
            const brush_solid_side_t &side = pDonor->brush.sides.pData[s];
            const geometry_attribute_candidate_t candidate{
                side.sourceId, side.plane.normal,
                &pDonor->attributes.records.pData[side.iAttributeIndex] };
            if ( !common::Vector_PushBack( &candidates, candidate ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
        }
    }

    // ---- Brush and attributes ---------------------------------------------
    brush_solid_t brush{};
    geometry_brush_side_attribute_store_t attributes{};
    status = BrushSolid_Init( &brush, pAllocator, brushId );
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TryReserve( &brush, policy.limits, cPlanes );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_Init( &attributes, pAllocator );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_TryReserve( &attributes, policy.limits, cPlanes );
    }
    for ( usize i = 0u; status == geometry_status_t::OK && i < cPlanes; ++i ) {
        const geometry_piece_plane_t &plane = desc.pPiece->planes.pData[i];
        const bool_t bFromSide = plane.origin == geometry_piece_plane_origin_t::SOURCE_SIDE ||
                                 plane.origin == geometry_piece_plane_origin_t::FLIPPED_SIDE;
        geometry_brush_side_attributes_t record{};
        status = AttributePropagation_TrySelectForFace(
            policy.numerical, candidates.pData, common::Vector_Count( &candidates ),
            bFromSide ? plane.sourceSideId : GEOMETRY_SOURCE_ID_INVALID,
            plane.origin == geometry_piece_plane_origin_t::FLIPPED_SIDE,
            plane.plane.normal, &record );
        usize iRecord = 0u;
        if ( status == geometry_status_t::OK ) {
            status = BrushSideAttributeStore_TryAppend( &attributes, policy, record, &iRecord );
        }
        if ( status == geometry_status_t::OK ) {
            brush_solid_side_t side{};
            side.plane = plane.plane;
            side.sourceId = sideIds.pData[i];
            side.iAttributeIndex = static_cast<common::u32>( iRecord );
            status = BrushSolid_TryAddSide( &brush, policy.limits, side, nullptr );
        }
    }
    if ( status == geometry_status_t::OK ) {
        status = GeometryDocument_TryCreateBrushValue(
            pDocument, &brush, &attributes, ppValueOut, nullptr );
    }
    BrushSideAttributeStore_Shutdown( &attributes );
    BrushSolid_Shutdown( &brush );
    return status;
}

} // namespace cypher::editor::geometry
