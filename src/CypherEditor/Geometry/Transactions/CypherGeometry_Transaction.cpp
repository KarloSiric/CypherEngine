//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Transaction.cpp
//  Purpose: Implements preview/commit/cancel transactions.
//  Details: Preview operations only rewrite the transaction's own entry
//           table. Each one secures storage before taking or dropping any
//           reference, so a failure leaves the table exactly as it was.
//           Commit copies the net entries into a record, applies it, and
//           only then ends the transaction; a failed apply destroys the
//           record and leaves the previews in place.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Transaction.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::usize;

constexpr usize cEntryNotFound = common::CY_USIZE_MAX;

usize FindEntry(
    const geometry_transaction_t *pTransaction, geometry_source_id_t brushId ) noexcept
{
    // Linear: transactions touch a handful of brushes in interactive edits.
    // Bulk operations that touch thousands will want an index here.
    const usize cEntries = common::Vector_Count( &pTransaction->entries );
    for ( usize i = 0u; i < cEntries; ++i ) {
        if ( pTransaction->entries.pData[i].brushId.value == brushId.value ) {
            return i;
        }
    }
    return cEntryNotFound;
}

// Shared precondition for every preview mutation.
geometry_status_t CheckPreviewable(
    const geometry_transaction_t *pTransaction ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( GeometryDocument_Revision( pTransaction->pDocument ) !=
         pTransaction->baseRevision ) {
        return geometry_status_t::STALE_REVISION;
    }
    return geometry_status_t::OK;
}

