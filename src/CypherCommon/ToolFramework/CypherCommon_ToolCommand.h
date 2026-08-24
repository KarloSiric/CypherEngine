//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCommand.h
//  Purpose: Declares commands exposed by CLI and graphical tool hosts.
//  Details: Commands describe intent and accepted options; application frontends
//           remain responsible for parsing terminal syntax or constructing forms.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Command Contract

Commands declare accepted options and execution callbacks without depending on terminal or GUI
presentation. Dispatch validates the invocation before entering tool-specific code.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMMAND_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMMAND_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolOption.h"

namespace cypher::common
{

enum tool_command_flags_t : flags32_t {
    TOOL_COMMAND_FLAG_NONE = 0u,                         // No optional command policy.
    TOOL_COMMAND_FLAG_PROJECT_REQUIRED = CYPHER_BIT32( 0 ), // Requires resolved project context.
    TOOL_COMMAND_FLAG_ACCEPTS_INPUTS = CYPHER_BIT32( 1 ),   // Positional resource inputs are valid.
    TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS = CYPHER_BIT32( 2 ), // More than one input is valid.
    TOOL_COMMAND_FLAG_SUPPORTS_DRY_RUN = CYPHER_BIT32( 3 ), // Can validate without publishing.
    TOOL_COMMAND_FLAG_HIDDEN = CYPHER_BIT32( 4 )             // Omit from normal discovery/help.
};

struct tool_command_desc_t {
    string_view_t name{};    // Stable CLI/discovery command name.
    string_view_t summary{}; // One-line help description.
    string_view_t usage{};   // Positional usage suffix.
    const tool_option_desc_t *pOptions{ nullptr }; // Borrowed option descriptor array.
    usize nOptions{ 0u };    // Number of entries in pOptions.
    flags32_t flags{ TOOL_COMMAND_FLAG_NONE }; // tool_command_flags_t bitset.
    string_view_t details{}; // Optional long-form command help.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCommand_CheckDescriptor(
    const tool_command_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_option_desc_t *ToolCommand_FindOption(
    const tool_command_desc_t &desc,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_option_desc_t *ToolCommand_FindShortOption(
    const tool_command_desc_t &desc,
    char shortName ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCOMMAND_H
