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

constexpr usize CY_COMMAND_MAX_ARGUMENTS = 64u;           // Hard parser argument limit.
constexpr usize CY_COMMAND_MAX_NAME_BYTES = 127u;         // Maximum command-name bytes.
constexpr usize CY_COMMAND_MAX_LINE_BYTES = 4u * CY_KIB;  // Maximum accepted command line.

enum concommand_flags_t : flags32_t {
    CONCOMMAND_FLAG_NONE           = 0u,                // No policy restrictions.
    CONCOMMAND_FLAG_CHEAT          = CYPHER_BIT32( 0 ), // Requires cheat permission.
    CONCOMMAND_FLAG_DEVELOPMENT    = CYPHER_BIT32( 1 ), // Development builds or contexts only.
    CONCOMMAND_FLAG_SERVER_ONLY    = CYPHER_BIT32( 2 ), // Requires server execution context.
    CONCOMMAND_FLAG_CLIENT_ONLY    = CYPHER_BIT32( 3 ), // Requires client execution context.
    CONCOMMAND_FLAG_HIDDEN         = CYPHER_BIT32( 4 ), // Omit from normal discovery/help.
    CONCOMMAND_FLAG_ARCHIVE        = CYPHER_BIT32( 5 ), // Reserved for persisted command state.
    CONCOMMAND_FLAG_REMOTE_ALLOWED = CYPHER_BIT32( 6 )  // May be called by an authorized peer.
};

constexpr flags32_t CONCOMMAND_VALID_FLAGS =
    CONCOMMAND_FLAG_CHEAT |
    CONCOMMAND_FLAG_DEVELOPMENT |
    CONCOMMAND_FLAG_SERVER_ONLY |
    CONCOMMAND_FLAG_CLIENT_ONLY |
    CONCOMMAND_FLAG_HIDDEN |
    CONCOMMAND_FLAG_ARCHIVE |
    CONCOMMAND_FLAG_REMOTE_ALLOWED;

enum class command_parse_status_t : u8 {
    OK = 0u,                 // Parsing produced at least one valid argument.
    INVALID_ARGUMENT,        // Source or destination contract is invalid.
    EMPTY_LINE,              // Input contains no command token.
    LINE_TOO_LONG,           // Input exceeds CY_COMMAND_MAX_LINE_BYTES.
    EMBEDDED_NULL,           // Bounded input contains an interior null byte.
    LINE_BREAK,              // A single-line parse encountered CR or LF.
    INVALID_CHARACTER,       // Control or forbidden byte appears in the line.
    UNEXPECTED_QUOTE,        // Quote began inside an unquoted token.
    UNTERMINATED_QUOTE,      // Closing quote is missing.
    TRAILING_BYTES_AFTER_QUOTE, // Non-space bytes follow a quoted token.
    TOO_MANY_ARGUMENTS,      // Parsed token count exceeds the fixed output array.
    INVALID_COMMAND_NAME     // First token violates command-name syntax.
};

struct command_parse_result_t {
    command_parse_status_t status{ command_parse_status_t::INVALID_ARGUMENT }; // Final parse status.
    usize iError{ CY_STRING_VIEW_NPOS }; // Byte offset associated with failure.
};

enum class command_source_t : u8 {
    ENGINE = 0u,  // Internal engine code.
    LOCAL_CONSOLE, // Interactive local console.
    CONFIG,        // Configuration-file execution.
    COMMAND_LINE,  // Process startup arguments.
    SCRIPT,        // Gameplay or tool scripting runtime.
    REMOTE_CLIENT, // Authenticated remote console client.
    TOOL           // Editor or command-line tool host.
};

struct command_args_t {
    string_view_t commandLine{}; // Original borrowed line containing all views.
    string_view_t arguments[CY_COMMAND_MAX_ARGUMENTS]{}; // Parsed borrowed tokens.
    usize nArgumentCount{ 0u };  // Number of valid entries in arguments.
};

struct command_context_t {
    command_source_t source{ command_source_t::ENGINE }; // Origin used for policy checks.
    u64 nCallerId{ 0u };                    // Host-defined local or remote caller identity.
    bool_t bCheatsAllowed{ CY_FALSE };       // Grants CHEAT commands.
    bool_t bDevelopmentAllowed{ CY_FALSE };  // Grants DEVELOPMENT commands.
    bool_t bServerContext{ CY_FALSE };       // Execution belongs to server state.
    bool_t bClientContext{ CY_FALSE };       // Execution belongs to client state.
    void *pUserData{ nullptr };              // Host state visible to command callbacks.
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
    string_view_t name{};                         // Unique command name.
    string_view_t help{};                         // Human-readable purpose text.
    string_view_t usage{};                        // Expected argument syntax.
    flags32_t flags{ CONCOMMAND_FLAG_NONE };      // concommand_flags_t policy bits.
    concommand_callback_t pfnExecute{ nullptr };  // Required execution callback.
    concommand_complete_fn_t pfnComplete{ nullptr }; // Optional completion provider.
    void *pUserData{ nullptr };                   // Opaque callback state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConCommand_IsValidName( string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConCommand_ValidateDesc( const concommand_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConCommand_ParseSucceeded( command_parse_result_t result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ConCommand_ParseStatusName( command_parse_status_t status ) noexcept;

// Splits one single command line into borrowed argument views. Quoted arguments
// exclude their surrounding quote bytes and may contain spaces or tabs. Escapes
// are intentionally not decoded because the result owns no writable storage.
CYPHER_NODISCARD CYPHER_COMMON_API
command_parse_result_t ConCommand_ParseArgs(
    string_view_t commandLine,
    command_args_t *pArgsOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONCOMMAND_H
