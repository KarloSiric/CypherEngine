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

#ifndef CYPHER_COMMON_TIER1_TOKENREADER_H
#define CYPHER_COMMON_TIER1_TOKENREADER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Lexer.h"
#include "CypherCommon_StringParse.h"

namespace cypher::common
{

constexpr usize CY_TOKEN_READER_LOOKAHEAD_CAPACITY = 4u;

enum class token_reader_status_t : u8 {
    OK = 0u,
    END_OF_INPUT,
    INVALID_ARGUMENT,
    LEXER_ERROR,
    UNEXPECTED_KIND,
    UNEXPECTED_TEXT,
    VALUE_PARSE_FAILED,
    LOOKAHEAD_EXCEEDED
};

struct token_reader_error_t {
    token_reader_status_t status{ token_reader_status_t::OK };
    lexer_status_t lexerStatus{ lexer_status_t::OK };
    string_parse_result_t parseResult{};
    token_kind_t expectedKind{ token_kind_t::END_OF_INPUT };
    string_view_t expectedText{};
    bool_t bCaseInsensitiveAscii{ CY_FALSE };
    token_t actual{};
};

struct token_reader_t {
    lexer_t lexer{};
    token_t lookahead[CY_TOKEN_READER_LOOKAHEAD_CAPACITY]{};
    usize nLookahead{ 0u };
    token_reader_error_t error{};
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
