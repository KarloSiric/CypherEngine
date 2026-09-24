//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_ChangeRecord.cpp
//  Purpose: Implements invertible change records.
//  Details: Applying a record translates each entry into a document change
//           for the requested direction and hands the whole set to
//           GeometryDocument_TryApply, which owns atomicity. The translated
//           change array is scratch and is released before returning.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_ChangeRecord.h"

#include <new>

namespace cypher::editor::geometry
{

geometry_change_kind_t GeometryChangeEntry_Kind(
    const geometry_change_entry_t &entry ) noexcept
{
    if ( entry.pBefore == entry.pAfter ) {
        return geometry_change_kind_t::NONE;
    }
    if ( entry.pBefore == nullptr ) {
        return geometry_change_kind_t::INSERTED;
    }
    if ( entry.pAfter == nullptr ) {
        return geometry_change_kind_t::REMOVED;
    }
    return geometry_change_kind_t::MODIFIED;
}

geometry_status_t GeometryChangeRecord_TryCreate(
    const common::allocator_t *pAllocator,
    const geometry_document_t *pDomain,
    common::usize cEntries,
    geometry_change_record_t **ppRecordOut ) noexcept
{
    if ( ppRecordOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppRecordOut = nullptr;
    if ( !common::Allocator_IsValid( pAllocator ) || pDomain == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    void *pBlock = common::Allocator_Allocate(
        pAllocator,
        sizeof( geometry_change_record_t ), alignof( geometry_change_record_t ) );
    if ( pBlock == nullptr ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_change_record_t *pRecord = new ( pBlock ) geometry_change_record_t{};
    pRecord->pAllocator = pAllocator;
    pRecord->pDomain = pDomain;
    if ( !common::Vector_Init( &pRecord->entries, pAllocator, cEntries ) ) {
        pRecord->~geometry_change_record_t();
        common::Allocator_Free(
            pAllocator, pBlock,
            sizeof( geometry_change_record_t ), alignof( geometry_change_record_t ) );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    *ppRecordOut = pRecord;
    return geometry_status_t::OK;
}

void GeometryChangeRecord_Destroy( geometry_change_record_t *pRecord ) noexcept
{
    if ( pRecord == nullptr ) {
        return;
    }
    const common::allocator_t *pAllocator = pRecord->pAllocator;
    const common::usize cEntries = common::Vector_Count( &pRecord->entries );
    for ( common::usize i = 0u; i < cEntries; ++i ) {
        BrushValue_Release( pRecord->entries.pData[i].pBefore );
        BrushValue_Release( pRecord->entries.pData[i].pAfter );
    }
    pRecord->~geometry_change_record_t();
    common::Allocator_Free(
        pAllocator, pRecord,
        sizeof( geometry_change_record_t ), alignof( geometry_change_record_t ) );
}

common::usize GeometryChangeRecord_Count(
    const geometry_change_record_t *pRecord ) noexcept
{
    return pRecord == nullptr ? 0u : common::Vector_Count( &pRecord->entries );
}

geometry_status_t GeometryChangeRecord_TryGetEntry(
    const geometry_change_record_t *pRecord,
    common::usize iIndex,
    geometry_change_entry_t *pEntryOut ) noexcept
{
    if ( pEntryOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pEntryOut = {};
    if ( pRecord == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iIndex >= common::Vector_Count( &pRecord->entries ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pEntryOut = pRecord->entries.pData[iIndex];
    return geometry_status_t::OK;
}

geometry_status_t GeometryChangeRecord_TryApply(
    const geometry_change_record_t *pRecord,
    geometry_document_t *pDocument,
    geometry_change_direction_t direction,
    common::u64 expectedRevision,
    common::u64 *pNewRevisionOut ) noexcept
{
    if ( pRecord == nullptr || pDocument == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pRecord->pDomain != pDocument ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( direction != geometry_change_direction_t::FORWARD &&
         direction != geometry_change_direction_t::INVERSE ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize cEntries = common::Vector_Count( &pRecord->entries );
    common::vector_t<geometry_document_change_t> changes{};
    if ( !common::Vector_Init( &changes, pRecord->pAllocator, cEntries ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const bool bForward = direction == geometry_change_direction_t::FORWARD;
    for ( common::usize i = 0u; i < cEntries; ++i ) {
        const geometry_change_entry_t &entry = pRecord->entries.pData[i];
        const geometry_brush_value_t *pFrom = bForward ? entry.pBefore : entry.pAfter;
        const geometry_brush_value_t *pTo = bForward ? entry.pAfter : entry.pBefore;

        geometry_document_change_t change{};
        change.brushId = entry.brushId;
        change.pValue = pTo;
        if ( pFrom == nullptr && pTo != nullptr ) {
            change.kind = geometry_document_change_kind_t::INSERT_BRUSH;
        } else if ( pFrom != nullptr && pTo == nullptr ) {
            change.kind = geometry_document_change_kind_t::REMOVE_BRUSH;
        } else if ( pFrom != nullptr && pTo != nullptr && pFrom != pTo ) {
            change.kind = geometry_document_change_kind_t::REPLACE_BRUSH;
        } else {
            // A committed record never holds no-op entries; one here means
            // the record was corrupted after commit.
            return geometry_status_t::CORRUPT_STATE;
        }
        // Cannot fail: capacity reserved by Vector_Init above.
        ( void )common::Vector_PushBack( &changes, change );
    }

    return GeometryDocument_TryApply(
        pDocument, expectedRevision,
        common::span_t<const geometry_document_change_t>{ changes.pData, cEntries },
        pNewRevisionOut );
}

} // namespace cypher::editor::geometry
