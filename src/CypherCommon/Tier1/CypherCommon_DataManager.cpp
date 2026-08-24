//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_DataManager.cpp
//  Purpose: Implements the instance-owned named pointer registry.
//  Details: Names are copied into registry-owned storage. Removal occurs before an
//           optional destroy callback runs, making callback re-entry deterministic.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_DataManager.h"

namespace cypher::common
{

namespace
{

struct data_entry_t {
    char *pName{ nullptr };                    // Registry-owned, NUL-terminated lookup key.
    usize cchName{ 0u };                       // Name bytes excluding the stored terminator.
    void *pData{ nullptr };                    // Opaque object registered by the caller.
    data_destroy_fn_t pfnDestroy{ nullptr };   // Optional ownership-release callback.
    void *pUserData{ nullptr };                // Caller context forwarded to pfnDestroy.
};

bool_t DataNameIsValid( string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) || name.cchLength == 0u ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < name.cchLength; ++iByte ) {
        if ( name.pData[iByte] == '\0' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t DataNameEquals( const data_entry_t &entry, string_view_t name ) noexcept
{
    if ( entry.cchName != name.cchLength ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < entry.cchName; ++iByte ) {
        if ( entry.pName[iByte] != name.pData[iByte] ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace

struct data_manager_t {
    const allocator_t *pAllocator{ nullptr };  // Owns the manager, entry array, and copied names.
    data_entry_t *pEntries{ nullptr };          // Dense unordered registry storage.
    usize nCount{ 0u };                        // Number of initialized entries.
    usize nCapacity{ 0u };                     // Allocated entry slots.
};

namespace
{

usize DataManager_FindIndex(
    const data_manager_t *pManager,
    string_view_t name ) noexcept
{
    if ( pManager == nullptr || !DataNameIsValid( name ) ) {
        return CY_INVALID_SIZE;
    }
    for ( usize iEntry = 0u; iEntry < pManager->nCount; ++iEntry ) {
        if ( DataNameEquals( pManager->pEntries[iEntry], name ) ) {
            return iEntry;
        }
    }
    return CY_INVALID_SIZE;
}

bool_t DataManager_Reserve(
    data_manager_t *pManager,
    usize nCapacity ) noexcept
{
    if ( nCapacity <= pManager->nCapacity ) {
        return CY_TRUE;
    }
    usize cbOld = 0u;
    usize cbNew = 0u;
    if ( !Cy_TryArrayByteCount<data_entry_t>( pManager->nCapacity, cbOld ) ||
         !Cy_TryArrayByteCount<data_entry_t>( nCapacity, cbNew ) ) {
        return CY_FALSE;
    }
    void *pMemory = Allocator_Reallocate(
        pManager->pAllocator,
        pManager->pEntries,
        cbOld,
        cbNew,
        alignof( data_entry_t ) );
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }
    pManager->pEntries = static_cast<data_entry_t *>( pMemory );
    // Keep unused slots in the null state so failed/debug inspection never sees
    // stale callback or object pointers left by the allocator.
    Cy_MemZero(
        pManager->pEntries + pManager->nCapacity,
        ( nCapacity - pManager->nCapacity ) * sizeof( data_entry_t ) );
    pManager->nCapacity = nCapacity;
    return CY_TRUE;
}

void DataManager_RemoveAt(
    data_manager_t *pManager,
    usize iEntry,
    bool_t bDestroy,
    void **ppDetachedOut ) noexcept
{
    const data_entry_t removed = pManager->pEntries[iEntry];
    // Compact and logically remove the entry before invoking user code. A
    // destructor may safely re-enter the manager without finding this record.
    for ( usize iMove = iEntry; iMove + 1u < pManager->nCount; ++iMove ) {
        pManager->pEntries[iMove] = pManager->pEntries[iMove + 1u];
    }
    --pManager->nCount;
    pManager->pEntries[pManager->nCount] = {};

    if ( ppDetachedOut != nullptr ) {
        *ppDetachedOut = removed.pData;
    }
    Allocator_Free(
        pManager->pAllocator,
        removed.pName,
        removed.cchName + 1u,
        alignof( char ) );
    if ( bDestroy && removed.pfnDestroy != nullptr ) {
        removed.pfnDestroy( removed.pData, removed.pUserData );
    }
}

} // namespace

data_manager_t *DataManager_Create(
    const allocator_t *pAllocator,
    usize nInitialCapacity ) noexcept
{
    if ( !Allocator_IsValid( pAllocator ) ) {
        return nullptr;
    }
    auto *pManager = static_cast<data_manager_t *>( Allocator_AllocateZeroed(
        pAllocator,
        sizeof( data_manager_t ),
        alignof( data_manager_t ) ) );
    if ( pManager == nullptr ) {
        return nullptr;
    }
    pManager->pAllocator = pAllocator;
    if ( nInitialCapacity > 0u &&
         !DataManager_Reserve( pManager, nInitialCapacity ) ) {
        Allocator_Free(
            pAllocator,
            pManager,
            sizeof( data_manager_t ),
            alignof( data_manager_t ) );
        return nullptr;
    }
    return pManager;
}

void DataManager_Destroy( data_manager_t *pManager ) noexcept
{
    if ( pManager == nullptr ) {
        return;
    }
    const allocator_t *pAllocator = pManager->pAllocator;
    DataManager_Clear( pManager );
    Allocator_Free(
        pAllocator,
        pManager->pEntries,
        pManager->nCapacity * sizeof( data_entry_t ),
        alignof( data_entry_t ) );
    Allocator_Free(
        pAllocator,
        pManager,
        sizeof( data_manager_t ),
        alignof( data_manager_t ) );
}

void DataManager_Clear( data_manager_t *pManager ) noexcept
{
    if ( pManager == nullptr ) {
        return;
    }
    while ( pManager->nCount > 0u ) {
        DataManager_RemoveAt( pManager, pManager->nCount - 1u, CY_TRUE, nullptr );
    }
}

bool_t DataManager_Register(
    data_manager_t *pManager,
    const data_entry_desc_t &entry ) noexcept
{
    if ( pManager == nullptr || !DataNameIsValid( entry.name ) ||
         entry.pData == nullptr ||
         DataManager_FindIndex( pManager, entry.name ) != CY_INVALID_SIZE ) {
        return CY_FALSE;
    }
    if ( pManager->nCount == pManager->nCapacity ) {
        const usize nNewCapacity = pManager->nCapacity == 0u
            ? 8u
            : ( pManager->nCapacity <= CY_USIZE_MAX / 2u
                ? pManager->nCapacity * 2u
                : 0u );
        if ( nNewCapacity == 0u || !DataManager_Reserve( pManager, nNewCapacity ) ) {
            return CY_FALSE;
        }
    }
    if ( entry.name.cchLength == CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    char *pName = static_cast<char *>( Allocator_Allocate(
        pManager->pAllocator,
        entry.name.cchLength + 1u,
        alignof( char ) ) );
    if ( pName == nullptr ) {
        return CY_FALSE;
    }
    Cy_MemCopy( pName, entry.name.pData, entry.name.cchLength );
    pName[entry.name.cchLength] = '\0';

    pManager->pEntries[pManager->nCount++] = {
        pName,
        entry.name.cchLength,
        entry.pData,
        entry.pfnDestroy,
        entry.pUserData
    };
    return CY_TRUE;
}

void *DataManager_Find(
    const data_manager_t *pManager,
    string_view_t name ) noexcept
{
    const usize iEntry = DataManager_FindIndex( pManager, name );
    return iEntry != CY_INVALID_SIZE ? pManager->pEntries[iEntry].pData : nullptr;
}

bool_t DataManager_Remove(
    data_manager_t *pManager,
    string_view_t name ) noexcept
{
    const usize iEntry = DataManager_FindIndex( pManager, name );
    if ( iEntry == CY_INVALID_SIZE ) {
        return CY_FALSE;
    }
    DataManager_RemoveAt( pManager, iEntry, CY_TRUE, nullptr );
    return CY_TRUE;
}

void *DataManager_Detach(
    data_manager_t *pManager,
    string_view_t name ) noexcept
{
    const usize iEntry = DataManager_FindIndex( pManager, name );
    if ( iEntry == CY_INVALID_SIZE ) {
        return nullptr;
    }
    void *pDetached = nullptr;
    DataManager_RemoveAt( pManager, iEntry, CY_FALSE, &pDetached );
    return pDetached;
}

} // namespace cypher::common
