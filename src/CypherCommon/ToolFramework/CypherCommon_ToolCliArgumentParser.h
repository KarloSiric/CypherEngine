//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliArgumentParser.h
//  Purpose: Declares typed parsing for Cypher command-line tool arguments.
//  Details: The parser consumes already-expanded argument views, resolves option
//           precedence, and writes into caller-owned storage without retaining
//           process argv or response-file ownership.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIARGUMENTPARSER_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIARGUMENTPARSER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolCommand.h"
#include "CypherCommon_ToolOptionSet.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

enum class tool_cli_parse_action_t : u8 {
    EXECUTE = 0u, // Invoke the selected command after validation.
    SHOW_HELP,    // Print application or command help without executing work.
    SHOW_VERSION // Print version metadata without selecting a command.
};

struct tool_cli_parse_error_t {
    tool_status_t status{ tool_status_t::OK }; // Stable failure category returned by Parse.
    usize iArgument{ CY_INVALID_SIZE };        // Index in the argv[1..] view, if applicable.
    string_view_t argument{};                  // Borrowed spelling of the offending argument.
    string_view_t message{};                   // Static human-readable explanation.
};

struct tool_cli_parse_result_t {
    const tool_command_desc_t *pCommand{ nullptr }; // Borrowed descriptor selected by the first token.
    tool_option_set_t options{};                    // Values written into caller-provided storage.
    string_view_t *pInputs{ nullptr };              // Caller-owned positional-input array.
    usize nInputs{ 0u };                            // Positional inputs written by the parser.
    usize nInputCapacity{ 0u };                     // Maximum entries available in pInputs.
    tool_cli_parse_action_t action{ tool_cli_parse_action_t::SHOW_HELP }; // Requested host action.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliArgumentParser_InitResult(
    tool_cli_parse_result_t *pResult,
    tool_option_value_t *pOptionStorage,
    usize nOptionCapacity,
    string_view_t *pInputStorage,
    usize nInputCapacity ) noexcept;

CYPHER_COMMON_API void ToolCliArgumentParser_ClearResult(
    tool_cli_parse_result_t *pResult ) noexcept;

// Arguments exclude argv[0]. The first non-built-in argument selects a command.
CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliArgumentParser_Parse(
    span_t<const string_view_t> arguments,
    const tool_command_desc_t *pCommands,
    usize nCommands,
    tool_cli_parse_result_t *pResult,
    tool_cli_parse_error_t *pErrorOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIARGUMENTPARSER_H
