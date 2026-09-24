//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Document.cpp
//  Purpose: Implements the geometry-local aggregate authoring store.
//  Details: Brush pool operations validate identity domain membership,
//           deep-copy brush data into document-owned storage, and maintain
//           the source-ID registry's live/claimed invariant. Revision
//           advancement is deliberately not performed here — that
//           responsibility belongs to the transaction commit path.
//
//           Each brush is individually heap-allocated because brush_solid_t
//           contains a vector_t (non-copyable/non-movable). The document
//           stores a vector of pointers and manages allocation/deallocation
//           through its allocator.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Document.h"
#include "CypherGeometry_DocumentMeshes.h"

#include <new>

namespace cypher::editor::geometry
{

namespace
{

// Allocates a brush on the heap via the document's allocator and
// placement-constructs it. Returns nullptr on allocation failure.
brush_solid_t *AllocateBrush(
    const common::allocator_t *pAllocator ) noexcept
{
    void *pMemory = common::Allocator_AllocateZeroed(
        pAllocator, sizeof( brush_solid_t ), alignof( brush_solid_t ) );
    if ( pMemory == nullptr ) {
        return nullptr;
    }
    return new ( pMemory ) brush_solid_t{};
}

// Shuts down the brush's side storage, calls the destructor, and frees
// the allocation. Safe on nullptr.
void FreeBrush(
    const common::allocator_t *pAllocator,
    brush_solid_t *pBrush ) noexcept
{
    if ( pBrush == nullptr ) {
        return;
    }
    BrushSolid_Shutdown( pBrush );
    pBrush->~brush_solid_t();
    common::Allocator_Free(
        pAllocator, pBrush,
        sizeof( brush_solid_t ), alignof( brush_solid_t ) );
}

// Linear scan by source ID. Brush counts in editors are hundreds, not
// millions, so a flat scan keeps the data contiguous and avoids a second
// index structure that would need its own consistency invariant.
common::usize FindBrushIndex(
    const common::vector_t<brush_solid_t *> &brushes,
    geometry_source_id_t brushId ) noexcept
{
    for ( common::usize i = 0u; i < brushes.nCount; ++i ) {
        if ( brushes.pData[i] != nullptr &&
             brushes.pData[i]->sourceId.value == brushId.value ) {
            return i;
        }
    }
    return brushes.nCount;
}

struct document_source_id_activation_t {
    geometry_source_id_t id{};
    bool bWasClaimed{ false };
};

geometry_status_t GetBrushSourceId(
    const brush_solid_t *pBrush,
    common::usize iSource,
    geometry_source_id_t *pIdOut ) noexcept
{
    if ( pBrush == nullptr || pIdOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( iSource == 0u ) {
        *pIdOut = pBrush->sourceId;
        return GeometrySourceId_IsValid( *pIdOut )
            ? geometry_status_t::OK
            : geometry_status_t::INVALID_ARGUMENT;
    }

    brush_solid_side_t side{};
    const geometry_status_t sideStatus = BrushSolid_TryGetSide(
        pBrush, iSource - 1u, &side );
    if ( sideStatus != geometry_status_t::OK ) {
        return sideStatus;
    }
    if ( !GeometrySourceId_IsValid( side.sourceId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    *pIdOut = side.sourceId;
    return geometry_status_t::OK;
}

geometry_status_t ValidateBrushSourceIdsForAdd(
    const geometry_document_t *pDocument,
    const brush_solid_t *pBrush,
    common::usize cSources ) noexcept
{
    for ( common::usize iSource = 0u; iSource < cSources; ++iSource ) {
        geometry_source_id_t id{};
        const geometry_status_t idStatus =
            GetBrushSourceId( pBrush, iSource, &id );
        if ( idStatus != geometry_status_t::OK ) {
            return idStatus;
        }

        if ( GeometrySourceIdRegistry_Contains(
                 &pDocument->sourceIds, id ) ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }

        // The incoming brush must not reuse one identity for multiple
        // canonical elements. Checking this before any allocation keeps
        // identity-conflict diagnostics deterministic under memory pressure.
        for ( common::usize iPrior = 0u;
              iPrior < iSource;
              ++iPrior ) {
            geometry_source_id_t priorId{};
            const geometry_status_t priorStatus =
                GetBrushSourceId( pBrush, iPrior, &priorId );
            if ( priorStatus != geometry_status_t::OK ) {
                return priorStatus;
            }
            if ( priorId.value == id.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }
    }

    return geometry_status_t::OK;
}

geometry_status_t ActivateDocumentSourceId(
    geometry_source_id_registry_t *pRegistry,
    const document_source_id_activation_t &activation ) noexcept
{
    if ( activation.bWasClaimed ) {
        return GeometrySourceIdRegistry_RestoreRetired(
            pRegistry, activation.id );
    }

    // TryAddBrush is the document boundary that admits a complete canonical
    // brush. Temporarily open explicit registration for a fresh identity,
    // while preserving the registry's load-phase state for the caller.
    const bool bLoadRegistrationOpen = pRegistry->bLoadRegistrationOpen;
    pRegistry->bLoadRegistrationOpen = true;
    const geometry_status_t status = GeometrySourceIdRegistry_Register(
        pRegistry, activation.id );
    pRegistry->bLoadRegistrationOpen = bLoadRegistrationOpen;
    return status;
}

bool RollBackDocumentSourceIds(
    geometry_source_id_registry_t *pRegistry,
    const common::vector_t<document_source_id_activation_t> &activations,
    common::usize cActivated,
    geometry_source_id_allocator_t allocatorBefore,
    bool bLoadRegistrationOpenBefore ) noexcept
{
    bool bRolledBack = true;
    while ( cActivated > 0u ) {
        --cActivated;
        const document_source_id_activation_t &activation =
            activations.pData[cActivated];

        if ( activation.bWasClaimed ) {
            bRolledBack =
                GeometrySourceIdRegistry_Release(
                    pRegistry, activation.id ) == geometry_status_t::OK &&
                bRolledBack;
            continue;
        }

        const bool bRemovedLive = common::HashSet_Erase(
            &pRegistry->liveIds, activation.id );
        const bool bRemovedClaim = common::HashSet_Erase(
            &pRegistry->claimedIds, activation.id );
        bRolledBack = bRemovedLive && bRemovedClaim && bRolledBack;
    }

    // Fresh identities above were provisional and never became observable
    // through the document, so restore the exact pre-call sequence state.
    pRegistry->allocator = allocatorBefore;
    pRegistry->bLoadRegistrationOpen = bLoadRegistrationOpenBefore;
    return bRolledBack;
}

geometry_status_t ValidateBrushSourceIdsForRemove(
    const geometry_document_t *pDocument,
    const brush_solid_t *pBrush,
    common::usize cSources ) noexcept
{
    for ( common::usize iSource = 0u; iSource < cSources; ++iSource ) {
        geometry_source_id_t id{};
        if ( GetBrushSourceId( pBrush, iSource, &id ) !=
             geometry_status_t::OK ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        if ( !GeometrySourceIdRegistry_Contains(
                 &pDocument->sourceIds, id ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }

        for ( common::usize iPrior = 0u;
              iPrior < iSource;
              ++iPrior ) {
            geometry_source_id_t priorId{};
            if ( GetBrushSourceId( pBrush, iPrior, &priorId ) !=
                     geometry_status_t::OK ||
                 priorId.value == id.value ) {
                return geometry_status_t::CORRUPT_STATE;
            }
        }
    }

    return geometry_status_t::OK;
}

bool RestoreReleasedBrushSourceIds(
    geometry_source_id_registry_t *pRegistry,
    const brush_solid_t *pBrush,
    common::usize cReleased ) noexcept
{
    bool bRestored = true;
    for ( common::usize iSource = 0u;
          iSource < cReleased;
          ++iSource ) {
        geometry_source_id_t id{};
        if ( GetBrushSourceId( pBrush, iSource, &id ) !=
             geometry_status_t::OK ) {
            bRestored = false;
            continue;
        }
        bRestored =
            GeometrySourceIdRegistry_RestoreRetired(
                pRegistry, id ) == geometry_status_t::OK &&
            bRestored;
    }
    return bRestored;
}

} // namespace

// ---------------------------------------------------------------------------
// BrushSolid_DeepCopy
// ---------------------------------------------------------------------------

geometry_status_t BrushSolid_DeepCopy(
    brush_solid_t *pDst,
    const brush_solid_t *pSrc,
    const common::allocator_t *pAllocator,
    const geometry_limit_policy_t &limits ) noexcept
{
    if ( pDst == nullptr || pSrc == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometrySourceId_IsValid( pSrc->sourceId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_status_t initStatus =
        BrushSolid_Init( pDst, pAllocator, pSrc->sourceId );
    if ( initStatus != geometry_status_t::OK ) {
        return initStatus;
    }

    const common::usize cSides = BrushSolid_SideCount( pSrc );
    if ( cSides > 0u ) {
        const geometry_status_t reserveStatus =
            BrushSolid_TryReserve( pDst, limits, cSides );
        if ( reserveStatus != geometry_status_t::OK ) {
            BrushSolid_Shutdown( pDst );
            return reserveStatus;
        }

        for ( common::usize i = 0u; i < cSides; ++i ) {
            brush_solid_side_t side{};
            (void)BrushSolid_TryGetSide( pSrc, i, &side );
            const geometry_status_t addStatus =
                BrushSolid_TryAddSide( pDst, limits, side, nullptr );
            if ( addStatus != geometry_status_t::OK ) {
                BrushSolid_Shutdown( pDst );
                return addStatus;
            }
        }
    }

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

common::u64 GeometryDocument_SourceIdCapacity( const geometry_policy_t &policy ) noexcept
{
    const common::u64 terms[] = { policy.limits.cBrushesMax, policy.limits.cBrushSidesMax, policy.limits.cShellsMax,
                                  policy.limits.cVerticesMax, policy.limits.cFacesMax };
    common::u64 total = 0u;
    for ( const common::u64 term : terms ) {
        if ( term > ~static_cast<common::u64>( 0u ) - total ) { return 0u; }
        total += term;
    }
    return total;
}

geometry_status_t GeometryDocument_Init(
    geometry_document_t *pDocument,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy ) noexcept
{
    if ( pDocument == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pDocument->pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    if ( !common::Vector_Init( &pDocument->brushes, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( !common::Vector_Init( &pDocument->meshes, pAllocator, 0u ) ) {
        common::Vector_Shutdown( &pDocument->brushes );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const common::u64 cIdCapacity64 = GeometryDocument_SourceIdCapacity( policy );
    if ( cIdCapacity64 == 0u || cIdCapacity64 > common::CY_USIZE_MAX ) {
        common::Vector_Shutdown( &pDocument->meshes );
        common::Vector_Shutdown( &pDocument->brushes );
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cIdCapacity = static_cast<common::usize>( cIdCapacity64 );
    const geometry_status_t registryStatus =
        GeometrySourceIdRegistry_Init(
            &pDocument->sourceIds,
            pAllocator,
            cIdCapacity );
    if ( registryStatus != geometry_status_t::OK ) {
        common::Vector_Shutdown( &pDocument->meshes );
        common::Vector_Shutdown( &pDocument->brushes );
        return registryStatus;
    }

    // Seal loaded IDs immediately — fresh documents have no serialized
    // IDs to register.
    (void)GeometrySourceIdRegistry_SealLoadedIds( &pDocument->sourceIds );

    pDocument->policy = policy;
    pDocument->revision = GEOMETRY_REVISION_INITIAL;
    pDocument->pAllocator = pAllocator;

    return geometry_status_t::OK;
}

void GeometryDocument_Shutdown(
    geometry_document_t *pDocument ) noexcept
{
    if ( pDocument == nullptr || pDocument->pAllocator == nullptr ) {
        return;
    }

    for ( common::usize i = 0u; i < pDocument->brushes.nCount; ++i ) {
        FreeBrush( pDocument->pAllocator, pDocument->brushes.pData[i] );
    }
    common::Vector_Shutdown( &pDocument->brushes );
    GeometryDocument_FreeAllMeshes( pDocument );
    GeometrySourceIdRegistry_Shutdown( &pDocument->sourceIds );

    pDocument->policy = {};
    pDocument->revision = GEOMETRY_REVISION_INITIAL;
    pDocument->pAllocator = nullptr;
}

bool GeometryDocument_IsInitialized(
    const geometry_document_t *pDocument ) noexcept
{
    return pDocument != nullptr && pDocument->pAllocator != nullptr;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

geometry_revision_t GeometryDocument_GetRevision(
    const geometry_document_t *pDocument ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return GEOMETRY_REVISION_INITIAL;
    }
    return pDocument->revision;
}

common::usize GeometryDocument_BrushCount(
    const geometry_document_t *pDocument ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return 0u;
    }
    return common::Vector_Count( &pDocument->brushes );
}

// ---------------------------------------------------------------------------
// Brush lookup
// ---------------------------------------------------------------------------

const brush_solid_t *GeometryDocument_FindBrush(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ||
         !GeometrySourceId_IsValid( brushId ) ) {
        return nullptr;
    }

    const common::usize iIndex =
        FindBrushIndex( pDocument->brushes, brushId );
    if ( iIndex >= pDocument->brushes.nCount ) {
        return nullptr;
    }
    return pDocument->brushes.pData[iIndex];
}

brush_solid_t *GeometryDocument_FindBrushMutable(
    geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ||
         !GeometrySourceId_IsValid( brushId ) ) {
        return nullptr;
    }

    const common::usize iIndex =
        FindBrushIndex( pDocument->brushes, brushId );
    if ( iIndex >= pDocument->brushes.nCount ) {
        return nullptr;
    }

    return pDocument->brushes.pData[iIndex];
}

// ---------------------------------------------------------------------------
// Brush publication
// ---------------------------------------------------------------------------

geometry_status_t GeometryDocument_TryAddBrush(
    geometry_document_t *pDocument,
    const brush_solid_t *pBrush ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pBrush == nullptr ||
         !GeometrySourceId_IsValid( pBrush->sourceId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !common::Vector_IsValid( &pDocument->brushes ) ||
         !GeometrySourceIdRegistry_IsValid( &pDocument->sourceIds ) ||
         !GeometrySourceIdRegistry_IsInitialized( &pDocument->sourceIds ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    if ( FindBrushIndex( pDocument->brushes, pBrush->sourceId ) <
         pDocument->brushes.nCount ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    if ( static_cast<common::u64>( pDocument->brushes.nCount ) >=
         pDocument->policy.limits.cBrushesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( static_cast<common::u64>( cSides ) >
         pDocument->policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cSources = cSides + 1u;

    const geometry_status_t validationStatus =
        ValidateBrushSourceIdsForAdd(
            pDocument, pBrush, cSources );
    if ( validationStatus != geometry_status_t::OK ) {
        return validationStatus;
    }

    // Capture whether each identity is fresh or retired before changing the
    // registry. The record gives every later failure an exact rollback path.
    common::vector_t<document_source_id_activation_t> activations{};
    if ( !common::Vector_Init(
             &activations, pDocument->pAllocator, cSources ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    common::usize cFreshIds = 0u;
    for ( common::usize iSource = 0u;
          iSource < cSources;
          ++iSource ) {
        geometry_source_id_t id{};
        const geometry_status_t idStatus =
            GetBrushSourceId( pBrush, iSource, &id );
        if ( idStatus != geometry_status_t::OK ) {
            common::Vector_Shutdown( &activations );
            return idStatus;
        }

        const bool bWasClaimed = common::HashSet_Contains(
            &pDocument->sourceIds.claimedIds, id );
        if ( !bWasClaimed ) {
            ++cFreshIds;
        }
        if ( !common::Vector_PushBack(
                 &activations,
                 document_source_id_activation_t{ id, bWasClaimed } ) ) {
            common::Vector_Shutdown( &activations );
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }

    // Secure all container growth before activating any identity. Successful
    // activation and pointer publication below are allocation-free.
    if ( !common::Vector_Reserve(
             &pDocument->brushes,
             pDocument->brushes.nCount + 1u ) ) {
        common::Vector_Shutdown( &activations );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( &pDocument->sourceIds );
    if ( cFreshIds > pDocument->sourceIds.cEntriesMax - cClaimed ) {
        common::Vector_Shutdown( &activations );
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const geometry_status_t reserveStatus =
        GeometrySourceIdRegistry_Reserve(
            &pDocument->sourceIds, cClaimed + cFreshIds );
    if ( reserveStatus != geometry_status_t::OK ) {
        common::Vector_Shutdown( &activations );
        return reserveStatus;
    }

    // Allocate a new brush on the heap and deep-copy into it only after every
    // owning container has enough capacity for the final commit.
    brush_solid_t *pNewBrush = AllocateBrush( pDocument->pAllocator );
    if ( pNewBrush == nullptr ) {
        common::Vector_Shutdown( &activations );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const geometry_status_t copyStatus = BrushSolid_DeepCopy(
        pNewBrush, pBrush, pDocument->pAllocator,
        pDocument->policy.limits );
    if ( copyStatus != geometry_status_t::OK ) {
        FreeBrush( pDocument->pAllocator, pNewBrush );
        common::Vector_Shutdown( &activations );
        return copyStatus;
    }

    const geometry_source_id_allocator_t allocatorBefore =
        pDocument->sourceIds.allocator;
    const bool bLoadRegistrationOpenBefore =
        pDocument->sourceIds.bLoadRegistrationOpen;
    common::usize cActivated = 0u;
    for ( ; cActivated < activations.nCount; ++cActivated ) {
        const geometry_status_t activationStatus =
            ActivateDocumentSourceId(
                &pDocument->sourceIds,
                activations.pData[cActivated] );
        if ( activationStatus != geometry_status_t::OK ) {
            const bool bRolledBack = RollBackDocumentSourceIds(
                &pDocument->sourceIds,
                activations,
                cActivated,
                allocatorBefore,
                bLoadRegistrationOpenBefore );
            FreeBrush( pDocument->pAllocator, pNewBrush );
            common::Vector_Shutdown( &activations );
            return bRolledBack
                ? activationStatus
                : geometry_status_t::CORRUPT_STATE;
        }
    }
    pDocument->sourceIds.bLoadRegistrationOpen =
        bLoadRegistrationOpenBefore;

    if ( !common::Vector_PushBack( &pDocument->brushes, pNewBrush ) ) {
        const bool bRolledBack = RollBackDocumentSourceIds(
            &pDocument->sourceIds,
            activations,
            cActivated,
            allocatorBefore,
            bLoadRegistrationOpenBefore );
        FreeBrush( pDocument->pAllocator, pNewBrush );
        common::Vector_Shutdown( &activations );
        return bRolledBack
            ? geometry_status_t::ALLOCATION_FAILED
            : geometry_status_t::CORRUPT_STATE;
    }

    common::Vector_Shutdown( &activations );
    return geometry_status_t::OK;
}

geometry_status_t GeometryDocument_TryRemoveBrush(
    geometry_document_t *pDocument,
    geometry_source_id_t brushId ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !common::Vector_IsValid( &pDocument->brushes ) ||
         !GeometrySourceIdRegistry_IsValid( &pDocument->sourceIds ) ||
         !GeometrySourceIdRegistry_IsInitialized( &pDocument->sourceIds ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const common::usize iIndex =
        FindBrushIndex( pDocument->brushes, brushId );
    if ( iIndex >= pDocument->brushes.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    brush_solid_t *pBrush = pDocument->brushes.pData[iIndex];
    const common::usize cSides = BrushSolid_SideCount( pBrush );
    const common::usize cSources = cSides + 1u;
    const geometry_status_t validationStatus =
        ValidateBrushSourceIdsForRemove(
            pDocument, pBrush, cSources );
    if ( validationStatus != geometry_status_t::OK ) {
        return validationStatus;
    }

    const bool bLoadRegistrationOpenBefore =
        pDocument->sourceIds.bLoadRegistrationOpen;
    common::usize cReleased = 0u;
    for ( ; cReleased < cSources; ++cReleased ) {
        geometry_source_id_t id{};
        if ( GetBrushSourceId( pBrush, cReleased, &id ) !=
             geometry_status_t::OK ) {
            const bool bRestored = RestoreReleasedBrushSourceIds(
                &pDocument->sourceIds, pBrush, cReleased );
            pDocument->sourceIds.bLoadRegistrationOpen =
                bLoadRegistrationOpenBefore;
            CY_ASSERT_MSG(
                bRestored,
                "Document remove rollback must restore every released ID." );
            (void)bRestored;
            return geometry_status_t::CORRUPT_STATE;
        }

        const geometry_status_t releaseStatus =
            GeometrySourceIdRegistry_Release(
                &pDocument->sourceIds, id );
        if ( releaseStatus != geometry_status_t::OK ) {
            const bool bRestored = RestoreReleasedBrushSourceIds(
                &pDocument->sourceIds, pBrush, cReleased );
            pDocument->sourceIds.bLoadRegistrationOpen =
                bLoadRegistrationOpenBefore;
            CY_ASSERT_MSG(
                bRestored,
                "Document remove rollback must restore every released ID." );
            (void)bRestored;
            return geometry_status_t::CORRUPT_STATE;
        }
    }

    // Free the brush and swap-erase the pointer from the pool.
    FreeBrush( pDocument->pAllocator, pBrush );
    common::Vector_EraseSwap( &pDocument->brushes, iIndex );

    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
