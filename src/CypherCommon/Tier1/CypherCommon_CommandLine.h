//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandLine.h
//  Purpose: Declares a borrowed process-command-line query view.
//  Details: CommandLine references argv storage supplied by the host. It performs no
//           allocation and does not reinterpret shell quoting after process startup.
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

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_Init(
    command_line_t *pCommandLine,
    i32 argc,
    const char *const *ppArgv ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t CommandLine_Argument(
    const command_line_t *pCommandLine,
    i32 iArgument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
i32 CommandLine_FindSwitch(
    const command_line_t *pCommandLine,
    string_view_t name,
    bool_t bCaseInsensitiveAscii = CY_TRUE ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandLine_HasSwitch(
    const command_line_t *pCommandLine,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t CommandLine_SwitchValue(
    const command_line_t *pCommandLine,
    string_view_t name ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDLINE_H
