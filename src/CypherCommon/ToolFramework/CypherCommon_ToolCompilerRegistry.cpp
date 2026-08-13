//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCompilerRegistry.cpp
//  Purpose: Implements caller-owned compiler registration and lookup.
//  Details: Registration order remains stable for diagnostics, but ambiguous
//           automatic dispatch fails instead of silently choosing order-dependent
//           compiler behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCompilerRegistry.h"

namespace cypher::common
{
namespace
{

bool_t RegistryIsValid( const tool_compiler_registry_t *pRegistry ) noexcept
{
    return pRegistry != nullptr &&
           pRegistry->nCount <= pRegistry->nCapacity &&
           ( pRegistry->nCapacity == 0u || pRegistry->ppCompilers != nullptr );
}

} // namespace

tool_status_t ToolCompilerRegistry_Init(
    tool_compiler_registry_t *pRegistry,
    const tool_compiler_desc_t **ppStorage,
    usize nCapacity ) noexcept
{
    if ( pRegistry == nullptr || ( nCapacity != 0u && ppStorage == nullptr ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    *pRegistry = { ppStorage, 0u, nCapacity };
    return tool_status_t::OK;
}

void ToolCompilerRegistry_Clear( tool_compiler_registry_t *pRegistry ) noexcept
{
    if ( pRegistry != nullptr ) {
        pRegistry->nCount = 0u;
    }
}

tool_status_t ToolCompilerRegistry_Register(
    tool_compiler_registry_t *pRegistry,
    const tool_compiler_desc_t *pCompiler ) noexcept
{
    if ( !RegistryIsValid( pRegistry ) || pCompiler == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    const tool_status_t status = ToolCompiler_CheckDescriptor( *pCompiler );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( ToolCompilerRegistry_FindById( pRegistry, pCompiler->id ) != nullptr ) {
        return tool_status_t::ALREADY_EXISTS;
    }
    if ( pRegistry->nCount == pRegistry->nCapacity ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    pRegistry->ppCompilers[pRegistry->nCount++] = pCompiler;
    return tool_status_t::OK;
}

const tool_compiler_desc_t *ToolCompilerRegistry_FindById(
    const tool_compiler_registry_t *pRegistry,
    string_view_t id ) noexcept
{
    if ( !RegistryIsValid( pRegistry ) || !StringView_IsValid( id ) ) {
        return nullptr;
    }
    for ( usize i = 0u; i < pRegistry->nCount; ++i ) {
        const tool_compiler_desc_t *pCompiler = pRegistry->ppCompilers[i];
        if ( pCompiler != nullptr && StringView_Equals( pCompiler->id, id ) ) {
            return pCompiler;
        }
    }
    return nullptr;
}

tool_status_t ToolCompilerRegistry_FindForInput(
    const tool_compiler_registry_t *pRegistry,
    string_view_t input,
    const tool_compiler_desc_t **ppCompilerOut ) noexcept
{
    if ( ppCompilerOut == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    *ppCompilerOut = nullptr;
    if ( !RegistryIsValid( pRegistry ) || !StringView_IsValid( input ) ||
         input.cchLength == 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    const tool_compiler_desc_t *pMatch = nullptr;
    for ( usize i = 0u; i < pRegistry->nCount; ++i ) {
        const tool_compiler_desc_t *pCompiler = pRegistry->ppCompilers[i];
        if ( pCompiler != nullptr &&
             ToolCompiler_SupportsInput( *pCompiler, input ) ) {
            if ( pMatch != nullptr ) {
                return tool_status_t::INVALID_CONFIGURATION;
            }
            pMatch = pCompiler;
        }
    }
    if ( pMatch == nullptr ) {
        return tool_status_t::NOT_FOUND;
    }
    *ppCompilerOut = pMatch;
    return tool_status_t::OK;
}

const tool_compiler_desc_t *ToolCompilerRegistry_At(
    const tool_compiler_registry_t *pRegistry,
    usize iCompiler ) noexcept
{
    return RegistryIsValid( pRegistry ) && iCompiler < pRegistry->nCount
        ? pRegistry->ppCompilers[iCompiler]
        : nullptr;
}

} // namespace cypher::common
