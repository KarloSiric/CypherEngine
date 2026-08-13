//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolOutput.cpp
//  Purpose: Implements shared tool output-policy validation and naming.
//  Details: JSON progress and diagnostics disable ambiguous interactive terminal
//           behavior while leaving physical stream ownership to the frontend.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolOutput.h"

namespace cypher::common
{

tool_status_t ToolOutput_ValidatePolicy(
    const tool_output_policy_t &policy ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_OUTPUT_FLAG_COLOR |
        TOOL_OUTPUT_FLAG_TIMESTAMPS |
        TOOL_OUTPUT_FLAG_WARNINGS_AS_ERRORS |
        TOOL_OUTPUT_FLAG_FLUSH_EACH_RECORD |
        TOOL_OUTPUT_FLAG_FORCE_COLOR;

    if ( policy.diagnosticsFormat > tool_output_format_t::JSON ||
         policy.progressMode > tool_progress_mode_t::NONE ||
         policy.verbosity > tool_verbosity_t::TRACE ||
         ( policy.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( policy.diagnosticsFormat == tool_output_format_t::JSON &&
         ( policy.flags &
           ( TOOL_OUTPUT_FLAG_COLOR | TOOL_OUTPUT_FLAG_FORCE_COLOR ) ) != 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    if ( ( policy.flags & TOOL_OUTPUT_FLAG_FORCE_COLOR ) != 0u &&
         ( policy.flags & TOOL_OUTPUT_FLAG_COLOR ) == 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    if ( policy.diagnosticsFormat == tool_output_format_t::JSON &&
         policy.progressMode != tool_progress_mode_t::JSON &&
         policy.progressMode != tool_progress_mode_t::NONE ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    return tool_status_t::OK;
}

tool_status_t ToolOutput_ValidateSink( const tool_text_sink_t &sink ) noexcept
{
    return sink.pfnWrite != nullptr
        ? tool_status_t::OK
        : tool_status_t::INVALID_ARGUMENT;
}

tool_status_t ToolOutput_WriteText(
    const tool_text_sink_t &sink,
    string_view_t text ) noexcept
{
    if ( ToolStatus_Failed( ToolOutput_ValidateSink( sink ) ) ||
         !StringView_IsValid( text ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( text.cchLength == 0u ) {
        return tool_status_t::OK;
    }
    return sink.pfnWrite( text, sink.pUserData )
        ? tool_status_t::OK
        : tool_status_t::IO_ERROR;
}

const char *ToolOutput_FormatName( tool_output_format_t format ) noexcept
{
    switch ( format ) {
        case tool_output_format_t::TEXT: return "text";
        case tool_output_format_t::JSON: return "json";
    }
    return "unknown";
}

const char *ToolOutput_ProgressModeName( tool_progress_mode_t mode ) noexcept
{
    switch ( mode ) {
        case tool_progress_mode_t::AUTO: return "auto";
        case tool_progress_mode_t::PLAIN: return "plain";
        case tool_progress_mode_t::JSON: return "json";
        case tool_progress_mode_t::NONE: return "none";
    }
    return "unknown";
}

} // namespace cypher::common
