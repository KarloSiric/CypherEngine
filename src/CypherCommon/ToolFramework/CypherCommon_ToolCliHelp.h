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

/*
================
Tool Cli Help Contract

Presentation is a host concern layered over structured tool events. Terminal width, ANSI color,
and verbosity affect rendering only, never compiler decisions.
================
*/

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
    u32 nColumns{ 100u };                 // Preferred wrapping width; writers enforce a usable minimum.
    bool_t bIncludeHidden{ CY_FALSE };     // Include internal commands and options when true.
    bool_t bIncludeDefaults{ CY_TRUE };    // Append descriptor defaults to option descriptions.
    bool_t bUseColor{ CY_FALSE };          // Permit terminal emphasis sequences in generated text.
    string_view_t version{};               // Borrowed product version displayed in application help.
    string_view_t epilogue{};              // Optional borrowed text written after generated sections.
};

struct tool_cli_help_result_t {
    tool_status_t status{ tool_status_t::OK }; // OK or BUFFER_TOO_SMALL for a valid truncated write.
    usize cchWritten{ 0u };                    // Characters stored, excluding the trailing null.
    usize cchRequired{ 0u };                   // Complete size required, excluding the trailing null.
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
