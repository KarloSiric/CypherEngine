//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SourceIdRegistry.cpp
//  Purpose: Implements document-local persistent geometry identity ownership.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SourceIdRegistry.h"

#include "CypherCommon_Sort.h"

namespace cypher::editor::geometry
{

namespace
{

struct geometry_source_id_less_t {
    bool operator()(
        const geometry_source_id_t &left,
        const geometry_source_id_t &right ) const noexcept
    {
        return left.value < right.value;
    }
};

bool IsCanonicalRegistry(
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
           registry.allocator.next.value == 1u &&
           registry.cEntriesMax == 0u &&
           registry.pAllocator == nullptr &&
           !registry.bLoadRegistrationOpen;
}

bool IsCanonicalRemap( const geometry_source_id_remap_t &remap ) noexcept
{
    return remap.entries.pData == nullptr &&
           remap.entries.nCount == 0u &&
           remap.entries.nCapacity == 0u &&
           remap.entries.pAllocator == nullptr;
}

void ResetRegistryScalars(
    geometry_source_id_registry_t &registry ) noexcept
{
    registry.allocator = {};
    registry.cEntriesMax = 0u;
    registry.pAllocator = nullptr;
    registry.bLoadRegistrationOpen = false;
}

bool HasIdRange(
    const geometry_source_id_allocator_t &allocator,
    common::usize cIds ) noexcept
{
    static_assert( sizeof( common::usize ) <= sizeof( common::u64 ) );
    if ( cIds == 0u ) {
        return true;
    }
    if ( !GeometrySourceId_IsValid( allocator.next ) ) {
        return false;
    }

    const common::u64 cAdditional =
        static_cast<common::u64>( cIds - 1u );
    return cAdditional <= common::CY_U64_MAX - allocator.next.value;
}

void RollBackRemapInsertions(
    geometry_source_id_registry_t &registry,
    geometry_source_id_remap_t &remap,
    common::usize cInserted,
    geometry_source_id_allocator_t allocatorBefore ) noexcept
{
    for ( common::usize iEntry = 0u; iEntry < cInserted; ++iEntry ) {
        const geometry_source_id_t id = remap.entries.pData[iEntry].destination;
        const bool bRemovedLive = common::HashSet_Erase( &registry.liveIds, id );
        const bool bRemovedClaim =
            common::HashSet_Erase( &registry.claimedIds, id );
        CY_ASSERT_MSG(
            bRemovedLive && bRemovedClaim,
            "Source-ID remap rollback must remove every provisional identity "
            "from both ownership sets." );
        (void)bRemovedLive;
        (void)bRemovedClaim;
    }

    // The IDs were provisional and never left this call. Restoring the snapshot
    // therefore preserves the externally visible never-recycle contract.
    registry.allocator = allocatorBefore;
}

bool RollBackFreshIdentity(
    geometry_source_id_registry_t &registry,
    geometry_source_id_t id ) noexcept
{
    const bool bRemovedLive = common::HashSet_Erase( &registry.liveIds, id );
    const bool bRemovedClaim = common::HashSet_Erase( &registry.claimedIds, id );
    CY_ASSERT_MSG(
        bRemovedLive && bRemovedClaim,
        "Fresh source-ID rollback must remove live and claimed membership." );
    return bRemovedLive && bRemovedClaim;
}

} // namespace

common::hash64_t geometry_source_id_hasher_t::operator()(
    const geometry_source_id_t &id ) const noexcept
{
    return common::hash_functor_t<common::u64>{}( id.value );
}

bool geometry_source_id_equal_t::operator()(
    const geometry_source_id_t &left,
    const geometry_source_id_t &right ) const noexcept
{
    return left.value == right.value;
}

geometry_status_t GeometrySourceIdRegistry_Init(
    geometry_source_id_registry_t *pRegistry,
    const common::allocator_t *pAllocator,
    common::usize cEntriesMax,
    common::usize cInitialCapacity,
    geometry_source_id_t first ) noexcept
{
    if ( pRegistry == nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         cEntriesMax == 0u ||
         cInitialCapacity > cEntriesMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !IsCanonicalRegistry( *pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    geometry_source_id_allocator_t sourceAllocator{};
    if ( GeometrySourceId_IsValid( first ) ) {
        const geometry_status_t resetStatus =
            GeometrySourceIdAllocator_Reset( &sourceAllocator, first );
        if ( resetStatus != geometry_status_t::OK ) {
            return resetStatus;
        }
    } else {
        sourceAllocator.next = GEOMETRY_SOURCE_ID_INVALID;
    }

    if ( !common::HashSet_Init(
             &pRegistry->claimedIds,
             pAllocator,
             cInitialCapacity,
             geometry_source_id_hasher_t{},
             geometry_source_id_equal_t{} ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::HashSet_Init(
             &pRegistry->liveIds,
             pAllocator,
             cInitialCapacity,
             geometry_source_id_hasher_t{},
             geometry_source_id_equal_t{} ) ) {
        common::HashSet_Shutdown( &pRegistry->claimedIds );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    pRegistry->allocator = sourceAllocator;
    pRegistry->cEntriesMax = cEntriesMax;
    pRegistry->pAllocator = pAllocator;
    pRegistry->bLoadRegistrationOpen = true;
    return geometry_status_t::OK;
}

void GeometrySourceIdRegistry_Shutdown(
    geometry_source_id_registry_t *pRegistry ) noexcept
{
    if ( pRegistry == nullptr ) {
        return;
    }

    const bool bValid = GeometrySourceIdRegistry_IsValid( pRegistry );
    CY_ASSERT_MSG(
        bValid,
        "Source-ID registry shutdown requires a structurally valid registry." );
    if ( !bValid ) {
        return;
    }

    common::HashSet_Shutdown( &pRegistry->liveIds );
    common::HashSet_Shutdown( &pRegistry->claimedIds );
    ResetRegistryScalars( *pRegistry );
}

geometry_status_t GeometrySourceIdRegistry_Clear(
    geometry_source_id_registry_t *pRegistry ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    pRegistry->bLoadRegistrationOpen = false;
    common::HashSet_Clear( &pRegistry->liveIds );
    return geometry_status_t::OK;
}

bool GeometrySourceIdRegistry_IsValid(
    const geometry_source_id_registry_t *pRegistry ) noexcept
{
    if ( pRegistry == nullptr ||
         !common::HashSet_IsValid( &pRegistry->claimedIds ) ||
         !common::HashSet_IsValid( &pRegistry->liveIds ) ) {
        return false;
    }

    if ( pRegistry->pAllocator == nullptr ) {
        return IsCanonicalRegistry( *pRegistry );
    }

    return common::Allocator_IsValid( pRegistry->pAllocator ) &&
           pRegistry->claimedIds.pAllocator == pRegistry->pAllocator &&
           pRegistry->liveIds.pAllocator == pRegistry->pAllocator &&
           pRegistry->cEntriesMax > 0u &&
           common::HashSet_Count( &pRegistry->claimedIds ) <=
               pRegistry->cEntriesMax &&
           common::HashSet_Count( &pRegistry->liveIds ) <=
               common::HashSet_Count( &pRegistry->claimedIds );
}

bool GeometrySourceIdRegistry_ValidateDeep(
    const geometry_source_id_registry_t *pRegistry ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return false;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return IsCanonicalRegistry( *pRegistry );
    }

    common::usize cClaimsSeen = 0u;
    common::usize iClaim = 0u;
    while ( const auto *pSlot = common::HashTable_NextOccupied(
                &pRegistry->claimedIds,
                &iClaim ) ) {
        const geometry_source_id_t *pId = common::HashTable_SlotKey( pSlot );
        if ( pId == nullptr || !GeometrySourceId_IsValid( *pId ) ||
             ( GeometrySourceId_IsValid( pRegistry->allocator.next ) &&
               pId->value >= pRegistry->allocator.next.value ) ) {
            return false;
        }
        ++cClaimsSeen;
    }
    if ( cClaimsSeen != common::HashSet_Count( &pRegistry->claimedIds ) ) {
        return false;
    }

    common::usize cLiveSeen = 0u;
    common::usize iLive = 0u;
    while ( const auto *pSlot = common::HashTable_NextOccupied(
                &pRegistry->liveIds,
                &iLive ) ) {
        const geometry_source_id_t *pId = common::HashTable_SlotKey( pSlot );
        if ( pId == nullptr || !GeometrySourceId_IsValid( *pId ) ||
             !common::HashSet_Contains( &pRegistry->claimedIds, *pId ) ) {
            return false;
        }
        ++cLiveSeen;
    }
    return cLiveSeen == common::HashSet_Count( &pRegistry->liveIds );
}

bool GeometrySourceIdRegistry_IsInitialized(
    const geometry_source_id_registry_t *pRegistry ) noexcept
{
    return GeometrySourceIdRegistry_IsValid( pRegistry ) &&
           pRegistry->pAllocator != nullptr;
}

geometry_status_t GeometrySourceIdRegistry_TryClone(
    const geometry_source_id_registry_t *pSource,
    geometry_source_id_registry_t *pCloneOut ) noexcept
{
    if ( pCloneOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !IsCanonicalRegistry( *pCloneOut ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pSource ) ||
         !GeometrySourceIdRegistry_ValidateDeep( pSource ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( pSource );
    geometry_status_t status = GeometrySourceIdRegistry_Init(
        pCloneOut,
        pSource->pAllocator,
        pSource->cEntriesMax,
        cClaimed,
        pSource->allocator.next );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    common::usize iSlot = 0u;
    while ( const auto *pSlot = common::HashTable_NextOccupied(
                &pSource->claimedIds, &iSlot ) ) {
        const geometry_source_id_t *pId =
            common::HashTable_SlotKey( pSlot );
        if ( pId == nullptr ||
             !common::HashSet_Insert( &pCloneOut->claimedIds, *pId ) ) {
            GeometrySourceIdRegistry_Shutdown( pCloneOut );
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    iSlot = 0u;
    while ( const auto *pSlot = common::HashTable_NextOccupied(
                &pSource->liveIds, &iSlot ) ) {
        const geometry_source_id_t *pId =
            common::HashTable_SlotKey( pSlot );
        if ( pId == nullptr ||
             !common::HashSet_Insert( &pCloneOut->liveIds, *pId ) ) {
            GeometrySourceIdRegistry_Shutdown( pCloneOut );
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    pCloneOut->allocator = pSource->allocator;
    pCloneOut->bLoadRegistrationOpen =
        pSource->bLoadRegistrationOpen;
    if ( !GeometrySourceIdRegistry_ValidateDeep( pCloneOut ) ) {
        GeometrySourceIdRegistry_Shutdown( pCloneOut );
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometrySourceIdRegistry_SealLoadedIds(
    geometry_source_id_registry_t *pRegistry ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    pRegistry->bLoadRegistrationOpen = false;
    return geometry_status_t::OK;
}

geometry_status_t GeometrySourceIdRegistry_Reserve(
    geometry_source_id_registry_t *pRegistry,
    common::usize cEntries ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( cEntries > pRegistry->cEntriesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    if ( !common::HashSet_Reserve( &pRegistry->claimedIds, cEntries ) ||
         !common::HashSet_Reserve( &pRegistry->liveIds, cEntries ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometrySourceIdRegistry_Register(
    geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pRegistry->bLoadRegistrationOpen ) {
        return geometry_status_t::UNSUPPORTED;
    }
    if ( common::HashSet_Contains( &pRegistry->claimedIds, id ) ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    const common::usize cClaimed =
        common::HashSet_Count( &pRegistry->claimedIds );
    if ( cClaimed >= pRegistry->cEntriesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const geometry_status_t reserveStatus =
        GeometrySourceIdRegistry_Reserve( pRegistry, cClaimed + 1u );
    if ( reserveStatus != geometry_status_t::OK ) {
        return reserveStatus;
    }

    if ( !common::HashSet_Insert( &pRegistry->claimedIds, id ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !common::HashSet_Insert( &pRegistry->liveIds, id ) ) {
        const bool bRemovedClaim =
            common::HashSet_Erase( &pRegistry->claimedIds, id );
        CY_ASSERT_MSG(
            bRemovedClaim,
            "Failed registration must roll back its fresh identity claim." );
        (void)bRemovedClaim;
        return geometry_status_t::CORRUPT_STATE;
    }

    const geometry_status_t advanceStatus =
        GeometrySourceIdAllocator_AdvancePast( &pRegistry->allocator, id );
    if ( advanceStatus != geometry_status_t::OK ) {
        (void)RollBackFreshIdentity( *pRegistry, id );
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

geometry_source_id_result_t GeometrySourceIdRegistry_Allocate(
    geometry_source_id_registry_t *pRegistry ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return { {}, geometry_status_t::CORRUPT_STATE };
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return { {}, geometry_status_t::NOT_INITIALIZED };
    }

    const common::usize cClaimed =
        common::HashSet_Count( &pRegistry->claimedIds );
    if ( cClaimed >= pRegistry->cEntriesMax ) {
        return { {}, geometry_status_t::LIMIT_EXCEEDED };
    }
    if ( GeometrySourceIdAllocator_IsExhausted( &pRegistry->allocator ) ) {
        return { {}, geometry_status_t::INSUFFICIENT_CAPACITY };
    }

    const geometry_status_t reserveStatus =
        GeometrySourceIdRegistry_Reserve( pRegistry, cClaimed + 1u );
    if ( reserveStatus != geometry_status_t::OK ) {
        return { {}, reserveStatus };
    }

    pRegistry->bLoadRegistrationOpen = false;
    const geometry_source_id_allocator_t allocatorBefore =
        pRegistry->allocator;
    const geometry_source_id_result_t allocated =
        GeometrySourceIdAllocator_Allocate( &pRegistry->allocator );
    if ( allocated.status != geometry_status_t::OK ) {
        return allocated;
    }
    if ( common::HashSet_Contains( &pRegistry->claimedIds, allocated.id ) ||
         common::HashSet_Contains( &pRegistry->liveIds, allocated.id ) ||
         !common::HashSet_Insert( &pRegistry->claimedIds, allocated.id ) ) {
        // The candidate was never returned to a caller, so rolling back this
        // impossible-under-valid-state failure does not recycle an issued ID.
        pRegistry->allocator = allocatorBefore;
        return { {}, geometry_status_t::CORRUPT_STATE };
    }
    if ( !common::HashSet_Insert( &pRegistry->liveIds, allocated.id ) ) {
        const bool bRemovedClaim =
            common::HashSet_Erase( &pRegistry->claimedIds, allocated.id );
        CY_ASSERT_MSG(
            bRemovedClaim,
            "Failed allocation must roll back its fresh identity claim." );
        (void)bRemovedClaim;
        pRegistry->allocator = allocatorBefore;
        return { {}, geometry_status_t::CORRUPT_STATE };
    }
    return allocated;
}

geometry_status_t GeometrySourceIdRegistry_Release(
    geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( !common::HashSet_Contains( &pRegistry->liveIds, id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    pRegistry->bLoadRegistrationOpen = false;

    return common::HashSet_Erase( &pRegistry->liveIds, id )
        ? geometry_status_t::OK
        : geometry_status_t::INVALID_ARGUMENT;
}

geometry_status_t GeometrySourceIdRegistry_RestoreRetired(
    geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !common::HashSet_Contains( &pRegistry->claimedIds, id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( common::HashSet_Contains( &pRegistry->liveIds, id ) ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    const common::usize cLive =
        common::HashSet_Count( &pRegistry->liveIds );
    if ( !common::HashSet_Reserve( &pRegistry->liveIds, cLive + 1u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pRegistry->bLoadRegistrationOpen = false;
    return common::HashSet_Insert( &pRegistry->liveIds, id )
        ? geometry_status_t::OK
        : geometry_status_t::CORRUPT_STATE;
}

bool GeometrySourceIdRegistry_Contains(
    const geometry_source_id_registry_t *pRegistry,
    geometry_source_id_t id ) noexcept
{
    return GeometrySourceIdRegistry_IsInitialized( pRegistry ) &&
           GeometrySourceId_IsValid( id ) &&
           common::HashSet_Contains( &pRegistry->liveIds, id );
}

common::usize GeometrySourceIdRegistry_Count(
    const geometry_source_id_registry_t *pRegistry ) noexcept
{
    return GeometrySourceIdRegistry_IsInitialized( pRegistry )
        ? common::HashSet_Count( &pRegistry->liveIds )
        : 0u;
}

common::usize GeometrySourceIdRegistry_ClaimedCount(
    const geometry_source_id_registry_t *pRegistry ) noexcept
{
    return GeometrySourceIdRegistry_IsInitialized( pRegistry )
        ? common::HashSet_Count( &pRegistry->claimedIds )
        : 0u;
}

geometry_status_t GeometrySourceIdRegistry_CreateDeterministicRemap(
    geometry_source_id_registry_t *pRegistry,
    common::span_t<const geometry_source_id_t> sourceIds,
    geometry_source_id_remap_t *pRemapOut ) noexcept
{
    if ( !GeometrySourceIdRegistry_IsValid( pRegistry ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    if ( !GeometrySourceIdRegistry_IsInitialized( pRegistry ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pRemapOut == nullptr || !common::Span_IsValid( sourceIds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !IsCanonicalRemap( *pRemapOut ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    // Invalid caller data takes precedence over target-domain capacity. This is
    // allocation-free and keeps zero-ID diagnostics stable even for a full or
    // exhausted destination registry.
    for ( common::usize iId = 0u; iId < sourceIds.nCount; ++iId ) {
        if ( !GeometrySourceId_IsValid( sourceIds.pData[iId] ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }

    // Bound temporary storage before copying or sorting caller input. Identity
    // validation below remains exact for every batch admitted by this document.
    const common::usize cClaimed =
        common::HashSet_Count( &pRegistry->claimedIds );
    if ( sourceIds.nCount > pRegistry->cEntriesMax - cClaimed ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !HasIdRange( pRegistry->allocator, sourceIds.nCount ) ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }

    common::vector_t<geometry_source_id_t> orderedIds{};
    if ( !common::Vector_Init(
             &orderedIds,
             pRegistry->pAllocator,
             sourceIds.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Append( &orderedIds, sourceIds ) ) {
        common::Vector_Shutdown( &orderedIds );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    common::Sort_Unstable(
        common::Vector_Span( &orderedIds ),
        geometry_source_id_less_t{} );
    for ( common::usize iId = 0u; iId < orderedIds.nCount; ++iId ) {
        if ( iId > 0u &&
             orderedIds.pData[iId - 1u].value ==
                 orderedIds.pData[iId].value ) {
            common::Vector_Shutdown( &orderedIds );
            return geometry_status_t::IDENTITY_CONFLICT;
        }
    }

    if ( !common::Vector_Init(
             &pRemapOut->entries,
             pRegistry->pAllocator,
             sourceIds.nCount ) ||
         !common::Vector_Resize(
             &pRemapOut->entries,
             sourceIds.nCount ) ) {
        common::Vector_Shutdown( &pRemapOut->entries );
        common::Vector_Shutdown( &orderedIds );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const geometry_status_t reserveStatus =
        GeometrySourceIdRegistry_Reserve(
            pRegistry,
            cClaimed + sourceIds.nCount );
    if ( reserveStatus != geometry_status_t::OK ) {
        common::Vector_Shutdown( &pRemapOut->entries );
        common::Vector_Shutdown( &orderedIds );
        return reserveStatus;
    }

    const geometry_source_id_allocator_t allocatorBefore =
        pRegistry->allocator;
    common::usize cInserted = 0u;
    for ( common::usize iId = 0u; iId < orderedIds.nCount; ++iId ) {
        const geometry_source_id_result_t allocated =
            GeometrySourceIdRegistry_Allocate( pRegistry );
        if ( allocated.status != geometry_status_t::OK ) {
            RollBackRemapInsertions(
                *pRegistry,
                *pRemapOut,
                cInserted,
                allocatorBefore );
            common::Vector_Shutdown( &pRemapOut->entries );
            common::Vector_Shutdown( &orderedIds );
            return geometry_status_t::CORRUPT_STATE;
        }

        pRemapOut->entries.pData[iId] = {
            orderedIds.pData[iId],
            allocated.id
        };
        ++cInserted;
    }

    common::Vector_Shutdown( &orderedIds );
    return geometry_status_t::OK;
}

void GeometrySourceIdRemap_Shutdown(
    geometry_source_id_remap_t *pRemap ) noexcept
{
    if ( pRemap == nullptr ) {
        return;
    }
    if ( common::Vector_IsValid( &pRemap->entries ) ) {
        common::Vector_Shutdown( &pRemap->entries );
    }
}

bool GeometrySourceIdRemap_IsValid(
    const geometry_source_id_remap_t *pRemap ) noexcept
{
    if ( pRemap == nullptr ||
         !common::Vector_IsValid( &pRemap->entries ) ) {
        return false;
    }

    for ( common::usize iEntry = 0u;
          iEntry < pRemap->entries.nCount;
          ++iEntry ) {
        const geometry_source_id_remap_entry_t &entry =
            pRemap->entries.pData[iEntry];
        if ( !GeometrySourceId_IsValid( entry.source ) ||
             !GeometrySourceId_IsValid( entry.destination ) ||
             ( iEntry > 0u &&
               ( pRemap->entries.pData[iEntry - 1u].source.value >=
                     entry.source.value ||
                 pRemap->entries.pData[iEntry - 1u].destination.value >=
                     entry.destination.value ) ) ) {
            return false;
        }
    }
    return true;
}

bool GeometrySourceIdRemap_IsInitialized(
    const geometry_source_id_remap_t *pRemap ) noexcept
{
    return GeometrySourceIdRemap_IsValid( pRemap ) &&
           pRemap->entries.pAllocator != nullptr;
}

common::usize GeometrySourceIdRemap_Count(
    const geometry_source_id_remap_t *pRemap ) noexcept
{
    return GeometrySourceIdRemap_IsInitialized( pRemap )
        ? common::Vector_Count( &pRemap->entries )
        : 0u;
}

bool GeometrySourceIdRemap_Find(
    const geometry_source_id_remap_t *pRemap,
    geometry_source_id_t source,
    geometry_source_id_t *pDestinationOut ) noexcept
{
    if ( !GeometrySourceIdRemap_IsInitialized( pRemap ) ||
         !GeometrySourceId_IsValid( source ) ||
         pDestinationOut == nullptr ) {
        return false;
    }

    common::usize iFirst = 0u;
    common::usize iEnd = pRemap->entries.nCount;
    while ( iFirst < iEnd ) {
        const common::usize iMiddle = iFirst + ( iEnd - iFirst ) / 2u;
        const geometry_source_id_remap_entry_t &entry =
            pRemap->entries.pData[iMiddle];
        if ( entry.source.value < source.value ) {
            iFirst = iMiddle + 1u;
        } else {
            iEnd = iMiddle;
        }
    }

    if ( iFirst >= pRemap->entries.nCount ||
         pRemap->entries.pData[iFirst].source.value != source.value ) {
        return false;
    }

    *pDestinationOut = pRemap->entries.pData[iFirst].destination;
    return true;
}

} // namespace cypher::editor::geometry
