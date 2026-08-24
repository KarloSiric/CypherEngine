//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCompiler.cpp
//  Purpose: Implements compiler descriptor, input, and execution validation.
//  Details: The wrapper rejects malformed requests before entering a compiler
//           callback and verifies the returned report before exposing it to hosts.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCompiler.h"

#include "CypherCommon_StringPath.h"

namespace cypher::common
{
namespace
{

bool_t IsCompilerId( string_view_t id ) noexcept
{
    if ( !StringView_IsValid( id ) || id.cchLength == 0u ) {
        return CY_FALSE;
    }
    // Restrict IDs to a portable cache-key and command-line alphabet.
    for ( usize i = 0u; i < id.cchLength; ++i ) {
        const char ch = id.pData[i];
        const bool_t bLetter = ch >= 'a' && ch <= 'z';
        const bool_t bDigit = ch >= '0' && ch <= '9';
        if ( !bLetter && !bDigit && ch != '-' && ch != '_' && ch != '.' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t IsSourceExtension( string_view_t extension ) noexcept
{
    // Extensions are suffixes, not paths; directory separators would make
    // dispatch dependent on source layout.
    return StringView_IsValid( extension ) && extension.cchLength > 1u &&
           extension.pData[0] == '.' &&
           StringView_FindChar( extension, '/', 0u ) == CY_STRING_VIEW_NPOS &&
           StringView_FindChar( extension, '\\', 0u ) == CY_STRING_VIEW_NPOS;
}

} // namespace

tool_status_t ToolCompiler_CheckDescriptor(
    const tool_compiler_desc_t &compiler ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_COMPILER_FLAG_DETERMINISTIC |
        TOOL_COMPILER_FLAG_THREAD_SAFE |
        TOOL_COMPILER_FLAG_INCREMENTAL |
        TOOL_COMPILER_FLAG_SUPPORTS_VALIDATE |
        TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN;

    if ( !IsCompilerId( compiler.id ) ||
         !StringView_IsValid( compiler.displayName ) ||
         compiler.displayName.cchLength == 0u ||
         !StringView_IsValid( compiler.resourceType ) ||
         compiler.resourceType.cchLength == 0u ||
         !IsSourceExtension( compiler.cookedExtension ) ||
         compiler.nSourceExtensions == 0u ||
         compiler.pSourceExtensions == nullptr ||
         compiler.nApiVersion == 0u || compiler.nCompilerVersion == 0u ||
         ( compiler.flags & ~knownFlags ) != 0u ||
         compiler.pfnExecute == nullptr ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    // Duplicate extensions within one compiler are configuration errors even
    // when their ASCII case differs.
    for ( usize i = 0u; i < compiler.nSourceExtensions; ++i ) {
        if ( !IsSourceExtension( compiler.pSourceExtensions[i] ) ) {
            return tool_status_t::INVALID_CONFIGURATION;
        }
        for ( usize j = 0u; j < i; ++j ) {
            if ( StringView_EqualsInsensitiveAscii(
                     compiler.pSourceExtensions[i],
                     compiler.pSourceExtensions[j] ) ) {
                return tool_status_t::INVALID_CONFIGURATION;
            }
        }
    }
    return tool_status_t::OK;
}

bool_t ToolCompiler_SupportsInput(
    const tool_compiler_desc_t &compiler,
    string_view_t input ) noexcept
{
    if ( ToolStatus_Failed( ToolCompiler_CheckDescriptor( compiler ) ) ||
         !StringView_IsValid( input ) || input.cchLength == 0u ) {
        return CY_FALSE;
    }
    // A compiler-specific probe may recognize containers whose extension is not
    // sufficient. Extension matching remains the deterministic fallback.
    if ( compiler.pfnProbe != nullptr &&
         compiler.pfnProbe( input, compiler.pUserData ) ) {
        return CY_TRUE;
    }

    const string_view_t extension = StringPath_Extension( input );
    for ( usize i = 0u; i < compiler.nSourceExtensions; ++i ) {
        if ( StringView_EqualsInsensitiveAscii(
                 extension,
                 compiler.pSourceExtensions[i] ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

tool_status_t ToolCompiler_Execute(
    const tool_compiler_desc_t &compiler,
    const tool_compile_request_t &request,
    tool_report_t *pReport ) noexcept
{
    if ( ToolStatus_Failed( ToolCompiler_CheckDescriptor( compiler ) ) ||
         request.pInvocation == nullptr || pReport == nullptr ||
         request.operationId == CY_TOOL_INVALID_OPERATION_ID ||
         !StringView_IsValid( request.input ) || request.input.cchLength == 0u ||
         !StringView_IsValid( request.output ) ||
         !StringView_IsValid( request.resourceType ) ||
         !StringView_Equals( request.resourceType, compiler.resourceType ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    const tool_status_t invocationStatus =
        ToolInvocation_Validate( *request.pInvocation );
    if ( ToolStatus_Failed( invocationStatus ) ) {
        return invocationStatus;
    }
    if ( !ToolCompiler_SupportsInput( compiler, request.input ) ) {
        return tool_status_t::UNSUPPORTED;
    }
    if ( ( request.pInvocation->flags & TOOL_INVOCATION_FLAG_DRY_RUN ) != 0u &&
         ( compiler.flags & TOOL_COMPILER_FLAG_SUPPORTS_DRY_RUN ) == 0u ) {
        return tool_status_t::UNSUPPORTED;
    }

    // The wrapper owns operation timing and final status. The compiler callback
    // fills counters, diagnostics, dependencies, and artifacts only.
    *pReport = {};
    const timer_tick_t nStartTicks = Cy_TimerNowTicks();
    tool_status_t status =
        compiler.pfnExecute( request, pReport, compiler.pUserData );
    if ( !ToolStatus_IsKnown( status ) ) {
        // Do not allow arbitrary callback integers to enter machine-readable
        // reports or process exit-code mapping.
        status = tool_status_t::INTERNAL_ERROR;
    }
    pReport->operationId = request.operationId;
    pReport->status = status;
    pReport->nStartTicks = nStartTicks;
    pReport->nEndTicks = Cy_TimerNowTicks();
    if ( pReport->nEndTicks < pReport->nStartTicks ) {
        pReport->nEndTicks = pReport->nStartTicks;
    }

    // Validate callback output before forwarding it to a host boundary.
    const tool_status_t reportStatus = ToolReport_Validate( *pReport );
    if ( ToolStatus_Failed( reportStatus ) ) {
        return tool_status_t::INTERNAL_ERROR;
    }
    ToolHost_EmitReport( request.pInvocation->pHost, *pReport );
    return status;
}

} // namespace cypher::common
