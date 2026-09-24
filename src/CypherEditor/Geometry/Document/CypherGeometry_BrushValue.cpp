//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushValue.cpp
//  Purpose: Implements creation, validation, and reference counting of
//           immutable committed brush values.
//  Details: Creation validates in cheapest-first order (planes, binding,
//           attributes) before allocating anything, so the common invalid
//           preview costs no allocation. The block is then built member by
//           member; any failure destroys the partial value, which is safe
//           because every member's Shutdown accepts a zeroed object.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushValue.h"

#include <new>

namespace cypher::editor::geometry
{

namespace
{

void DestroyValue( geometry_brush_value_t *pValue ) noexcept
{
    const common::allocator_t *pAllocator = pValue->pAllocator;
    BrushBoundary_Shutdown( &pValue->boundary );
    BrushSideAttributeStore_Shutdown( &pValue->attributes );
    BrushSolid_Shutdown( &pValue->brush );
    pValue->~geometry_brush_value_t();
    common::Allocator_Free(
        pAllocator, pValue,
        sizeof( geometry_brush_value_t ), alignof( geometry_brush_value_t ) );
}

} // namespace

geometry_status_t BrushValue_ValidateAttributeBinding(
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes ) noexcept
{
    if ( pBrush == nullptr || pAttributes == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBrush->sides.pAllocator == nullptr ||
         pAttributes->records.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( BrushSideAttributeStore_Count( pAttributes ) != cSides ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Equal counts plus in-range, pairwise-distinct indices make the
    // binding a permutation. Quadratic, but the side count is bounded by
    // cBrushSidesPerBrushMax before any caller in this module gets here.
    for ( common::usize i = 0u; i < cSides; ++i ) {
        const common::u32 iAttribute = pBrush->sides.pData[i].iAttributeIndex;
        if ( static_cast<common::usize>( iAttribute ) >= cSides ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        for ( common::usize j = 0u; j < i; ++j ) {
            if ( pBrush->sides.pData[j].iAttributeIndex == iAttribute ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t BrushValue_TryCreate(
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    const geometry_document_t *pDomain,
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const geometry_brush_value_t **ppValueOut,
    brush_validation_result_t *pValidationOut ) noexcept
{
    if ( pValidationOut != nullptr ) {
        *pValidationOut = {};
    }
    if ( ppValueOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppValueOut = nullptr;
    if ( pAllocator == nullptr || pDomain == nullptr ||
         pBrush == nullptr || pAttributes == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // ---- Cheap validation before any allocation --------------------------

    brush_validation_result_t validation{};
    validation.status = BrushValidation_Quick( pBrush, policy );
    if ( validation.status != geometry_status_t::OK ) {
        if ( pValidationOut != nullptr ) {
            *pValidationOut = validation;
        }
        return validation.status;
    }

    geometry_status_t status =
        BrushValue_ValidateAttributeBinding( pBrush, pAttributes );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = BrushSideAttributeStore_Validate( pAttributes, policy );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    // ---- Build the value --------------------------------------------------

    void *pBlock = common::Allocator_Allocate(
        pAllocator,
        sizeof( geometry_brush_value_t ), alignof( geometry_brush_value_t ) );
    if ( pBlock == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_brush_value_t *pValue = new ( pBlock ) geometry_brush_value_t{};
    pValue->pAllocator = pAllocator;
    pValue->pDomain = pDomain;

    status = BrushSolid_Init( &pValue->brush, pAllocator, pBrush->sourceId );
    if ( status == geometry_status_t::OK ) {
        status = BrushSolid_TryCopyFrom( &pValue->brush, pBrush, policy.limits );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_Init( &pValue->attributes, pAllocator );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushSideAttributeStore_TryCopyFrom(
            &pValue->attributes, pAttributes, policy.limits );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushBoundary_Init( &pValue->boundary, pAllocator );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushBoundary_TryReconstruct(
            &pValue->boundary, &pValue->brush, policy );
        validation.status = status;
    }
    if ( status == geometry_status_t::OK ) {
        validation = BrushValidation_CheckBoundary(
            &pValue->brush, &pValue->boundary, policy );
        status = validation.status;
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushBoundary_TryGetBounds( &pValue->boundary, &pValue->bounds );
    }

    if ( pValidationOut != nullptr ) {
        *pValidationOut = validation;
    }
    if ( status != geometry_status_t::OK ) {
        DestroyValue( pValue );
        return status;
    }

    common::RefCount_Init( &pValue->refs, 1u );
    *ppValueOut = pValue;
    return geometry_status_t::OK;
}

void BrushValue_AddRef( const geometry_brush_value_t *pValue ) noexcept
{
    if ( pValue == nullptr ) {
        return;
    }
    ( void )common::RefCount_AddRef( &pValue->refs );
}

void BrushValue_Release( const geometry_brush_value_t *pValue ) noexcept
{
    if ( pValue == nullptr ) {
        return;
    }
    if ( common::RefCount_Release( &pValue->refs ) == 0u ) {
        // The last reference owns destruction. Nothing else can observe the
        // value now, so shedding const here cannot break immutability.
        DestroyValue( const_cast<geometry_brush_value_t *>( pValue ) );
    }
}

common::u32 BrushValue_RefCount( const geometry_brush_value_t *pValue ) noexcept
{
    return pValue == nullptr ? 0u : common::RefCount_Load( &pValue->refs );
}

} // namespace cypher::editor::geometry
