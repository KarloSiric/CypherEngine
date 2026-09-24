//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_History.cpp
//  Purpose: Implements the bounded linear undo/redo stack.
//  Details: Push reserves its slot before discarding the redo tail or the
//           oldest record, so a failed push changes nothing. Undo and redo
//           delegate atomicity to the document apply and move the cursor
//           only after it succeeds.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_History.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::usize;

bool_t IsReady( const geometry_history_t *pHistory ) noexcept
{
    return pHistory != nullptr && pHistory->bInitialized;
}

void DestroyRange( geometry_history_t *pHistory, usize iFirst ) noexcept
{
    while ( common::Vector_Count( &pHistory->records ) > iFirst ) {
        const usize iLast = common::Vector_Count( &pHistory->records ) - 1u;
        GeometryChangeRecord_Destroy( pHistory->records.pData[iLast] );
        common::Vector_PopBack( &pHistory->records );
    }
}

} // namespace

geometry_history_t::~geometry_history_t() noexcept
{
    GeometryHistory_Shutdown( this );
}

geometry_status_t GeometryHistory_Init(
    geometry_history_t *pHistory,
    geometry_document_t *pDocument,
    usize cDepthMax ) noexcept
{
    if ( pHistory == nullptr || cDepthMax == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pHistory->bInitialized ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( pDocument == nullptr || !pDocument->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Vector_Init( &pHistory->records, pDocument->pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pHistory->pDocument = pDocument;
    pHistory->cUndoable = 0u;
    pHistory->cDepthMax = cDepthMax;
    pHistory->expectedRevision = GeometryDocument_Revision( pDocument );
    pHistory->bInitialized = true;
    return geometry_status_t::OK;
}

void GeometryHistory_Shutdown( geometry_history_t *pHistory ) noexcept
{
    if ( !IsReady( pHistory ) ) {
        return;
    }
    DestroyRange( pHistory, 0u );
    common::Vector_Shutdown( &pHistory->records );
    pHistory->pDocument = nullptr;
    pHistory->cUndoable = 0u;
    pHistory->cDepthMax = 0u;
    pHistory->expectedRevision = 0u;
    pHistory->bInitialized = false;
}

usize GeometryHistory_UndoCount( const geometry_history_t *pHistory ) noexcept
{
    return IsReady( pHistory ) ? pHistory->cUndoable : 0u;
}

usize GeometryHistory_RedoCount( const geometry_history_t *pHistory ) noexcept
{
    return IsReady( pHistory )
        ? common::Vector_Count( &pHistory->records ) - pHistory->cUndoable
        : 0u;
}

geometry_status_t GeometryHistory_TryPush(
    geometry_history_t *pHistory,
    geometry_change_record_t *pRecord ) noexcept
{
    if ( !IsReady( pHistory ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pRecord == nullptr || pRecord->pDomain != pHistory->pDocument ||
         GeometryChangeRecord_Count( pRecord ) == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pRecord->revisionBefore != pHistory->expectedRevision ||
         pRecord->revisionAfter != GeometryDocument_Revision( pHistory->pDocument ) ) {
        return geometry_status_t::STALE_REVISION;
    }

    // Secure the slot before discarding anything.
    const usize cKept = pHistory->cUndoable < pHistory->cDepthMax
        ? pHistory->cUndoable
        : pHistory->cDepthMax - 1u;
    if ( !common::Vector_Reserve( &pHistory->records, cKept + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    DestroyRange( pHistory, pHistory->cUndoable );
    while ( common::Vector_Count( &pHistory->records ) >= pHistory->cDepthMax ) {
        GeometryChangeRecord_Destroy( pHistory->records.pData[0] );
        common::Vector_Erase( &pHistory->records, 0u );
    }
    // Cannot fail: capacity reserved above.
    ( void )common::Vector_PushBack( &pHistory->records, pRecord );
    pHistory->cUndoable = common::Vector_Count( &pHistory->records );
    pHistory->expectedRevision = pRecord->revisionAfter;
    return geometry_status_t::OK;
}

geometry_status_t GeometryHistory_TryUndo(
    geometry_history_t *pHistory,
    const geometry_change_record_t **ppRecordOut ) noexcept
{
    if ( ppRecordOut != nullptr ) {
        *ppRecordOut = nullptr;
    }
    if ( !IsReady( pHistory ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pHistory->cUndoable == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( GeometryDocument_Revision( pHistory->pDocument ) != pHistory->expectedRevision ) {
        return geometry_status_t::STALE_REVISION;
    }

    const geometry_change_record_t *pRecord = pHistory->records.pData[pHistory->cUndoable - 1u];
    common::u64 newRevision = 0u;
    const geometry_status_t status = GeometryChangeRecord_TryApply(
        pRecord, pHistory->pDocument, geometry_change_direction_t::INVERSE,
        pHistory->expectedRevision, &newRevision );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    --pHistory->cUndoable;
    pHistory->expectedRevision = newRevision;
    if ( ppRecordOut != nullptr ) {
        *ppRecordOut = pRecord;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryHistory_TryRedo(
    geometry_history_t *pHistory,
    const geometry_change_record_t **ppRecordOut ) noexcept
{
    if ( ppRecordOut != nullptr ) {
        *ppRecordOut = nullptr;
    }
    if ( !IsReady( pHistory ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pHistory->cUndoable >= common::Vector_Count( &pHistory->records ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( GeometryDocument_Revision( pHistory->pDocument ) != pHistory->expectedRevision ) {
        return geometry_status_t::STALE_REVISION;
    }

    const geometry_change_record_t *pRecord = pHistory->records.pData[pHistory->cUndoable];
    common::u64 newRevision = 0u;
    const geometry_status_t status = GeometryChangeRecord_TryApply(
        pRecord, pHistory->pDocument, geometry_change_direction_t::FORWARD,
        pHistory->expectedRevision, &newRevision );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    ++pHistory->cUndoable;
    pHistory->expectedRevision = newRevision;
    if ( ppRecordOut != nullptr ) {
        *ppRecordOut = pRecord;
    }
    return geometry_status_t::OK;
}

void GeometryHistory_Clear( geometry_history_t *pHistory ) noexcept
{
    if ( !IsReady( pHistory ) ) {
        return;
    }
    DestroyRange( pHistory, 0u );
    pHistory->cUndoable = 0u;
    pHistory->expectedRevision = GeometryDocument_Revision( pHistory->pDocument );
}

} // namespace cypher::editor::geometry
