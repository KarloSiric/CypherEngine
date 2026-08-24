//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandSystem.h
//  Purpose: Declares an instance-owned command and ConVar registry.
//  Details: CommandSystem has no process-global singleton. It owns registered metadata
//           and runtime string values, uses one namespace for commands and ConVars,
//           and returns generational handles that reject stale references.
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

inline constexpr command_handle_t CY_COMMAND_HANDLE_INVALID = CY_HANDLE32_INVALID;
inline constexpr convar_handle_t CY_CONVAR_HANDLE_INVALID = CY_HANDLE32_INVALID;
inline constexpr usize CY_COMMAND_MAX_EXECUTION_DEPTH = 16u;

enum class command_system_error_t : u16 {
    OK = 0u,          // Command operation completed successfully.
    INVALID_ARGUMENT, // Descriptor, handle, or command line is invalid.
    OUT_OF_MEMORY,    // Registry growth or owned text allocation failed.
    PARSE_FAILED,     // Command line could not be tokenized.
    NOT_FOUND,        // Requested command does not exist.
    ALREADY_EXISTS,   // Name is already registered in this command domain.
    PERMISSION_DENIED, // Context does not satisfy command flags.
    BUSY,             // Mutation was requested during protected execution.
    RECURSION_LIMIT   // Nested command execution exceeded the hard limit.
};

enum class convar_system_error_t : u16 {
    OK = 0u,           // ConVar operation completed successfully.
    INVALID_ARGUMENT,  // Descriptor, handle, or text is invalid.
    OUT_OF_MEMORY,     // Registry growth or owned text allocation failed.
    NOT_FOUND,         // Requested ConVar does not exist.
    ALREADY_EXISTS,    // Name is already registered in this ConVar domain.
    READ_ONLY,         // Runtime mutation is forbidden by registration flags.
    PERMISSION_DENIED, // Context does not satisfy ConVar flags.
    INVALID_VALUE,     // Text cannot be converted to the declared type.
    BELOW_MINIMUM,     // Parsed scalar is lower than the configured bound.
    ABOVE_MAXIMUM,     // Parsed scalar is higher than the configured bound.
    BUSY               // Mutation was requested during a protected callback.
};

struct command_register_result_t {
    error_code_t error{ CY_ERROR_OK };                   // Stable registration result.
    command_handle_t handle{ CY_COMMAND_HANDLE_INVALID }; // New handle when error is OK.
};

struct convar_register_result_t {
    error_code_t error{ CY_ERROR_OK };                 // Stable registration result.
    convar_handle_t handle{ CY_CONVAR_HANDLE_INVALID }; // New handle when error is OK.
};

using command_output_fn_t = void ( * )(
    string_view_t text,
    void *pUserData ) noexcept;

using command_visit_fn_t = bool_t ( * )(
    command_handle_t handle,
    const concommand_desc_t &desc,
    void *pUserData ) noexcept;

using convar_visit_fn_t = bool_t ( * )(
    convar_handle_t handle,
    const convar_desc_t &desc,
    const convar_value_t &value,
    void *pUserData ) noexcept;

struct command_system_desc_t {
    const allocator_t *pAllocator{ nullptr }; // Owns registry records and copied strings.
    usize nInitialCommands{ 128u };            // Initial command-table reservation.
    usize nInitialConVars{ 256u };             // Initial ConVar-table reservation.
    bool_t bCaseInsensitiveAscii{ CY_TRUE };   // Fold ASCII names during lookup.
    command_output_fn_t pfnOutput{ nullptr };  // Optional command text sink.
    void *pOutputUserData{ nullptr };          // Opaque state passed to pfnOutput.
};

struct command_system_t;

CYPHER_NODISCARD constexpr error_code_t CommandSystem_MakeError(
    command_system_error_t error ) noexcept
{
    return error == command_system_error_t::OK
        ? CY_ERROR_OK
        : Cy_ErrorMake( error_domain_t::COMMAND, static_cast<u16>( error ) );
}

CYPHER_NODISCARD constexpr error_code_t CommandSystem_MakeError(
    convar_system_error_t error ) noexcept
{
    return error == convar_system_error_t::OK
        ? CY_ERROR_OK
        : Cy_ErrorMake( error_domain_t::CVAR, static_cast<u16>( error ) );
}

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CommandSystem_ErrorName( command_system_error_t error ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CommandSystem_ErrorName( convar_system_error_t error ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
command_system_t *CommandSystem_Create(
    const command_system_desc_t &desc ) noexcept;

CYPHER_COMMON_API void CommandSystem_Destroy(
    command_system_t *pSystem ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandSystem_IsValid( const command_system_t *pSystem ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandSystem_CommandCount( const command_system_t *pSystem ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandSystem_ConVarCount( const command_system_t *pSystem ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
command_register_result_t CommandSystem_RegisterCommand(
    command_system_t *pSystem,
    const concommand_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
convar_register_result_t CommandSystem_RegisterConVar(
    command_system_t *pSystem,
    const convar_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
error_code_t CommandSystem_UnregisterCommand(
    command_system_t *pSystem,
    command_handle_t handle ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
error_code_t CommandSystem_UnregisterConVar(
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

// Copies a descriptor whose string views remain owned by pSystem. Those views stay
// valid until the matching registration is removed or the system is destroyed.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandSystem_GetCommandDesc(
    const command_system_t *pSystem,
    command_handle_t handle,
    concommand_desc_t *pDescOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CommandSystem_GetConVarDesc(
    const command_system_t *pSystem,
    convar_handle_t handle,
    convar_desc_t *pDescOut ) noexcept;

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
error_code_t CommandSystem_ResetConVar(
    command_system_t *pSystem,
    convar_handle_t handle,
    const command_context_t &context ) noexcept;

// Visitors execute synchronously. Registry mutation is rejected while a visitor
// or command callback is active.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandSystem_ForEachCommand(
    command_system_t *pSystem,
    command_visit_fn_t pfnVisitor,
    void *pUserData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandSystem_ForEachConVar(
    command_system_t *pSystem,
    convar_visit_fn_t pfnVisitor,
    void *pUserData ) noexcept;

// Returned suggestions are borrowed from the registry or completion callback.
// Registry-owned views remain valid until unregister/destroy.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CommandSystem_Complete(
    command_system_t *pSystem,
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDSYSTEM_H
