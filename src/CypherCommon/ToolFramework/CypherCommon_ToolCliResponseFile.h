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

inline constexpr usize CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_DEPTH = 16u; // Nested @file include limit.
inline constexpr usize CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_ARGUMENTS = 65536u; // Expanded argv limit.
inline constexpr usize CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_BYTES = 16u * CY_MIB; // Owned text limit.

struct tool_cli_response_file_options_t {
    usize nMaxDepth{ CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_DEPTH }; // Zero rejects all @file expansion.
    usize nMaxArguments{ CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_ARGUMENTS }; // Includes direct arguments.
    usize cbMaxExpandedText{ CY_TOOL_RESPONSE_FILE_DEFAULT_MAX_BYTES }; // Sum of copied argument bytes.
    bool_t bAllowComments{ CY_TRUE }; // Enables response-file line comments outside quoted text.
};

struct tool_cli_response_file_result_t {
    vector_t<owned_allocation_t> ownedArguments{}; // Owns every copied argument string.
    vector_t<string_view_t> arguments{};           // Views into ownedArguments in argv order.
    text_buffer_t errorPath{};                     // Path of the response file that failed.
    usize nErrorLine{ 0u };                        // One-based source line; zero when unavailable.
    usize nErrorColumn{ 0u };                      // One-based source column; zero when unavailable.
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
