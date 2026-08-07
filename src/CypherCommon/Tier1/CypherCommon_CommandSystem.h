//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandSystem.h
//  Purpose: Declares an instance-owned command and ConVar registry.
//  Details: CommandSystem has no process-global singleton. It owns registered names and
//           runtime string values through an explicit allocator.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COMMANDSYSTEM_H
#define CYPHER_COMMON_TIER1_COMMANDSYSTEM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_ConCommand.h"
#include "CypherCommon_ConVar.h"

namespace cypher::common
{

using command_handle_t = handle32_t;
using convar_handle_t = handle32_t;

using command_output_fn_t = void ( * )(
    string_view_t text,
    void *pUserData ) noexcept;

struct command_system_desc_t {
    const allocator_t *pAllocator{ nullptr };
    usize nInitialCommands{ 128u };
    usize nInitialConVars{ 256u };
    bool_t bCaseInsensitiveAscii{ CY_TRUE };
    command_output_fn_t pfnOutput{ nullptr };
    void *pOutputUserData{ nullptr };
};

struct command_system_t;

CYPHER_NODISCARD CYPHER_COMMON_API
command_system_t *CommandSystem_Create(
    const command_system_desc_t &desc ) noexcept;

CYPHER_COMMON_API void CommandSystem_Destroy(
    command_system_t *pSystem ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
command_handle_t CommandSystem_RegisterCommand(
    command_system_t *pSystem,
    const concommand_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
convar_handle_t CommandSystem_RegisterConVar(
    command_system_t *pSystem,
    const convar_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandSystem_UnregisterCommand(
    command_system_t *pSystem,
    command_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandSystem_UnregisterConVar(
    command_system_t *pSystem,
    convar_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
command_handle_t CommandSystem_FindCommand(
    const command_system_t *pSystem,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
convar_handle_t CommandSystem_FindConVar(
    const command_system_t *pSystem,
    string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
error_code_t CommandSystem_ExecuteLine(
    command_system_t *pSystem,
    string_view_t commandLine,
    const command_context_t &context ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandSystem_GetConVar(
    const command_system_t *pSystem,
    convar_handle_t handle,
    convar_value_t *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
error_code_t CommandSystem_SetConVar(
    command_system_t *pSystem,
    convar_handle_t handle,
    string_view_t value,
    const command_context_t &context ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandSystem_Complete(
    command_system_t *pSystem,
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDSYSTEM_H
