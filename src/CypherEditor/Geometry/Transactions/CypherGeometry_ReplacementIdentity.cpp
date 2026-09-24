//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_ReplacementIdentity.cpp
//  Purpose: Implements atomic source-identity planning for brush replacement.
//  Details: Validation, registry cloning, and membership changes happen before
//           either the canonical brush or its registry is published.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_ReplacementIdentity.h"

namespace cypher::editor::geometry
{

namespace
{

bool BrushContainsSideId(
    const brush_solid_t &brush,
    geometry_source_id_t id ) noexcept
{
    const common::usize cSides = BrushSolid_SideCount( &brush );
    for ( common::usize iSide = 0u; iSide < cSides; ++iSide ) {
        if ( brush.sides.pData[iSide].sourceId.value == id.value ) {
            return true;
        }
    }
    return false;
}

geometry_status_t ValidateReplacementIdentity(
    geometry_source_id_t targetBrushId,
    const geometry_document_t &document,
    const brush_solid_t &currentBrush,
    const brush_solid_t &replacementBrush,
    common::usize *pFreshIdCountOut,
    bool *pRegistryChangesOut ) noexcept
{
    *pFreshIdCountOut = 0u;
    *pRegistryChangesOut = false;

    if ( !GeometrySourceId_IsValid( targetBrushId ) ||
         replacementBrush.sourceId.value != targetBrushId.value ||
         !common::Vector_IsValid( &replacementBrush.sides ) ||
         replacementBrush.sides.pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( currentBrush.sourceId.value != targetBrushId.value ||
         !common::Vector_IsValid( &currentBrush.sides ) ||
         currentBrush.sides.pAllocator == nullptr ||
         !GeometrySourceIdRegistry_ValidateDeep( &document.sourceIds ) ||
         !GeometrySourceIdRegistry_Contains(
             &document.sourceIds, targetBrushId ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const common::usize cCurrentSides =
        BrushSolid_SideCount( &currentBrush );
    for ( common::usize iSide = 0u;
          iSide < cCurrentSides;
          ++iSide ) {
        const geometry_source_id_t id =
            currentBrush.sides.pData[iSide].sourceId;
        if ( !GeometrySourceId_IsValid( id ) ||
             id.value == targetBrushId.value ||
             !GeometrySourceIdRegistry_Contains(
                 &document.sourceIds, id ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        for ( common::usize iPrior = 0u;
              iPrior < iSide;
              ++iPrior ) {
            if ( currentBrush.sides.pData[iPrior].sourceId.value ==
                 id.value ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
    }

    const common::usize cReplacementSides =
        BrushSolid_SideCount( &replacementBrush );
    for ( common::usize iSide = 0u;
          iSide < cReplacementSides;
          ++iSide ) {
        const geometry_source_id_t id =
            replacementBrush.sides.pData[iSide].sourceId;
        if ( !GeometrySourceId_IsValid( id ) ||
             id.value == targetBrushId.value ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        for ( common::usize iPrior = 0u;
              iPrior < iSide;
              ++iPrior ) {
            if ( replacementBrush.sides.pData[iPrior].sourceId.value ==
                 id.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }

        const bool bBelongsToCurrentBrush =
            BrushContainsSideId( currentBrush, id );
        if ( GeometrySourceIdRegistry_Contains(
                 &document.sourceIds, id ) &&
             !bBelongsToCurrentBrush ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        if ( !bBelongsToCurrentBrush ) {
            *pRegistryChangesOut = true;
            if ( !common::HashSet_Contains(
                     &document.sourceIds.claimedIds, id ) ) {
                if ( !GeometrySourceId_IsValid(
                         document.sourceIds.allocator.next ) ) {
                    return geometry_status_t::INSUFFICIENT_CAPACITY;
                }
                if ( id.value <
                     document.sourceIds.allocator.next.value ) {
                    return geometry_status_t::IDENTITY_CONFLICT;
                }
                ++( *pFreshIdCountOut );
            }
        }
    }

    for ( common::usize iSide = 0u;
          iSide < cCurrentSides;
          ++iSide ) {
        if ( !BrushContainsSideId(
                 replacementBrush,
                 currentBrush.sides.pData[iSide].sourceId ) ) {
            *pRegistryChangesOut = true;
        }
    }

    return geometry_status_t::OK;
}

geometry_status_t CloneRegistryForReplacement(
    const geometry_source_id_registry_t &source,
    common::usize cFreshIds,
    geometry_source_id_registry_t *pCloneOut ) noexcept
{
    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( &source );
    if ( cFreshIds > source.cEntriesMax - cClaimed ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const geometry_status_t cloneStatus =
        GeometrySourceIdRegistry_TryClone( &source, pCloneOut );
    if ( cloneStatus != geometry_status_t::OK ) {
        return cloneStatus;
    }
    const geometry_status_t reserveStatus =
        GeometrySourceIdRegistry_Reserve(
            pCloneOut, cClaimed + cFreshIds );
    if ( reserveStatus != geometry_status_t::OK ) {
        GeometrySourceIdRegistry_Shutdown( pCloneOut );
        return reserveStatus;
    }
    return geometry_status_t::OK;
}

geometry_status_t PrepareReplacementRegistry(
    const brush_solid_t &currentBrush,
    const brush_solid_t &replacementBrush,
    common::usize cFreshIds,
    const geometry_source_id_registry_t &source,
    geometry_source_id_registry_t *pPreparedOut ) noexcept
{
    geometry_status_t status = CloneRegistryForReplacement(
        source, cFreshIds, pPreparedOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    const bool bLoadRegistrationOpenBefore =
        pPreparedOut->bLoadRegistrationOpen;
    const common::usize cReplacementSides =
        BrushSolid_SideCount( &replacementBrush );
    for ( common::usize iSide = 0u;
          iSide < cReplacementSides;
          ++iSide ) {
        const geometry_source_id_t id =
            replacementBrush.sides.pData[iSide].sourceId;
        if ( BrushContainsSideId( currentBrush, id ) ) {
            continue;
        }

        if ( common::HashSet_Contains(
                 &pPreparedOut->claimedIds, id ) ) {
            status = GeometrySourceIdRegistry_RestoreRetired(
                pPreparedOut, id );
        }
        else {
            pPreparedOut->bLoadRegistrationOpen = true;
            status = GeometrySourceIdRegistry_Register(
                pPreparedOut, id );
        }
        if ( status != geometry_status_t::OK ) {
            GeometrySourceIdRegistry_Shutdown( pPreparedOut );
            return status;
        }
    }

    const common::usize cCurrentSides =
        BrushSolid_SideCount( &currentBrush );
    for ( common::usize iSide = 0u;
          iSide < cCurrentSides;
          ++iSide ) {
        const geometry_source_id_t id =
            currentBrush.sides.pData[iSide].sourceId;
        if ( BrushContainsSideId( replacementBrush, id ) ) {
            continue;
        }
        status = GeometrySourceIdRegistry_Release( pPreparedOut, id );
        if ( status != geometry_status_t::OK ) {
            GeometrySourceIdRegistry_Shutdown( pPreparedOut );
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    pPreparedOut->bLoadRegistrationOpen = bLoadRegistrationOpenBefore;
    return GeometrySourceIdRegistry_ValidateDeep( pPreparedOut )
        ? geometry_status_t::OK
        : geometry_status_t::CORRUPT_STATE;
}

void SwapSourceIdSetStorage(
    geometry_source_id_set_t &left,
    geometry_source_id_set_t &right ) noexcept
{
    auto *pSlots = left.pSlots;
    left.pSlots = right.pSlots;
    right.pSlots = pSlots;

    common::usize value = left.nCount;
    left.nCount = right.nCount;
    right.nCount = value;

    value = left.nCapacity;
    left.nCapacity = right.nCapacity;
    right.nCapacity = value;

    const common::allocator_t *pAllocator = left.pAllocator;
    left.pAllocator = right.pAllocator;
    right.pAllocator = pAllocator;
}

void SwapRegistries(
    geometry_source_id_registry_t &left,
    geometry_source_id_registry_t &right ) noexcept
{
    SwapSourceIdSetStorage( left.claimedIds, right.claimedIds );
    SwapSourceIdSetStorage( left.liveIds, right.liveIds );

    const geometry_source_id_allocator_t allocator = left.allocator;
    left.allocator = right.allocator;
    right.allocator = allocator;

    common::usize cEntriesMax = left.cEntriesMax;
    left.cEntriesMax = right.cEntriesMax;
    right.cEntriesMax = cEntriesMax;

    const common::allocator_t *pAllocator = left.pAllocator;
    left.pAllocator = right.pAllocator;
    right.pAllocator = pAllocator;

    const bool bLoadRegistrationOpen = left.bLoadRegistrationOpen;
    left.bLoadRegistrationOpen = right.bLoadRegistrationOpen;
    right.bLoadRegistrationOpen = bLoadRegistrationOpen;
}

bool RegistryIsCanonicalEmpty(
    const geometry_source_id_registry_t &registry ) noexcept
{
    return registry.claimedIds.pSlots == nullptr &&
           registry.claimedIds.nCount == 0u &&
           registry.claimedIds.nCapacity == 0u &&
           registry.claimedIds.pAllocator == nullptr &&
           registry.liveIds.pSlots == nullptr &&
           registry.liveIds.nCount == 0u &&
           registry.liveIds.nCapacity == 0u &&
           registry.liveIds.pAllocator == nullptr &&
           registry.pAllocator == nullptr;
}

} // namespace

geometry_status_t GeometryReplacementIdentity_TryPrepare(
    const geometry_document_t *pDocument,
    geometry_source_id_t targetBrushId,
    const brush_solid_t *pCurrentBrush,
    const brush_solid_t *pReplacementBrush,
    geometry_source_id_registry_t *pPreparedOut,
    bool *pHasRegistryChangesOut ) noexcept
{
    if ( pPreparedOut == nullptr ||
         pHasRegistryChangesOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !RegistryIsCanonicalEmpty( *pPreparedOut ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    *pHasRegistryChangesOut = false;

    if ( !GeometryDocument_IsInitialized( pDocument ) ||
         pCurrentBrush == nullptr ||
         pReplacementBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::usize cFreshIds = 0u;
    bool bRegistryChanges = false;
    const geometry_status_t validateStatus = ValidateReplacementIdentity(
        targetBrushId,
        *pDocument,
        *pCurrentBrush,
        *pReplacementBrush,
        &cFreshIds,
        &bRegistryChanges );
    if ( validateStatus != geometry_status_t::OK ) {
        return validateStatus;
    }
    if ( !bRegistryChanges ) {
        return geometry_status_t::OK;
    }

    const geometry_status_t prepareStatus = PrepareReplacementRegistry(
        *pCurrentBrush,
        *pReplacementBrush,
        cFreshIds,
        pDocument->sourceIds,
        pPreparedOut );
    if ( prepareStatus != geometry_status_t::OK ) {
        return prepareStatus;
    }

    *pHasRegistryChangesOut = true;
    return geometry_status_t::OK;
}

void GeometryReplacementIdentity_Publish(
    geometry_document_t *pDocument,
    geometry_source_id_registry_t *pPrepared ) noexcept
{
    if ( pDocument == nullptr || pPrepared == nullptr ||
         !GeometrySourceIdRegistry_IsInitialized(
             &pDocument->sourceIds ) ||
         !GeometrySourceIdRegistry_IsInitialized( pPrepared ) ) {
        return;
    }
    SwapRegistries( pDocument->sourceIds, *pPrepared );
}

} // namespace cypher::editor::geometry
