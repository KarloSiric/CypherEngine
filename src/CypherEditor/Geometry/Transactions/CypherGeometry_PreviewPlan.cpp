//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PreviewPlan.cpp
//  Purpose: Implements atomic batches of preview changes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_PreviewPlan.h"

namespace cypher::editor::geometry
{

namespace
{

using common::usize;

struct saved_preview_t {
    geometry_source_id_t brushId;
    const geometry_brush_value_t *pOriginal; // owned; null when absent
};

void Restore( geometry_transaction_t *pTransaction, const saved_preview_t &saved ) noexcept
{
    const geometry_brush_value_t *pCurrent = nullptr;
    const bool bPresent =
        GeometryTransaction_TryGetPreview( pTransaction, saved.brushId, &pCurrent ) ==
        geometry_status_t::OK;
    geometry_status_t status = geometry_status_t::OK;
    if ( saved.pOriginal == nullptr ) {
        if ( bPresent ) {
            status = GeometryTransaction_TryPreviewRemove( pTransaction, saved.brushId );
        }
    } else if ( !bPresent ) {
        status = GeometryTransaction_TryPreviewInsert( pTransaction, saved.pOriginal );
    } else if ( pCurrent != saved.pOriginal ) {
        status = GeometryTransaction_TryPreviewReplace( pTransaction, saved.pOriginal );
    }
    CY_ASSERT_MSG( status == geometry_status_t::OK, "Preview restoration must not fail." );
    ( void )status;
}

} // namespace

geometry_preview_plan_t::~geometry_preview_plan_t() noexcept
{
    PreviewPlan_Shutdown( this );
}

geometry_status_t PreviewPlan_Init(
    geometry_preview_plan_t *pPlan, const common::allocator_t *pAllocator ) noexcept
{
    if ( pPlan == nullptr || !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pPlan->steps.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    return common::Vector_Init( &pPlan->steps, pAllocator, 0u )
        ? geometry_status_t::OK
        : geometry_status_t::ALLOCATION_FAILED;
}

void PreviewPlan_Shutdown( geometry_preview_plan_t *pPlan ) noexcept
{
    if ( pPlan == nullptr || pPlan->steps.pAllocator == nullptr ) {
        return;
    }
    for ( usize i = 0u; i < common::Vector_Count( &pPlan->steps ); ++i ) {
        BrushValue_Release( pPlan->steps.pData[i].pValue );
    }
    common::Vector_Shutdown( &pPlan->steps );
}

geometry_status_t PreviewPlan_TryAddValue(
    geometry_preview_plan_t *pPlan,
    geometry_preview_step_kind_t kind,
    const geometry_brush_value_t *pValue ) noexcept
{
    if ( pPlan == nullptr || pPlan->steps.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pValue == nullptr || ( kind != geometry_preview_step_kind_t::REPLACE &&
                                kind != geometry_preview_step_kind_t::INSERT ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_preview_step_t step{ kind, pValue->brush.sourceId, pValue };
    return common::Vector_PushBack( &pPlan->steps, step ) ? geometry_status_t::OK
                                                          : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t PreviewPlan_TryAddRemove(
    geometry_preview_plan_t *pPlan, geometry_source_id_t brushId ) noexcept
{
    if ( pPlan == nullptr || pPlan->steps.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_preview_step_t step{ geometry_preview_step_kind_t::REMOVE, brushId, nullptr };
    return common::Vector_PushBack( &pPlan->steps, step ) ? geometry_status_t::OK
                                                          : geometry_status_t::ALLOCATION_FAILED;
}

geometry_status_t PreviewPlan_TryApply(
    geometry_preview_plan_t *pPlan, geometry_transaction_t *pTransaction ) noexcept
{
    if ( pPlan == nullptr || pPlan->steps.pAllocator == nullptr ||
         !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const usize cSteps = common::Vector_Count( &pPlan->steps );

    // Save each touched brush's prior preview, and make sure the
    // transaction has an entry for every brush up front, so both applying
    // and restoring only rewrite existing entries.
    common::vector_t<saved_preview_t> saved{};
    if ( !common::Vector_Init( &saved, pPlan->steps.pAllocator, cSteps ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t status = geometry_status_t::OK;
    for ( usize i = 0u; i < cSteps; ++i ) {
        const geometry_source_id_t id = pPlan->steps.pData[i].brushId;
        bool bKnown = false;
        for ( usize j = 0u; j < common::Vector_Count( &saved ) && !bKnown; ++j ) {
            bKnown = saved.pData[j].brushId.value == id.value;
        }
        if ( bKnown ) {
            continue;
        }
        const geometry_brush_value_t *pOriginal = nullptr;
        if ( GeometryTransaction_TryGetPreview( pTransaction, id, &pOriginal ) !=
             geometry_status_t::OK ) {
            pOriginal = nullptr;
        }
        BrushValue_AddRef( pOriginal );
        ( void )common::Vector_PushBack( &saved, saved_preview_t{ id, pOriginal } );
    }

    usize cApplied = 0u;
    for ( ; cApplied < cSteps; ++cApplied ) {
        const geometry_preview_step_t &step = pPlan->steps.pData[cApplied];
        switch ( step.kind ) {
            case geometry_preview_step_kind_t::REPLACE:
                status = GeometryTransaction_TryPreviewReplace( pTransaction, step.pValue );
                break;
            case geometry_preview_step_kind_t::INSERT:
                status = GeometryTransaction_TryPreviewInsert( pTransaction, step.pValue );
                break;
            case geometry_preview_step_kind_t::REMOVE:
                status = GeometryTransaction_TryPreviewRemove( pTransaction, step.brushId );
                break;
            default:
                status = geometry_status_t::INVALID_ARGUMENT;
                break;
        }
        if ( status != geometry_status_t::OK ) {
            break;
        }
    }

    if ( status != geometry_status_t::OK ) {
        for ( usize i = common::Vector_Count( &saved ); i > 0u; --i ) {
            Restore( pTransaction, saved.pData[i - 1u] );
        }
    }
    for ( usize i = 0u; i < common::Vector_Count( &saved ); ++i ) {
        BrushValue_Release( saved.pData[i].pOriginal );
    }
    return status;
}

geometry_status_t PreviewPlan_TryPin(
    const geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    common::vector_t<const geometry_brush_value_t *> *pPinnedOut ) noexcept
{
    if ( pPinnedOut == nullptr || pPinnedOut->pAllocator == nullptr ||
         !common::Span_IsValid( brushIds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    PreviewPlan_ReleasePinned( pPinnedOut );
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Vector_Reserve( pPinnedOut, brushIds.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < brushIds.nCount; ++i ) {
        const geometry_brush_value_t *pValue = nullptr;
        if ( GeometryTransaction_TryGetPreview( pTransaction, brushIds.pData[i], &pValue ) !=
             geometry_status_t::OK ) {
            PreviewPlan_ReleasePinned( pPinnedOut );
            return geometry_status_t::INVALID_HANDLE;
        }
        BrushValue_AddRef( pValue );
        ( void )common::Vector_PushBack( pPinnedOut, pValue );
    }
    return geometry_status_t::OK;
}

void PreviewPlan_ReleasePinned( common::vector_t<const geometry_brush_value_t *> *pPinned ) noexcept
{
    if ( pPinned == nullptr || pPinned->pAllocator == nullptr ) {
        return;
    }
    for ( usize i = 0u; i < common::Vector_Count( pPinned ); ++i ) {
        BrushValue_Release( pPinned->pData[i] );
    }
    common::Vector_Clear( pPinned );
}

} // namespace cypher::editor::geometry
