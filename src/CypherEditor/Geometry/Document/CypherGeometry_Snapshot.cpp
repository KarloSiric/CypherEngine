//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Snapshot.cpp
//  Purpose: Implements publication, lookup, and release of immutable
//           document snapshots.
//  Details: Publication copies one pointer per committed brush and takes
//           one reference on each value, then sorts by brush source ID so
//           snapshot order is independent of pool slot reuse. The document
//           keeps one reference to the current snapshot so repeated
//           acquisition at the same revision is O(1).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Snapshot.h"

#include "CypherCommon_Sort.h"

#include <new>

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::usize;

struct snapshot_brush_less_t {
    CYPHER_NODISCARD bool_t operator()(
        const geometry_snapshot_brush_t &left,
        const geometry_snapshot_brush_t &right ) const noexcept
    {
        return left.brushId.value < right.brushId.value;
    }
};

void DestroySnapshot( geometry_document_snapshot_t *pSnapshot ) noexcept
{
    const common::allocator_t *pAllocator = pSnapshot->pAllocator;
    for ( usize i = 0u; i < pSnapshot->cBrushes; ++i ) {
        BrushValue_Release( pSnapshot->pBrushes[i].pValue );
    }
    if ( pSnapshot->pBrushes != nullptr ) {
        common::Allocator_FreeArrayStorage(
            pAllocator, pSnapshot->pBrushes, pSnapshot->cBrushes );
    }
    pSnapshot->~geometry_document_snapshot_t();
    common::Allocator_Free(
        pAllocator, pSnapshot,
        sizeof( geometry_document_snapshot_t ),
        alignof( geometry_document_snapshot_t ) );
}

} // namespace

geometry_status_t GeometryDocument_TryAcquireSnapshot(
    geometry_document_t *pDocument,
    const geometry_document_snapshot_t **ppSnapshotOut ) noexcept
{
    if ( ppSnapshotOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppSnapshotOut = nullptr;
    if ( pDocument == nullptr || !pDocument->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    if ( pDocument->pCachedSnapshot != nullptr ) {
        CY_ASSERT_MSG( pDocument->pCachedSnapshot->revision == pDocument->revision,
                       "Cached snapshot survived a revision change." );
        GeometrySnapshot_AddRef( pDocument->pCachedSnapshot );
        *ppSnapshotOut = pDocument->pCachedSnapshot;
        return geometry_status_t::OK;
    }

    const common::allocator_t *pAllocator = pDocument->pAllocator;
    const usize cBrushes = common::GenerationPool_Count( &pDocument->brushes );

    geometry_snapshot_brush_t *pBrushes = nullptr;
    if ( cBrushes > 0u ) {
        pBrushes = common::Allocator_AllocateArrayStorage<geometry_snapshot_brush_t>(
            pAllocator, cBrushes );
        if ( pBrushes == nullptr ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    void *pBlock = common::Allocator_Allocate(
        pAllocator,
        sizeof( geometry_document_snapshot_t ),
        alignof( geometry_document_snapshot_t ) );
    if ( pBlock == nullptr ) {
        if ( pBrushes != nullptr ) {
            common::Allocator_FreeArrayStorage( pAllocator, pBrushes, cBrushes );
        }
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // Nothing below can fail, so references are taken as entries are filled.
    struct fill_t {
        geometry_snapshot_brush_t *pBrushes;
        usize cFilled;
    } fill{ pBrushes, 0u };
    ( void )common::GenerationPool_ForEach(
        &pDocument->brushes,
        [&fill]( geometry_brush_handle_t handle,
                 const geometry_brush_value_t *const &pValue ) noexcept -> bool_t {
            BrushValue_AddRef( pValue );
            new ( &fill.pBrushes[fill.cFilled] ) geometry_snapshot_brush_t{
                pValue->brush.sourceId, handle, pValue };
            ++fill.cFilled;
            return true;
        } );
    CY_ASSERT_MSG( fill.cFilled == cBrushes, "Brush pool count disagrees with iteration." );

    common::Sort_Unstable(
        common::span_t<geometry_snapshot_brush_t>{ pBrushes, cBrushes },
        snapshot_brush_less_t{} );

    geometry_document_snapshot_t *pSnapshot =
        new ( pBlock ) geometry_document_snapshot_t{};
    pSnapshot->pAllocator = pAllocator;
    pSnapshot->pDomain = pDocument;
    pSnapshot->revision = pDocument->revision;
    pSnapshot->policy = pDocument->policy;
    pSnapshot->cBrushes = cBrushes;
    pSnapshot->pBrushes = pBrushes;

    // One reference for the caller, one for the document's cache.
    common::RefCount_Init( &pSnapshot->refs, 2u );
    pDocument->pCachedSnapshot = pSnapshot;
    *ppSnapshotOut = pSnapshot;
    return geometry_status_t::OK;
}

void GeometrySnapshot_AddRef( const geometry_document_snapshot_t *pSnapshot ) noexcept
{
    if ( pSnapshot != nullptr ) {
        ( void )common::RefCount_AddRef( &pSnapshot->refs );
    }
}

void GeometrySnapshot_Release( const geometry_document_snapshot_t *pSnapshot ) noexcept
{
    if ( pSnapshot == nullptr ) {
        return;
    }
    if ( common::RefCount_Release( &pSnapshot->refs ) == 0u ) {
        DestroySnapshot( const_cast<geometry_document_snapshot_t *>( pSnapshot ) );
    }
}

common::u64 GeometrySnapshot_Revision(
    const geometry_document_snapshot_t *pSnapshot ) noexcept
{
    return pSnapshot == nullptr ? 0u : pSnapshot->revision;
}

usize GeometrySnapshot_BrushCount(
    const geometry_document_snapshot_t *pSnapshot ) noexcept
{
    return pSnapshot == nullptr ? 0u : pSnapshot->cBrushes;
}

geometry_status_t GeometrySnapshot_TryGetBrush(
    const geometry_document_snapshot_t *pSnapshot,
    usize iIndex,
    geometry_snapshot_brush_t *pEntryOut ) noexcept
{
    if ( pEntryOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pEntryOut = {};
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iIndex >= pSnapshot->cBrushes ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pEntryOut = pSnapshot->pBrushes[iIndex];
    return geometry_status_t::OK;
}

geometry_status_t GeometrySnapshot_TryFindBrush(
    const geometry_document_snapshot_t *pSnapshot,
    geometry_source_id_t brushId,
    const geometry_brush_value_t **ppValueOut ) noexcept
{
    if ( ppValueOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppValueOut = nullptr;
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    usize iLow = 0u;
    usize iHigh = pSnapshot->cBrushes;
    while ( iLow < iHigh ) {
        const usize iMid = iLow + ( iHigh - iLow ) / 2u;
        const common::u64 midValue = pSnapshot->pBrushes[iMid].brushId.value;
        if ( midValue == brushId.value ) {
            *ppValueOut = pSnapshot->pBrushes[iMid].pValue;
            return geometry_status_t::OK;
        }
        if ( midValue < brushId.value ) {
            iLow = iMid + 1u;
        } else {
            iHigh = iMid;
        }
    }
    return geometry_status_t::INVALID_ARGUMENT;
}

} // namespace cypher::editor::geometry
