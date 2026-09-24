//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshTransaction.cpp
//  Purpose: Implements mesh edit transactions and whole-mesh publish
//           commands.
//  Details: Fresh IDs at commit come from a transaction-local copy of the
//           registry allocator. That cursor survives failed commit attempts,
//           so further edits cannot reuse IDs already assigned in the private
//           working copy. The document registry advances only when the atomic
//           replace publishes those IDs.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshTransaction.h"

#include <algorithm>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

void EndTransaction( geometry_mesh_transaction_t *pTransaction ) noexcept
{
    MeshSource_Shutdown( &pTransaction->working );
    MeshSourceDescription_Shutdown( &pTransaction->baseline );
    pTransaction->pDocument = nullptr;
    pTransaction->meshId = GEOMETRY_SOURCE_ID_INVALID;
    pTransaction->baselineRevision = GEOMETRY_REVISION_INITIAL;
    pTransaction->tentativeIds = {};
    pTransaction->bActive = false;
}

// Replaces the delta's contents: kind, ID, and the two descriptions.
void FillDelta(
    geometry_mesh_delta_t *pDelta,
    geometry_mesh_delta_kind_t kind,
    geometry_source_id_t meshId,
    mesh_source_description_t *pBefore,
    mesh_source_description_t *pAfter ) noexcept
{
    pDelta->kind = kind;
    pDelta->meshId = meshId;
    if ( pBefore ) {
        MeshSourceDescription_Move( &pDelta->before, pBefore );
    } else {
        MeshSourceDescription_Clear( &pDelta->before, GEOMETRY_SOURCE_ID_INVALID );
    }
    if ( pAfter ) {
        MeshSourceDescription_Move( &pDelta->after, pAfter );
    } else {
        MeshSourceDescription_Clear( &pDelta->after, GEOMETRY_SOURCE_ID_INVALID );
    }
}

// Builds a complete delta using the allocator chosen when pDestination was
// initialized. This must happen before the document edit is published: an
// undo record is part of the command's success contract, and allocation
// failure must leave both document and destination delta unchanged.
geometry_status_t PrepareDelta(
    const geometry_mesh_delta_t *pDestination,
    geometry_mesh_delta_kind_t kind,
    geometry_source_id_t meshId,
    const mesh_source_description_t *pBefore,
    const mesh_source_description_t *pAfter,
    geometry_mesh_delta_t *pPrepared ) noexcept
{
    const allocator_t *pAllocator = pDestination->before.vertices.pAllocator;
    geometry_status_t st = GeometryMeshDelta_Init( pPrepared, pAllocator );
    if ( st == geometry_status_t::OK && pBefore != nullptr ) {
        st = MeshSourceDescription_TryCopy( &pPrepared->before, pBefore );
    }
    if ( st == geometry_status_t::OK && pAfter != nullptr ) {
        st = MeshSourceDescription_TryCopy( &pPrepared->after, pAfter );
    }
    if ( st != geometry_status_t::OK ) {
        GeometryMeshDelta_Shutdown( pPrepared );
        return st;
    }
    pPrepared->kind = kind;
    pPrepared->meshId = meshId;
    return geometry_status_t::OK;
}

void PublishPreparedDelta(
    geometry_mesh_delta_t *pDestination,
    geometry_mesh_delta_t *pPrepared ) noexcept
{
    FillDelta( pDestination, pPrepared->kind, pPrepared->meshId,
               &pPrepared->before, &pPrepared->after );
    GeometryMeshDelta_Shutdown( pPrepared );
}

bool DescriptionContainsVertexId(
    const mesh_source_description_t *pDescription,
    geometry_source_id_t id ) noexcept
{
    if ( pDescription == nullptr ) { return false; }
    if ( pDescription->vertices.nCount > 0u ) {
        const mesh_source_vertex_t *pBegin = pDescription->vertices.pData;
        const mesh_source_vertex_t *pEnd = pBegin + pDescription->vertices.nCount;
        const mesh_source_vertex_t *pFound = std::lower_bound(
            pBegin, pEnd, id.value,
            []( const mesh_source_vertex_t &vertex, u64 value ) noexcept {
                return vertex.sourceId.value < value;
            } );
        if ( pFound != pEnd && pFound->sourceId.value == id.value ) { return true; }
    }
    return false;
}

