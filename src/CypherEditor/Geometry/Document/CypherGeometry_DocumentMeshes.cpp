//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentMeshes.cpp
//  Purpose: Implements document mesh ownership and the single atomic
//           mesh-change step shared by add, remove, and replace.
//  Details: Identity activation mirrors brush add exactly: a previously
//           claimed (retired) ID is restored, a fresh ID is registered with
//           load registration opened for that one call. Keeping one rule for
//           both kinds is what makes cross-kind conflicts impossible: every
//           live ID in the document is in the same registry.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentMeshes.h"

#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr usize kNoMesh = ~static_cast<usize>( 0u );

mesh_source_t *AllocateMesh( const allocator_t *pAllocator ) noexcept
{
    void *pMemory = Allocator_AllocateZeroed( pAllocator, sizeof( mesh_source_t ), alignof( mesh_source_t ) );
    if ( pMemory == nullptr ) { return nullptr; }
    return new ( pMemory ) mesh_source_t{};
}

void FreeMesh( const allocator_t *pAllocator, mesh_source_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) { return; }
    MeshSource_Shutdown( pMesh );
    pMesh->~mesh_source_t();
    Allocator_Free( pAllocator, pMesh, sizeof( mesh_source_t ), alignof( mesh_source_t ) );
}

usize FindMeshIndex( const geometry_document_t *pDocument, geometry_source_id_t meshId ) noexcept
{
    for ( usize i = 0u; i < pDocument->meshes.nCount; ++i ) {
        const mesh_source_t *pMesh = pDocument->meshes.pData[i];
        if ( pMesh != nullptr && pMesh->sourceId.value == meshId.value ) { return i; }
    }
    return kNoMesh;
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
    u64 cVertices{ 0u };
    u64 cHalfEdges{ 0u };
    u64 cEdges{ 0u };
    u64 cLoops{ 0u };
    u64 cFaces{ 0u };
    u64 cShells{ 0u };
};

void AddTotals( mesh_totals_t *pT, const mesh_source_t *pMesh, bool bAdd ) noexcept
{
    const u64 v = GenerationPool_Count( &pMesh->mesh.vertices );
    const u64 h = GenerationPool_Count( &pMesh->mesh.halfEdges );
    const u64 e = GenerationPool_Count( &pMesh->mesh.edges );
    const u64 l = GenerationPool_Count( &pMesh->mesh.loops );
    const u64 f = GenerationPool_Count( &pMesh->mesh.faces );
    const u64 s = GenerationPool_Count( &pMesh->mesh.shells );
    if ( bAdd ) {
        pT->cVertices += v;
        pT->cHalfEdges += h;
        pT->cEdges += e;
        pT->cLoops += l;
        pT->cFaces += f;
        pT->cShells += s;
    } else {
        pT->cVertices -= v;
        pT->cHalfEdges -= h;
        pT->cEdges -= e;
        pT->cLoops -= l;
        pT->cFaces -= f;
        pT->cShells -= s;
    }
}

struct activation_t {
    geometry_source_id_t id;
    bool bWasClaimed;
};

// Undo `cActivated` activations (in reverse) and `cReleased` releases.
bool RollBack(
    geometry_source_id_registry_t *pRegistry,
    const vector_t<activation_t> &activations,
    usize cActivated,
    const vector_t<geometry_source_id_t> &released,
    usize cReleased,
    geometry_source_id_allocator_t allocatorBefore,
    bool bLoadOpenBefore ) noexcept
{
    bool bOk = true;
    while ( cActivated > 0u ) {
        const activation_t &a = activations.pData[--cActivated];
        if ( a.bWasClaimed ) {
            bOk = GeometrySourceIdRegistry_Release( pRegistry, a.id ) == geometry_status_t::OK && bOk;
        } else {
            const bool bLive = HashSet_Erase( &pRegistry->liveIds, a.id );
            const bool bClaim = HashSet_Erase( &pRegistry->claimedIds, a.id );
            bOk = bLive && bClaim && bOk;
        }
    }
    while ( cReleased > 0u ) {
        bOk = GeometrySourceIdRegistry_RestoreRetired( pRegistry, released.pData[--cReleased] ) ==
                  geometry_status_t::OK &&
              bOk;
    }
    pRegistry->allocator = allocatorBefore;
    pRegistry->bLoadRegistrationOpen = bLoadOpenBefore;
    return bOk;
}

