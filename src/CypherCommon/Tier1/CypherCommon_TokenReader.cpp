//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_TokenReader.cpp
//  Purpose: Implements parser-facing lookahead and typed-token operations.
//  Details: TokenReader adds fixed-capacity lookahead and deterministic expectation
//           handling above Lexer without allocating or owning source text. Failed
//           expectations leave the offending token unconsumed for parser recovery.
//
//  History:
//  - Created by Karlo Siric on 2026-08-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TokenReader.h"

#include "CypherCommon_Assert.h"

namespace cypher::common
{

namespace
{

static void TokenReader_SetError(
    token_reader_t *pReader,
    token_reader_status_t status,
    const token_t &actual = {} ) noexcept
{
    if ( pReader == nullptr ) {
        return;
    }

    pReader->error = {};
    pReader->error.status = status;
    pReader->error.actual = actual;
}

static void TokenReader_SetLexerError(
    token_reader_t *pReader,
    lexer_status_t lexerStatus,
    const token_t &actual ) noexcept
{
    TokenReader_SetError( pReader, token_reader_status_t::LEXER_ERROR, actual );
    pReader->error.lexerStatus = lexerStatus;
}

static void TokenReader_SetKindError(
    token_reader_t *pReader,
    token_reader_status_t status,
    token_kind_t expectedKind,
    const token_t &actual ) noexcept
{
    TokenReader_SetError( pReader, status, actual );
    pReader->error.expectedKind = expectedKind;
}

static void TokenReader_SetTextError(
    token_reader_t *pReader,
    token_reader_status_t status,
    string_view_t expectedText,
    bool_t bCaseInsensitiveAscii,
    const token_t &actual ) noexcept
{
    TokenReader_SetError( pReader, status, actual );
    pReader->error.expectedText = expectedText;
    pReader->error.bCaseInsensitiveAscii = bCaseInsensitiveAscii;
}

static void TokenReader_SetParseError(
    token_reader_t *pReader,
    const string_parse_result_t &parseResult,
    const token_t &actual ) noexcept
{
    TokenReader_SetError( pReader, token_reader_status_t::VALUE_PARSE_FAILED, actual );
    pReader->error.parseResult = parseResult;
}

static bool_t TokenReader_TextEquals(
    string_view_t actual,
    string_view_t expected,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    return bCaseInsensitiveAscii
        ? StringView_EqualsInsensitiveAscii( actual, expected )
        : StringView_Equals( actual, expected );
}

static token_reader_status_t TokenReader_FillLookahead(
    token_reader_t *pReader,
    usize iLookahead ) noexcept
{
    if ( iLookahead >= CY_TOKEN_READER_LOOKAHEAD_CAPACITY ) {
        TokenReader_SetError( pReader, token_reader_status_t::LOOKAHEAD_EXCEEDED );
        return token_reader_status_t::LOOKAHEAD_EXCEEDED;
    }

    while ( pReader->nLookahead <= iLookahead ) {
        if ( pReader->nLookahead > 0u &&
             pReader->lookahead[pReader->nLookahead - 1u].kind == token_kind_t::END_OF_INPUT ) {
            return token_reader_status_t::END_OF_INPUT;
        }

        token_t token{};
        const lexer_status_t lexerStatus = Lexer_Read( &pReader->lexer, &token );
        if ( lexerStatus != lexer_status_t::OK &&
             lexerStatus != lexer_status_t::END_OF_INPUT ) {
            TokenReader_SetLexerError( pReader, lexerStatus, token );
            return token_reader_status_t::LEXER_ERROR;
        }

        pReader->lookahead[pReader->nLookahead] = token;
        ++pReader->nLookahead;
        if ( lexerStatus == lexer_status_t::END_OF_INPUT ) {
            return token_reader_status_t::END_OF_INPUT;
        }
    }

    return pReader->lookahead[iLookahead].kind == token_kind_t::END_OF_INPUT
        ? token_reader_status_t::END_OF_INPUT
        : token_reader_status_t::OK;
}

static void TokenReader_PopFront( token_reader_t *pReader ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_PopFront requires a valid reader." );
    CY_ASSERT_MSG( pReader != nullptr && pReader->nLookahead > 0u,
                   "TokenReader_PopFront requires a buffered token." );
    if ( pReader == nullptr || pReader->nLookahead == 0u ) {
        return;
    }

    for ( usize iToken = 1u; iToken < pReader->nLookahead; ++iToken ) {
        pReader->lookahead[iToken - 1u] = pReader->lookahead[iToken];
    }
    --pReader->nLookahead;
    pReader->lookahead[pReader->nLookahead] = {};
}

static token_reader_status_t TokenReader_PeekRequired(
    token_reader_t *pReader,
    token_t *pTokenOut ) noexcept
{
    const token_reader_status_t status = TokenReader_Peek( pReader, 0u, pTokenOut );
    if ( status == token_reader_status_t::END_OF_INPUT ) {
        TokenReader_SetError( pReader, token_reader_status_t::END_OF_INPUT, *pTokenOut );
    }
    return status;
}

static token_reader_status_t TokenReader_CommitBufferedToken(
    token_reader_t *pReader ) noexcept
{
    token_t discarded{};
    const token_reader_status_t status = TokenReader_Read( pReader, &discarded );
    return status == token_reader_status_t::END_OF_INPUT
        ? token_reader_status_t::OK
        : status;
}

} // namespace

bool_t TokenReader_Init(
    token_reader_t *pReader,
    string_view_t source,
    const lexer_rules_t &rules ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_Init requires a valid reader." );
    if ( pReader == nullptr ) {
        return CY_FALSE;
    }

    *pReader = token_reader_t{};
    if ( !Lexer_Init( &pReader->lexer, source, rules ) ) {
        TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        pReader->error.lexerStatus = pReader->lexer.status;
        return CY_FALSE;
    }
    return CY_TRUE;
}

void TokenReader_Reset( token_reader_t *pReader ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_Reset requires a valid reader." );
    if ( pReader == nullptr ) {
        return;
    }

    Lexer_Reset( &pReader->lexer );
    for ( usize iToken = 0u; iToken < CY_TOKEN_READER_LOOKAHEAD_CAPACITY; ++iToken ) {
        pReader->lookahead[iToken] = {};
    }
    pReader->nLookahead = 0u;
    pReader->error = {};
}

token_reader_status_t TokenReader_Peek(
    token_reader_t *pReader,
    usize iLookahead,
    token_t *pTokenOut ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_Peek requires a valid reader." );
    CY_ASSERT_MSG( pTokenOut != nullptr, "TokenReader_Peek requires token output storage." );
    if ( pReader == nullptr ) {
        return token_reader_status_t::INVALID_ARGUMENT;
    }
    if ( pTokenOut == nullptr ) {
        TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    *pTokenOut = {};
    const token_reader_status_t status = TokenReader_FillLookahead( pReader, iLookahead );
    if ( status == token_reader_status_t::LOOKAHEAD_EXCEEDED ||
         status == token_reader_status_t::LEXER_ERROR ) {
        return status;
    }

    if ( iLookahead < pReader->nLookahead ) {
        *pTokenOut = pReader->lookahead[iLookahead];
        return pTokenOut->kind == token_kind_t::END_OF_INPUT
            ? token_reader_status_t::END_OF_INPUT
            : token_reader_status_t::OK;
    }

    CY_ASSERT_MSG( pReader->nLookahead > 0u,
                   "TokenReader end-of-input state requires a buffered sentinel." );
    if ( pReader->nLookahead > 0u ) {
        *pTokenOut = pReader->lookahead[pReader->nLookahead - 1u];
    }
    return status;
}

token_reader_status_t TokenReader_Read(
    token_reader_t *pReader,
    token_t *pTokenOut ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_Read requires a valid reader." );
    CY_ASSERT_MSG( pTokenOut != nullptr, "TokenReader_Read requires token output storage." );
    if ( pReader == nullptr ) {
        return token_reader_status_t::INVALID_ARGUMENT;
    }
    if ( pTokenOut == nullptr ) {
        TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    *pTokenOut = {};
    if ( pReader->nLookahead > 0u ) {
        *pTokenOut = pReader->lookahead[0];
        TokenReader_PopFront( pReader );
        return pTokenOut->kind == token_kind_t::END_OF_INPUT
            ? token_reader_status_t::END_OF_INPUT
            : token_reader_status_t::OK;
    }

    const lexer_status_t lexerStatus = Lexer_Read( &pReader->lexer, pTokenOut );
    if ( lexerStatus == lexer_status_t::OK ) {
        return token_reader_status_t::OK;
    }
    if ( lexerStatus == lexer_status_t::END_OF_INPUT ) {
        return token_reader_status_t::END_OF_INPUT;
    }

    TokenReader_SetLexerError( pReader, lexerStatus, *pTokenOut );
    return token_reader_status_t::LEXER_ERROR;
}

bool_t TokenReader_ConsumeKind(
    token_reader_t *pReader,
    token_kind_t kind,
    token_t *pTokenOut ) noexcept
{
    token_t token{};
    const token_reader_status_t status = TokenReader_Peek( pReader, 0u, &token );
    if ( ( status != token_reader_status_t::OK &&
           status != token_reader_status_t::END_OF_INPUT ) ||
         token.kind != kind ) {
        return CY_FALSE;
    }

    token_t consumed{};
    const token_reader_status_t readStatus = TokenReader_Read( pReader, &consumed );
    if ( readStatus != token_reader_status_t::OK &&
         readStatus != token_reader_status_t::END_OF_INPUT ) {
        return CY_FALSE;
    }
    if ( pTokenOut != nullptr ) {
        *pTokenOut = consumed;
    }
    return CY_TRUE;
}

bool_t TokenReader_ConsumeText(
    token_reader_t *pReader,
    string_view_t text,
    bool_t bCaseInsensitiveAscii,
    token_t *pTokenOut ) noexcept
{
    if ( pReader == nullptr || !StringView_IsValid( text ) ) {
        if ( pReader != nullptr ) {
            TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        }
        return CY_FALSE;
    }

    token_t token{};
    const token_reader_status_t status = TokenReader_Peek( pReader, 0u, &token );
    if ( ( status != token_reader_status_t::OK &&
           status != token_reader_status_t::END_OF_INPUT ) ||
         !TokenReader_TextEquals( token.lexeme, text, bCaseInsensitiveAscii ) ) {
        return CY_FALSE;
    }

    token_t consumed{};
    const token_reader_status_t readStatus = TokenReader_Read( pReader, &consumed );
    if ( readStatus != token_reader_status_t::OK &&
         readStatus != token_reader_status_t::END_OF_INPUT ) {
        return CY_FALSE;
    }
    if ( pTokenOut != nullptr ) {
        *pTokenOut = consumed;
    }
    return CY_TRUE;
}

token_reader_status_t TokenReader_ExpectKind(
    token_reader_t *pReader,
    token_kind_t kind,
    token_t *pTokenOut ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_ExpectKind requires a valid reader." );
    CY_ASSERT_MSG( pTokenOut != nullptr, "TokenReader_ExpectKind requires token output storage." );
    if ( pReader == nullptr || pTokenOut == nullptr ) {
        if ( pReader != nullptr ) {
            TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        }
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    token_t token{};
    const token_reader_status_t status = TokenReader_Peek( pReader, 0u, &token );
    if ( status != token_reader_status_t::OK &&
         !( status == token_reader_status_t::END_OF_INPUT &&
            kind == token_kind_t::END_OF_INPUT ) ) {
        if ( status == token_reader_status_t::END_OF_INPUT ) {
            TokenReader_SetKindError(
                pReader,
                token_reader_status_t::END_OF_INPUT,
                kind,
                token );
        }
        return status;
    }
    if ( token.kind != kind ) {
        TokenReader_SetKindError(
            pReader,
            token_reader_status_t::UNEXPECTED_KIND,
            kind,
            token );
        return token_reader_status_t::UNEXPECTED_KIND;
    }

    *pTokenOut = token;
    return TokenReader_CommitBufferedToken( pReader );
}

token_reader_status_t TokenReader_ExpectText(
    token_reader_t *pReader,
    string_view_t text,
    bool_t bCaseInsensitiveAscii,
    token_t *pTokenOut ) noexcept
{
    if ( pReader == nullptr || !StringView_IsValid( text ) ) {
        if ( pReader != nullptr ) {
            TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        }
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    token_t token{};
    const token_reader_status_t status = TokenReader_PeekRequired( pReader, &token );
    if ( status != token_reader_status_t::OK ) {
        if ( status == token_reader_status_t::END_OF_INPUT ) {
            TokenReader_SetTextError(
                pReader,
                token_reader_status_t::END_OF_INPUT,
                text,
                bCaseInsensitiveAscii,
                token );
        }
        return status;
    }
    if ( !TokenReader_TextEquals( token.lexeme, text, bCaseInsensitiveAscii ) ) {
        TokenReader_SetTextError(
            pReader,
            token_reader_status_t::UNEXPECTED_TEXT,
            text,
            bCaseInsensitiveAscii,
            token );
        return token_reader_status_t::UNEXPECTED_TEXT;
    }

    if ( pTokenOut != nullptr ) {
        *pTokenOut = token;
    }
    return TokenReader_CommitBufferedToken( pReader );
}

token_reader_status_t TokenReader_ReadU64(
    token_reader_t *pReader,
    const string_parse_options_t &options,
    u64 *pValueOut ) noexcept
{
    if ( pReader == nullptr || pValueOut == nullptr ) {
        if ( pReader != nullptr ) {
            TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        }
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    token_t token{};
    const token_reader_status_t status = TokenReader_PeekRequired( pReader, &token );
    if ( status != token_reader_status_t::OK ) {
        return status;
    }
    if ( token.kind != token_kind_t::INTEGER ) {
        TokenReader_SetKindError(
            pReader,
            token_reader_status_t::UNEXPECTED_KIND,
            token_kind_t::INTEGER,
            token );
        return token_reader_status_t::UNEXPECTED_KIND;
    }

    const string_parse_result_t parseResult =
        StringParse_U64( token.lexeme, options, pValueOut );
    if ( !StringParse_Succeeded( parseResult ) ) {
        TokenReader_SetParseError( pReader, parseResult, token );
        return token_reader_status_t::VALUE_PARSE_FAILED;
    }
    return TokenReader_CommitBufferedToken( pReader );
}

token_reader_status_t TokenReader_ReadI64(
    token_reader_t *pReader,
    const string_parse_options_t &options,
    i64 *pValueOut ) noexcept
{
    if ( pReader == nullptr || pValueOut == nullptr ) {
        if ( pReader != nullptr ) {
            TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        }
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    token_t token{};
    const token_reader_status_t status = TokenReader_PeekRequired( pReader, &token );
    if ( status != token_reader_status_t::OK ) {
        return status;
    }
    if ( token.kind != token_kind_t::INTEGER ) {
        TokenReader_SetKindError(
            pReader,
            token_reader_status_t::UNEXPECTED_KIND,
            token_kind_t::INTEGER,
            token );
        return token_reader_status_t::UNEXPECTED_KIND;
    }

    const string_parse_result_t parseResult =
        StringParse_I64( token.lexeme, options, pValueOut );
    if ( !StringParse_Succeeded( parseResult ) ) {
        TokenReader_SetParseError( pReader, parseResult, token );
        return token_reader_status_t::VALUE_PARSE_FAILED;
    }
    return TokenReader_CommitBufferedToken( pReader );
}

token_reader_status_t TokenReader_ReadF64(
    token_reader_t *pReader,
    flags32_t parseFlags,
    f64 *pValueOut ) noexcept
{
    if ( pReader == nullptr || pValueOut == nullptr ) {
        if ( pReader != nullptr ) {
            TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        }
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    token_t token{};
    const token_reader_status_t status = TokenReader_PeekRequired( pReader, &token );
    if ( status != token_reader_status_t::OK ) {
        return status;
    }
    if ( token.kind != token_kind_t::FLOAT && token.kind != token_kind_t::INTEGER ) {
        TokenReader_SetKindError(
            pReader,
            token_reader_status_t::UNEXPECTED_KIND,
            token_kind_t::FLOAT,
            token );
        return token_reader_status_t::UNEXPECTED_KIND;
    }

    const string_parse_result_t parseResult =
        StringParse_F64( token.lexeme, parseFlags, pValueOut );
    if ( !StringParse_Succeeded( parseResult ) ) {
        TokenReader_SetParseError( pReader, parseResult, token );
        return token_reader_status_t::VALUE_PARSE_FAILED;
    }
    return TokenReader_CommitBufferedToken( pReader );
}

token_reader_status_t TokenReader_ReadBool(
    token_reader_t *pReader,
    flags32_t parseFlags,
    bool_t *pValueOut ) noexcept
{
    if ( pReader == nullptr || pValueOut == nullptr ) {
        if ( pReader != nullptr ) {
            TokenReader_SetError( pReader, token_reader_status_t::INVALID_ARGUMENT );
        }
        return token_reader_status_t::INVALID_ARGUMENT;
    }

    token_t token{};
    const token_reader_status_t status = TokenReader_PeekRequired( pReader, &token );
    if ( status != token_reader_status_t::OK ) {
        return status;
    }
    if ( token.kind != token_kind_t::IDENTIFIER && token.kind != token_kind_t::INTEGER ) {
        TokenReader_SetKindError(
            pReader,
            token_reader_status_t::UNEXPECTED_KIND,
            token_kind_t::IDENTIFIER,
            token );
        return token_reader_status_t::UNEXPECTED_KIND;
    }

    const string_parse_result_t parseResult =
        StringParse_Bool( token.lexeme, parseFlags, pValueOut );
    if ( !StringParse_Succeeded( parseResult ) ) {
        TokenReader_SetParseError( pReader, parseResult, token );
        return token_reader_status_t::VALUE_PARSE_FAILED;
    }
    return TokenReader_CommitBufferedToken( pReader );
}

const token_reader_error_t *TokenReader_LastError(
    const token_reader_t *pReader ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_LastError requires a valid reader." );
    return pReader != nullptr ? &pReader->error : nullptr;
}

void TokenReader_ClearError( token_reader_t *pReader ) noexcept
{
    CY_ASSERT_MSG( pReader != nullptr, "TokenReader_ClearError requires a valid reader." );
    if ( pReader != nullptr ) {
        pReader->error = {};
    }
}

const char *TokenReader_StatusName( token_reader_status_t status ) noexcept
{
    switch ( status ) {
        case token_reader_status_t::OK:                 return "OK";
        case token_reader_status_t::END_OF_INPUT:       return "END_OF_INPUT";
        case token_reader_status_t::INVALID_ARGUMENT:   return "INVALID_ARGUMENT";
        case token_reader_status_t::LEXER_ERROR:        return "LEXER_ERROR";
        case token_reader_status_t::UNEXPECTED_KIND:    return "UNEXPECTED_KIND";
        case token_reader_status_t::UNEXPECTED_TEXT:    return "UNEXPECTED_TEXT";
        case token_reader_status_t::VALUE_PARSE_FAILED: return "VALUE_PARSE_FAILED";
        case token_reader_status_t::LOOKAHEAD_EXCEEDED: return "LOOKAHEAD_EXCEEDED";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