bool DescriptionContainsFaceId(
    const mesh_source_description_t *pDescription,
    geometry_source_id_t id ) noexcept
{
    if ( pDescription == nullptr ) { return false; }
    if ( pDescription->faces.nCount > 0u ) {
        const mesh_source_face_t *pBegin = pDescription->faces.pData;
        const mesh_source_face_t *pEnd = pBegin + pDescription->faces.nCount;
        const mesh_source_face_t *pFound = std::lower_bound(
            pBegin, pEnd, id.value,
            []( const mesh_source_face_t &face, u64 value ) noexcept {
                return face.sourceId.value < value;
            } );
        if ( pFound != pEnd && pFound->sourceId.value == id.value ) { return true; }
    }
    return false;
}

// Normal editor commands may retain IDs from their own baseline and may add
// truly fresh IDs. A claimed-but-retired ID owned by some other history entry
// is reserved for explicit delta replay; accepting it here would let one edit
// steal another object's undo identity.
bool IntroducesClaimedId(
    const geometry_source_id_registry_t *pRegistry,
    const mesh_source_description_t *pBaseline,
    const mesh_source_description_t &after ) noexcept
{
    auto conflicts = [&]( geometry_source_id_t id ) noexcept {
        return HashSet_Contains( &pRegistry->claimedIds, id );
    };
    if ( ( pBaseline == nullptr || pBaseline->sourceId.value != after.sourceId.value ) &&
         conflicts( after.sourceId ) ) { return true; }
    for ( usize i = 0u; i < after.vertices.nCount; ++i ) {
        const geometry_source_id_t id = after.vertices.pData[i].sourceId;
        if ( !DescriptionContainsVertexId( pBaseline, id ) && conflicts( id ) ) { return true; }
    }
    for ( usize i = 0u; i < after.faces.nCount; ++i ) {
        const geometry_source_id_t id = after.faces.pData[i].sourceId;
        if ( !DescriptionContainsFaceId( pBaseline, id ) && conflicts( id ) ) { return true; }
    }
    return false;
}

bool IdInTentativeRange(
    geometry_source_id_t id,
    geometry_source_id_t first,
    geometry_source_id_t onePastLast ) noexcept
{
    if ( !GeometrySourceId_IsValid( first ) || id.value < first.value ) { return false; }
    // An invalid next value is the allocator's exhausted state. In that case
    // the issued range extends through the largest representable source ID.
    return !GeometrySourceId_IsValid( onePastLast ) || id.value < onePastLast.value;
}

// IDs absent from the baseline must have been issued by this transaction's
// private allocator. This prevents callers from writing arbitrary unclaimed
// values into newly created elements while still allowing tentative IDs to
// survive a failed commit and a later retry.
bool IntroducesUnallocatedId(
    const mesh_source_description_t &baseline,
    const mesh_source_description_t &after,
    geometry_source_id_t firstTentative,
    geometry_source_id_t onePastLastTentative ) noexcept
{
    if ( after.sourceId.value != baseline.sourceId.value ) { return true; }
    for ( usize i = 0u; i < after.vertices.nCount; ++i ) {
        const geometry_source_id_t id = after.vertices.pData[i].sourceId;
        if ( !DescriptionContainsVertexId( &baseline, id ) &&
             !IdInTentativeRange( id, firstTentative, onePastLastTentative ) ) { return true; }
    }
    for ( usize i = 0u; i < after.faces.nCount; ++i ) {
        const geometry_source_id_t id = after.faces.pData[i].sourceId;
        if ( !DescriptionContainsFaceId( &baseline, id ) &&
             !IdInTentativeRange( id, firstTentative, onePastLastTentative ) ) { return true; }
    }
    return false;
}

} // namespace

