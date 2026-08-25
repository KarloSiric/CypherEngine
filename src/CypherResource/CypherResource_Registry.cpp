//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherResource/CypherResource_Registry.cpp
//  Purpose: Implements resource-loader type registration.
//  Details: Stable resource type IDs are associated with compact, non-reused
//           runtime type slots. A type cannot be removed while any of its
//           resources remains live or loading.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource_Internal.h"

namespace cypher::engine::resource
{

// Type slots are compact runtime identifiers embedded in resource handles. They
// increase monotonically and are never recycled during a manager lifetime.

resource_error_t Res_RegisterType(
    resource_manager_t *pManager,
    const resource_loader_t &loader,
    common::resource_type_slot_t *pTypeSlotOut ) noexcept
{
    using namespace detail;

    if ( pTypeSlotOut != nullptr ) {
        *pTypeSlotOut = common::CY_RESOURCE_TYPE_SLOT_INVALID;
    }

    resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }
    if ( pImpl->cCallbackDepth != 0u || pImpl->bShuttingDown ) {
        return resource_error_t::REENTRANT_LIFECYCLE;
    }
    if ( loader.type == 0u ||
         loader.pfnLoad == nullptr ||
         loader.pfnUnload == nullptr ) {
        return resource_error_t::INVALID_ARGUMENT;
    }
    if ( FindType( *pImpl, loader.type ) != nullptr ) {
        return resource_error_t::TYPE_ALREADY_REGISTERED;
    }
    if ( pImpl->cRegisteredTypes == pImpl->cTypeCapacity ||
         pImpl->iNextTypeSlot > common::CY_RESOURCE_TYPE_SLOT_MAX ) {
        return resource_error_t::TYPE_CAPACITY_EXCEEDED;
    }

    resource_type_entry_t &entry = pImpl->pTypes[pImpl->cRegisteredTypes];
    entry.loader = loader;
    entry.iTypeSlot = pImpl->iNextTypeSlot;
    ++pImpl->iNextTypeSlot;
    ++pImpl->cRegisteredTypes;
    pImpl->stats.cRegisteredTypes = pImpl->cRegisteredTypes;

    if ( pTypeSlotOut != nullptr ) {
        *pTypeSlotOut = entry.iTypeSlot;
    }
    return resource_error_t::OK;
}

resource_error_t Res_UnregisterType(
    resource_manager_t *pManager,
    common::resource_type_id_t type ) noexcept
{
    using namespace detail;

    resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }
    if ( pImpl->cCallbackDepth != 0u || pImpl->bShuttingDown ) {
        return resource_error_t::REENTRANT_LIFECYCLE;
    }
    if ( type == 0u ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    common::u32 iTypeFound = CYPHER_RESOURCE_INVALID_INDEX;
    for ( common::u32 iType = 0u;
          iType < pImpl->cRegisteredTypes;
          ++iType ) {
        if ( pImpl->pTypes[iType].loader.type == type ) {
            iTypeFound = iType;
            break;
        }
    }
    if ( iTypeFound == CYPHER_RESOURCE_INVALID_INDEX ) {
        return resource_error_t::TYPE_NOT_REGISTERED;
    }

    for ( common::u32 iRecord = 0u;
          iRecord < pImpl->cResourceCapacity;
          ++iRecord ) {
        if ( pImpl->pRecords[iRecord].state != resource_state_t::EMPTY &&
             pImpl->pRecords[iRecord].type == type ) {
            // The record may still call this type's unload callback.
            return resource_error_t::TYPE_IN_USE;
        }
    }

    // Registration is cold-path work; compact the short array instead of adding
    // tombstones and another branch to every type lookup.
    for ( common::u32 iType = iTypeFound;
          iType + 1u < pImpl->cRegisteredTypes;
          ++iType ) {
        pImpl->pTypes[iType] = pImpl->pTypes[iType + 1u];
    }
    --pImpl->cRegisteredTypes;
    pImpl->pTypes[pImpl->cRegisteredTypes] = {};
    pImpl->stats.cRegisteredTypes = pImpl->cRegisteredTypes;
    return resource_error_t::OK;
}

} // namespace cypher::engine::resource
