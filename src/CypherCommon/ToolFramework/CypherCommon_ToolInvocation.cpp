//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolInvocation.cpp
//  Purpose: Implements validation for resolved tool command invocations.
//  Details: Cross-field checks reject unsupported dry runs, contradictory force
//           policies, and input cardinality errors before work begins.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolInvocation.h"

namespace cypher::common
{

tool_status_t ToolInvocation_Validate(
    const tool_invocation_t &invocation ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_INVOCATION_FLAG_DRY_RUN |
        TOOL_INVOCATION_FLAG_FORCE_ROOTS |
        TOOL_INVOCATION_FLAG_FORCE_CLOSURE |
        TOOL_INVOCATION_FLAG_KEEP_GOING |
        TOOL_INVOCATION_FLAG_NO_CACHE;

    if ( invocation.pApplication == nullptr ||
         invocation.pCommand == nullptr ||
         invocation.pContext == nullptr ||
         invocation.pOptions == nullptr ||
         invocation.pHost == nullptr ||
         ( invocation.nInputs != 0u && invocation.pInputs == nullptr ) ||
         ( invocation.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    tool_status_t status =
        ToolApplication_CheckDescriptor( *invocation.pApplication );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    status = ToolCommand_CheckDescriptor( *invocation.pCommand );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    status = ToolContext_Validate( *invocation.pContext );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    status = ToolHost_Validate( *invocation.pHost );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    status = ToolOutput_ValidatePolicy( invocation.output );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }

    if ( !StringView_Equals(
             invocation.pApplication->id,
             invocation.pContext->applicationId ) ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    const flags32_t commandFlags = invocation.pCommand->flags;
    if ( ( commandFlags & TOOL_COMMAND_FLAG_PROJECT_REQUIRED ) != 0u &&
         invocation.pContext->projectFile.cchLength == 0u ) {
        return tool_status_t::INVALID_PROJECT;
    }
    if ( invocation.nInputs != 0u &&
         ( commandFlags & TOOL_COMMAND_FLAG_ACCEPTS_INPUTS ) == 0u ) {
        return tool_status_t::INVALID_COMMAND;
    }
    if ( invocation.nInputs > 1u &&
         ( commandFlags & TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS ) == 0u ) {
        return tool_status_t::INVALID_COMMAND;
    }
    if ( ( invocation.flags & TOOL_INVOCATION_FLAG_DRY_RUN ) != 0u &&
         ( commandFlags & TOOL_COMMAND_FLAG_SUPPORTS_DRY_RUN ) == 0u ) {
        return tool_status_t::INVALID_COMMAND;
    }
    if ( ( invocation.flags & TOOL_INVOCATION_FLAG_FORCE_ROOTS ) != 0u &&
         ( invocation.flags & TOOL_INVOCATION_FLAG_FORCE_CLOSURE ) != 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    for ( usize i = 0u; i < invocation.nInputs; ++i ) {
        if ( !StringView_IsValid( invocation.pInputs[i] ) ||
             invocation.pInputs[i].cchLength == 0u ) {
            return tool_status_t::INVALID_ARGUMENT;
        }
    }
    return tool_status_t::OK;
}

} // namespace cypher::common
