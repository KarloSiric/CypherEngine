//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliHelp.h
//  Purpose: Declares generated command-line help for Cypher tools.
//  Details: Help is derived from application, command, and option descriptors so
//           executable usage cannot drift from the contracts consumed by parsers
//           and graphical option forms.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIHELP_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIHELP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolApplication.h"
#include "CypherCommon_ToolCommand.h"

namespace cypher::common
{

struct tool_cli_help_options_t {
    u32 nColumns{ 100u };
    bool_t bIncludeHidden{ CY_FALSE };
    bool_t bIncludeDefaults{ CY_TRUE };
    bool_t bUseColor{ CY_FALSE };
    string_view_t version{};
    string_view_t epilogue{};
};

struct tool_cli_help_result_t {
    tool_status_t status{ tool_status_t::OK };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_cli_help_result_t ToolCliHelp_WriteApplication(
    const tool_application_desc_t &application,
    const tool_command_desc_t *pCommands,
    usize nCommands,
    const tool_cli_help_options_t &options,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_cli_help_result_t ToolCliHelp_WriteCommand(
    const tool_application_desc_t &application,
    const tool_command_desc_t &command,
    const tool_cli_help_options_t &options,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLIHELP_H
