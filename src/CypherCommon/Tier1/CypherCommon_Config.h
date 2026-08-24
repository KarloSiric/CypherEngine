//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Config.h
//  Purpose: Declares command/ConVar configuration text import and export.
//  Details: Config consumes caller-supplied text and emits through a writer callback.
//           VFS reads/writes are intentionally owned by the calling subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Config Contract

Configuration composition is deterministic: later layers override earlier layers only through
documented precedence rules. Source storage remains owned by the caller or parsed document.
================
*/

#ifndef CYPHER_COMMON_TIER1_CONFIG_H
#define CYPHER_COMMON_TIER1_CONFIG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CommandSystem.h"

namespace cypher::common
{

enum config_flags_t : flags32_t {
    CONFIG_FLAG_NONE          = 0u,                // No optional behavior.
    CONFIG_FLAG_ALLOW_COMMANDS = CYPHER_BIT32( 0 ), // Execute non-ConVar lines.
    CONFIG_FLAG_ALLOW_CHEATS   = CYPHER_BIT32( 1 ), // Grant cheat context while loading.
    CONFIG_FLAG_STOP_ON_ERROR  = CYPHER_BIT32( 2 ), // Abort after the first rejected line.
    CONFIG_FLAG_ARCHIVED_ONLY  = CYPHER_BIT32( 3 )  // Restrict assignments to archived ConVars.
};

constexpr flags32_t CONFIG_VALID_FLAGS =
    CONFIG_FLAG_ALLOW_COMMANDS |
    CONFIG_FLAG_ALLOW_CHEATS |
    CONFIG_FLAG_STOP_ON_ERROR |
    CONFIG_FLAG_ARCHIVED_ONLY;

enum class config_error_t : u16 {
    OK = 0u,          // Configuration operation completed.
    INVALID_ARGUMENT, // Source, context, or writer is invalid.
    PARSE_FAILED,     // At least one line could not be interpreted.
    PERMISSION_DENIED, // A line violates command or ConVar policy.
    WRITE_FAILED      // Output callback rejected serialized text.
};

CYPHER_NODISCARD constexpr error_code_t Config_MakeError(
    config_error_t error ) noexcept
{
    return error == config_error_t::OK
        ? CY_ERROR_OK
        : Cy_ErrorMake( error_domain_t::CONFIG, static_cast<u16>( error ) );
}

struct config_source_t {
    string_view_t name{}; // Diagnostic source name or path.
    string_view_t text{}; // Complete borrowed configuration text.
    flags32_t flags{ CONFIG_FLAG_ALLOW_COMMANDS }; // config_flags_t load policy.
};

struct config_load_result_t {
    error_code_t error{ CY_ERROR_OK };            // First or terminal stable error.
    usize nLinesRead{ 0u };                       // Physical lines examined.
    usize nCommandsExecuted{ 0u };                // Lines accepted by the command system.
    usize nErrors{ 0u };                          // Rejected lines encountered.
    usize iErrorByte{ CY_STRING_VIEW_NPOS };      // First failing byte in source text.
};

using config_write_fn_t = bool_t ( * )(
    string_view_t text,
    void *pUserData ) noexcept;

struct config_writer_t {
    config_write_fn_t pfnWrite{ nullptr }; // Receives each serialized text fragment.
    void *pUserData{ nullptr };            // Opaque writer state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
config_load_result_t Config_Load(
    const config_source_t &source,
    command_system_t *pCommandSystem,
    const command_context_t &context ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
error_code_t Config_WriteArchivedConVars(
    const command_system_t *pCommandSystem,
    const config_writer_t &writer ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONFIG_H