geometry_status_t GeometryMeshTransaction_Begin(
    geometry_mesh_transaction_t *pTransaction,
    geometry_document_t *pDocument,
    geometry_source_id_t meshId ) noexcept
{
    if ( pTransaction == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pTransaction->bActive ) { return geometry_status_t::TRANSACTION_ACTIVE; }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const mesh_source_t *pMesh = GeometryDocument_FindMesh( pDocument, meshId );
    if ( pMesh == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }

    geometry_status_t st = MeshSourceDescription_Init( &pTransaction->baseline, pDocument->pAllocator, meshId );
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( pMesh, &pTransaction->baseline ); }
    if ( st == geometry_status_t::OK ) {
        // Building the working copy from the baseline (rather than cloning
        // separately) guarantees the two start identical.
        st = MeshSource_TryBuild( &pTransaction->baseline, pDocument->pAllocator, &pTransaction->working );
    }
    if ( st != geometry_status_t::OK ) {
        EndTransaction( pTransaction );
        return st;
    }
    pTransaction->pDocument = pDocument;
    pTransaction->meshId = meshId;
    pTransaction->baselineRevision = pDocument->revision;
    pTransaction->tentativeIds = pDocument->sourceIds.allocator;
    pTransaction->bActive = true;
    return geometry_status_t::OK;
}

mesh_source_t *GeometryMeshTransaction_Working( geometry_mesh_transaction_t *pTransaction ) noexcept
{
    return pTransaction != nullptr && pTransaction->bActive ? &pTransaction->working : nullptr;
}

