//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherResource/CypherResource.cpp
//  Purpose: Implements resource-manager lifecycle and public diagnostics.
//  Details: Initialization allocates every manager table as one aligned block.
//           Shutdown unloads live resources in reverse acquisition order before
//           releasing that block through the configured allocator.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource_Internal.h"

#include <cstddef>
#include <new>

namespace cypher::engine::resource
{

//==========================================================================
// Manager construction and destruction
//==========================================================================

resource_manager_config_t CypherResource_DefaultConfig() noexcept
{
    return {
        CYPHER_RESOURCE_DEFAULT_CAPACITY,
        CYPHER_RESOURCE_DEFAULT_TYPE_CAPACITY,
        common::Allocator_GetSystem()
    };
}

resource_error_t CypherResource_Init(
    resource_manager_t *pManager,
    const resource_manager_config_t &config ) noexcept
{
    using namespace detail;

    if ( pManager == nullptr ) {
        return resource_error_t::INVALID_ARGUMENT;
    }
    if ( pManager->pImplementation != nullptr ) {
        return resource_error_t::ALREADY_INITIALIZED;
    }
    if ( config.cResourceCapacity == 0u ||
         config.cResourceCapacity > CYPHER_RESOURCE_MAX_CAPACITY ||
         config.cTypeCapacity == 0u ||
         config.cTypeCapacity > common::CY_RESOURCE_TYPE_SLOT_MAX ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    const common::allocator_t *pAllocator = config.pAllocator != nullptr
        ? config.pAllocator
        : common::Allocator_GetSystem();
    if ( !common::Allocator_IsValid( pAllocator ) ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    resource_manager_layout_t layout{};
    // Layout calculation performs all size and alignment overflow checks before
    // the manager asks the allocator for a single backing block.
    if ( !CalculateLayout( config, layout ) ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    void *pAllocation = common::Allocator_AllocateZeroed(
        pAllocator,
        layout.cbTotal,
        alignof( std::max_align_t ) );
    if ( pAllocation == nullptr ) {
        return resource_error_t::ALLOCATION_FAILED;
    }

    auto *pBytes = static_cast<common::byte *>( pAllocation );
    // The implementation object and all fixed-capacity tables share this block.
    // No further manager allocation is required after successful initialization.
    auto *pImpl = new ( pAllocation ) resource_manager_impl_t{};
    pImpl->pRecords = reinterpret_cast<resource_record_t *>(
        pBytes + layout.iRecordsOffset );
    pImpl->pTypes = reinterpret_cast<resource_type_entry_t *>(
        pBytes + layout.iTypesOffset );
    pImpl->pLookup = reinterpret_cast<common::u32 *>(
        pBytes + layout.iLookupOffset );
    pImpl->cResourceCapacity = config.cResourceCapacity;
    pImpl->cTypeCapacity = config.cTypeCapacity;
    pImpl->cLookupCapacity = layout.cLookupCapacity;
    pImpl->iFreeHead = 0u;
    pImpl->stats.cResourceCapacity = config.cResourceCapacity;
    pImpl->stats.cTypeCapacity = config.cTypeCapacity;

    // Every unused record begins on a singly linked free list.
    for ( common::u32 iRecord = 0u;
          iRecord < config.cResourceCapacity;
          ++iRecord ) {
        new ( &pImpl->pRecords[iRecord] ) resource_record_t{};
        pImpl->pRecords[iRecord].iNextFree =
            iRecord + 1u < config.cResourceCapacity
                ? iRecord + 1u
                : CYPHER_RESOURCE_INVALID_INDEX;
    }
    for ( common::u32 iType = 0u;
          iType < config.cTypeCapacity;
          ++iType ) {
        new ( &pImpl->pTypes[iType] ) resource_type_entry_t{};
    }

    pManager->pImplementation = pImpl;
    pManager->pAllocator = pAllocator;
    pManager->cbAllocation = layout.cbTotal;
    return resource_error_t::OK;
}

resource_error_t CypherResource_Shutdown(
    resource_manager_t *pManager ) noexcept
{
    using namespace detail;

    resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }
    if ( pImpl->cCallbackDepth != 0u ) {
        return resource_error_t::REENTRANT_LIFECYCLE;
    }

    pImpl->bShuttingDown = common::CY_TRUE;
    // Reverse acquisition order protects dependencies: users unload before the
    // resources they acquired during their own load callbacks.
    while ( pImpl->iLiveTail != CYPHER_RESOURCE_INVALID_INDEX ) {
        const resource_error_t unloadResult = UnloadRecord(
            *pImpl,
            pImpl->iLiveTail );
        if ( unloadResult != resource_error_t::OK ) {
            pImpl->bShuttingDown = common::CY_FALSE;
            return unloadResult;
        }
    }

    // Preserve allocator metadata before clearing the public manager shell.
    const common::allocator_t *pAllocator = pManager->pAllocator;
    const common::usize cbAllocation = pManager->cbAllocation;
    for ( common::u32 iType = 0u;
          iType < pImpl->cTypeCapacity;
          ++iType ) {
        pImpl->pTypes[iType].~resource_type_entry_t();
    }
    for ( common::u32 iRecord = 0u;
          iRecord < pImpl->cResourceCapacity;
          ++iRecord ) {
        pImpl->pRecords[iRecord].~resource_record_t();
    }
    pImpl->~resource_manager_impl_t();
    pManager->pImplementation = nullptr;
    pManager->pAllocator = nullptr;
    pManager->cbAllocation = 0u;
    common::Allocator_Free(
        pAllocator,
        pImpl,
        cbAllocation,
        alignof( std::max_align_t ) );
    return resource_error_t::OK;
}

common::bool_t CypherResource_IsInitialized(
    const resource_manager_t *pManager ) noexcept
{
    return detail::ManagerImpl( pManager ) != nullptr;
}

// Keep this switch exhaustive and stable; tool diagnostics and tests use these
// symbolic names without allocating temporary strings.
const char *CypherResource_ErrorName( resource_error_t error ) noexcept
{
    switch ( error ) {
        case resource_error_t::OK: return "OK";
        case resource_error_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case resource_error_t::NOT_INITIALIZED: return "NOT_INITIALIZED";
        case resource_error_t::ALREADY_INITIALIZED: return "ALREADY_INITIALIZED";
        case resource_error_t::ALLOCATION_FAILED: return "ALLOCATION_FAILED";
        case resource_error_t::CAPACITY_EXCEEDED: return "CAPACITY_EXCEEDED";
        case resource_error_t::TYPE_CAPACITY_EXCEEDED: return "TYPE_CAPACITY_EXCEEDED";
        case resource_error_t::TYPE_ALREADY_REGISTERED: return "TYPE_ALREADY_REGISTERED";
        case resource_error_t::TYPE_NOT_REGISTERED: return "TYPE_NOT_REGISTERED";
        case resource_error_t::TYPE_IN_USE: return "TYPE_IN_USE";
        case resource_error_t::PATH_TOO_LONG: return "PATH_TOO_LONG";
        case resource_error_t::ID_COLLISION: return "ID_COLLISION";
        case resource_error_t::LOAD_FAILED: return "LOAD_FAILED";
        case resource_error_t::INVALID_HANDLE: return "INVALID_HANDLE";
        case resource_error_t::RESOURCE_BUSY: return "RESOURCE_BUSY";
        case resource_error_t::DEPENDENCY_CYCLE: return "DEPENDENCY_CYCLE";
        case resource_error_t::REFERENCE_OVERFLOW: return "REFERENCE_OVERFLOW";
        case resource_error_t::REENTRANT_LIFECYCLE: return "REENTRANT_LIFECYCLE";
        case resource_error_t::INTERNAL_ERROR: return "INTERNAL_ERROR";
    }
    return "UNKNOWN_RESOURCE_ERROR";
}

} // namespace cypher::engine::resource
