//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentSurfaces.cpp
//  Purpose: Implements the geometry document's patch and heightfield
//           stores.
//  Details: Patches and heightfields differ only in how they are validated,
//           copied, and which IDs they carry, so one templated store drives
//           both through small per-type traits.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_DocumentIdentity.h"

#include <algorithm>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr usize kAbsent = ~static_cast<usize>( 0u );

bool IdLess( geometry_source_id_t a, geometry_source_id_t b ) noexcept { return a.value < b.value; }

struct patch_traits_t {
    using object_t = patch_surface_t;
    static constexpr usize kMax = kGeometryDocumentPatchesMax;
    static vector_t<object_t *> &Store( geometry_document_t *pDoc ) noexcept { return pDoc->patches; }
    static const vector_t<object_t *> &Store( const geometry_document_t *pDoc ) noexcept { return pDoc->patches; }
    static bool IsInitialized( const object_t *p ) noexcept { return Patch_IsInitialized( p ); }
    static geometry_status_t Validate( const object_t *p, const allocator_t *pA ) noexcept
    {
        switch ( Patch_Validate( p, pA ).fault ) {
            case patch_fault_t::NONE: return geometry_status_t::OK;
            case patch_fault_t::DUPLICATE_SOURCE_ID: return geometry_status_t::IDENTITY_CONFLICT;
            case patch_fault_t::NON_FINITE:
            case patch_fault_t::COORDINATE_RANGE: return geometry_status_t::NUMERIC_FAILURE;
            case patch_fault_t::COLLAPSED_SURFACE: return geometry_status_t::DEGENERATE;
            case patch_fault_t::VALIDATION_INCOMPLETE: return geometry_status_t::ALLOCATION_FAILED;
            default: return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    static geometry_status_t Clone( const object_t *p, const allocator_t *pA, object_t *pOut ) noexcept
    {
        return Patch_TryClone( p, pA, pOut );
    }
    static void Shutdown( object_t *p ) noexcept { Patch_Shutdown( p ); }
    static bool CollectIds( const object_t *p, vector_t<geometry_source_id_t> *pOut ) noexcept
    {
        if ( !Vector_Reserve( pOut, pOut->nCount + p->controls.nCount + 1u ) ) { return false; }
        (void)Vector_PushBack( pOut, p->sourceId );
        for ( usize i = 0u; i < p->controls.nCount; ++i ) { (void)Vector_PushBack( pOut, p->controls.pData[i].sourceId ); }
        return true;
    }
};

struct heightfield_traits_t {
    using object_t = heightfield_t;
    static constexpr usize kMax = kGeometryDocumentHeightFieldsMax;
    static vector_t<object_t *> &Store( geometry_document_t *pDoc ) noexcept { return pDoc->heightFields; }
    static const vector_t<object_t *> &Store( const geometry_document_t *pDoc ) noexcept { return pDoc->heightFields; }
    static bool IsInitialized( const object_t *p ) noexcept { return HeightField_IsInitialized( p ); }
    static geometry_status_t Validate( const object_t *p, const allocator_t *pA ) noexcept
    {
        switch ( HeightField_Validate( p, pA ).fault ) {
            case heightfield_fault_t::NONE: return geometry_status_t::OK;
            case heightfield_fault_t::DUPLICATE_SOURCE_ID: return geometry_status_t::IDENTITY_CONFLICT;
            case heightfield_fault_t::NON_FINITE:
            case heightfield_fault_t::COORDINATE_RANGE: return geometry_status_t::NUMERIC_FAILURE;
            case heightfield_fault_t::VALIDATION_INCOMPLETE: return geometry_status_t::ALLOCATION_FAILED;
            default: return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    static geometry_status_t Clone( const object_t *p, const allocator_t *pA, object_t *pOut ) noexcept
    {
        return HeightField_TryClone( p, pA, pOut );
    }
    static void Shutdown( object_t *p ) noexcept { HeightField_Shutdown( p ); }
    static bool CollectIds( const object_t *p, vector_t<geometry_source_id_t> *pOut ) noexcept
    {
        if ( !Vector_Reserve( pOut, pOut->nCount + p->tiles.nCount + 1u ) ) { return false; }
        (void)Vector_PushBack( pOut, p->sourceId );
        for ( usize i = 0u; i < p->tiles.nCount; ++i ) { (void)Vector_PushBack( pOut, p->tiles.pData[i].sourceId ); }
        return true;
    }
};

template <typename traits_t> typename traits_t::object_t *Allocate( const allocator_t *pA ) noexcept
{
    using object_t = typename traits_t::object_t;
    void *pMemory = Allocator_AllocateZeroed( pA, sizeof( object_t ), alignof( object_t ) );
    return pMemory != nullptr ? new ( pMemory ) object_t{} : nullptr;
}

template <typename traits_t> void Free( const allocator_t *pA, typename traits_t::object_t *p ) noexcept
{
    using object_t = typename traits_t::object_t;
    if ( p == nullptr ) { return; }
    traits_t::Shutdown( p );
    p->~object_t();
    Allocator_Free( pA, p, sizeof( object_t ), alignof( object_t ) );
}

template <typename traits_t> usize FindIndex( const geometry_document_t *pDoc, geometry_source_id_t id ) noexcept
{
    const auto &store = traits_t::Store( pDoc );
    for ( usize i = 0u; i < store.nCount; ++i ) {
        if ( store.pData[i]->sourceId.value == id.value ) { return i; }
    }
    return kAbsent;
}

bool Ready( const geometry_document_t *pDoc ) noexcept
{
    return GeometryDocument_IsInitialized( pDoc ) && pDoc->patches.pAllocator != nullptr && pDoc->heightFields.pAllocator != nullptr;
}

// The shared atomic step. iOld == kAbsent means "add"; pNew == nullptr means
// "remove".
template <typename traits_t>
geometry_status_t Commit( geometry_document_t *pDoc, usize iOld, const typename traits_t::object_t *pNew ) noexcept
{
    using object_t = typename traits_t::object_t;
    const allocator_t *pA = pDoc->pAllocator;
    auto &store = traits_t::Store( pDoc );
    const object_t *pOld = iOld != kAbsent ? store.pData[iOld] : nullptr;
    if ( pNew != nullptr ) {
        const geometry_status_t valid = traits_t::Validate( pNew, pA );
        if ( valid != geometry_status_t::OK ) { return valid; }
    }
    if ( pOld == nullptr && store.nCount >= traits_t::kMax ) { return geometry_status_t::LIMIT_EXCEEDED; }

    vector_t<geometry_source_id_t> oldIds{}, newIds{};
    if ( !Vector_Init( &oldIds, pA ) || !Vector_Init( &newIds, pA ) || ( pOld != nullptr && !traits_t::CollectIds( pOld, &oldIds ) ) ||
         ( pNew != nullptr && !traits_t::CollectIds( pNew, &newIds ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    std::sort( oldIds.pData, oldIds.pData + oldIds.nCount, IdLess );
    std::sort( newIds.pData, newIds.pData + newIds.nCount, IdLess );

    object_t *pCopy = nullptr;
    auto prepare = [&]() noexcept -> geometry_status_t {
        if ( pOld == nullptr && !Vector_Reserve( &store, store.nCount + 1u ) ) { return geometry_status_t::ALLOCATION_FAILED; }
        if ( pNew == nullptr ) { return geometry_status_t::OK; }
        pCopy = Allocate<traits_t>( pA );
        if ( pCopy == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
        const geometry_status_t st = traits_t::Clone( pNew, pA, pCopy );
        if ( st != geometry_status_t::OK ) {
            Free<traits_t>( pA, pCopy );
            pCopy = nullptr;
        }
        return st;
    };
    auto publish = [&]() noexcept {
        if ( pOld == nullptr ) {
            (void)Vector_PushBack( &store, pCopy ); // reserved in prepare
        } else if ( pCopy != nullptr ) {
            Free<traits_t>( pA, store.pData[iOld] );
            store.pData[iOld] = pCopy;
        } else {
            Free<traits_t>( pA, store.pData[iOld] );
            Vector_EraseSwap( &store, iOld );
        }
    };
    auto discard = [&]() noexcept { Free<traits_t>( pA, pCopy ); };
    return document_detail::TryCommitIdentity(
        &pDoc->sourceIds, span_t<const geometry_source_id_t>{ oldIds.pData, oldIds.nCount },
        span_t<const geometry_source_id_t>{ newIds.pData, newIds.nCount }, pA, prepare, publish, discard );
}

template <typename traits_t> geometry_status_t TryAdd( geometry_document_t *pDoc, const typename traits_t::object_t *p ) noexcept
{
    if ( !Ready( pDoc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( p == nullptr || !traits_t::IsInitialized( p ) || !GeometrySourceId_IsValid( p->sourceId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( FindIndex<traits_t>( pDoc, p->sourceId ) != kAbsent ) { return geometry_status_t::IDENTITY_CONFLICT; }
    return Commit<traits_t>( pDoc, kAbsent, p );
}

template <typename traits_t> geometry_status_t TryRemove( geometry_document_t *pDoc, geometry_source_id_t id ) noexcept
{
    if ( !Ready( pDoc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const usize i = GeometrySourceId_IsValid( id ) ? FindIndex<traits_t>( pDoc, id ) : kAbsent;
    if ( i == kAbsent ) { return geometry_status_t::INVALID_ARGUMENT; }
    return Commit<traits_t>( pDoc, i, nullptr );
}

template <typename traits_t> geometry_status_t TryReplace( geometry_document_t *pDoc, const typename traits_t::object_t *p ) noexcept
{
    if ( !Ready( pDoc ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( p == nullptr || !traits_t::IsInitialized( p ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    const usize i = FindIndex<traits_t>( pDoc, p->sourceId );
    if ( i == kAbsent ) { return geometry_status_t::INVALID_ARGUMENT; }
    return Commit<traits_t>( pDoc, i, p );
}

template <typename traits_t> const typename traits_t::object_t *At( const geometry_document_t *pDoc, usize i ) noexcept
{
    return Ready( pDoc ) && i < traits_t::Store( pDoc ).nCount ? traits_t::Store( pDoc ).pData[i] : nullptr;
}

template <typename traits_t> const typename traits_t::object_t *Find( const geometry_document_t *pDoc, geometry_source_id_t id ) noexcept
{
    if ( !Ready( pDoc ) || !GeometrySourceId_IsValid( id ) ) { return nullptr; }
    const usize i = FindIndex<traits_t>( pDoc, id );
    return i != kAbsent ? traits_t::Store( pDoc ).pData[i] : nullptr;
}

} // namespace

usize GeometryDocument_PatchCount( const geometry_document_t *pDocument ) noexcept
{
    return Ready( pDocument ) ? pDocument->patches.nCount : 0u;
}
const patch_surface_t *GeometryDocument_PatchAt( const geometry_document_t *pDocument, usize iPatch ) noexcept
{
    return At<patch_traits_t>( pDocument, iPatch );
}
const patch_surface_t *GeometryDocument_FindPatch( const geometry_document_t *pDocument, geometry_source_id_t patchId ) noexcept
{
    return Find<patch_traits_t>( pDocument, patchId );
}
geometry_status_t GeometryDocument_TryAddPatch( geometry_document_t *pDocument, const patch_surface_t *pPatch ) noexcept
{
    return TryAdd<patch_traits_t>( pDocument, pPatch );
}
geometry_status_t GeometryDocument_TryRemovePatch( geometry_document_t *pDocument, geometry_source_id_t patchId ) noexcept
{
    return TryRemove<patch_traits_t>( pDocument, patchId );
}
geometry_status_t GeometryDocument_TryReplacePatch( geometry_document_t *pDocument, const patch_surface_t *pPatch ) noexcept
{
    return TryReplace<patch_traits_t>( pDocument, pPatch );
}

usize GeometryDocument_HeightFieldCount( const geometry_document_t *pDocument ) noexcept
{
    return Ready( pDocument ) ? pDocument->heightFields.nCount : 0u;
}
const heightfield_t *GeometryDocument_HeightFieldAt( const geometry_document_t *pDocument, usize iField ) noexcept
{
    return At<heightfield_traits_t>( pDocument, iField );
}
const heightfield_t *GeometryDocument_FindHeightField( const geometry_document_t *pDocument, geometry_source_id_t fieldId ) noexcept
{
    return Find<heightfield_traits_t>( pDocument, fieldId );
}
geometry_status_t GeometryDocument_TryAddHeightField( geometry_document_t *pDocument, const heightfield_t *pField ) noexcept
{
    return TryAdd<heightfield_traits_t>( pDocument, pField );
}
geometry_status_t GeometryDocument_TryRemoveHeightField( geometry_document_t *pDocument, geometry_source_id_t fieldId ) noexcept
{
    return TryRemove<heightfield_traits_t>( pDocument, fieldId );
}
geometry_status_t GeometryDocument_TryReplaceHeightField( geometry_document_t *pDocument, const heightfield_t *pField ) noexcept
{
    return TryReplace<heightfield_traits_t>( pDocument, pField );
}

geometry_status_t GeometryDocument_TryCollectSurfaceIds( const geometry_document_t *pDocument,
                                                        vector_t<geometry_source_id_t> *pIdsOut ) noexcept
{
    if ( !Ready( pDocument ) || pIdsOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < pDocument->patches.nCount; ++i ) {
        if ( pDocument->patches.pData[i] == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        if ( !patch_traits_t::CollectIds( pDocument->patches.pData[i], pIdsOut ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    }
    for ( usize i = 0u; i < pDocument->heightFields.nCount; ++i ) {
        if ( pDocument->heightFields.pData[i] == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        if ( !heightfield_traits_t::CollectIds( pDocument->heightFields.pData[i], pIdsOut ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    return geometry_status_t::OK;
}

void GeometryDocument_FreeAllSurfaces( geometry_document_t *pDocument ) noexcept
{
    if ( pDocument == nullptr || pDocument->pAllocator == nullptr ) { return; }
    for ( usize i = 0u; i < pDocument->patches.nCount; ++i ) { Free<patch_traits_t>( pDocument->pAllocator, pDocument->patches.pData[i] ); }
    for ( usize i = 0u; i < pDocument->heightFields.nCount; ++i ) {
        Free<heightfield_traits_t>( pDocument->pAllocator, pDocument->heightFields.pData[i] );
    }
    Vector_Shutdown( &pDocument->patches );
    Vector_Shutdown( &pDocument->heightFields );
}

} // namespace cypher::editor::geometry
