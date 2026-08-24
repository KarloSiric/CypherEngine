//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCliTerminal.h
//  Purpose: Declares portable terminal access for command-line Cypher tools.
//  Details: Terminal owns no operating-system stream. It detects redirection,
//           color and cursor capabilities, exposes bounded writes, and adapts a
//           standard output stream to the host-neutral tool text sink contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLITERMINAL_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLITERMINAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolOutput.h"

namespace cypher::common
{

enum class tool_cli_stream_t : u8 {
    STANDARD_OUTPUT = 0u, // Normal command output and machine-readable records.
    STANDARD_ERROR        // Diagnostics and fatal host failures.
};

enum tool_cli_terminal_flags_t : flags32_t {
    TOOL_CLI_TERMINAL_FLAG_NONE = 0u,
    TOOL_CLI_TERMINAL_FLAG_INTERACTIVE = CYPHER_BIT32( 0 ),    // Stream is attached to a terminal.
    TOOL_CLI_TERMINAL_FLAG_COLOR = CYPHER_BIT32( 1 ),          // ANSI or native color is usable.
    TOOL_CLI_TERMINAL_FLAG_CURSOR_CONTROL = CYPHER_BIT32( 2 ), // In-place progress may move the cursor.
    TOOL_CLI_TERMINAL_FLAG_UNICODE = CYPHER_BIT32( 3 )         // Terminal can display UTF-8 safely.
};

struct tool_cli_terminal_t {
    uintptr nNativeHandle{ 0u }; // Borrowed HANDLE or POSIX file descriptor encoded as an integer.
    tool_cli_stream_t stream{ tool_cli_stream_t::STANDARD_OUTPUT }; // Selected process stream.
    u32 nColumns{ 0u };          // Width sampled during initialization; zero means unknown.
    u32 nOriginalMode{ 0u };     // Win32 console mode restored by Shutdown.
    flags32_t flags{ TOOL_CLI_TERMINAL_FLAG_NONE }; // Public capability bits.
    flags32_t internalFlags{ 0u }; // Private ownership and restoration state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliTerminal_Init(
    tool_cli_terminal_t *pTerminal,
    tool_cli_stream_t stream ) noexcept;

CYPHER_COMMON_API void ToolCliTerminal_Shutdown(
    tool_cli_terminal_t *pTerminal ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolCliTerminal_IsInteractive(
    const tool_cli_terminal_t *pTerminal ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolCliTerminal_SupportsColor(
    const tool_cli_terminal_t *pTerminal ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 ToolCliTerminal_Columns( const tool_cli_terminal_t *pTerminal ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliTerminal_Write(
    tool_cli_terminal_t *pTerminal,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliTerminal_Flush(
    tool_cli_terminal_t *pTerminal ) noexcept;

// Erases the current interactive line; redirected streams receive no control text.
CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolCliTerminal_ClearCurrentLine(
    tool_cli_terminal_t *pTerminal ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_text_sink_t ToolCliTerminal_AsSink(
    tool_cli_terminal_t *pTerminal ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCLITERMINAL_H