geometry_status_t CheckValue(
    const geometry_transaction_t *pTransaction,
    const geometry_brush_value_t *pValue ) noexcept
{
    if ( pValue == nullptr || pValue->pDomain != pTransaction->pDocument ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    return geometry_status_t::OK;
}

// Appends a new entry, taking references only once storage is secured.
geometry_status_t AppendEntry(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    const geometry_brush_value_t *pBefore,
    const geometry_brush_value_t *pAfter ) noexcept
{
    const geometry_change_entry_t entry{ brushId, pBefore, pAfter };
    if ( !common::Vector_PushBack( &pTransaction->entries, entry ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    BrushValue_AddRef( pBefore );
    BrushValue_AddRef( pAfter );
    return geometry_status_t::OK;
}

void SetAfter( geometry_change_entry_t *pEntry, const geometry_brush_value_t *pAfter ) noexcept
{
    BrushValue_AddRef( pAfter );
    BrushValue_Release( pEntry->pAfter );
    pEntry->pAfter = pAfter;
}

// Ends the transaction: drops every entry reference and retires every
// pending ID that no committed brush now owns.
void EndTransaction( geometry_transaction_t *pTransaction ) noexcept
{
    const usize cEntries = common::Vector_Count( &pTransaction->entries );
    for ( usize i = 0u; i < cEntries; ++i ) {
        BrushValue_Release( pTransaction->entries.pData[i].pBefore );
        BrushValue_Release( pTransaction->entries.pData[i].pAfter );
    }
    const usize cPending = common::Vector_Count( &pTransaction->pendingIds );
    for ( usize i = 0u; i < cPending; ++i ) {
        // IDENTITY_CONFLICT means a committed value now owns the ID, which
        // is exactly the case that must keep it; any other failure means it
        // is already retired. Neither needs handling.
        ( void )GeometryDocument_ReleasePendingId(
            pTransaction->pDocument, pTransaction->pendingIds.pData[i] );
    }
    common::Vector_Shutdown( &pTransaction->entries );
    common::Vector_Shutdown( &pTransaction->pendingIds );
    pTransaction->pDocument = nullptr;
    pTransaction->baseRevision = 0u;
    pTransaction->bActive = false;
}

} // namespace

geometry_transaction_t::~geometry_transaction_t() noexcept
{
    GeometryTransaction_Cancel( this );
}

geometry_status_t GeometryTransaction_Begin(
    geometry_transaction_t *pTransaction,
    geometry_document_t *pDocument ) noexcept
{
    if ( pTransaction == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pTransaction->bActive ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( pDocument == nullptr || !pDocument->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Vector_Init( &pTransaction->entries, pDocument->pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Init( &pTransaction->pendingIds, pDocument->pAllocator, 0u ) ) {
        common::Vector_Shutdown( &pTransaction->entries );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pTransaction->pDocument = pDocument;
    pTransaction->baseRevision = GeometryDocument_Revision( pDocument );
    pTransaction->bActive = true;
    return geometry_status_t::OK;
}

bool_t GeometryTransaction_IsActive( const geometry_transaction_t *pTransaction ) noexcept
{
    return pTransaction != nullptr && pTransaction->bActive;
}

geometry_status_t GeometryTransaction_TryAllocateSourceIds(
    geometry_transaction_t *pTransaction,
    common::span_t<geometry_source_id_t> idsOut ) noexcept
{
    const geometry_status_t ready = CheckPreviewable( pTransaction );
    if ( ready != geometry_status_t::OK ) {
        return ready;
    }
    if ( !common::Span_IsValid( idsOut ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const usize cPending = common::Vector_Count( &pTransaction->pendingIds );
    if ( idsOut.nCount > common::CY_USIZE_MAX - cPending ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    // Secure tracking storage first: an ID the transaction cannot track
    // could never be retired on cancel.
    if ( !common::Vector_Reserve( &pTransaction->pendingIds, cPending + idsOut.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const geometry_status_t status =
        GeometryDocument_TryAllocateSourceIds( pTransaction->pDocument, idsOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    for ( usize i = 0u; i < idsOut.nCount; ++i ) {
        ( void )common::Vector_PushBack( &pTransaction->pendingIds, idsOut.pData[i] );
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryTransaction_TryPreviewInsert(
    geometry_transaction_t *pTransaction,
    const geometry_brush_value_t *pValue ) noexcept
{
    geometry_status_t status = CheckPreviewable( pTransaction );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = CheckValue( pTransaction, pValue );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const geometry_source_id_t brushId = pValue->brush.sourceId;
    const usize iEntry = FindEntry( pTransaction, brushId );
    if ( iEntry != cEntryNotFound ) {
        geometry_change_entry_t &entry = pTransaction->entries.pData[iEntry];
        if ( entry.pAfter != nullptr ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        SetAfter( &entry, pValue );
        return geometry_status_t::OK;
    }

    geometry_brush_handle_t handle{};
    if ( GeometryDocument_TryFindBrush( pTransaction->pDocument, brushId, &handle ) ==
         geometry_status_t::OK ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }
    return AppendEntry( pTransaction, brushId, nullptr, pValue );
}

geometry_status_t GeometryTransaction_TryPreviewReplace(
    geometry_transaction_t *pTransaction,
    const geometry_brush_value_t *pValue ) noexcept
{
    geometry_status_t status = CheckPreviewable( pTransaction );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = CheckValue( pTransaction, pValue );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const geometry_source_id_t brushId = pValue->brush.sourceId;
    const usize iEntry = FindEntry( pTransaction, brushId );
    if ( iEntry != cEntryNotFound ) {
        geometry_change_entry_t &entry = pTransaction->entries.pData[iEntry];
        if ( entry.pAfter == nullptr ) {
            return geometry_status_t::INVALID_HANDLE;
        }
        SetAfter( &entry, pValue );
        return geometry_status_t::OK;
    }

    const geometry_brush_value_t *pCommitted = nullptr;
    if ( GeometryDocument_TryGetBrushById( pTransaction->pDocument, brushId, &pCommitted ) !=
         geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    return AppendEntry( pTransaction, brushId, pCommitted, pValue );
}

geometry_status_t GeometryTransaction_TryPreviewRemove(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId ) noexcept
{
    const geometry_status_t status = CheckPreviewable( pTransaction );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const usize iEntry = FindEntry( pTransaction, brushId );
    if ( iEntry != cEntryNotFound ) {
        geometry_change_entry_t &entry = pTransaction->entries.pData[iEntry];
        if ( entry.pAfter == nullptr ) {
            return geometry_status_t::INVALID_HANDLE;
        }
        SetAfter( &entry, nullptr );
        return geometry_status_t::OK;
    }

    const geometry_brush_value_t *pCommitted = nullptr;
    if ( GeometryDocument_TryGetBrushById( pTransaction->pDocument, brushId, &pCommitted ) !=
         geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    return AppendEntry( pTransaction, brushId, pCommitted, nullptr );
}

geometry_status_t GeometryTransaction_TryGetPreview(
    const geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    const geometry_brush_value_t **ppValueOut ) noexcept
{
    if ( ppValueOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppValueOut = nullptr;
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const usize iEntry = FindEntry( pTransaction, brushId );
    if ( iEntry != cEntryNotFound ) {
        const geometry_brush_value_t *pAfter = pTransaction->entries.pData[iEntry].pAfter;
        if ( pAfter == nullptr ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        *ppValueOut = pAfter;
        return geometry_status_t::OK;
    }
    return GeometryDocument_TryGetBrushById( pTransaction->pDocument, brushId, ppValueOut );
}

usize GeometryTransaction_ChangeCount( const geometry_transaction_t *pTransaction ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return 0u;
    }
    usize cChanged = 0u;
    const usize cEntries = common::Vector_Count( &pTransaction->entries );
    for ( usize i = 0u; i < cEntries; ++i ) {
        if ( pTransaction->entries.pData[i].pBefore != pTransaction->entries.pData[i].pAfter ) {
            ++cChanged;
        }
    }
    return cChanged;
}

geometry_status_t GeometryTransaction_TryCommit(
    geometry_transaction_t *pTransaction,
    geometry_change_record_t **ppRecordOut ) noexcept
{
    if ( ppRecordOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppRecordOut = nullptr;
    const geometry_status_t ready = CheckPreviewable( pTransaction );
    if ( ready != geometry_status_t::OK ) {
        return ready;
    }

    const usize cChanged = GeometryTransaction_ChangeCount( pTransaction );
    if ( cChanged == 0u ) {
        EndTransaction( pTransaction );
        return geometry_status_t::OK;
    }

    geometry_document_t *pDocument = pTransaction->pDocument;
    geometry_change_record_t *pRecord = nullptr;
    geometry_status_t status = GeometryChangeRecord_TryCreate(
        pDocument->pAllocator, pDocument, cChanged, &pRecord );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    // The record takes its own references; the transaction keeps its
    // entries until the apply succeeds.
    const usize cEntries = common::Vector_Count( &pTransaction->entries );
    for ( usize i = 0u; i < cEntries; ++i ) {
        const geometry_change_entry_t &entry = pTransaction->entries.pData[i];
        if ( entry.pBefore == entry.pAfter ) {
            continue;
        }
        // Cannot fail: capacity for cChanged entries reserved at creation.
        ( void )common::Vector_PushBack( &pRecord->entries, entry );
        BrushValue_AddRef( entry.pBefore );
        BrushValue_AddRef( entry.pAfter );
    }

    common::u64 newRevision = 0u;
    status = GeometryChangeRecord_TryApply(
        pRecord, pDocument, geometry_change_direction_t::FORWARD,
        pTransaction->baseRevision, &newRevision );
    if ( status != geometry_status_t::OK ) {
        GeometryChangeRecord_Destroy( pRecord );
        return status;
    }

    pRecord->revisionBefore = pTransaction->baseRevision;
    pRecord->revisionAfter = newRevision;
    EndTransaction( pTransaction );
    *ppRecordOut = pRecord;
    return geometry_status_t::OK;
}

void GeometryTransaction_Cancel( geometry_transaction_t *pTransaction ) noexcept
{
    if ( pTransaction == nullptr || !pTransaction->bActive ) {
        return;
    }
    EndTransaction( pTransaction );
}

} // namespace cypher::editor::geometry
