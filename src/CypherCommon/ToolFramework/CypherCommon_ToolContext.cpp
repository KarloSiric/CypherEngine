//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolContext.cpp
//  Purpose: Implements validation for one immutable tool invocation context.
//  Details: Validation checks structural invariants only; project loading and
//           filesystem resolution remain responsibilities of the invoking host.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolContext.h"

namespace cypher::common
{

tool_status_t ToolContext_Validate( const tool_context_t &context ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_CONTEXT_FLAG_INTERACTIVE |
        TOOL_CONTEXT_FLAG_AUTOMATION |
        TOOL_CONTEXT_FLAG_OFFLINE |
        TOOL_CONTEXT_FLAG_REPRODUCIBLE;

    if ( !StringView_IsValid( context.applicationId ) ||
         context.applicationId.cchLength == 0u ||
         !StringView_IsValid( context.projectFile ) ||
         !StringView_IsValid( context.workingDirectory ) ||
         !StringView_IsValid( context.sourceRoot ) ||
         !StringView_IsValid( context.outputRoot ) ||
         !StringView_IsValid( context.cacheRoot ) ||
         !ToolTarget_IsValid( context.target ) ||
         !ToolProfile_IsValid( context.profile ) ||
         ( context.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    if ( ( context.flags & TOOL_CONTEXT_FLAG_INTERACTIVE ) != 0u &&
         ( context.flags & TOOL_CONTEXT_FLAG_AUTOMATION ) != 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    return tool_status_t::OK;
}

} // namespace cypher::common
