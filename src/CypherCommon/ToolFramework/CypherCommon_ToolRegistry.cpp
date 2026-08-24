//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolRegistry.cpp
//  Purpose: Implements caller-owned tool product registration and lookup.
//  Details: Registration rejects malformed descriptors and duplicate stable IDs;
//           descriptor and storage lifetimes remain the caller's responsibility.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolRegistry.h"

namespace cypher::common
{

// The registry owns neither descriptors nor pointer storage. The host supplies a
// fixed array so registration stays allocation-free and deterministic.

tool_status_t ToolRegistry_Init(
    tool_registry_t *pRegistry,
    const tool_application_desc_t **ppStorage,
    usize nCapacity ) noexcept
{
    if ( pRegistry == nullptr || ( nCapacity != 0u && ppStorage == nullptr ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    *pRegistry = { ppStorage, 0u, nCapacity };
    return tool_status_t::OK;
}

void ToolRegistry_Clear( tool_registry_t *pRegistry ) noexcept
{
    if ( pRegistry != nullptr ) {
        // Descriptors remain alive; clearing only forgets the registered prefix.
        pRegistry->nCount = 0u;
    }
}

tool_status_t ToolRegistry_Register(
    tool_registry_t *pRegistry,
    const tool_application_desc_t *pApplication ) noexcept
{
    if ( pRegistry == nullptr || pApplication == nullptr ||
         ( pRegistry->nCapacity != 0u && pRegistry->ppEntries == nullptr ) ||
         pRegistry->nCount > pRegistry->nCapacity ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    const tool_status_t descriptorStatus =
        ToolApplication_CheckDescriptor( *pApplication );
    if ( ToolStatus_Failed( descriptorStatus ) ) {
        return descriptorStatus;
    }

    if ( ToolRegistry_Find( pRegistry, pApplication->id ) != nullptr ) {
        return tool_status_t::ALREADY_EXISTS;
    }
    if ( pRegistry->nCount == pRegistry->nCapacity ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }

    // Entries occupy a compact prefix, preserving registration order for help UI.
    pRegistry->ppEntries[pRegistry->nCount] = pApplication;
    ++pRegistry->nCount;
    return tool_status_t::OK;
}

const tool_application_desc_t *ToolRegistry_Find(
    const tool_registry_t *pRegistry,
    string_view_t id ) noexcept
{
    if ( pRegistry == nullptr || !StringView_IsValid( id ) ||
         pRegistry->nCount > pRegistry->nCapacity ||
         ( pRegistry->nCount != 0u && pRegistry->ppEntries == nullptr ) ) {
        return nullptr;
    }

    for ( usize i = 0u; i < pRegistry->nCount; ++i ) {
        const tool_application_desc_t *pEntry = pRegistry->ppEntries[i];
        if ( pEntry != nullptr && StringView_Equals( pEntry->id, id ) ) {
            return pEntry;
        }
    }
    return nullptr;
}

const tool_application_desc_t *ToolRegistry_At(
    const tool_registry_t *pRegistry,
    usize iApplication ) noexcept
{
    if ( pRegistry == nullptr || iApplication >= pRegistry->nCount ||
         pRegistry->ppEntries == nullptr ) {
        return nullptr;
    }
    return pRegistry->ppEntries[iApplication];
}

} // namespace cypher::common
