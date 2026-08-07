//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ConCommand.h
//  Purpose: Declares console-command descriptors and invocation contracts.
//  Details: Command descriptors borrow static metadata. Parsed argument views borrow
//           the command line for the duration of one synchronous callback.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CONCOMMAND_H
#define CYPHER_COMMON_TIER1_CONCOMMAND_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

constexpr usize CY_COMMAND_MAX_ARGUMENTS = 64u;

enum concommand_flags_t : flags32_t {
    CONCOMMAND_FLAG_NONE          = 0u,
    CONCOMMAND_FLAG_CHEAT         = CYPHER_BIT32( 0 ),
    CONCOMMAND_FLAG_DEVELOPMENT   = CYPHER_BIT32( 1 ),
    CONCOMMAND_FLAG_SERVER_ONLY   = CYPHER_BIT32( 2 ),
    CONCOMMAND_FLAG_CLIENT_ONLY   = CYPHER_BIT32( 3 ),
    CONCOMMAND_FLAG_HIDDEN        = CYPHER_BIT32( 4 ),
    CONCOMMAND_FLAG_ARCHIVE       = CYPHER_BIT32( 5 )
};

enum class command_source_t : u8 {
    ENGINE = 0u,
    LOCAL_CONSOLE,
    CONFIG,
    COMMAND_LINE,
    SCRIPT,
    REMOTE_CLIENT,
    TOOL
};

struct command_args_t {
    string_view_t commandLine{};
    string_view_t arguments[CY_COMMAND_MAX_ARGUMENTS]{};
    usize nArgumentCount{ 0u };
};

struct command_context_t {
    command_source_t source{ command_source_t::ENGINE };
    u64 nCallerId{ 0u };
    bool_t bCheatsAllowed{ CY_FALSE };
    void *pUserData{ nullptr };
};

using concommand_callback_t = error_code_t ( * )(
    const command_context_t &context,
    const command_args_t &args,
    void *pCommandUserData ) noexcept;

using concommand_complete_fn_t = usize ( * )(
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity,
    void *pCommandUserData ) noexcept;

struct concommand_desc_t {
    string_view_t name{};
    string_view_t help{};
    string_view_t usage{};
    flags32_t flags{ CONCOMMAND_FLAG_NONE };
    concommand_callback_t pfnExecute{ nullptr };
    concommand_complete_fn_t pfnComplete{ nullptr };
    void *pUserData{ nullptr };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConCommand_IsValidName( string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConCommand_ValidateDesc( const concommand_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConCommand_ParseArgs(
    string_view_t commandLine,
    command_args_t *pArgsOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONCOMMAND_H
