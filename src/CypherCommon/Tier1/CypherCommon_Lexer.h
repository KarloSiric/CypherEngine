//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Lexer.h
//  Purpose: Declares the allocation-free lexical scanner used by Cypher text formats.
//  Details: The lexer turns bounded source text into borrowed token views with exact
//           byte, line, and column locations. Grammar and semantic parsing stay above it.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_LEXER_H
#define CYPHER_COMMON_TIER1_LEXER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CharacterSet.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct text_location_t {
    usize iByte{ 0u };
    u32 nLine{ 1u };
    u32 nColumn{ 1u };
};

struct text_source_range_t {
    text_location_t begin{};
    text_location_t end{};
};

enum class token_kind_t : u8 {
    END_OF_INPUT = 0u,
    IDENTIFIER,
    INTEGER,
    FLOAT,
    STRING,
    CHARACTER,
    PUNCTUATION,
    NEWLINE,
    COMMENT,
    ERROR
};

enum token_flags_t : flags32_t {
    TOKEN_FLAG_NONE          = 0u,
    TOKEN_FLAG_QUOTED        = CYPHER_BIT32( 0 ),
    TOKEN_FLAG_HAS_ESCAPES   = CYPHER_BIT32( 1 ),
    TOKEN_FLAG_NEGATIVE      = CYPHER_BIT32( 2 ),
    TOKEN_FLAG_BASE_PREFIX   = CYPHER_BIT32( 3 ),
    TOKEN_FLAG_UNSIGNED      = CYPHER_BIT32( 4 ),
    TOKEN_FLAG_MULTILINE     = CYPHER_BIT32( 5 )
};

struct token_t {
    token_kind_t kind{ token_kind_t::END_OF_INPUT };
    string_view_t lexeme{};
    text_source_range_t range{};
    flags32_t flags{ TOKEN_FLAG_NONE };
};

enum lexer_flags_t : flags32_t {
    LEXER_FLAG_NONE                       = 0u,
    LEXER_FLAG_EMIT_NEWLINES              = CYPHER_BIT32( 0 ),
    LEXER_FLAG_EMIT_COMMENTS              = CYPHER_BIT32( 1 ),
    LEXER_FLAG_ALLOW_LINE_COMMENTS        = CYPHER_BIT32( 2 ),
    LEXER_FLAG_ALLOW_BLOCK_COMMENTS       = CYPHER_BIT32( 3 ),
    LEXER_FLAG_ALLOW_NESTED_BLOCK_COMMENT = CYPHER_BIT32( 4 ),
    LEXER_FLAG_ALLOW_SINGLE_QUOTED_STRING = CYPHER_BIT32( 5 ),
    LEXER_FLAG_ALLOW_ESCAPE_SEQUENCES     = CYPHER_BIT32( 6 ),
    LEXER_FLAG_ALLOW_UTF8_IDENTIFIERS     = CYPHER_BIT32( 7 ),
    LEXER_FLAG_SIGN_IS_NUMBER_PART        = CYPHER_BIT32( 8 ),
    LEXER_FLAG_ALLOW_UNSIGNED_SUFFIX      = CYPHER_BIT32( 9 ),
    LEXER_FLAG_ALLOW_MULTILINE_STRING     = CYPHER_BIT32( 10 )
};

enum class lexer_status_t : u8 {
    OK = 0u,
    END_OF_INPUT,
    INVALID_ARGUMENT,
    INVALID_BYTE,
    INVALID_NUMBER,
    INVALID_ESCAPE,
    INVALID_MULTILINE_STRING,
    UNTERMINATED_STRING,
    UNTERMINATED_COMMENT,
    COMMENT_DEPTH_LIMIT,
    TOKEN_TOO_LONG
};

struct lexer_rules_t {
    flags32_t flags{ LEXER_FLAG_ALLOW_LINE_COMMENTS |
                     LEXER_FLAG_ALLOW_BLOCK_COMMENTS |
                     LEXER_FLAG_ALLOW_ESCAPE_SEQUENCES };
    character_set_t identifierStartExtra{};
    character_set_t identifierBodyExtra{};
    string_view_t lineCommentBegin{ "//", 2u };
    string_view_t blockCommentBegin{ "/*", 2u };
    string_view_t blockCommentEnd{ "*/", 2u };
    const string_view_t *pPunctuations{ nullptr };
    usize nPunctuationCount{ 0u };
    usize cchMaxToken{ CY_INVALID_SIZE };
    usize nMaxCommentDepth{ 64u };
};

struct lexer_checkpoint_t {
    usize iByte{ 0u };
    u32 nLine{ 1u };
    u32 nColumn{ 1u };
};

struct lexer_t {
    string_view_t source{};
    lexer_rules_t rules{};
    lexer_checkpoint_t cursor{};
    lexer_status_t status{ lexer_status_t::OK };
    text_location_t errorLocation{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
lexer_rules_t Lexer_DefaultRules() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Lexer_Init(
    lexer_t *pLexer,
    string_view_t source,
    const lexer_rules_t &rules ) noexcept;

CYPHER_COMMON_API void Lexer_Reset( lexer_t *pLexer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
lexer_status_t Lexer_Read( lexer_t *pLexer, token_t *pTokenOut ) noexcept;

// Reads no token; it only advances over whitespace and non-emitted comments.
CYPHER_NODISCARD CYPHER_COMMON_API
lexer_status_t Lexer_SkipTrivia( lexer_t *pLexer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Lexer_IsAtEnd( const lexer_t *pLexer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
text_location_t Lexer_Location( const lexer_t *pLexer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
lexer_checkpoint_t Lexer_Save( const lexer_t *pLexer ) noexcept;

CYPHER_COMMON_API void Lexer_Restore(
    lexer_t *pLexer,
    lexer_checkpoint_t checkpoint ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *Lexer_StatusName( lexer_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_LEXER_H
