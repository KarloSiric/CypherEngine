//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentIdentity.h
//  Purpose: Internal helper that moves a document's identity registry from
//           one object's IDs to another's in one failure-atomic step, for
//           the document stores that add, remove, and replace whole objects
//           (patches, heightfields).
//  Details: Given the old object's IDs and the new object's IDs (each
//           sorted ascending, no duplicates), IDs only in the old set are
//           released (retired), IDs only in the new set are activated
//           (registered fresh, or restored when retired earlier), and shared
//           IDs are left alone. A new ID that is live elsewhere in the
//           document is IDENTITY_CONFLICT.
//
//           Order of work, the same as the mesh store's: plan (pure),
//           reserve registry capacity, run the caller's `prepare` (every
//           allocation the caller still needs), apply the registry changes
//           with an exact rollback, then the caller's allocation-free
//           `publish`. If anything after `prepare` fails, `discard` frees
//           what prepare made.
//
//           Not part of the public geometry API.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_IDENTITY_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_IDENTITY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry::document_detail
{

struct identity_activation_t {
    geometry_source_id_t id{};
    bool bWasClaimed{ false };
};

// Undoes cActivated activations (in reverse) and cReleased releases.
inline bool RollBackIdentity(
    geometry_source_id_registry_t *pRegistry,
    const common::vector_t<identity_activation_t> &activations,
    common::usize cActivated,
    const common::vector_t<geometry_source_id_t> &released,
    common::usize cReleased,
    geometry_source_id_allocator_t allocatorBefore,
    bool bLoadOpenBefore ) noexcept
{
    bool bOk = true;
    while ( cActivated > 0u ) {
        const identity_activation_t &a = activations.pData[--cActivated];
        if ( a.bWasClaimed ) {
            bOk = GeometrySourceIdRegistry_Release( pRegistry, a.id ) == geometry_status_t::OK && bOk;
        } else {
            const bool bLive = common::HashSet_Erase( &pRegistry->liveIds, a.id );
            const bool bClaim = common::HashSet_Erase( &pRegistry->claimedIds, a.id );
            bOk = bLive && bClaim && bOk;
        }
    }
    while ( cReleased > 0u ) {
        bOk = GeometrySourceIdRegistry_RestoreRetired( pRegistry, released.pData[--cReleased] ) == geometry_status_t::OK && bOk;
    }
    pRegistry->allocator = allocatorBefore;
    pRegistry->bLoadRegistrationOpen = bLoadOpenBefore;
    return bOk;
}

// See the file comment. prepare: geometry_status_t(); publish: void();
// discard: void().
template <typename prepare_t, typename publish_t, typename discard_t>
geometry_status_t TryCommitIdentity(
    geometry_source_id_registry_t *pRegistry,
    common::span_t<const geometry_source_id_t> oldIds,
    common::span_t<const geometry_source_id_t> newIds,
    const common::allocator_t *pAllocator,
    prepare_t &&prepare,
    publish_t &&publish,
    discard_t &&discard ) noexcept
{
    common::vector_t<geometry_source_id_t> toRelease{};
    common::vector_t<identity_activation_t> toActivate{};
    if ( !common::Vector_Init( &toRelease, pAllocator ) || !common::Vector_Init( &toActivate, pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // Sorted-set difference.
    common::usize i = 0u, j = 0u, cFresh = 0u;
    while ( i < oldIds.nCount || j < newIds.nCount ) {
        const bool bTakeOld = j >= newIds.nCount || ( i < oldIds.nCount && oldIds.pData[i].value < newIds.pData[j].value );
        const bool bTakeNew = i >= oldIds.nCount || ( j < newIds.nCount && newIds.pData[j].value < oldIds.pData[i].value );
        bool bPushed = true;
        if ( bTakeOld ) {
            bPushed = common::Vector_PushBack( &toRelease, oldIds.pData[i++] );
        } else if ( bTakeNew ) {
            const geometry_source_id_t id = newIds.pData[j++];
            if ( common::HashSet_Contains( &pRegistry->liveIds, id ) ) { return geometry_status_t::IDENTITY_CONFLICT; }
            const bool bClaimed = common::HashSet_Contains( &pRegistry->claimedIds, id );
            cFresh += bClaimed ? 0u : 1u;
            bPushed = common::Vector_PushBack( &toActivate, identity_activation_t{ id, bClaimed } );
        } else {
            ++i;
            ++j;
        }
        if ( !bPushed ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    const common::usize cClaimed = GeometrySourceIdRegistry_ClaimedCount( pRegistry );
    if ( cFresh > pRegistry->cEntriesMax - cClaimed ) { return geometry_status_t::LIMIT_EXCEEDED; }
    geometry_status_t st = GeometrySourceIdRegistry_Reserve( pRegistry, cClaimed + cFresh );
    if ( st != geometry_status_t::OK ) { return st; }
    st = prepare();
    if ( st != geometry_status_t::OK ) { return st; }

    const geometry_source_id_allocator_t allocatorBefore = pRegistry->allocator;
    const bool bLoadOpenBefore = pRegistry->bLoadRegistrationOpen;
    common::usize cReleased = 0u, cActivated = 0u;
    for ( ; cReleased < toRelease.nCount; ++cReleased ) {
        st = GeometrySourceIdRegistry_Release( pRegistry, toRelease.pData[cReleased] );
        if ( st != geometry_status_t::OK ) { break; }
    }
    for ( ; st == geometry_status_t::OK && cActivated < toActivate.nCount; ++cActivated ) {
        const identity_activation_t &a = toActivate.pData[cActivated];
        if ( a.bWasClaimed ) {
            st = GeometrySourceIdRegistry_RestoreRetired( pRegistry, a.id );
        } else {
            pRegistry->bLoadRegistrationOpen = true;
            st = GeometrySourceIdRegistry_Register( pRegistry, a.id );
            pRegistry->bLoadRegistrationOpen = bLoadOpenBefore;
        }
        if ( st != geometry_status_t::OK ) { break; }
    }
    if ( st != geometry_status_t::OK ) {
        const bool bRolledBack =
            RollBackIdentity( pRegistry, toActivate, cActivated, toRelease, cReleased, allocatorBefore, bLoadOpenBefore );
        discard();
        return bRolledBack ? st : geometry_status_t::CORRUPT_STATE;
    }
    pRegistry->bLoadRegistrationOpen = bLoadOpenBefore;
    publish();
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry::document_detail

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_IDENTITY_H
