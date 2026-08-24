//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Interface.cpp
//  Purpose: Implements versioned interface factories.
//  Details: Factories are selected by exact byte name and major version, then by a
//           provided minor version at least as new as the requested contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Interface.h"

namespace cypher::common
{

namespace
{

struct interface_factory_t {
    char *pName{ nullptr };                    // Registry-owned interface family name.
    usize cchName{ 0u };                       // Name bytes excluding the terminator.
    u32 nMajorVersion{ 0u };                   // Exact ABI-breaking version key.
    u32 nMinorVersion{ 0u };                   // Highest backward-compatible contract supplied.
    interface_create_fn_t pfnCreate{ nullptr };// Factory entry point owned by provider code.
    interface_release_fn_t pfnRelease{ nullptr };// Optional matching release entry point.
    void *pUserData{ nullptr };                // Provider context passed to both callbacks.
};

bool_t InterfaceNameIsValid( string_view_t name ) noexcept
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

bool_t InterfaceNameEquals(
    const interface_factory_t &factory,
    string_view_t name ) noexcept
{
    if ( factory.cchName != name.cchLength ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < factory.cchName; ++iByte ) {
        if ( factory.pName[iByte] != name.pData[iByte] ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace

struct interface_registry_t {
    const allocator_t *pAllocator{ nullptr };  // Owns registry storage and copied names.
    interface_factory_t *pFactories{ nullptr };// Dense registered factory array.
    usize nCount{ 0u };                        // Initialized factory records.
    usize nCapacity{ 0u };                     // Allocated factory slots.
};

namespace
{

bool_t InterfaceRegistry_Reserve(
    interface_registry_t *pRegistry,
    usize nCapacity ) noexcept
{
    if ( nCapacity <= pRegistry->nCapacity ) {
        return CY_TRUE;
    }
    usize cbOld = 0u;
    usize cbNew = 0u;
    if ( !Cy_TryArrayByteCount<interface_factory_t>( pRegistry->nCapacity, cbOld ) ||
         !Cy_TryArrayByteCount<interface_factory_t>( nCapacity, cbNew ) ) {
        return CY_FALSE;
    }
    void *pMemory = Allocator_Reallocate(
        pRegistry->pAllocator,
        pRegistry->pFactories,
        cbOld,
        cbNew,
        alignof( interface_factory_t ) );
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }
    pRegistry->pFactories = static_cast<interface_factory_t *>( pMemory );
    Cy_MemZero(
        pRegistry->pFactories + pRegistry->nCapacity,
        ( nCapacity - pRegistry->nCapacity ) * sizeof( interface_factory_t ) );
    pRegistry->nCapacity = nCapacity;
    return CY_TRUE;
}

usize InterfaceRegistry_Find(
    const interface_registry_t *pRegistry,
    string_view_t name,
    u32 nMajorVersion ) noexcept
{
    if ( pRegistry == nullptr || !InterfaceNameIsValid( name ) ) {
        return CY_INVALID_SIZE;
    }
    for ( usize iFactory = 0u; iFactory < pRegistry->nCount; ++iFactory ) {
        const interface_factory_t &factory = pRegistry->pFactories[iFactory];
        if ( factory.nMajorVersion == nMajorVersion &&
             InterfaceNameEquals( factory, name ) ) {
            return iFactory;
        }
    }
    return CY_INVALID_SIZE;
}

} // namespace

interface_registry_t *InterfaceRegistry_Create(
    const allocator_t *pAllocator,
    usize nInitialFactories ) noexcept
{
    if ( !Allocator_IsValid( pAllocator ) ) {
        return nullptr;
    }
    auto *pRegistry = static_cast<interface_registry_t *>( Allocator_AllocateZeroed(
        pAllocator,
        sizeof( interface_registry_t ),
        alignof( interface_registry_t ) ) );
    if ( pRegistry == nullptr ) {
        return nullptr;
    }
    pRegistry->pAllocator = pAllocator;
    if ( nInitialFactories > 0u &&
         !InterfaceRegistry_Reserve( pRegistry, nInitialFactories ) ) {
        Allocator_Free(
            pAllocator,
            pRegistry,
            sizeof( interface_registry_t ),
            alignof( interface_registry_t ) );
        return nullptr;
    }
    return pRegistry;
}

void InterfaceRegistry_Destroy( interface_registry_t *pRegistry ) noexcept
{
    if ( pRegistry == nullptr ) {
        return;
    }
    const allocator_t *pAllocator = pRegistry->pAllocator;
    for ( usize iFactory = 0u; iFactory < pRegistry->nCount; ++iFactory ) {
        const interface_factory_t &factory = pRegistry->pFactories[iFactory];
        Allocator_Free(
            pAllocator,
            factory.pName,
            factory.cchName + 1u,
            alignof( char ) );
    }
    Allocator_Free(
        pAllocator,
        pRegistry->pFactories,
        pRegistry->nCapacity * sizeof( interface_factory_t ),
        alignof( interface_factory_t ) );
    Allocator_Free(
        pAllocator,
        pRegistry,
        sizeof( interface_registry_t ),
        alignof( interface_registry_t ) );
}

bool_t InterfaceRegistry_Register(
    interface_registry_t *pRegistry,
    const interface_factory_desc_t &factory ) noexcept
{
    if ( pRegistry == nullptr || !InterfaceNameIsValid( factory.provided.name ) ||
         factory.provided.nMajorVersion == 0u || factory.pfnCreate == nullptr ||
         InterfaceRegistry_Find(
             pRegistry,
             factory.provided.name,
             factory.provided.nMajorVersion ) != CY_INVALID_SIZE ) {
        return CY_FALSE;
    }
    if ( pRegistry->nCount == pRegistry->nCapacity ) {
        const usize nNewCapacity = pRegistry->nCapacity == 0u
            ? 8u
            : ( pRegistry->nCapacity <= CY_USIZE_MAX / 2u
                ? pRegistry->nCapacity * 2u
                : 0u );
        if ( nNewCapacity == 0u ||
             !InterfaceRegistry_Reserve( pRegistry, nNewCapacity ) ) {
            return CY_FALSE;
        }
    }
    if ( factory.provided.name.cchLength == CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    char *pName = static_cast<char *>( Allocator_Allocate(
        pRegistry->pAllocator,
        factory.provided.name.cchLength + 1u,
        alignof( char ) ) );
    if ( pName == nullptr ) {
        return CY_FALSE;
    }
    Cy_MemCopy(
        pName,
        factory.provided.name.pData,
        factory.provided.name.cchLength );
    pName[factory.provided.name.cchLength] = '\0';

    pRegistry->pFactories[pRegistry->nCount++] = {
        pName,
        factory.provided.name.cchLength,
        factory.provided.nMajorVersion,
        factory.provided.nMinorVersion,
        factory.pfnCreate,
        factory.pfnRelease,
        factory.pUserData
    };
    return CY_TRUE;
}

bool_t InterfaceRegistry_Unregister(
    interface_registry_t *pRegistry,
    string_view_t name,
    u32 nMajorVersion ) noexcept
{
    const usize iFactory = InterfaceRegistry_Find( pRegistry, name, nMajorVersion );
    if ( iFactory == CY_INVALID_SIZE ) {
        return CY_FALSE;
    }
    const interface_factory_t removed = pRegistry->pFactories[iFactory];
    for ( usize iMove = iFactory; iMove + 1u < pRegistry->nCount; ++iMove ) {
        pRegistry->pFactories[iMove] = pRegistry->pFactories[iMove + 1u];
    }
    --pRegistry->nCount;
    pRegistry->pFactories[pRegistry->nCount] = {};
    Allocator_Free(
        pRegistry->pAllocator,
        removed.pName,
        removed.cchName + 1u,
        alignof( char ) );
    return CY_TRUE;
}

void *InterfaceRegistry_CreateInterface(
    const interface_registry_t *pRegistry,
    const interface_id_t &requested,
    interface_release_fn_t *ppfnReleaseOut,
    void **ppReleaseUserDataOut ) noexcept
{
    if ( ppfnReleaseOut != nullptr ) {
        *ppfnReleaseOut = nullptr;
    }
    if ( ppReleaseUserDataOut != nullptr ) {
        *ppReleaseUserDataOut = nullptr;
    }
    const usize iFactory = InterfaceRegistry_Find(
        pRegistry,
        requested.name,
        requested.nMajorVersion );
    if ( iFactory == CY_INVALID_SIZE ) {
        return nullptr;
    }

    const interface_factory_t &factory = pRegistry->pFactories[iFactory];
    // Major versions must match exactly. A provider with a newer minor version
    // promises backward compatibility with the requested minor contract.
    if ( factory.nMinorVersion < requested.nMinorVersion ) {
        return nullptr;
    }
    void *pInterface = factory.pfnCreate( requested, factory.pUserData );
    if ( pInterface == nullptr ) {
        return nullptr;
    }
    // Return release metadata only after object construction succeeds; callers
    // can then destroy the instance through the same provider boundary.
    if ( ppfnReleaseOut != nullptr ) {
        *ppfnReleaseOut = factory.pfnRelease;
    }
    if ( ppReleaseUserDataOut != nullptr ) {
        *ppReleaseUserDataOut = factory.pUserData;
    }
    return pInterface;
}

} // namespace cypher::common
