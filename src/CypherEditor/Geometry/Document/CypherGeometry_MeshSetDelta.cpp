//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSetDelta.cpp
//  Purpose: Implements ordered mesh-set undo/redo records.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSetDelta.h"

#include <algorithm>
#include <new>
#include <utility>

namespace cypher::editor::geometry
{

namespace
{

mesh_source_description_t *AllocateDescription(
    const common::allocator_t *pAllocator ) noexcept
{
    void *pMemory = common::Allocator_AllocateZeroed(
        pAllocator,
        sizeof( mesh_source_description_t ),
        alignof( mesh_source_description_t ) );
    return pMemory != nullptr
        ? new ( pMemory ) mesh_source_description_t{}
        : nullptr;
}

void FreeDescription(
    const common::allocator_t *pAllocator,
    mesh_source_description_t *pDescription ) noexcept
{
    if ( pDescription == nullptr ) {
        return;
    }
    MeshSourceDescription_Shutdown( pDescription );
    pDescription->~mesh_source_description_t();
    common::Allocator_Free(
        pAllocator,
        pDescription,
        sizeof( mesh_source_description_t ),
        alignof( mesh_source_description_t ) );
}

bool ListIsInitialized(
    const geometry_mesh_description_list_t &list ) noexcept
{
    if ( !common::Vector_IsValid( &list.meshes ) ||
         list.meshes.pAllocator == nullptr ) {
        return false;
    }
    for ( common::usize i = 0u; i < list.meshes.nCount; ++i ) {
        if ( !MeshSourceDescription_IsInitialized( list.meshes.pData[i] ) ) {
            return false;
        }
    }
    return true;
}

void ListShutdown( geometry_mesh_description_list_t *pList ) noexcept
{
    if ( pList == nullptr ) {
        return;
    }
    const common::allocator_t *pAllocator = pList->meshes.pAllocator;
    if ( pAllocator != nullptr ) {
        for ( common::usize i = 0u; i < pList->meshes.nCount; ++i ) {
            FreeDescription( pAllocator, pList->meshes.pData[i] );
        }
    }
    common::Vector_Shutdown( &pList->meshes );
}

void ListSwap(
    geometry_mesh_description_list_t &a,
    geometry_mesh_description_list_t &b ) noexcept
{
    std::swap( a.meshes.pData, b.meshes.pData );
    std::swap( a.meshes.nCount, b.meshes.nCount );
    std::swap( a.meshes.nCapacity, b.meshes.nCapacity );
    std::swap( a.meshes.pAllocator, b.meshes.pAllocator );
}

geometry_status_t ListTryCopy(
    geometry_mesh_description_list_t *pList,
    common::span_t<const mesh_source_description_t *const> source,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( !common::Vector_Init( &pList->meshes, pAllocator, source.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    common::vector_t<geometry_source_id_t> ids{};
    if ( !common::Vector_Init( &ids, pAllocator ) ) {
        ListShutdown( pList );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    for ( common::usize i = 0u; i < source.nCount; ++i ) {
        if ( source.pData[i] == nullptr ) {
            common::Vector_Shutdown( &ids );
            ListShutdown( pList );
            return geometry_status_t::INVALID_ARGUMENT;
        }

        // Build then describe so the delta owns the canonical form even when
        // a caller supplied a valid but non-canonical vertex/face order. This
        // is required for an inverted delta to match the state it just
        // published exactly.
        mesh_source_t mesh{};
        geometry_status_t status = MeshSource_TryBuild(
            source.pData[i], pAllocator, &mesh );
        if ( status == geometry_status_t::OK ) {
            status = MeshSource_TryCollectSourceIds( &mesh, &ids );
        }
        if ( status != geometry_status_t::OK ) {
            MeshSource_Shutdown( &mesh );
            common::Vector_Shutdown( &ids );
            ListShutdown( pList );
            return status;
        }

        mesh_source_description_t *pCopy = AllocateDescription( pAllocator );
        if ( pCopy == nullptr ) {
            MeshSource_Shutdown( &mesh );
            common::Vector_Shutdown( &ids );
            ListShutdown( pList );
            return geometry_status_t::ALLOCATION_FAILED;
        }
        status = MeshSourceDescription_Init(
            pCopy, pAllocator, source.pData[i]->sourceId );
        if ( status == geometry_status_t::OK ) {
            status = MeshSource_TryDescribe( &mesh, pCopy );
        }
        MeshSource_Shutdown( &mesh );
        if ( status != geometry_status_t::OK ) {
            FreeDescription( pAllocator, pCopy );
            common::Vector_Shutdown( &ids );
            ListShutdown( pList );
            return status;
        }
        if ( !common::Vector_PushBack( &pList->meshes, pCopy ) ) {
            FreeDescription( pAllocator, pCopy );
            common::Vector_Shutdown( &ids );
            ListShutdown( pList );
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }

    if ( ids.nCount > 1u ) {
        std::sort(
            ids.pData,
            ids.pData + ids.nCount,
            []( geometry_source_id_t a, geometry_source_id_t b ) noexcept {
                return a.value < b.value;
            } );
        for ( common::usize i = 1u; i < ids.nCount; ++i ) {
            if ( ids.pData[i - 1u].value == ids.pData[i].value ) {
                common::Vector_Shutdown( &ids );
                ListShutdown( pList );
                return geometry_status_t::IDENTITY_CONFLICT;
            }
        }
    }
    common::Vector_Shutdown( &ids );
    return geometry_status_t::OK;
}

geometry_status_t DocumentMatchesList(
    const geometry_document_t &document,
    const geometry_mesh_description_list_t &expected,
    bool *pMatchesOut ) noexcept
{
    *pMatchesOut = false;
    if ( document.meshes.nCount != expected.meshes.nCount ) {
        return geometry_status_t::OK;
    }

    mesh_source_description_t current{};
    geometry_status_t status = MeshSourceDescription_Init(
        &current, document.pAllocator, GEOMETRY_SOURCE_ID_INVALID );
    for ( common::usize i = 0u;
          status == geometry_status_t::OK && i < document.meshes.nCount;
          ++i ) {
        status = MeshSource_TryDescribe( document.meshes.pData[i], &current );
        if ( status == geometry_status_t::OK &&
             !MeshSourceDescription_Equal(
                 &current, expected.meshes.pData[i] ) ) {
            MeshSourceDescription_Shutdown( &current );
            return geometry_status_t::OK;
        }
    }
    MeshSourceDescription_Shutdown( &current );
    if ( status == geometry_status_t::OK ) {
        *pMatchesOut = true;
    }
    return status;
}

} // namespace

geometry_status_t GeometryMeshSetDelta_Init(
    geometry_mesh_set_delta_t *pDelta,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pDelta == nullptr || !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( GeometryMeshSetDelta_IsInitialized( pDelta ) ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !common::Vector_Init( &pDelta->before.meshes, pAllocator ) ||
         !common::Vector_Init( &pDelta->after.meshes, pAllocator ) ) {
        GeometryMeshSetDelta_Shutdown( pDelta );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pDelta->bHasPayload = false;
    return geometry_status_t::OK;
}

void GeometryMeshSetDelta_Shutdown(
    geometry_mesh_set_delta_t *pDelta ) noexcept
{
    if ( pDelta == nullptr ) {
        return;
    }
    ListShutdown( &pDelta->after );
    ListShutdown( &pDelta->before );
    pDelta->bHasPayload = false;
}

bool GeometryMeshSetDelta_IsInitialized(
    const geometry_mesh_set_delta_t *pDelta ) noexcept
{
    return pDelta != nullptr && ListIsInitialized( pDelta->before ) &&
           ListIsInitialized( pDelta->after );
}

geometry_status_t GeometryMeshSetDelta_TryAssign(
    geometry_mesh_set_delta_t *pDelta,
    common::span_t<const mesh_source_description_t *const> before,
    common::span_t<const mesh_source_description_t *const> after ) noexcept
{
    if ( !GeometryMeshSetDelta_IsInitialized( pDelta ) ||
         !common::Span_IsValid( before ) || !common::Span_IsValid( after ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::allocator_t *pAllocator = pDelta->before.meshes.pAllocator;
    geometry_mesh_description_list_t preparedBefore{};
    geometry_mesh_description_list_t preparedAfter{};
    geometry_status_t status = ListTryCopy(
        &preparedBefore, before, pAllocator );
    if ( status == geometry_status_t::OK ) {
        status = ListTryCopy( &preparedAfter, after, pAllocator );
    }
    if ( status != geometry_status_t::OK ) {
        ListShutdown( &preparedAfter );
        ListShutdown( &preparedBefore );
        return status;
    }

    ListSwap( pDelta->before, preparedBefore );
    ListSwap( pDelta->after, preparedAfter );
    pDelta->bHasPayload = true;
    ListShutdown( &preparedAfter );
    ListShutdown( &preparedBefore );
    return geometry_status_t::OK;
}

void GeometryMeshSetDelta_Invert(
    geometry_mesh_set_delta_t *pDelta ) noexcept
{
    if ( !GeometryMeshSetDelta_IsInitialized( pDelta ) ||
         !pDelta->bHasPayload ) {
        return;
    }
    ListSwap( pDelta->before, pDelta->after );
}

geometry_status_t GeometryMeshSetDelta_TryApply(
    const geometry_mesh_set_delta_t *pDelta,
    geometry_document_t *pDocument,
    geometry_revision_t *pNewRevisionOut ) noexcept
{
    if ( !GeometryMeshSetDelta_IsInitialized( pDelta ) ||
         !pDelta->bHasPayload ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    bool bMatches = false;
    geometry_status_t status = DocumentMatchesList(
        *pDocument, pDelta->before, &bMatches );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !bMatches ) {
        return geometry_status_t::STALE_REVISION;
    }

    common::vector_t<geometry_source_id_t> removals{};
    common::vector_t<const mesh_source_description_t *> replacements{};
    common::vector_t<geometry_source_id_t> finalOrder{};
    if ( !common::Vector_Init(
             &removals, pDocument->pAllocator, pDocument->meshes.nCount ) ||
         !common::Vector_Init(
             &replacements,
             pDocument->pAllocator,
             pDelta->after.meshes.nCount ) ||
         !common::Vector_Init(
             &finalOrder,
             pDocument->pAllocator,
             pDelta->after.meshes.nCount ) ) {
        common::Vector_Shutdown( &finalOrder );
        common::Vector_Shutdown( &replacements );
        common::Vector_Shutdown( &removals );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    // Retain a source only when the target contains the exact same canonical
    // description under the same root ID. This keeps unrelated document
    // objects and their in-memory handles stable across Join/Separate undo,
    // redo, and pure order changes.
    for ( common::usize iBefore = 0u;
          iBefore < pDelta->before.meshes.nCount;
          ++iBefore ) {
        const mesh_source_description_t *pBefore =
            pDelta->before.meshes.pData[iBefore];
        bool bRetained = false;
        for ( common::usize iAfter = 0u;
              iAfter < pDelta->after.meshes.nCount;
              ++iAfter ) {
            const mesh_source_description_t *pAfter =
                pDelta->after.meshes.pData[iAfter];
            if ( pAfter->sourceId.value == pBefore->sourceId.value ) {
                bRetained = MeshSourceDescription_Equal( pBefore, pAfter );
                break;
            }
        }
        if ( !bRetained ) {
            (void)common::Vector_PushBack(
                &removals, pBefore->sourceId );
        }
    }

    for ( common::usize iAfter = 0u;
          iAfter < pDelta->after.meshes.nCount;
          ++iAfter ) {
        const mesh_source_description_t *pAfter =
            pDelta->after.meshes.pData[iAfter];
        bool bRetained = false;
        for ( common::usize iBefore = 0u;
              iBefore < pDelta->before.meshes.nCount;
              ++iBefore ) {
            const mesh_source_description_t *pBefore =
                pDelta->before.meshes.pData[iBefore];
            if ( pBefore->sourceId.value == pAfter->sourceId.value ) {
                bRetained = MeshSourceDescription_Equal( pBefore, pAfter );
                break;
            }
        }
        if ( !bRetained ) {
            (void)common::Vector_PushBack( &replacements, pAfter );
        }
        (void)common::Vector_PushBack(
            &finalOrder, pAfter->sourceId );
    }

    status = GeometryDocument_TryPublishMeshSetExact(
        pDocument,
        common::span_t<const geometry_source_id_t>{
            removals.pData, removals.nCount },
        common::span_t<const mesh_source_description_t *const>{
            replacements.pData, replacements.nCount },
        common::span_t<const geometry_source_id_t>{
            finalOrder.pData, finalOrder.nCount } );
    common::Vector_Shutdown( &finalOrder );
    common::Vector_Shutdown( &replacements );
    common::Vector_Shutdown( &removals );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    ++pDocument->revision;
    if ( pNewRevisionOut != nullptr ) {
        *pNewRevisionOut = pDocument->revision;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
