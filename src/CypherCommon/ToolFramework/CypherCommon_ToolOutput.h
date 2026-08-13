//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolOutput.h
//  Purpose: Declares output and progress policies shared by tool frontends.
//  Details: The contract distinguishes presentation mode from operation behavior;
//           terminal escape sequences and Qt rendering remain host implementations.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLOUTPUT_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLOUTPUT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_Defines.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

using tool_text_write_fn_t = bool_t ( * )(
    string_view_t text,
    void *pUserData ) noexcept;

struct tool_text_sink_t {
    tool_text_write_fn_t pfnWrite{ nullptr };
    void *pUserData{ nullptr };
};

enum class tool_output_format_t : u8 {
    TEXT = 0u,
    JSON
};

enum class tool_progress_mode_t : u8 {
    AUTO = 0u,
    PLAIN,
    JSON,
    NONE
};

enum class tool_verbosity_t : u8 {
    QUIET = 0u,
    NORMAL,
    VERBOSE,
    TRACE
};

enum tool_output_flags_t : flags32_t {
    TOOL_OUTPUT_FLAG_NONE = 0u,
    TOOL_OUTPUT_FLAG_COLOR = CYPHER_BIT32( 0 ),
    TOOL_OUTPUT_FLAG_TIMESTAMPS = CYPHER_BIT32( 1 ),
    TOOL_OUTPUT_FLAG_WARNINGS_AS_ERRORS = CYPHER_BIT32( 2 ),
    TOOL_OUTPUT_FLAG_FLUSH_EACH_RECORD = CYPHER_BIT32( 3 ),
    TOOL_OUTPUT_FLAG_FORCE_COLOR = CYPHER_BIT32( 4 )
};

struct tool_output_policy_t {
    tool_output_format_t diagnosticsFormat{ tool_output_format_t::TEXT };
    tool_progress_mode_t progressMode{ tool_progress_mode_t::AUTO };
    tool_verbosity_t verbosity{ tool_verbosity_t::NORMAL };
    flags32_t flags{ TOOL_OUTPUT_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolOutput_ValidatePolicy(
    const tool_output_policy_t &policy ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolOutput_ValidateSink( const tool_text_sink_t &sink ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolOutput_WriteText(
    const tool_text_sink_t &sink,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolOutput_FormatName( tool_output_format_t format ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolOutput_ProgressModeName( tool_progress_mode_t mode ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLOUTPUT_H
