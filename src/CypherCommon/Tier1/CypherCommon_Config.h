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

#ifndef CYPHER_COMMON_TIER1_CONFIG_H
#define CYPHER_COMMON_TIER1_CONFIG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CommandSystem.h"

namespace cypher::common
{

enum config_flags_t : flags32_t {
    CONFIG_FLAG_NONE                = 0u,
    CONFIG_FLAG_ALLOW_COMMANDS      = CYPHER_BIT32( 0 ),
    CONFIG_FLAG_ALLOW_CHEATS        = CYPHER_BIT32( 1 ),
    CONFIG_FLAG_STOP_ON_ERROR       = CYPHER_BIT32( 2 ),
    CONFIG_FLAG_ARCHIVED_ONLY       = CYPHER_BIT32( 3 )
};

constexpr flags32_t CONFIG_VALID_FLAGS =
    CONFIG_FLAG_ALLOW_COMMANDS |
    CONFIG_FLAG_ALLOW_CHEATS |
    CONFIG_FLAG_STOP_ON_ERROR |
    CONFIG_FLAG_ARCHIVED_ONLY;

enum class config_error_t : u16 {
    OK = 0u,
    INVALID_ARGUMENT,
    PARSE_FAILED,
    PERMISSION_DENIED,
    WRITE_FAILED
};

CYPHER_NODISCARD constexpr error_code_t Config_MakeError(
    config_error_t error ) noexcept
{
    return error == config_error_t::OK
        ? CY_ERROR_OK
        : Cy_ErrorMake( error_domain_t::CONFIG, static_cast<u16>( error ) );
}

struct config_source_t {
    string_view_t name{};
    string_view_t text{};
    flags32_t flags{ CONFIG_FLAG_ALLOW_COMMANDS };
};

struct config_load_result_t {
    error_code_t error{ CY_ERROR_OK };
    usize nLinesRead{ 0u };
    usize nCommandsExecuted{ 0u };
    usize nErrors{ 0u };
    usize iErrorByte{ CY_STRING_VIEW_NPOS };
};

using config_write_fn_t = bool_t ( * )(
    string_view_t text,
    void *pUserData ) noexcept;

struct config_writer_t {
    config_write_fn_t pfnWrite{ nullptr };
    void *pUserData{ nullptr };
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
