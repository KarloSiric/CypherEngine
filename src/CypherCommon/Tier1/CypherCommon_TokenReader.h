//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_TokenReader.h
//  Purpose: Declares lookahead and typed-token helpers above the Tier1 lexer.
//  Details: TokenReader owns lexer state but not source text. It provides deterministic
//           expect/consume behavior for CYKV, configs, commands, and tool formats.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Token Reader Contract

Text operations distinguish bounded byte ranges from null-terminated strings. Cursor movement
and conversion validate limits before reading, and failure never relies on ambient locale state.
================
*/

#ifndef CYPHER_COMMON_TIER1_TOKENREADER_H
#define CYPHER_COMMON_TIER1_TOKENREADER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Lexer.h"
#include "CypherCommon_StringParse.h"

namespace cypher::common
{

constexpr usize CY_TOKEN_READER_LOOKAHEAD_CAPACITY = 4u; // Fixed allocation-free token queue.

enum class token_reader_status_t : u8 {
    OK = 0u,          // Requested token operation completed successfully.
    END_OF_INPUT,     // Reader reached the lexer's terminal token.
    INVALID_ARGUMENT,// Reader or output storage violates the API contract.
    LEXER_ERROR,     // Underlying lexer rejected the source stream.
    UNEXPECTED_KIND, // Actual token kind differs from the required kind.
    UNEXPECTED_TEXT, // Actual token spelling differs from the required text.
    VALUE_PARSE_FAILED,// Token text could not be converted to the requested value.
    LOOKAHEAD_EXCEEDED // Requested lookahead exceeds the fixed queue capacity.
};

struct token_reader_error_t {
    token_reader_status_t status{ token_reader_status_t::OK }; // Reader-level failure.
    lexer_status_t lexerStatus{ lexer_status_t::OK };          // Nested lexer failure, if any.
    string_parse_result_t parseResult{};                       // Nested conversion failure.
    token_kind_t expectedKind{ token_kind_t::END_OF_INPUT };   // Required kind for ExpectKind.
    string_view_t expectedText{};                              // Required bytes for ExpectText.
    bool_t bCaseInsensitiveAscii{ CY_FALSE };                  // Comparison policy used.
    token_t actual{};                                          // Token observed at failure.
};

struct token_reader_t {
    lexer_t lexer{}; // Owned cursor state over borrowed source text.
    token_t lookahead[CY_TOKEN_READER_LOOKAHEAD_CAPACITY]{}; // Oldest token at index zero.
    usize nLookahead{ 0u };        // Valid prefix of lookahead[].
    token_reader_error_t error{};  // Last failed reader operation.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TokenReader_Init(
    token_reader_t *pReader,
    string_view_t source,
    const lexer_rules_t &rules ) noexcept;

CYPHER_COMMON_API void TokenReader_Reset( token_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_Peek(
    token_reader_t *pReader,
    usize iLookahead,
    token_t *pTokenOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_Read(
    token_reader_t *pReader,
    token_t *pTokenOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TokenReader_ConsumeKind(
    token_reader_t *pReader,
    token_kind_t kind,
    token_t *pTokenOut = nullptr ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t TokenReader_ConsumeText(
    token_reader_t *pReader,
    string_view_t text,
    bool_t bCaseInsensitiveAscii,
    token_t *pTokenOut = nullptr ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_ExpectKind(
    token_reader_t *pReader,
    token_kind_t kind,
    token_t *pTokenOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_ExpectText(
    token_reader_t *pReader,
    string_view_t text,
    bool_t bCaseInsensitiveAscii,
    token_t *pTokenOut = nullptr ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_ReadU64(
    token_reader_t *pReader,
    const string_parse_options_t &options,
    u64 *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_ReadI64(
    token_reader_t *pReader,
    const string_parse_options_t &options,
    i64 *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_ReadF64(
    token_reader_t *pReader,
    flags32_t parseFlags,
    f64 *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
token_reader_status_t TokenReader_ReadBool(
    token_reader_t *pReader,
    flags32_t parseFlags,
    bool_t *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const token_reader_error_t *TokenReader_LastError(
    const token_reader_t *pReader ) noexcept;

CYPHER_COMMON_API void TokenReader_ClearError( token_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *TokenReader_StatusName( token_reader_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_TOKENREADER_H
