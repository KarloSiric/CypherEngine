//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandLine.h
//  Purpose: Declares a borrowed process-command-line query view.
//  Details: CommandLine references argv storage supplied by the host. It performs no
//           allocation and does not reinterpret shell quoting after process startup.
//           Switch lookup supports explicit one- or two-dash names and bounded views.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COMMANDLINE_H
#define CYPHER_COMMON_TIER1_COMMANDLINE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct command_line_t {
    i32 nArgumentCount{ 0 };
    const char *const *ppArguments{ nullptr };
};

struct command_line_switch_t {
    i32 iArgument{ -1 };
    string_view_t name{};
    string_view_t value{};
    bool_t bHasValue{ CY_FALSE };
};

inline constexpr i32 CY_COMMAND_LINE_NOT_FOUND = -1;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_Init(
    command_line_t *pCommandLine,
    i32 argc,
    const char *const *ppArgv ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_IsValid( const command_line_t *pCommandLine ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
i32 CommandLine_Count( const command_line_t *pCommandLine ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t CommandLine_Program(
    const command_line_t *pCommandLine ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t CommandLine_Argument(
    const command_line_t *pCommandLine,
    i32 iArgument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_IsSwitchArgument(
    const command_line_t *pCommandLine,
    i32 iArgument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_FindSwitchInfo(
    const command_line_t *pCommandLine,
    string_view_t name,
    command_line_switch_t *pSwitchOut,
    bool_t bCaseInsensitiveAscii = CY_TRUE ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
i32 CommandLine_FindSwitch(
    const command_line_t *pCommandLine,
    string_view_t name,
    bool_t bCaseInsensitiveAscii = CY_TRUE ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_HasSwitch(
    const command_line_t *pCommandLine,
    string_view_t name,
    bool_t bCaseInsensitiveAscii = CY_TRUE ) noexcept;

// Returns true when the switch exists. bHasValue distinguishes a valueless switch
// from an explicitly empty value such as --name=.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_TrySwitchValue(
    const command_line_t *pCommandLine,
    string_view_t name,
    string_view_t *pValueOut,
    bool_t *pHasValueOut,
    bool_t bCaseInsensitiveAscii = CY_TRUE ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t CommandLine_SwitchValue(
    const command_line_t *pCommandLine,
    string_view_t name,
    bool_t bCaseInsensitiveAscii = CY_TRUE ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDLINE_H
