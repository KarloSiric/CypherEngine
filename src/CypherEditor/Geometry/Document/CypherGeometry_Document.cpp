//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Document.cpp
//  Purpose: Implements the geometry-local aggregate store and its atomic
//           change-set application.
//  Details: TryApply is split into a read-only phase that validates and
//           reserves every resource the change set could need, and a
//           mutation phase built only from operations that cannot fail
//           once capacity is reserved. Any failure therefore happens while
//           the document is still exactly as it was.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Document.h"
#include "CypherGeometry_Snapshot.h"

#include "CypherCommon_HashSet.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u64;
using common::usize;

using document_brush_pool_t = common::generation_pool_t<
    const geometry_brush_value_t *, geometry_brush_tag_t>;

bool_t IsReady( const geometry_document_t *pDocument ) noexcept
{
    return pDocument != nullptr && pDocument->bInitialized;
}

// Owns a temporary ID set for one apply. Released on scope exit.
struct scoped_id_set_t {
    geometry_source_id_set_t set{};
    bool_t bInitialized{ false };

    scoped_id_set_t() noexcept = default;
    scoped_id_set_t( const scoped_id_set_t & ) = delete;
    scoped_id_set_t &operator=( const scoped_id_set_t & ) = delete;
    ~scoped_id_set_t() noexcept
    {
        if ( bInitialized ) {
            common::HashSet_Shutdown( &set );
        }
    }

    CYPHER_NODISCARD bool_t TryInit(
        const common::allocator_t *pAllocator, usize cCapacity ) noexcept
    {
        bInitialized = common::HashSet_Init( &set, pAllocator, cCapacity );
        return bInitialized;
    }
};

// Per-change scratch resolved during validation and consumed during
// mutation, so the mutation phase never has to look anything up again.
struct resolved_change_t {
    geometry_brush_handle_t handle{};
    const geometry_brush_value_t *pOldValue{ nullptr };
};

struct scoped_resolved_t {
    resolved_change_t *pData{ nullptr };
    usize cCount{ 0u };
    const common::allocator_t *pAllocator{ nullptr };

    scoped_resolved_t() noexcept = default;
    scoped_resolved_t( const scoped_resolved_t & ) = delete;
    scoped_resolved_t &operator=( const scoped_resolved_t & ) = delete;
    ~scoped_resolved_t() noexcept
    {
        if ( pData != nullptr ) {
            common::Allocator_FreeArrayStorage( pAllocator, pData, cCount );
        }
    }

    CYPHER_NODISCARD bool_t TryInit(
        const common::allocator_t *pAlloc, usize cEntries ) noexcept
    {
        pData = common::Allocator_AllocateArrayStorage<resolved_change_t>(
            pAlloc, cEntries );
        if ( pData == nullptr ) {
            return false;
        }
        pAllocator = pAlloc;
        cCount = cEntries;
        for ( usize i = 0u; i < cEntries; ++i ) {
            pData[i] = resolved_change_t{};
        }
        return true;
    }
};

usize ValueIdCount( const geometry_brush_value_t *pValue ) noexcept
{
    return 1u + common::Vector_Count( &pValue->brush.sides );
}

geometry_source_id_t ValueId( const geometry_brush_value_t *pValue, usize i ) noexcept
{
    return i == 0u ? pValue->brush.sourceId
                   : pValue->brush.sides.pData[i - 1u].sourceId;
}

// Resolves a committed brush root. Returns null when brushId is not a
// committed ID or names a side rather than a root.
const geometry_brush_value_t *ResolveRoot(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    geometry_brush_handle_t *pHandleOut ) noexcept
{
    const geometry_brush_handle_t *pHandle =
        common::HashMap_Find( &pDocument->owners, brushId );
    if ( pHandle == nullptr ) {
        return nullptr;
    }
    const geometry_brush_value_t *const *ppValue =
        common::GenerationPool_Get( &pDocument->brushes, *pHandle );
    if ( ppValue == nullptr || *ppValue == nullptr ||
         ( *ppValue )->brush.sourceId.value != brushId.value ) {
        return nullptr;
    }
    if ( pHandleOut != nullptr ) {
        *pHandleOut = *pHandle;
    }
    return *ppValue;
}

