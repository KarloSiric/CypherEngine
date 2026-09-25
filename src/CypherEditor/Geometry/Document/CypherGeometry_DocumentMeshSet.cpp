//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentMeshSet.cpp
//  Purpose: Implements atomic N-to-M publication of ordered mesh sets.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentMeshSet.h"
#include "CypherGeometry_DocumentSurfaces.h"

#include <algorithm>
#include <new>

namespace cypher::editor::geometry
{

namespace
{

bool IdLess( geometry_source_id_t a, geometry_source_id_t b ) noexcept
{
    return a.value < b.value;
}

bool IdEqual( geometry_source_id_t a, geometry_source_id_t b ) noexcept
{
    return a.value == b.value;
}

void SortIds( common::vector_t<geometry_source_id_t> *pIds ) noexcept
{
    if ( pIds->nCount > 1u ) {
        std::sort( pIds->pData, pIds->pData + pIds->nCount, IdLess );
    }
}

bool SortedContains(
    const common::vector_t<geometry_source_id_t> &ids,
    geometry_source_id_t id ) noexcept
{
    return ids.nCount > 0u &&
           std::binary_search(
               ids.pData, ids.pData + ids.nCount, id, IdLess );
}

mesh_source_t *AllocateMesh(
    const common::allocator_t *pAllocator ) noexcept
{
    void *pMemory = common::Allocator_AllocateZeroed(
        pAllocator, sizeof( mesh_source_t ), alignof( mesh_source_t ) );
    return pMemory != nullptr ? new ( pMemory ) mesh_source_t{} : nullptr;
}

void FreeMesh(
    const common::allocator_t *pAllocator,
    mesh_source_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) {
        return;
    }
    MeshSource_Shutdown( pMesh );
    pMesh->~mesh_source_t();
    common::Allocator_Free(
        pAllocator, pMesh, sizeof( mesh_source_t ), alignof( mesh_source_t ) );
}

common::usize FindMeshIndex(
    const geometry_document_t &document,
    geometry_source_id_t root ) noexcept
{
    for ( common::usize i = 0u; i < document.meshes.nCount; ++i ) {
        const mesh_source_t *pMesh = document.meshes.pData[i];
        if ( pMesh != nullptr && pMesh->sourceId.value == root.value ) {
            return i;
        }
    }
    return document.meshes.nCount;
}

common::usize FindReplacementIndex(
    common::span_t<const mesh_source_description_t *const> replacements,
    geometry_source_id_t root ) noexcept
{
    for ( common::usize i = 0u; i < replacements.nCount; ++i ) {
        if ( replacements.pData[i] != nullptr &&
             replacements.pData[i]->sourceId.value == root.value ) {
            return i;
        }
    }
    return replacements.nCount;
}

geometry_status_t StatusFromFault( mesh_source_fault_t fault ) noexcept
{
    switch ( fault ) {
    case mesh_source_fault_t::NONE:
        return geometry_status_t::OK;
    case mesh_source_fault_t::NOT_INITIALIZED:
    case mesh_source_fault_t::INVALID_ROOT_ID:
    case mesh_source_fault_t::MISSING_VERTEX_ID:
    case mesh_source_fault_t::MISSING_FACE_ID:
        return geometry_status_t::INVALID_ARGUMENT;
    case mesh_source_fault_t::DUPLICATE_SOURCE_ID:
        return geometry_status_t::IDENTITY_CONFLICT;
    case mesh_source_fault_t::NON_FINITE:
    case mesh_source_fault_t::COORDINATE_RANGE:
        return geometry_status_t::NUMERIC_FAILURE;
    case mesh_source_fault_t::VALIDATION_INCOMPLETE:
        return geometry_status_t::ALLOCATION_FAILED;
    case mesh_source_fault_t::INVALID_TOPOLOGY:
    case mesh_source_fault_t::INVALID_ATTRIBUTES:
    default:
        return geometry_status_t::INVALID_TOPOLOGY;
    }
}

struct mesh_totals_t {
    common::u64 cVertices{ 0u };
    common::u64 cHalfEdges{ 0u };
    common::u64 cEdges{ 0u };
    common::u64 cLoops{ 0u };
    common::u64 cFaces{ 0u };
    common::u64 cShells{ 0u };
};

void AddMeshTotals(
    mesh_totals_t *pTotals,
    const mesh_source_t &mesh ) noexcept
{
    pTotals->cVertices += common::GenerationPool_Count( &mesh.mesh.vertices );
    pTotals->cHalfEdges += common::GenerationPool_Count( &mesh.mesh.halfEdges );
    pTotals->cEdges += common::GenerationPool_Count( &mesh.mesh.edges );
    pTotals->cLoops += common::GenerationPool_Count( &mesh.mesh.loops );
    pTotals->cFaces += common::GenerationPool_Count( &mesh.mesh.faces );
    pTotals->cShells += common::GenerationPool_Count( &mesh.mesh.shells );
}

bool TotalsWithinLimits(
    const mesh_totals_t &totals,
    const geometry_limit_policy_t &limits ) noexcept
{
    return totals.cVertices <= limits.cVerticesMax &&
           totals.cHalfEdges <= limits.cHalfEdgesMax &&
           totals.cEdges <= limits.cEdgesMax &&
           totals.cLoops <= limits.cLoopsMax &&
           totals.cFaces <= limits.cFacesMax &&
           totals.cShells <= limits.cShellsMax;
}

geometry_status_t CheckDocument(
    const geometry_document_t *pDocument ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Vector_IsValid( &pDocument->brushes ) ||
         !common::Vector_IsValid( &pDocument->meshes ) ||
         !GeometrySourceIdRegistry_IsInitialized( &pDocument->sourceIds ) ||
         !GeometrySourceIdRegistry_ValidateDeep( &pDocument->sourceIds ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateStoredOwnership(
    const geometry_document_t &document,
    mesh_totals_t *pTotalsOut ) noexcept
{
    common::vector_t<geometry_source_id_t> ownedIds{};
    if ( !common::Vector_Init( &ownedIds, document.pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    geometry_status_t status = geometry_status_t::OK;
    for ( common::usize iBrush = 0u;
          iBrush < document.brushes.nCount && status == geometry_status_t::OK;
          ++iBrush ) {
        const brush_solid_t *pBrush = document.brushes.pData[iBrush];
        if ( pBrush == nullptr || !common::Vector_IsValid( &pBrush->sides ) ||
             !GeometrySourceId_IsValid( pBrush->sourceId ) ||
             !common::Vector_PushBack( &ownedIds, pBrush->sourceId ) ) {
            status = pBrush == nullptr ||
                             !GeometrySourceId_IsValid( pBrush->sourceId )
                         ? geometry_status_t::CORRUPT_STATE
                         : geometry_status_t::ALLOCATION_FAILED;
            break;
        }
        for ( common::usize iSide = 0u; iSide < pBrush->sides.nCount; ++iSide ) {
            const geometry_source_id_t id = pBrush->sides.pData[iSide].sourceId;
            if ( !GeometrySourceId_IsValid( id ) ) {
                status = geometry_status_t::CORRUPT_STATE;
                break;
            }
            if ( !common::Vector_PushBack( &ownedIds, id ) ) {
                status = geometry_status_t::ALLOCATION_FAILED;
                break;
            }
        }
    }

    mesh_totals_t totals{};
    for ( common::usize iMesh = 0u;
          iMesh < document.meshes.nCount && status == geometry_status_t::OK;
          ++iMesh ) {
        const mesh_source_t *pMesh = document.meshes.pData[iMesh];
        if ( pMesh == nullptr ) {
            status = geometry_status_t::CORRUPT_STATE;
            break;
        }
        const mesh_source_validation_t validation =
            MeshSource_Validate( pMesh, document.pAllocator );
        status = StatusFromFault( validation.fault );
        if ( status != geometry_status_t::OK ) {
            status = status == geometry_status_t::ALLOCATION_FAILED
                ? status
                : geometry_status_t::CORRUPT_STATE;
            break;
        }
        status = MeshSource_TryCollectSourceIds( pMesh, &ownedIds );
        if ( status != geometry_status_t::OK ) {
            break;
        }
        AddMeshTotals( &totals, *pMesh );
    }

    // Patch and heightfield identities are owned too (DocumentSurfaces.h).
    if ( status == geometry_status_t::OK ) {
        status = GeometryDocument_TryCollectSurfaceIds( &document, &ownedIds );
    }

    if ( status == geometry_status_t::OK ) {
        SortIds( &ownedIds );
        for ( common::usize i = 0u; i < ownedIds.nCount; ++i ) {
            if ( ( i > 0u && IdEqual( ownedIds.pData[i - 1u], ownedIds.pData[i] ) ) ||
                 !GeometrySourceIdRegistry_Contains(
                     &document.sourceIds, ownedIds.pData[i] ) ) {
                status = geometry_status_t::CORRUPT_STATE;
                break;
            }
        }
        if ( status == geometry_status_t::OK &&
             ownedIds.nCount != GeometrySourceIdRegistry_Count(
                                    &document.sourceIds ) ) {
            status = geometry_status_t::CORRUPT_STATE;
        }
    }

    common::Vector_Shutdown( &ownedIds );
    if ( status == geometry_status_t::OK ) {
        *pTotalsOut = totals;
    }
    return status;
}

geometry_status_t PrepareRemovalRoots(
    const geometry_document_t &document,
    common::span_t<const geometry_source_id_t> roots,
    common::vector_t<geometry_source_id_t> *pSortedOut ) noexcept
{
    if ( !common::Vector_Init( pSortedOut, document.pAllocator, roots.nCount ) ||
         !common::Vector_Append( pSortedOut, roots ) ) {
        common::Vector_Shutdown( pSortedOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    SortIds( pSortedOut );
    for ( common::usize i = 0u; i < pSortedOut->nCount; ++i ) {
        const geometry_source_id_t id = pSortedOut->pData[i];
        if ( !GeometrySourceId_IsValid( id ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        if ( i > 0u && IdEqual( pSortedOut->pData[i - 1u], id ) ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        if ( FindMeshIndex( document, id ) >= document.meshes.nCount ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t ValidateRootOrder(
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &removals,
    common::span_t<const mesh_source_description_t *const> replacements,
    common::span_t<const geometry_source_id_t> finalOrder ) noexcept
{
    if ( removals.nCount > document.meshes.nCount ||
         replacements.nCount > common::CY_USIZE_MAX -
                                   ( document.meshes.nCount - removals.nCount ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    const common::usize cFinal = document.meshes.nCount - removals.nCount +
                                 replacements.nCount;
    if ( finalOrder.nCount != cFinal ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    for ( common::usize i = 0u; i < replacements.nCount; ++i ) {
        if ( replacements.pData[i] == nullptr ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        const geometry_source_id_t root = replacements.pData[i]->sourceId;
        if ( !GeometrySourceId_IsValid( root ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        for ( common::usize j = 0u; j < i; ++j ) {
            if ( replacements.pData[j] != nullptr &&
                 replacements.pData[j]->sourceId.value == root.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }
    }

    for ( common::usize i = 0u; i < finalOrder.nCount; ++i ) {
        const geometry_source_id_t root = finalOrder.pData[i];
        if ( !GeometrySourceId_IsValid( root ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        for ( common::usize j = 0u; j < i; ++j ) {
            if ( finalOrder.pData[j].value == root.value ) {
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }

        const common::usize iReplacement =
            FindReplacementIndex( replacements, root );
        if ( iReplacement < replacements.nCount ) {
            continue;
        }
        const common::usize iStored = FindMeshIndex( document, root );
        if ( iStored >= document.meshes.nCount ||
             SortedContains( removals, root ) ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    return geometry_status_t::OK;
}

void DestroyMeshes(
    common::vector_t<mesh_source_t *> *pMeshes,
    const common::allocator_t *pAllocator ) noexcept
{
    for ( common::usize i = 0u; i < pMeshes->nCount; ++i ) {
        FreeMesh( pAllocator, pMeshes->pData[i] );
    }
    common::Vector_Shutdown( pMeshes );
}

geometry_status_t BuildReplacements(
    const geometry_document_t &document,
    common::span_t<const mesh_source_description_t *const> descriptions,
    common::vector_t<mesh_source_t *> *pMeshesOut,
    common::vector_t<geometry_source_id_t> *pIdsOut,
    mesh_totals_t *pTotalsOut ) noexcept
{
    if ( !common::Vector_Init(
             pMeshesOut, document.pAllocator, descriptions.nCount ) ||
         !common::Vector_Init( pIdsOut, document.pAllocator ) ) {
        common::Vector_Shutdown( pIdsOut );
        common::Vector_Shutdown( pMeshesOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    mesh_totals_t totals{};
    for ( common::usize i = 0u; i < descriptions.nCount; ++i ) {
        mesh_source_t *pMesh = AllocateMesh( document.pAllocator );
        if ( pMesh == nullptr ) {
            DestroyMeshes( pMeshesOut, document.pAllocator );
            common::Vector_Shutdown( pIdsOut );
            return geometry_status_t::ALLOCATION_FAILED;
        }
        mesh_source_validation_t fault{};
        geometry_status_t status = MeshSource_TryBuild(
            descriptions.pData[i], document.pAllocator, pMesh, &fault );
        if ( status != geometry_status_t::OK ) {
            FreeMesh( document.pAllocator, pMesh );
            DestroyMeshes( pMeshesOut, document.pAllocator );
            common::Vector_Shutdown( pIdsOut );
            return status;
        }
        if ( !common::Vector_PushBack( pMeshesOut, pMesh ) ) {
            FreeMesh( document.pAllocator, pMesh );
            DestroyMeshes( pMeshesOut, document.pAllocator );
            common::Vector_Shutdown( pIdsOut );
            return geometry_status_t::ALLOCATION_FAILED;
        }
        status = MeshSource_TryCollectSourceIds( pMesh, pIdsOut );
        if ( status != geometry_status_t::OK ) {
            DestroyMeshes( pMeshesOut, document.pAllocator );
            common::Vector_Shutdown( pIdsOut );
            return status;
        }
        AddMeshTotals( &totals, *pMesh );
    }

    SortIds( pIdsOut );
    for ( common::usize i = 1u; i < pIdsOut->nCount; ++i ) {
        if ( IdEqual( pIdsOut->pData[i - 1u], pIdsOut->pData[i] ) ) {
            DestroyMeshes( pMeshesOut, document.pAllocator );
            common::Vector_Shutdown( pIdsOut );
            return geometry_status_t::IDENTITY_CONFLICT;
        }
    }
    *pTotalsOut = totals;
    return geometry_status_t::OK;
}

geometry_status_t CollectRemovedIds(
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &roots,
    common::vector_t<geometry_source_id_t> *pIdsOut,
    mesh_totals_t *pTotalsOut ) noexcept
{
    if ( !common::Vector_Init( pIdsOut, document.pAllocator ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    mesh_totals_t totals{};
    for ( common::usize i = 0u; i < roots.nCount; ++i ) {
        const mesh_source_t *pMesh = document.meshes.pData[
            FindMeshIndex( document, roots.pData[i] )];
        const geometry_status_t status =
            MeshSource_TryCollectSourceIds( pMesh, pIdsOut );
        if ( status != geometry_status_t::OK ) {
            common::Vector_Shutdown( pIdsOut );
            return status;
        }
        AddMeshTotals( &totals, *pMesh );
    }
    SortIds( pIdsOut );
    *pTotalsOut = totals;
    return geometry_status_t::OK;
}

geometry_status_t PreflightOutputIds(
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &removedIds,
    const common::vector_t<geometry_source_id_t> &outputIds,
    common::usize *pFreshCountOut ) noexcept
{
    common::usize cFresh = 0u;
    for ( common::usize i = 0u; i < outputIds.nCount; ++i ) {
        const geometry_source_id_t id = outputIds.pData[i];
        if ( GeometrySourceIdRegistry_Contains( &document.sourceIds, id ) &&
             !SortedContains( removedIds, id ) ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        if ( common::HashSet_Contains( &document.sourceIds.claimedIds, id ) ) {
            continue;
        }
        if ( !GeometrySourceId_IsValid( document.sourceIds.allocator.next ) ) {
            return geometry_status_t::INSUFFICIENT_CAPACITY;
        }
        if ( id.value < document.sourceIds.allocator.next.value ) {
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        ++cFresh;
    }
    const common::usize cClaimed =
        GeometrySourceIdRegistry_ClaimedCount( &document.sourceIds );
    if ( cFresh > document.sourceIds.cEntriesMax - cClaimed ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    *pFreshCountOut = cFresh;
    return geometry_status_t::OK;
}

geometry_status_t PrepareRegistry(
    const geometry_document_t &document,
    const common::vector_t<geometry_source_id_t> &removedIds,
    const common::vector_t<geometry_source_id_t> &outputIds,
    common::usize cFresh,
    geometry_source_id_registry_t *pRegistryOut ) noexcept
{
    geometry_status_t status = GeometrySourceIdRegistry_TryClone(
        &document.sourceIds, pRegistryOut );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    status = GeometrySourceIdRegistry_Reserve(
        pRegistryOut,
        GeometrySourceIdRegistry_ClaimedCount( pRegistryOut ) + cFresh );
    if ( status != geometry_status_t::OK ) {
        GeometrySourceIdRegistry_Shutdown( pRegistryOut );
        return status;
    }

    const bool bLoadOpen = pRegistryOut->bLoadRegistrationOpen;
    for ( common::usize i = 0u; i < removedIds.nCount; ++i ) {
        if ( GeometrySourceIdRegistry_Release(
                 pRegistryOut, removedIds.pData[i] ) !=
             geometry_status_t::OK ) {
            GeometrySourceIdRegistry_Shutdown( pRegistryOut );
            return geometry_status_t::CORRUPT_STATE;
        }
    }
    for ( common::usize i = 0u; i < outputIds.nCount; ++i ) {
        const geometry_source_id_t id = outputIds.pData[i];
        if ( common::HashSet_Contains( &pRegistryOut->liveIds, id ) ) {
            GeometrySourceIdRegistry_Shutdown( pRegistryOut );
            return geometry_status_t::IDENTITY_CONFLICT;
        }
        if ( common::HashSet_Contains( &pRegistryOut->claimedIds, id ) ) {
            status = GeometrySourceIdRegistry_RestoreRetired(
                pRegistryOut, id );
        } else {
            pRegistryOut->bLoadRegistrationOpen = true;
            status = GeometrySourceIdRegistry_Register( pRegistryOut, id );
        }
        if ( status != geometry_status_t::OK ) {
            GeometrySourceIdRegistry_Shutdown( pRegistryOut );
            return status;
        }
    }
    pRegistryOut->bLoadRegistrationOpen = bLoadOpen;
    if ( !GeometrySourceIdRegistry_ValidateDeep( pRegistryOut ) ) {
        GeometrySourceIdRegistry_Shutdown( pRegistryOut );
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

void SwapMeshVectors(
    common::vector_t<mesh_source_t *> &a,
    common::vector_t<mesh_source_t *> &b ) noexcept
{
    std::swap( a.pData, b.pData );
    std::swap( a.nCount, b.nCount );
    std::swap( a.nCapacity, b.nCapacity );
    std::swap( a.pAllocator, b.pAllocator );
}

void SwapIdSets(
    geometry_source_id_set_t &a,
    geometry_source_id_set_t &b ) noexcept
{
    std::swap( a.pSlots, b.pSlots );
    std::swap( a.nCount, b.nCount );
    std::swap( a.nCapacity, b.nCapacity );
    std::swap( a.pAllocator, b.pAllocator );
}

void SwapRegistries(
    geometry_source_id_registry_t &a,
    geometry_source_id_registry_t &b ) noexcept
{
    SwapIdSets( a.claimedIds, b.claimedIds );
    SwapIdSets( a.liveIds, b.liveIds );
    std::swap( a.allocator, b.allocator );
    std::swap( a.cEntriesMax, b.cEntriesMax );
    std::swap( a.pAllocator, b.pAllocator );
    std::swap( a.bLoadRegistrationOpen, b.bLoadRegistrationOpen );
}

} // namespace

geometry_status_t GeometryDocument_TryPublishMeshSetExact(
    geometry_document_t *pDocument,
    common::span_t<const geometry_source_id_t> removeMeshIds,
    common::span_t<const mesh_source_description_t *const> replacementMeshes,
    common::span_t<const geometry_source_id_t> finalMeshOrder ) noexcept
{
    geometry_status_t status = CheckDocument( pDocument );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !common::Span_IsValid( removeMeshIds ) ||
         !common::Span_IsValid( replacementMeshes ) ||
         !common::Span_IsValid( finalMeshOrder ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    mesh_totals_t currentTotals{};
    status = ValidateStoredOwnership( *pDocument, &currentTotals );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    common::vector_t<geometry_source_id_t> removalRoots{};
    status = PrepareRemovalRoots(
        *pDocument, removeMeshIds, &removalRoots );
    if ( status == geometry_status_t::OK ) {
        status = ValidateRootOrder(
            *pDocument, removalRoots, replacementMeshes, finalMeshOrder );
    }
    if ( status != geometry_status_t::OK ) {
        common::Vector_Shutdown( &removalRoots );
        return status;
    }

    common::vector_t<mesh_source_t *> outputs{};
    common::vector_t<geometry_source_id_t> outputIds{};
    mesh_totals_t outputTotals{};
    status = BuildReplacements(
        *pDocument,
        replacementMeshes,
        &outputs,
        &outputIds,
        &outputTotals );
    if ( status != geometry_status_t::OK ) {
        common::Vector_Shutdown( &removalRoots );
        return status;
    }

    common::vector_t<geometry_source_id_t> removedIds{};
    mesh_totals_t removedTotals{};
    status = CollectRemovedIds(
        *pDocument, removalRoots, &removedIds, &removedTotals );
    if ( status != geometry_status_t::OK ) {
        DestroyMeshes( &outputs, pDocument->pAllocator );
        common::Vector_Shutdown( &outputIds );
        common::Vector_Shutdown( &removalRoots );
        return status;
    }

    mesh_totals_t finalTotals{
        currentTotals.cVertices - removedTotals.cVertices + outputTotals.cVertices,
        currentTotals.cHalfEdges - removedTotals.cHalfEdges + outputTotals.cHalfEdges,
        currentTotals.cEdges - removedTotals.cEdges + outputTotals.cEdges,
        currentTotals.cLoops - removedTotals.cLoops + outputTotals.cLoops,
        currentTotals.cFaces - removedTotals.cFaces + outputTotals.cFaces,
        currentTotals.cShells - removedTotals.cShells + outputTotals.cShells
    };
    if ( !TotalsWithinLimits( finalTotals, pDocument->policy.limits ) ) {
        status = geometry_status_t::LIMIT_EXCEEDED;
    }

    common::usize cFresh = 0u;
    if ( status == geometry_status_t::OK ) {
        status = PreflightOutputIds(
            *pDocument, removedIds, outputIds, &cFresh );
    }

    geometry_source_id_registry_t preparedRegistry{};
    if ( status == geometry_status_t::OK ) {
        status = PrepareRegistry(
            *pDocument,
            removedIds,
            outputIds,
            cFresh,
            &preparedRegistry );
    }

    common::vector_t<mesh_source_t *> preparedMeshes{};
    if ( status == geometry_status_t::OK &&
         !common::Vector_Init(
             &preparedMeshes, pDocument->pAllocator, finalMeshOrder.nCount ) ) {
        status = geometry_status_t::ALLOCATION_FAILED;
    }
    for ( common::usize i = 0u;
          status == geometry_status_t::OK && i < finalMeshOrder.nCount;
          ++i ) {
        const geometry_source_id_t root = finalMeshOrder.pData[i];
        const common::usize iOutput =
            FindReplacementIndex( replacementMeshes, root );
        mesh_source_t *pMesh = iOutput < replacementMeshes.nCount
            ? outputs.pData[iOutput]
            : pDocument->meshes.pData[FindMeshIndex( *pDocument, root )];
        if ( !common::Vector_PushBack( &preparedMeshes, pMesh ) ) {
            status = geometry_status_t::ALLOCATION_FAILED;
        }
    }

    if ( status != geometry_status_t::OK ) {
        common::Vector_Shutdown( &preparedMeshes );
        GeometrySourceIdRegistry_Shutdown( &preparedRegistry );
        common::Vector_Shutdown( &removedIds );
        DestroyMeshes( &outputs, pDocument->pAllocator );
        common::Vector_Shutdown( &outputIds );
        common::Vector_Shutdown( &removalRoots );
        return status;
    }

    // Publish: no fallible operation remains.
    SwapMeshVectors( pDocument->meshes, preparedMeshes );
    SwapRegistries( pDocument->sourceIds, preparedRegistry );

    // preparedMeshes owns the old pointer vector after the swap. Retained
    // objects are also present in the new vector and must keep their address.
    for ( common::usize i = 0u; i < preparedMeshes.nCount; ++i ) {
        mesh_source_t *pOld = preparedMeshes.pData[i];
        if ( SortedContains( removalRoots, pOld->sourceId ) ) {
            FreeMesh( pDocument->pAllocator, pOld );
        }
    }
    common::Vector_Shutdown( &preparedMeshes );
    GeometrySourceIdRegistry_Shutdown( &preparedRegistry );

    // Output object ownership moved into the document; only release the
    // temporary pointer array.
    common::Vector_Shutdown( &outputs );
    common::Vector_Shutdown( &removedIds );
    common::Vector_Shutdown( &outputIds );
    common::Vector_Shutdown( &removalRoots );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