geometry_status_t GeometryMeshTransaction_Commit(
    geometry_mesh_transaction_t *pTransaction,
    geometry_mesh_delta_t *pDeltaOut,
    geometry_revision_t *pNewRevisionOut,
    bool *pbChangedOut ) noexcept
{
    if ( pbChangedOut ) { *pbChangedOut = false; }
    if ( pTransaction == nullptr || !pTransaction->bActive ) { return geometry_status_t::NO_ACTIVE_TRANSACTION; }
    if ( !GeometryMeshDelta_IsInitialized( pDeltaOut ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_document_t *pDocument = pTransaction->pDocument;
    if ( pDocument->revision != pTransaction->baselineRevision ) { return geometry_status_t::STALE_REVISION; }

    geometry_status_t st = MeshSource_TryAssignMissingIds(
        &pTransaction->working, &pTransaction->tentativeIds, nullptr );
    if ( st != geometry_status_t::OK ) { return st; }

    mesh_source_description_t after{};
    st = MeshSourceDescription_Init( &after, pDocument->pAllocator, pTransaction->meshId );
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( &pTransaction->working, &after ); }
    if ( st != geometry_status_t::OK ) {
        MeshSourceDescription_Shutdown( &after );
        return st;
    }
    if ( MeshSourceDescription_Equal( &after, &pTransaction->baseline ) ) {
        MeshSourceDescription_Shutdown( &after );
        FillDelta( pDeltaOut, geometry_mesh_delta_kind_t::INVALID,
                   GEOMETRY_SOURCE_ID_INVALID, nullptr, nullptr );
        EndTransaction( pTransaction );
        if ( pNewRevisionOut ) { *pNewRevisionOut = pDocument->revision; }
        return geometry_status_t::OK;
    }
    if ( IntroducesUnallocatedId(
             pTransaction->baseline,
             after,
             pDocument->sourceIds.allocator.next,
             pTransaction->tentativeIds.next ) ||
         IntroducesClaimedId( &pDocument->sourceIds, &pTransaction->baseline, after ) ) {
        MeshSourceDescription_Shutdown( &after );
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    geometry_mesh_delta_t pendingDelta{};
    st = PrepareDelta( pDeltaOut, geometry_mesh_delta_kind_t::MESH_REPLACED,
                       pTransaction->meshId, &pTransaction->baseline, &after,
                       &pendingDelta );
    if ( st != geometry_status_t::OK ) {
        MeshSourceDescription_Shutdown( &after );
        return st;
    }

    st = GeometryDocument_TryReplaceMesh( pDocument, &pTransaction->working );
    if ( st != geometry_status_t::OK ) {
        GeometryMeshDelta_Shutdown( &pendingDelta );
        MeshSourceDescription_Shutdown( &after );
        return st;
    }
    pDocument->revision += 1u;
    PublishPreparedDelta( pDeltaOut, &pendingDelta );
    MeshSourceDescription_Shutdown( &after );
    if ( pNewRevisionOut ) { *pNewRevisionOut = pDocument->revision; }
    if ( pbChangedOut ) { *pbChangedOut = true; }
    EndTransaction( pTransaction );
    return geometry_status_t::OK;
}

geometry_status_t GeometryMeshTransaction_TryAssignIds( geometry_mesh_transaction_t *pTransaction, u32 *pAssignedOut ) noexcept
{
    if ( pAssignedOut ) { *pAssignedOut = 0u; }
    if ( pTransaction == nullptr || !pTransaction->bActive ) { return geometry_status_t::NO_ACTIVE_TRANSACTION; }
    return MeshSource_TryAssignMissingIds( &pTransaction->working, &pTransaction->tentativeIds, pAssignedOut );
}

void GeometryMeshTransaction_Cancel( geometry_mesh_transaction_t *pTransaction ) noexcept
{
    if ( pTransaction == nullptr ) { return; }
    EndTransaction( pTransaction );
}

bool GeometryMeshTransaction_IsActive( const geometry_mesh_transaction_t *pTransaction ) noexcept
{
    return pTransaction != nullptr && pTransaction->bActive;
}

geometry_status_t GeometryMeshCommand_TryAdd(
    geometry_document_t *pDocument,
    const mesh_source_description_t *pDesc,
    geometry_mesh_delta_t *pDeltaOut,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !MeshSourceDescription_IsInitialized( pDesc ) || !GeometryMeshDelta_IsInitialized( pDeltaOut ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    // Build, then describe the built mesh, so the delta holds the canonical
    // form even when the caller's description used another order.
    mesh_source_t mesh{};
    mesh_source_description_t canonical{};
    geometry_status_t st = MeshSource_TryBuild( pDesc, pDocument->pAllocator, &mesh );
    if ( st == geometry_status_t::OK ) {
        st = MeshSourceDescription_Init( &canonical, pDocument->pAllocator, pDesc->sourceId );
    }
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( &mesh, &canonical ); }
    if ( st == geometry_status_t::OK &&
         IntroducesClaimedId( &pDocument->sourceIds, nullptr, canonical ) ) {
        st = geometry_status_t::IDENTITY_CONFLICT;
    }
    geometry_mesh_delta_t pendingDelta{};
    if ( st == geometry_status_t::OK ) {
        st = PrepareDelta( pDeltaOut, geometry_mesh_delta_kind_t::MESH_ADDED,
                           pDesc->sourceId, nullptr, &canonical, &pendingDelta );
    }
    if ( st == geometry_status_t::OK ) { st = GeometryDocument_TryAddMesh( pDocument, &mesh ); }
    MeshSource_Shutdown( &mesh );
    if ( st != geometry_status_t::OK ) {
        GeometryMeshDelta_Shutdown( &pendingDelta );
        MeshSourceDescription_Shutdown( &canonical );
        return st;
    }
    pDocument->revision += 1u;
    PublishPreparedDelta( pDeltaOut, &pendingDelta );
    MeshSourceDescription_Shutdown( &canonical );
    if ( pNewRevisionOut ) { *pNewRevisionOut = pDocument->revision; }
    return geometry_status_t::OK;
}

geometry_status_t GeometryMeshCommand_TryRemove(
    geometry_document_t *pDocument,
    geometry_source_id_t meshId,
    geometry_mesh_delta_t *pDeltaOut,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometryMeshDelta_IsInitialized( pDeltaOut ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const mesh_source_t *pMesh = GeometryDocument_FindMesh( pDocument, meshId );
    if ( pMesh == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    mesh_source_description_t before{};
    geometry_status_t st = MeshSourceDescription_Init( &before, pDocument->pAllocator, meshId );
    if ( st == geometry_status_t::OK ) { st = MeshSource_TryDescribe( pMesh, &before ); }
    geometry_mesh_delta_t pendingDelta{};
    if ( st == geometry_status_t::OK ) {
        st = PrepareDelta( pDeltaOut, geometry_mesh_delta_kind_t::MESH_REMOVED,
                           meshId, &before, nullptr, &pendingDelta );
    }
    if ( st == geometry_status_t::OK ) { st = GeometryDocument_TryRemoveMesh( pDocument, meshId ); }
    if ( st != geometry_status_t::OK ) {
        GeometryMeshDelta_Shutdown( &pendingDelta );
        MeshSourceDescription_Shutdown( &before );
        return st;
    }
    pDocument->revision += 1u;
    PublishPreparedDelta( pDeltaOut, &pendingDelta );
    MeshSourceDescription_Shutdown( &before );
    if ( pNewRevisionOut ) { *pNewRevisionOut = pDocument->revision; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
