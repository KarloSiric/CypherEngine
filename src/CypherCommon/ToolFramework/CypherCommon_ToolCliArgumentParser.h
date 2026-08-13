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
    EXECUTE = 0u,
    SHOW_HELP,
    SHOW_VERSION
};

struct tool_cli_parse_error_t {
    tool_status_t status{ tool_status_t::OK };
    usize iArgument{ CY_INVALID_SIZE };
    string_view_t argument{};
    string_view_t message{};
};

struct tool_cli_parse_result_t {
    const tool_command_desc_t *pCommand{ nullptr };
    tool_option_set_t options{};
    string_view_t *pInputs{ nullptr };
    usize nInputs{ 0u };
    usize nInputCapacity{ 0u };
    tool_cli_parse_action_t action{ tool_cli_parse_action_t::SHOW_HELP };
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
