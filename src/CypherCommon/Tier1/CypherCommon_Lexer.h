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
    usize iByte{ 0u };  // Zero-based byte offset from source start.
    u32 nLine{ 1u };    // One-based physical source line.
    u32 nColumn{ 1u };  // One-based byte column; tabs are not expanded here.
};

struct text_source_range_t {
    text_location_t begin{}; // Inclusive first token byte.
    text_location_t end{};   // Exclusive location immediately after the token.
};

enum class token_kind_t : u8 {
    END_OF_INPUT = 0u, // Synthetic token at source exhaustion.
    IDENTIFIER,       // Name accepted by the configured identifier rules.
    INTEGER,          // Integer spelling; conversion is deferred to StringParse.
    FLOAT,            // Floating-point spelling; conversion is deferred.
    STRING,           // Quoted or multiline string, delimiters included in lexeme.
    CHARACTER,        // Single quoted character token when grammar selects it.
    PUNCTUATION,      // Longest configured punctuation match.
    NEWLINE,          // Emitted only when LEXER_FLAG_EMIT_NEWLINES is set.
    COMMENT,          // Emitted only when LEXER_FLAG_EMIT_COMMENTS is set.
    ERROR             // Token describing the byte range where scanning failed.
};

enum token_flags_t : flags32_t {
    TOKEN_FLAG_NONE        = 0u,                // No spelling metadata.
    TOKEN_FLAG_QUOTED      = CYPHER_BIT32( 0 ), // Lexeme includes quote delimiters.
    TOKEN_FLAG_HAS_ESCAPES = CYPHER_BIT32( 1 ), // Lexeme contains escape sequences.
    TOKEN_FLAG_NEGATIVE    = CYPHER_BIT32( 2 ), // Numeric lexeme begins with '-'.
    TOKEN_FLAG_BASE_PREFIX = CYPHER_BIT32( 3 ), // Integer uses 0x, 0o, or 0b.
    TOKEN_FLAG_UNSIGNED    = CYPHER_BIT32( 4 ), // Integer has an unsigned suffix.
    TOKEN_FLAG_MULTILINE   = CYPHER_BIT32( 5 )  // String spans physical lines.
};

struct token_t {
    token_kind_t kind{ token_kind_t::END_OF_INPUT }; // Syntactic token category.
    string_view_t lexeme{};                          // Borrowed bytes from lexer source.
    text_source_range_t range{};                     // Half-open physical source range.
    flags32_t flags{ TOKEN_FLAG_NONE };              // TOKEN_FLAG_* spelling details.
};

enum lexer_flags_t : flags32_t {
    LEXER_FLAG_NONE = 0u, // Minimal ASCII token grammar.
    LEXER_FLAG_EMIT_NEWLINES = CYPHER_BIT32( 0 ), // Return NEWLINE tokens.
    LEXER_FLAG_EMIT_COMMENTS = CYPHER_BIT32( 1 ), // Return COMMENT tokens.
    LEXER_FLAG_ALLOW_LINE_COMMENTS = CYPHER_BIT32( 2 ), // Recognize line delimiters.
    LEXER_FLAG_ALLOW_BLOCK_COMMENTS = CYPHER_BIT32( 3 ), // Recognize block delimiters.
    LEXER_FLAG_ALLOW_NESTED_BLOCK_COMMENT = CYPHER_BIT32( 4 ), // Track nested blocks.
    LEXER_FLAG_ALLOW_SINGLE_QUOTED_STRING = CYPHER_BIT32( 5 ), // Accept apostrophe strings.
    LEXER_FLAG_ALLOW_ESCAPE_SEQUENCES = CYPHER_BIT32( 6 ), // Validate backslash escapes.
    LEXER_FLAG_ALLOW_UTF8_IDENTIFIERS = CYPHER_BIT32( 7 ), // Permit non-ASCII identifier bytes.
    LEXER_FLAG_SIGN_IS_NUMBER_PART = CYPHER_BIT32( 8 ), // Fold leading sign into numbers.
    LEXER_FLAG_ALLOW_UNSIGNED_SUFFIX = CYPHER_BIT32( 9 ), // Accept integer u/U suffix.
    LEXER_FLAG_ALLOW_MULTILINE_STRING = CYPHER_BIT32( 10 ) // Accept triple-quoted strings.
};

enum class lexer_status_t : u8 {
    OK = 0u,              // A token or trivia transition completed.
    END_OF_INPUT,         // Cursor reached source end cleanly.
    INVALID_ARGUMENT,     // Lexer state or output pointer is invalid.
    INVALID_BYTE,         // Source byte is not legal under active rules.
    INVALID_NUMBER,       // Numeric spelling is malformed.
    INVALID_ESCAPE,       // String escape is malformed or unsupported.
    INVALID_MULTILINE_STRING, // Multiline delimiter/indentation is malformed.
    UNTERMINATED_STRING,  // Closing quote is missing.
    UNTERMINATED_COMMENT, // Block comment reaches source end.
    COMMENT_DEPTH_LIMIT,  // Nested block comments exceed configured depth.
    TOKEN_TOO_LONG        // Lexeme exceeds cchMaxToken.
};

struct lexer_rules_t {
    flags32_t flags{ LEXER_FLAG_ALLOW_LINE_COMMENTS | // Enabled grammar features.
                     LEXER_FLAG_ALLOW_BLOCK_COMMENTS |
                     LEXER_FLAG_ALLOW_ESCAPE_SEQUENCES }; // lexer_flags_t grammar policy.
    character_set_t identifierStartExtra{}; // Extra bytes legal at identifier start.
    character_set_t identifierBodyExtra{};  // Extra bytes legal after the first byte.
    string_view_t lineCommentBegin{ "//", 2u };  // Line-comment introducer.
    string_view_t blockCommentBegin{ "/*", 2u }; // Block-comment opening delimiter.
    string_view_t blockCommentEnd{ "*/", 2u };   // Block-comment closing delimiter.
    const string_view_t *pPunctuations{ nullptr }; // Borrowed punctuation table.
    usize nPunctuationCount{ 0u };                 // Entries in pPunctuations.
    usize cchMaxToken{ CY_INVALID_SIZE };          // Maximum lexeme bytes, or unlimited.
    usize nMaxCommentDepth{ 64u };                 // Nested block-comment safety limit.
};

struct lexer_checkpoint_t {
    usize iByte{ 0u }; // Saved source byte offset.
    u32 nLine{ 1u };   // Saved one-based line.
    u32 nColumn{ 1u }; // Saved one-based column.
};

struct lexer_t {
    string_view_t source{};                     // Borrowed source for the lexer lifetime.
    lexer_rules_t rules{};                      // Rule snapshot copied at initialization.
    lexer_checkpoint_t cursor{};                // Next unread source position.
    lexer_status_t status{ lexer_status_t::OK }; // Sticky terminal/error state.
    text_location_t errorLocation{};            // First location that produced status.
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
