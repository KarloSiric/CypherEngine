//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliRunner.h
//  Purpose: Declares the standard process lifecycle for Cypher CLI tools.
//  Details: The runner owns command-line concerns only: response files, parsing,
//           help, terminal presentation, interrupt cancellation, and exit-code
//           mapping. Compiler and validator implementations remain host-neutral.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIRUNNER_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIRUNNER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolApplication.h"
#include "CypherCommon_ToolCliArgumentParser.h"
#include "CypherCommon_ToolCliResponseFile.h"
#include "CypherCommon_ToolHost.h"
#include "CypherCommon_ToolOutput.h"

namespace cypher::common
{

inline constexpr usize CY_TOOL_CLI_DEFAULT_OPTION_CAPACITY = 256u;
inline constexpr usize CY_TOOL_CLI_DEFAULT_INPUT_CAPACITY = 4096u;

using tool_cli_execute_fn_t = tool_status_t ( * )(
    const tool_cli_parse_result_t &arguments,
    const tool_host_t &host,
    const tool_output_policy_t &output,
    void *pUserData ) noexcept;

// Resolves presentation policy after command options have been parsed but before
// startup text or compiler records are emitted.
using tool_cli_output_policy_fn_t = tool_status_t ( * )(
    const tool_cli_parse_result_t &arguments,
    tool_output_policy_t *pPolicy,
    void *pUserData ) noexcept;

// Supplies product-owned identity text without coupling ToolFramework to one
// executable. All strings are borrowed for the duration of ToolCliRunner_Run.
struct tool_cli_presentation_t {
    string_view_t banner{};
    string_view_t startupSummary{};
    string_view_t applicationDetails{};
    // Product tools may use their full identity banner as the interactive
    // startup record. JSON and quiet output suppress it in the display layer.
    bool_t bShowBannerOnExecution{ CY_FALSE };
};

// Describes one command-line executable without coupling its commands to main().
struct tool_cli_run_desc_t {
    const tool_application_desc_t *pApplication{ nullptr };
    const tool_command_desc_t *pCommands{ nullptr };
    usize nCommands{ 0u };
    string_view_t version{};
    tool_cli_output_policy_fn_t pfnResolveOutputPolicy{ nullptr };
    tool_cli_execute_fn_t pfnExecute{ nullptr };
    void *pUserData{ nullptr };
    const tool_cli_presentation_t *pPresentation{ nullptr };
};

struct tool_cli_run_options_t {
    tool_output_policy_t output{};
    tool_cli_response_file_options_t responseFiles{};
    usize nOptionCapacity{ CY_TOOL_CLI_DEFAULT_OPTION_CAPACITY };
    usize nInputCapacity{ CY_TOOL_CLI_DEFAULT_INPUT_CAPACITY };
    bool_t bExpandResponseFiles{ CY_TRUE };
    bool_t bInstallInterruptHandler{ CY_TRUE };
    bool_t bWriteStartup{ CY_TRUE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliRunner_Validate(
    const tool_cli_run_desc_t &desc,
    const tool_cli_run_options_t &options ) noexcept;

// argv follows the operating-system main() contract and includes argv[0].
CYPHER_NODISCARD CYPHER_COMMON_API
tool_exit_code_t ToolCliRunner_Run(
    const tool_cli_run_desc_t &desc,
    i32 argc,
    const char *const *pArgv,
    const tool_cli_run_options_t &options = {} ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIRUNNER_H
