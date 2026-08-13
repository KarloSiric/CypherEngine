//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliResponseFile.h
//  Purpose: Declares response-file expansion for command-line Cypher tools.
//  Details: Response files provide reproducible large invocations without shell
//           length limits. Expansion owns copied UTF-8 argument text and supports
//           bounded recursive @file inclusion with cycle detection.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIRESPONSEFILE_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIRESPONSEFILE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_Vector.h"

namespace cypher::common
{

inline constexpr usize CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_DEPTH = 16u;
inline constexpr usize CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_ARGUMENTS = 65536u;
inline constexpr usize CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_BYTES = 16u * CY_MIB;

struct tool_cli_response_file_options_t {
    usize nMaxDepth{ CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_DEPTH };
    usize nMaxArguments{ CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_ARGUMENTS };
    usize cbMaxExpandedText{ CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_BYTES };
    bool_t bAllowComments{ CY_TRUE };
};

struct tool_cli_response_file_result_t {
    vector_t<owned_allocation_t> ownedArguments{};
    vector_t<string_view_t> arguments{};
    text_buffer_t errorPath{};
    usize nErrorLine{ 0u };
    usize nErrorColumn{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliResponseFile_InitResult(
    tool_cli_response_file_result_t *pResult,
    const allocator_t *pAllocator ) noexcept;

CYPHER_COMMON_API void ToolCliResponseFile_ShutdownResult(
    tool_cli_response_file_result_t *pResult ) noexcept;

// Expands @file arguments and copies every resulting argument into pResult.
CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliResponseFile_Expand(
    span_t<const string_view_t> arguments,
    const tool_cli_response_file_options_t &options,
    tool_cli_response_file_result_t *pResult ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIRESPONSEFILE_H
