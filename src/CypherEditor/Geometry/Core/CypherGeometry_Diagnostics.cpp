//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Diagnostics.cpp
//  Purpose: Implements bounded structured diagnostics for editable geometry.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Diagnostics.h"

namespace cypher::editor::geometry
{

bool_t GeometryDiagnosticTarget_IsEmpty(
    const geometry_diagnostic_target_t &target ) noexcept
{
    return !GeometrySourceId_IsValid( target.source ) &&
           !GeometrySourceId_IsValid( target.component ) &&
           target.representation ==
               geometry_source_representation_kind_t::INVALID &&
           target.componentCode == GEOMETRY_DIAGNOSTIC_COMPONENT_ROOT;
}

bool_t GeometryDiagnosticTarget_IsValid(
    const geometry_diagnostic_target_t &target ) noexcept
{
    if ( !GeometrySourceRepresentationKind_IsValid(
             target.representation ) ||
         !GeometrySourceId_IsValid( target.source ) ) {
        return false;
    }

    if ( target.componentCode == GEOMETRY_DIAGNOSTIC_COMPONENT_ROOT ) {
        return !GeometrySourceId_IsValid( target.component );
    }

    return GeometrySourceId_IsValid( target.component ) &&
           target.component.value != target.source.value;
}

bool_t GeometryDiagnostic_IsValid(
    const geometry_diagnostic_t &diagnostic ) noexcept
{
    if ( !GeometryDiagnosticCode_IsValid( diagnostic.code ) ||
         !GeometryDiagnosticModule_IsValid( diagnostic.module ) ||
         !GeometryDiagnosticSeverity_IsValid( diagnostic.severity ) ||
         GeometryDiagnosticCode_Module( diagnostic.code ) !=
             diagnostic.module ) {
        return false;
    }

    const bool_t bHasTarget =
        GeometryDiagnosticTarget_IsValid( diagnostic.target );
    const bool_t bTargetEmpty =
        GeometryDiagnosticTarget_IsEmpty( diagnostic.target );
    const bool_t bHasRelated =
        GeometryDiagnosticTarget_IsValid( diagnostic.related );
    const bool_t bRelatedEmpty =
        GeometryDiagnosticTarget_IsEmpty( diagnostic.related );

    return ( bHasTarget || bTargetEmpty ) &&
           ( bHasRelated || bRelatedEmpty ) &&
           ( !bHasRelated || bHasTarget );
}

geometry_status_t GeometryDiagnosticBuffer_Init(
    geometry_diagnostic_buffer_t *pBuffer,
    span_t<geometry_diagnostic_t> storage ) noexcept
{
    if ( pBuffer == nullptr || !common::Span_IsValid( storage ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pBuffer->bInitialized ) {
        return GeometryDiagnosticBuffer_IsValid( pBuffer )
            ? geometry_status_t::ALREADY_INITIALIZED
            : geometry_status_t::CORRUPT_STATE;
    }

    // Commit only after all validation has succeeded. The sink borrows storage,
    // so initialization cannot allocate and cannot partially acquire resources.
    const geometry_diagnostic_buffer_t initialized{
        storage,
        0u,
        0u,
        true
    };
    *pBuffer = initialized;
    return geometry_status_t::OK;
}

void GeometryDiagnosticBuffer_Shutdown(
    geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    if ( pBuffer != nullptr ) {
        *pBuffer = {};
    }
}

bool_t GeometryDiagnosticBuffer_IsValid(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    return pBuffer != nullptr && pBuffer->bInitialized &&
           common::Span_IsValid( pBuffer->storage ) &&
           pBuffer->cStored <= pBuffer->storage.nCount;
}

bool_t GeometryDiagnosticBuffer_ValidateDeep(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    if ( !GeometryDiagnosticBuffer_IsValid( pBuffer ) ) {
        return false;
    }

    for ( usize iRecord = 0u; iRecord < pBuffer->cStored; ++iRecord ) {
        if ( !GeometryDiagnostic_IsValid( pBuffer->storage.pData[iRecord] ) ) {
            return false;
        }
    }
    return true;
}

geometry_status_t GeometryDiagnosticBuffer_Clear(
    geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    if ( pBuffer == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pBuffer->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryDiagnosticBuffer_IsValid( pBuffer ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    pBuffer->cStored = 0u;
    pBuffer->cTruncated = 0u;
    return geometry_status_t::OK;
}

geometry_status_t GeometryDiagnosticBuffer_Append(
    geometry_diagnostic_buffer_t *pBuffer,
    const geometry_diagnostic_t &diagnostic ) noexcept
{
    if ( pBuffer == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pBuffer->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryDiagnosticBuffer_IsValid( pBuffer ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometryDiagnostic_IsValid( diagnostic ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( pBuffer->cStored == pBuffer->storage.nCount ) {
        // Saturation preserves the fact of truncation without wrapping to zero.
        if ( pBuffer->cTruncated != common::CY_U64_MAX ) {
            ++pBuffer->cTruncated;
        }
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }

    pBuffer->storage.pData[pBuffer->cStored] = diagnostic;
    ++pBuffer->cStored;
    return geometry_status_t::OK;
}

span_t<const geometry_diagnostic_t> GeometryDiagnosticBuffer_Records(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    return GeometryDiagnosticBuffer_IsValid( pBuffer )
        ? span_t<const geometry_diagnostic_t>{
              pBuffer->storage.pData,
              pBuffer->cStored }
        : span_t<const geometry_diagnostic_t>{};
}

usize GeometryDiagnosticBuffer_Count(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    return GeometryDiagnosticBuffer_IsValid( pBuffer )
        ? pBuffer->cStored
        : 0u;
}

usize GeometryDiagnosticBuffer_Capacity(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    return GeometryDiagnosticBuffer_IsValid( pBuffer )
        ? pBuffer->storage.nCount
        : 0u;
}

u64 GeometryDiagnosticBuffer_TruncatedCount(
    const geometry_diagnostic_buffer_t *pBuffer ) noexcept
{
    return GeometryDiagnosticBuffer_IsValid( pBuffer )
        ? pBuffer->cTruncated
        : 0u;
}

} // namespace cypher::editor::geometry
