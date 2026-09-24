//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_OperationContext.cpp
//  Purpose: Implements borrowed geometry operation services and checkpoints.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_OperationContext.h"

namespace cypher::editor::geometry
{

namespace
{

geometry_status_t ValidateDescriptor(
    const geometry_operation_context_desc_t &desc ) noexcept
{
    if ( desc.pPolicy == nullptr ||
         !GeometryPolicy_IsValid( *desc.pPolicy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( desc.pScratch != nullptr ) {
        if ( !GeometryScratch_IsValid( desc.pScratch ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( !GeometryScratch_IsInitialized( desc.pScratch ) ) {
            return geometry_status_t::NOT_INITIALIZED;
        }
        if ( desc.pScratch->cbBudget >
             desc.pPolicy->limits.cbScratchMax ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    }

    if ( desc.pDiagnostics != nullptr ) {
        if ( !desc.pDiagnostics->bInitialized ) {
            return geometry_status_t::NOT_INITIALIZED;
        }
        if ( !GeometryDiagnosticBuffer_IsValid( desc.pDiagnostics ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( GeometryDiagnosticBuffer_Capacity( desc.pDiagnostics ) >
             desc.pPolicy->limits.cDiagnosticsMax ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
    }

    if ( desc.pCancellation != nullptr &&
         !GeometryCancellation_IsValid( desc.pCancellation ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    return geometry_status_t::OK;
}

geometry_operation_context_desc_t DescriptorFromContext(
    const geometry_operation_context_t &context ) noexcept
{
    return {
        context.pPolicy,
        context.pScratch,
        context.pDiagnostics,
        context.pCancellation
    };
}

} // namespace

bool GeometryCancellation_IsValid(
    const geometry_cancellation_t *pCancellation ) noexcept
{
    return pCancellation != nullptr &&
           ( pCancellation->pfnQuery != nullptr ||
             pCancellation->pUserData == nullptr );
}

bool_t GeometryCancellation_IsRequested(
    const geometry_cancellation_t *pCancellation ) noexcept
{
    if ( pCancellation == nullptr ) {
        return common::CY_FALSE;
    }

    if ( pCancellation->pRequested != nullptr &&
         common::Cy_AtomicLoad(
             pCancellation->pRequested,
             common::CY_MEMORY_ORDER_ACQUIRE ) ) {
        return common::CY_TRUE;
    }

    return pCancellation->pfnQuery != nullptr
        ? pCancellation->pfnQuery( pCancellation->pUserData )
        : common::CY_FALSE;
}

geometry_status_t GeometryOperationContext_Bind(
    geometry_operation_context_t *pContext,
    const geometry_operation_context_desc_t &desc ) noexcept
{
    if ( pContext == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const geometry_status_t validation = ValidateDescriptor( desc );
    if ( validation != geometry_status_t::OK ) {
        return validation;
    }

    *pContext = {
        desc.pPolicy,
        desc.pScratch,
        desc.pDiagnostics,
        desc.pCancellation
    };
    return geometry_status_t::OK;
}

bool GeometryOperationContext_IsValid(
    const geometry_operation_context_t *pContext ) noexcept
{
    return pContext != nullptr &&
           ValidateDescriptor( DescriptorFromContext( *pContext ) ) ==
               geometry_status_t::OK;
}

geometry_status_t GeometryOperationContext_Checkpoint(
    const geometry_operation_context_t *pContext ) noexcept
{
    if ( pContext == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pContext->pPolicy == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryOperationContext_IsValid( pContext ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    return GeometryCancellation_IsRequested( pContext->pCancellation )
        ? geometry_status_t::CANCELLED
        : geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