bool_t IsValueChange( geometry_document_change_kind_t kind ) noexcept
{
    return kind == geometry_document_change_kind_t::INSERT_BRUSH ||
           kind == geometry_document_change_kind_t::REPLACE_BRUSH;
}

void ReleaseCachedSnapshot( geometry_document_t *pDocument ) noexcept
{
    if ( pDocument->pCachedSnapshot != nullptr ) {
        GeometrySnapshot_Release( pDocument->pCachedSnapshot );
        pDocument->pCachedSnapshot = nullptr;
    }
}

// Lifetime claim budget for the registry. Identities are never reused, so
// the budget must cover churn (edits that retire sides and mint new ones),
// not just the live limits. Four times the live limits, saturating.
usize RegistryClaimBudget( const geometry_limit_policy_t &limits ) noexcept
{
    const u64 cLive = limits.cBrushesMax + limits.cBrushSidesMax;
    const u64 cBudget = cLive > common::CY_U64_MAX / 4u
        ? common::CY_U64_MAX
        : cLive * 4u;
    return cBudget > static_cast<u64>( common::CY_USIZE_MAX )
        ? common::CY_USIZE_MAX
        : static_cast<usize>( cBudget );
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t GeometryDocument_Init(
    geometry_document_t *pDocument,
    const geometry_document_desc_t &desc ) noexcept
{
    if ( pDocument == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pDocument->bInitialized ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !common::Allocator_IsValid( desc.pAllocator ) ||
         !GeometryPolicy_IsValid( desc.policy ) ||
         desc.policy.limits.cBrushesMax == 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const u64 cSlotLimit =
        desc.policy.limits.cBrushesMax <
                static_cast<u64>( common::CY_GENERATION_POOL_MAX_CAPACITY )
            ? desc.policy.limits.cBrushesMax
            : static_cast<u64>( common::CY_GENERATION_POOL_MAX_CAPACITY );

    geometry_status_t status = GeometryStatus_FromGenerationPoolStatus(
        common::GenerationPool_Init(
            &pDocument->brushes, desc.pAllocator, static_cast<usize>( cSlotLimit ) ) );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !common::HashMap_Init( &pDocument->owners, desc.pAllocator ) ) {
        common::GenerationPool_Shutdown( &pDocument->brushes );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    status = GeometrySourceIdRegistry_Init(
        &pDocument->registry, desc.pAllocator,
        RegistryClaimBudget( desc.policy.limits ), 0u, desc.firstSourceId );
    if ( status != geometry_status_t::OK ) {
        common::HashMap_Shutdown( &pDocument->owners );
        common::GenerationPool_Shutdown( &pDocument->brushes );
        return status;
    }

    pDocument->policy = desc.policy;
    pDocument->revision = 1u;
    pDocument->cSides = 0u;
    pDocument->pAllocator = desc.pAllocator;
    pDocument->pCachedSnapshot = nullptr;
    pDocument->bInitialized = true;
    return geometry_status_t::OK;
}

void GeometryDocument_Shutdown( geometry_document_t *pDocument ) noexcept
{
    if ( !IsReady( pDocument ) ) {
        return;
    }
    ReleaseCachedSnapshot( pDocument );
    ( void )common::GenerationPool_ForEach(
        &pDocument->brushes,
        []( geometry_brush_handle_t, const geometry_brush_value_t *&pValue ) noexcept -> bool_t {
            BrushValue_Release( pValue );
            pValue = nullptr;
            return true;
        } );
    common::GenerationPool_Shutdown( &pDocument->brushes );
    common::HashMap_Shutdown( &pDocument->owners );
    GeometrySourceIdRegistry_Shutdown( &pDocument->registry );
    pDocument->policy = {};
    pDocument->revision = 0u;
    pDocument->cSides = 0u;
    pDocument->pAllocator = nullptr;
    pDocument->bInitialized = false;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

u64 GeometryDocument_Revision( const geometry_document_t *pDocument ) noexcept
{
    return IsReady( pDocument ) ? pDocument->revision : 0u;
}

usize GeometryDocument_BrushCount( const geometry_document_t *pDocument ) noexcept
{
    return IsReady( pDocument ) ? common::GenerationPool_Count( &pDocument->brushes ) : 0u;
}

geometry_status_t GeometryDocument_TryFindBrush(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    geometry_brush_handle_t *pHandleOut ) noexcept
{
    if ( pHandleOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pHandleOut = GEOMETRY_HANDLE_INVALID<geometry_brush_tag_t>;
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    return ResolveRoot( pDocument, brushId, pHandleOut ) != nullptr
        ? geometry_status_t::OK
        : geometry_status_t::INVALID_ARGUMENT;
}

geometry_status_t GeometryDocument_TryGetBrush(
    const geometry_document_t *pDocument,
    geometry_brush_handle_t handle,
    const geometry_brush_value_t **ppValueOut ) noexcept
{
    if ( ppValueOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppValueOut = nullptr;
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const common::generation_pool_resolve_result_t<const geometry_brush_value_t *const>
        resolved = common::GenerationPool_Resolve( &pDocument->brushes, handle );
    if ( resolved.status != common::generation_pool_status_t::OK ) {
        return GeometryStatus_FromGenerationPoolStatus( resolved.status );
    }
    *ppValueOut = *resolved.pValue;
    return geometry_status_t::OK;
}

geometry_status_t GeometryDocument_TryGetBrushById(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    const geometry_brush_value_t **ppValueOut ) noexcept
{
    if ( ppValueOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppValueOut = nullptr;
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const geometry_brush_value_t *pValue = ResolveRoot( pDocument, brushId, nullptr );
    if ( pValue == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppValueOut = pValue;
    return geometry_status_t::OK;
}

geometry_status_t GeometryDocument_TryFindOwner(
    const geometry_document_t *pDocument,
    geometry_source_id_t id,
    geometry_source_id_t *pBrushIdOut ) noexcept
{
    if ( pBrushIdOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pBrushIdOut = GEOMETRY_SOURCE_ID_INVALID;
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const geometry_brush_handle_t *pHandle =
        common::HashMap_Find( &pDocument->owners, id );
    if ( pHandle == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_brush_value_t *const *ppValue =
        common::GenerationPool_Get( &pDocument->brushes, *pHandle );
    if ( ppValue == nullptr || *ppValue == nullptr ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    *pBrushIdOut = ( *ppValue )->brush.sourceId;
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------

geometry_status_t GeometryDocument_TryAllocateSourceIds(
    geometry_document_t *pDocument,
    common::span_t<geometry_source_id_t> idsOut ) noexcept
{
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Span_IsValid( idsOut ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( usize i = 0u; i < idsOut.nCount; ++i ) {
        idsOut.pData[i] = GEOMETRY_SOURCE_ID_INVALID;
    }
    if ( idsOut.nCount == 0u ) {
        return geometry_status_t::OK;
    }

    // Check sequence headroom up front so exhaustion is reported before any
    // ID is claimed.
    const geometry_source_id_t next = pDocument->registry.allocator.next;
    if ( !GeometrySourceId_IsValid( next ) ||
         static_cast<u64>( idsOut.nCount - 1u ) > common::CY_U64_MAX - next.value ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    const usize cClaimed = GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry );
    if ( idsOut.nCount > pDocument->registry.cEntriesMax - cClaimed ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const geometry_status_t reserve = GeometrySourceIdRegistry_Reserve(
        &pDocument->registry, cClaimed + idsOut.nCount );
    if ( reserve != geometry_status_t::OK ) {
        return reserve;
    }

    for ( usize i = 0u; i < idsOut.nCount; ++i ) {
        const geometry_source_id_result_t result =
            GeometrySourceIdRegistry_Allocate( &pDocument->registry );
        if ( result.status != geometry_status_t::OK ) {
            // Unreachable after the checks above; unwind defensively. The
            // released IDs stay claimed and are never handed out again.
            for ( usize j = 0u; j < i; ++j ) {
                ( void )GeometrySourceIdRegistry_Release(
                    &pDocument->registry, idsOut.pData[j] );
                idsOut.pData[j] = GEOMETRY_SOURCE_ID_INVALID;
            }
            return result.status;
        }
        idsOut.pData[i] = result.id;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryDocument_ReleasePendingId(
    geometry_document_t *pDocument,
    geometry_source_id_t id ) noexcept
{
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( common::HashMap_Contains( &pDocument->owners, id ) ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }
    return GeometrySourceIdRegistry_Release( &pDocument->registry, id );
}

geometry_status_t GeometryDocument_TryRegisterLoadedIds(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> ids ) noexcept
{
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Span_IsValid( ids ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pDocument->registry.bLoadRegistrationOpen ) {
        return geometry_status_t::UNSUPPORTED;
    }
    const usize cClaimed = GeometrySourceIdRegistry_ClaimedCount( &pDocument->registry );
    if ( ids.nCount > pDocument->registry.cEntriesMax - cClaimed ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const geometry_status_t reserve =
        GeometrySourceIdRegistry_Reserve( &pDocument->registry, cClaimed + ids.nCount );
    if ( reserve != geometry_status_t::OK ) {
        return reserve;
    }
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        const geometry_status_t status =
            GeometrySourceIdRegistry_Register( &pDocument->registry, ids.pData[i] );
        if ( status != geometry_status_t::OK ) {
            // Registered IDs stay claimed; the caller abandons the load and
            // shuts the document down, so nothing observes them.
            return status;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryDocument_SealLoadedIds( geometry_document_t *pDocument ) noexcept
{
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    return GeometrySourceIdRegistry_SealLoadedIds( &pDocument->registry );
}

geometry_source_id_t GeometryDocument_NextSourceId( const geometry_document_t *pDocument ) noexcept
{
    return IsReady( pDocument ) ? pDocument->registry.allocator.next : GEOMETRY_SOURCE_ID_INVALID;
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

geometry_status_t GeometryDocument_TryCreateBrushValue(
    const geometry_document_t *pDocument,
    const brush_solid_t *pBrush,
    const geometry_brush_side_attribute_store_t *pAttributes,
    const geometry_brush_value_t **ppValueOut,
    brush_validation_result_t *pValidationOut ) noexcept
{
    if ( ppValueOut != nullptr ) {
        *ppValueOut = nullptr;
    }
    if ( pValidationOut != nullptr ) {
        *pValidationOut = {};
    }
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    return BrushValue_TryCreate(
        pDocument->pAllocator, pDocument->policy, pDocument,
        pBrush, pAttributes, ppValueOut, pValidationOut );
}

// ---------------------------------------------------------------------------
// Change sets
// ---------------------------------------------------------------------------

geometry_status_t GeometryDocument_TryApply(
    geometry_document_t *pDocument,
    u64 expectedRevision,
    common::span_t<const geometry_document_change_t> changes,
    u64 *pNewRevisionOut ) noexcept
{
    if ( !IsReady( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( expectedRevision != pDocument->revision ) {
        return geometry_status_t::STALE_REVISION;
    }
    if ( !common::Span_IsValid( changes ) || changes.nCount == 0u ||
         static_cast<u64>( changes.nCount ) >
             pDocument->policy.limits.cJournalRecordsMax ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::allocator_t *pAllocator = pDocument->pAllocator;
    const usize cChanges = changes.nCount;

    // ---- Phase 1: structure ---------------------------------------------

    scoped_id_set_t targets{};
    if ( !targets.TryInit( pAllocator, cChanges ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cChanges; ++i ) {
        const geometry_document_change_t &change = changes.pData[i];
        if ( !GeometrySourceId_IsValid( change.brushId ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( IsValueChange( change.kind ) ) {
            if ( change.pValue == nullptr ||
                 change.pValue->pDomain != pDocument ||
                 change.pValue->brush.sourceId.value != change.brushId.value ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
        } else if ( change.kind == geometry_document_change_kind_t::REMOVE_BRUSH ) {
            if ( change.pValue != nullptr ) {
                return geometry_status_t::INVALID_ARGUMENT;
            }
        } else {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( common::HashSet_Contains( &targets.set, change.brushId ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( !common::HashSet_Insert( &targets.set, change.brushId ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }

    // ---- Phase 2: resolve targets ---------------------------------------

    scoped_resolved_t resolved{};
    if ( !resolved.TryInit( pAllocator, cChanges ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    scoped_id_set_t freedRoots{};
    if ( !freedRoots.TryInit( pAllocator, cChanges ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    usize cInserts = 0u;
    usize cRemoves = 0u;
    u64 cSidesFreed = 0u;
    u64 cSidesAdded = 0u;
    usize cNewIdsUpperBound = 0u;
    for ( usize i = 0u; i < cChanges; ++i ) {
        const geometry_document_change_t &change = changes.pData[i];
        const geometry_brush_value_t *pCommitted = ResolveRoot(
            pDocument, change.brushId, &resolved.pData[i].handle );

        if ( change.kind == geometry_document_change_kind_t::INSERT_BRUSH ) {
            if ( pCommitted != nullptr ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
            ++cInserts;
        } else {
            if ( pCommitted == nullptr ) {
                return geometry_status_t::INVALID_HANDLE;
            }
            resolved.pData[i].pOldValue = pCommitted;
            cSidesFreed += BrushSolid_SideCount( &pCommitted->brush );
            if ( !common::HashSet_Insert( &freedRoots.set, change.brushId ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            if ( change.kind == geometry_document_change_kind_t::REMOVE_BRUSH ) {
                ++cRemoves;
            }
        }
        if ( IsValueChange( change.kind ) ) {
            cSidesAdded += BrushSolid_SideCount( &change.pValue->brush );
            cNewIdsUpperBound += ValueIdCount( change.pValue );
        }
    }

    // ---- Phase 3: identity ----------------------------------------------

    scoped_id_set_t newIds{};
    if ( !newIds.TryInit( pAllocator, cNewIdsUpperBound ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    usize cNewIds = 0u;
    usize cRestores = 0u;
    for ( usize i = 0u; i < cChanges; ++i ) {
        const geometry_document_change_t &change = changes.pData[i];
        if ( !IsValueChange( change.kind ) ) {
            continue;
        }
        const usize cIds = ValueIdCount( change.pValue );
        for ( usize k = 0u; k < cIds; ++k ) {
            const geometry_source_id_t id = ValueId( change.pValue, k );
            if ( common::HashSet_Contains( &newIds.set, id ) ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
            if ( !common::HashSet_Insert( &newIds.set, id ) ) {
                return geometry_status_t::ALLOCATION_FAILED;
            }
            ++cNewIds;
            if ( !GeometrySourceIdRegistry_IsClaimed( &pDocument->registry, id ) ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }

            const geometry_brush_handle_t *pOwner =
                common::HashMap_Find( &pDocument->owners, id );
            if ( pOwner != nullptr ) {
                // Owned IDs may only move out of a brush this set also frees.
                const geometry_brush_value_t *const *ppOwnerValue =
                    common::GenerationPool_Get( &pDocument->brushes, *pOwner );
                if ( ppOwnerValue == nullptr || *ppOwnerValue == nullptr ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
                if ( !common::HashSet_Contains(
                         &freedRoots.set, ( *ppOwnerValue )->brush.sourceId ) ) {
                    return geometry_status_t::IDENTITY_CONFLICT;
                }
            } else if ( !GeometrySourceIdRegistry_Contains( &pDocument->registry, id ) ) {
                ++cRestores;
            }
        }
    }

    // ---- Phase 4: limits --------------------------------------------------

    const usize cBrushes = common::GenerationPool_Count( &pDocument->brushes );
    const u64 cBrushesAfter =
        static_cast<u64>( cBrushes ) + cInserts - cRemoves;
    if ( cBrushesAfter > pDocument->policy.limits.cBrushesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const u64 cSidesAfter = pDocument->cSides - cSidesFreed + cSidesAdded;
    if ( cSidesAfter > pDocument->policy.limits.cBrushSidesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // ---- Phase 5: reserve every resource the mutation phase touches -------

    // Removals run before inserts in the mutation phase, so a slot freed by
    // this set is available to an insert in the same set -- unless removing
    // it exhausts its generation, in which case the pool retires it.
    document_brush_pool_t &pool = pDocument->brushes;
    usize cFreeSlots = pool.cSlots - pool.cRecords - pool.cRetiredSlots;
    for ( usize i = 0u; i < cChanges; ++i ) {
        if ( changes.pData[i].kind == geometry_document_change_kind_t::REMOVE_BRUSH &&
             resolved.pData[i].handle.nGeneration != common::CY_U32_MAX ) {
            ++cFreeSlots;
        }
    }
    if ( cInserts > cFreeSlots ) {
        const usize cExtra = cInserts - cFreeSlots;
        if ( cExtra > pool.cSlotLimit - pool.cSlots ) {
            return geometry_status_t::LIMIT_EXCEEDED;
        }
        const geometry_status_t reserve = GeometryStatus_FromGenerationPoolStatus(
            common::GenerationPool_Reserve( &pool, pool.cSlots + cExtra ) );
        if ( reserve != geometry_status_t::OK ) {
            return reserve;
        }
    }
    if ( !common::HashMap_Reserve(
             &pDocument->owners,
             common::HashMap_Count( &pDocument->owners ) + cNewIds ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( cRestores > 0u ) {
        const geometry_status_t reserve = GeometrySourceIdRegistry_Reserve(
            &pDocument->registry,
            GeometrySourceIdRegistry_Count( &pDocument->registry ) + cRestores );
        if ( reserve != geometry_status_t::OK ) {
            return reserve;
        }
    }

    // ---- Phase 6: mutate (cannot fail past this point) ----------------------

    // 6a. Unbind every ID of every freed value. IDs that no new value
    // re-uses leave live membership in the registry.
    for ( usize i = 0u; i < cChanges; ++i ) {
        const geometry_brush_value_t *pOld = resolved.pData[i].pOldValue;
        if ( pOld == nullptr ) {
            continue;
        }
        const usize cIds = ValueIdCount( pOld );
        for ( usize k = 0u; k < cIds; ++k ) {
            const geometry_source_id_t id = ValueId( pOld, k );
            const bool_t bErased = common::HashMap_Erase( &pDocument->owners, id );
            CY_ASSERT_MSG( bErased, "Committed ID missing from the owner map." );
            ( void )bErased;
            if ( !common::HashSet_Contains( &newIds.set, id ) ) {
                const geometry_status_t released =
                    GeometrySourceIdRegistry_Release( &pDocument->registry, id );
                CY_ASSERT_MSG( released == geometry_status_t::OK,
                               "Committed ID was not live in the registry." );
                ( void )released;
            }
        }
    }

    // 6b. Swap pool records: removals first so their slots can be reused
    // by inserts, then replacements, then inserts. New references are
    // taken before old ones are dropped (6d), so replacing a brush with its
    // own value is safe.
    for ( usize i = 0u; i < cChanges; ++i ) {
        if ( changes.pData[i].kind != geometry_document_change_kind_t::REMOVE_BRUSH ) {
            continue;
        }
        const common::generation_pool_status_t removed =
            common::GenerationPool_Remove( &pool, resolved.pData[i].handle );
        CY_ASSERT_MSG( removed == common::generation_pool_status_t::OK,
                       "Resolved brush handle could not be removed." );
        ( void )removed;
    }
    for ( usize i = 0u; i < cChanges; ++i ) {
        const geometry_document_change_t &change = changes.pData[i];
        if ( change.kind != geometry_document_change_kind_t::REPLACE_BRUSH ) {
            continue;
        }
        const geometry_brush_value_t **ppRecord =
            common::GenerationPool_Get( &pool, resolved.pData[i].handle );
        CY_ASSERT_MSG( ppRecord != nullptr, "Resolved brush handle went stale." );
        BrushValue_AddRef( change.pValue );
        *ppRecord = change.pValue;
    }
    for ( usize i = 0u; i < cChanges; ++i ) {
        const geometry_document_change_t &change = changes.pData[i];
        if ( change.kind != geometry_document_change_kind_t::INSERT_BRUSH ) {
            continue;
        }
        BrushValue_AddRef( change.pValue );
        const common::generation_pool_handle_result_t<geometry_brush_tag_t>
            inserted = common::GenerationPool_Insert( &pool, change.pValue );
        CY_ASSERT_MSG( inserted.status == common::generation_pool_status_t::OK,
                       "Reserved brush pool insert failed." );
        resolved.pData[i].handle = inserted.handle;
    }

    // 6c. Bind every ID of every new value, restoring retired identities.
    for ( usize i = 0u; i < cChanges; ++i ) {
        const geometry_document_change_t &change = changes.pData[i];
        if ( !IsValueChange( change.kind ) ) {
            continue;
        }
        const usize cIds = ValueIdCount( change.pValue );
        for ( usize k = 0u; k < cIds; ++k ) {
            const geometry_source_id_t id = ValueId( change.pValue, k );
            if ( !GeometrySourceIdRegistry_Contains( &pDocument->registry, id ) ) {
                const geometry_status_t restored =
                    GeometrySourceIdRegistry_RestoreRetired( &pDocument->registry, id );
                CY_ASSERT_MSG( restored == geometry_status_t::OK,
                               "Reserved identity restore failed." );
                ( void )restored;
            }
            const common::hash_table_insert_result_t<geometry_brush_handle_t> bound =
                common::HashMap_Insert( &pDocument->owners, id, resolved.pData[i].handle );
            CY_ASSERT_MSG( bound.bInserted, "Reserved owner-map insert failed." );
            ( void )bound;
        }
    }

    // 6d. Drop the references the document no longer holds.
    for ( usize i = 0u; i < cChanges; ++i ) {
        BrushValue_Release( resolved.pData[i].pOldValue );
    }

    pDocument->cSides = cSidesAfter;
    ReleaseCachedSnapshot( pDocument );
    ++pDocument->revision;
    if ( pNewRevisionOut != nullptr ) {
        *pNewRevisionOut = pDocument->revision;
    }
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

bool_t GeometryDocument_ValidateDeep( const geometry_document_t *pDocument ) noexcept
{
    if ( !IsReady( pDocument ) || pDocument->revision == 0u ) {
        return false;
    }
    if ( !common::GenerationPool_IsValid( &pDocument->brushes ) ||
         !common::HashMap_IsValid( &pDocument->owners ) ||
         !GeometrySourceIdRegistry_ValidateDeep( &pDocument->registry ) ) {
        return false;
    }

    struct audit_t {
        const geometry_document_t *pDocument;
        usize cIds;
        u64 cSides;
        bool_t bValid;
    } audit{ pDocument, 0u, 0u, true };

    ( void )common::GenerationPool_ForEach(
        &pDocument->brushes,
        [&audit]( geometry_brush_handle_t handle,
                  const geometry_brush_value_t *const &pValue ) noexcept -> bool_t {
            if ( pValue == nullptr || pValue->pDomain != audit.pDocument ||
                 BrushValue_RefCount( pValue ) == 0u ) {
                audit.bValid = false;
                return false;
            }
            const usize cIds = ValueIdCount( pValue );
            for ( usize k = 0u; k < cIds; ++k ) {
                const geometry_source_id_t id = ValueId( pValue, k );
                const geometry_brush_handle_t *pOwner =
                    common::HashMap_Find( &audit.pDocument->owners, id );
                if ( pOwner == nullptr || pOwner->nSlot != handle.nSlot ||
                     pOwner->nGeneration != handle.nGeneration ||
                     !GeometrySourceIdRegistry_Contains( &audit.pDocument->registry, id ) ) {
                    audit.bValid = false;
                    return false;
                }
            }
            audit.cIds += cIds;
            audit.cSides += BrushSolid_SideCount( &pValue->brush );
            return true;
        } );

    return audit.bValid &&
           audit.cIds == common::HashMap_Count( &pDocument->owners ) &&
           audit.cSides == pDocument->cSides;
}

} // namespace cypher::editor::geometry