// The shared atomic step. iOld == kNoMesh means "add"; pNew == nullptr
// means "remove".
geometry_status_t CommitMeshChange( geometry_document_t *pDocument, usize iOld, const mesh_source_t *pNew ) noexcept
{
    const allocator_t *pAllocator = pDocument->pAllocator;
    geometry_source_id_registry_t *pRegistry = &pDocument->sourceIds;
    const mesh_source_t *pOld = iOld != kNoMesh ? pDocument->meshes.pData[iOld] : nullptr;

    if ( pNew != nullptr ) {
        const mesh_source_validation_t v = MeshSource_Validate( pNew, pAllocator );
        if ( v.fault != mesh_source_fault_t::NONE ) { return StatusFromFault( v.fault ); }
    }

    // Identity sets (each sorted ascending by Collect).
    vector_t<geometry_source_id_t> oldIds{}, newIds{}, toRelease{};
    vector_t<activation_t> toActivate{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &oldIds );
        Vector_Shutdown( &newIds );
        Vector_Shutdown( &toRelease );
        Vector_Shutdown( &toActivate );
    };
    if ( !Vector_Init( &oldIds, pAllocator ) || !Vector_Init( &newIds, pAllocator ) ||
         !Vector_Init( &toRelease, pAllocator ) || !Vector_Init( &toActivate, pAllocator ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_status_t st = geometry_status_t::OK;
    if ( pOld != nullptr ) { st = MeshSource_TryCollectSourceIds( pOld, &oldIds ); }
    if ( st == geometry_status_t::OK && pNew != nullptr ) { st = MeshSource_TryCollectSourceIds( pNew, &newIds ); }
    if ( st != geometry_status_t::OK ) {
        cleanup();
        return st;
    }

    // Sorted-set difference.
    usize i = 0u, j = 0u;
    usize cFresh = 0u;
    bool bPushed = true;
    while ( ( i < oldIds.nCount || j < newIds.nCount ) && st == geometry_status_t::OK ) {
        const bool bTakeOld =
            j >= newIds.nCount || ( i < oldIds.nCount && oldIds.pData[i].value < newIds.pData[j].value );
        const bool bTakeNew =
            i >= oldIds.nCount || ( j < newIds.nCount && newIds.pData[j].value < oldIds.pData[i].value );
        if ( bTakeOld ) {
            bPushed = Vector_PushBack( &toRelease, oldIds.pData[i] );
            ++i;
        } else if ( bTakeNew ) {
            const geometry_source_id_t id = newIds.pData[j];
            ++j;
            // Not part of the mesh being replaced, so a live hit belongs to
            // some other document object.
            if ( HashSet_Contains( &pRegistry->liveIds, id ) ) {
                st = geometry_status_t::IDENTITY_CONFLICT;
                break;
            }
            const bool bClaimed = HashSet_Contains( &pRegistry->claimedIds, id );
            cFresh += bClaimed ? 0u : 1u;
            bPushed = Vector_PushBack( &toActivate, activation_t{ id, bClaimed } );
        } else {
            ++i;
            ++j; // kept identity: nothing to do
        }
        if ( !bPushed ) { st = geometry_status_t::ALLOCATION_FAILED; }
    }
    if ( st != geometry_status_t::OK ) {
        cleanup();
        return st;
    }

    // Document-wide limits after the change.
    mesh_totals_t totals{};
    for ( usize k = 0u; k < pDocument->meshes.nCount; ++k ) { AddTotals( &totals, pDocument->meshes.pData[k], true ); }
    if ( pOld != nullptr ) { AddTotals( &totals, pOld, false ); }
    if ( pNew != nullptr ) { AddTotals( &totals, pNew, true ); }
    const geometry_limit_policy_t &limits = pDocument->policy.limits;
    if ( totals.cVertices > limits.cVerticesMax || totals.cHalfEdges > limits.cHalfEdgesMax ||
         totals.cEdges > limits.cEdgesMax || totals.cLoops > limits.cLoopsMax ||
         totals.cFaces > limits.cFacesMax || totals.cShells > limits.cShellsMax ) {
        cleanup();
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // Every fallible allocation before the first registry change.
    mesh_source_t *pCopy = nullptr;
    if ( pNew != nullptr ) {
        pCopy = AllocateMesh( pAllocator );
        if ( pCopy == nullptr ) {
            cleanup();
            return geometry_status_t::ALLOCATION_FAILED;
        }
        st = MeshSource_TryClone( pNew, pAllocator, pCopy );
        if ( st != geometry_status_t::OK ) {
            FreeMesh( pAllocator, pCopy );
            cleanup();
            return st;
        }
    }
    if ( pOld == nullptr && !Vector_Reserve( &pDocument->meshes, pDocument->meshes.nCount + 1u ) ) {
        FreeMesh( pAllocator, pCopy );
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const usize cClaimed = GeometrySourceIdRegistry_ClaimedCount( pRegistry );
    if ( cFresh > pRegistry->cEntriesMax - cClaimed ) {
        FreeMesh( pAllocator, pCopy );
        cleanup();
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    st = GeometrySourceIdRegistry_Reserve( pRegistry, cClaimed + cFresh );
    if ( st != geometry_status_t::OK ) {
        FreeMesh( pAllocator, pCopy );
        cleanup();
        return st;
    }

    // Registry changes, with exact rollback.
    const geometry_source_id_allocator_t allocatorBefore = pRegistry->allocator;
    const bool bLoadOpenBefore = pRegistry->bLoadRegistrationOpen;
    usize cReleased = 0u, cActivated = 0u;
    for ( ; cReleased < toRelease.nCount && st == geometry_status_t::OK; ++cReleased ) {
        st = GeometrySourceIdRegistry_Release( pRegistry, toRelease.pData[cReleased] );
        if ( st != geometry_status_t::OK ) { break; }
    }
    for ( ; st == geometry_status_t::OK && cActivated < toActivate.nCount; ++cActivated ) {
        const activation_t &a = toActivate.pData[cActivated];
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
            RollBack( pRegistry, toActivate, cActivated, toRelease, cReleased, allocatorBefore, bLoadOpenBefore );
        FreeMesh( pAllocator, pCopy );
        cleanup();
        return bRolledBack ? st : geometry_status_t::CORRUPT_STATE;
    }
    pRegistry->bLoadRegistrationOpen = bLoadOpenBefore;

    // Publish (allocation-free).
    if ( pOld == nullptr ) {
        (void)Vector_PushBack( &pDocument->meshes, pCopy );
    } else if ( pCopy != nullptr ) {
        FreeMesh( pAllocator, pDocument->meshes.pData[iOld] );
        pDocument->meshes.pData[iOld] = pCopy;
    } else {
        FreeMesh( pAllocator, pDocument->meshes.pData[iOld] );
        Vector_EraseSwap( &pDocument->meshes, iOld );
    }
    cleanup();
    return geometry_status_t::OK;
}

geometry_status_t CheckDocument( const geometry_document_t *pDocument ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !Vector_IsValid( &pDocument->meshes ) || !GeometrySourceIdRegistry_IsValid( &pDocument->sourceIds ) ||
         !GeometrySourceIdRegistry_IsInitialized( &pDocument->sourceIds ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

} // namespace

usize GeometryDocument_MeshCount( const geometry_document_t *pDocument ) noexcept
{
    return GeometryDocument_IsInitialized( pDocument ) ? pDocument->meshes.nCount : 0u;
}

const mesh_source_t *GeometryDocument_MeshAt( const geometry_document_t *pDocument, usize iMesh ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) || iMesh >= pDocument->meshes.nCount ) { return nullptr; }
    return pDocument->meshes.pData[iMesh];
}

const mesh_source_t *GeometryDocument_FindMesh( const geometry_document_t *pDocument, geometry_source_id_t meshId ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) || !GeometrySourceId_IsValid( meshId ) ) { return nullptr; }
    const usize i = FindMeshIndex( pDocument, meshId );
    return i == kNoMesh ? nullptr : pDocument->meshes.pData[i];
}

geometry_status_t GeometryDocument_TryAddMesh( geometry_document_t *pDocument, const mesh_source_t *pMesh ) noexcept
{
    const geometry_status_t check = CheckDocument( pDocument );
    if ( check != geometry_status_t::OK ) { return check; }
    if ( !MeshSource_IsInitialized( pMesh ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( FindMeshIndex( pDocument, pMesh->sourceId ) != kNoMesh ) { return geometry_status_t::IDENTITY_CONFLICT; }
    return CommitMeshChange( pDocument, kNoMesh, pMesh );
}

geometry_status_t GeometryDocument_TryRemoveMesh( geometry_document_t *pDocument, geometry_source_id_t meshId ) noexcept
{
    const geometry_status_t check = CheckDocument( pDocument );
    if ( check != geometry_status_t::OK ) { return check; }
    if ( !GeometrySourceId_IsValid( meshId ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const usize i = FindMeshIndex( pDocument, meshId );
    if ( i == kNoMesh ) { return geometry_status_t::INVALID_ARGUMENT; }
    return CommitMeshChange( pDocument, i, nullptr );
}

geometry_status_t GeometryDocument_TryReplaceMesh( geometry_document_t *pDocument, const mesh_source_t *pMesh ) noexcept
{
    const geometry_status_t check = CheckDocument( pDocument );
    if ( check != geometry_status_t::OK ) { return check; }
    if ( !MeshSource_IsInitialized( pMesh ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const usize i = FindMeshIndex( pDocument, pMesh->sourceId );
    if ( i == kNoMesh ) { return geometry_status_t::INVALID_ARGUMENT; }
    return CommitMeshChange( pDocument, i, pMesh );
}

void GeometryDocument_FreeAllMeshes( geometry_document_t *pDocument ) noexcept
{
    if ( pDocument == nullptr || pDocument->pAllocator == nullptr ) { return; }
    for ( usize i = 0u; i < pDocument->meshes.nCount; ++i ) {
        FreeMesh( pDocument->pAllocator, pDocument->meshes.pData[i] );
    }
    Vector_Shutdown( &pDocument->meshes );
}

} // namespace cypher::editor::geometry
