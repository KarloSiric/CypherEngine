//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp
//  Purpose: Implements bounded transactional CYKV text parsing.
//  Details: A temporary document receives all parsed nodes. The destination changes
//           only after lexical, syntactic, limit, and allocation checks all succeed.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParserInternal.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_KeyValueInternal.h"
#include "CypherCommon_StringConvert.h"
#include "CypherCommon_StringEscape.h"
#include "CypherCommon_StringParse.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_KEY_VALUE_PARSE_VALID_FLAGS =
    KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS |
    KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA |
    KEY_VALUE_PARSE_FLAG_ALLOW_UNQUOTED_KEYS |
    KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS |
    KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE;

constexpr flags32_t CY_KEY_VALUE_ESCAPE_FLAGS =
    STRING_ESCAPE_FLAG_QUOTES |
    STRING_ESCAPE_FLAG_BACKSLASH |
    STRING_ESCAPE_FLAG_CONTROL_CHARS |
    STRING_ESCAPE_FLAG_NON_ASCII |
    STRING_ESCAPE_FLAG_PATH_SLASHES;

struct decoded_text_t {
    string_view_t text{};
    char *pAllocation{ nullptr };
    usize cbAllocation{ 0u };
};

struct parser_t {
    lexer_t lexer{};
    token_t token{};
    key_value_parse_options_t options{};
    key_value_document_t *pDocument{ nullptr };
    key_value_parse_result_t result{};
    usize iPreviousTokenEnd{ 0u };
    bool_t bStrictJson{ CY_FALSE };
    bool_t bAtEnd{ CY_FALSE };
    bool_t bHasToken{ CY_FALSE };
};

CYPHER_NODISCARD bool_t HasFlag(
    const parser_t &parser,
    flags32_t flag ) noexcept
{
    return ( parser.options.flags & flag ) != 0u;
}

void Fail(
    parser_t &parser,
    key_value_parse_status_t status,
    text_location_t location ) noexcept
{
    if ( parser.result.status == key_value_parse_status_t::OK ) {
        parser.result.status = status;
        parser.result.errorLocation = location;
    }
}

void FailCurrent(
    parser_t &parser,
    key_value_parse_status_t status ) noexcept
{
    Fail( parser, status, parser.token.range.begin );
}

CYPHER_NODISCARD bool_t Advance( parser_t &parser ) noexcept
{
    if ( parser.bHasToken ) {
        parser.iPreviousTokenEnd = parser.token.range.end.iByte;
    }
    const lexer_status_t status = Lexer_Read( &parser.lexer, &parser.token );
    if ( status == lexer_status_t::END_OF_INPUT ) {
        parser.bAtEnd = CY_TRUE;
        parser.bHasToken = CY_FALSE;
        return CY_TRUE;
    }
    if ( status != lexer_status_t::OK ) {
        Fail(
            parser,
            status == lexer_status_t::TOKEN_TOO_LONG
                ? key_value_parse_status_t::STRING_LIMIT
                : ( status == lexer_status_t::COMMENT_DEPTH_LIMIT
                    ? key_value_parse_status_t::COMMENT_DEPTH_LIMIT
                    : key_value_parse_status_t::LEXER_ERROR ),
            parser.lexer.errorLocation );
        return CY_FALSE;
    }
    parser.bAtEnd = CY_FALSE;
    parser.bHasToken = CY_TRUE;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t HasTriviaBeforeCurrent(
    const parser_t &parser ) noexcept
{
    return parser.bHasToken &&
           parser.token.range.begin.iByte > parser.iPreviousTokenEnd;
}

CYPHER_NODISCARD bool_t IsPunctuation(
    const parser_t &parser,
    char ch ) noexcept
{
    return !parser.bAtEnd &&
           parser.token.kind == token_kind_t::PUNCTUATION &&
           parser.token.lexeme.cchLength == 1u &&
           parser.token.lexeme.pData[0] == ch;
}

CYPHER_NODISCARD bool_t TokenEqualsIdentifier(
    const parser_t &parser,
    const char *pText ) noexcept
{
    return !parser.bAtEnd &&
           parser.token.kind == token_kind_t::IDENTIFIER &&
           StringView_Equals(
               parser.token.lexeme,
               StringView_FromCString( pText ) );
}

CYPHER_NODISCARD bool_t IsNewlineToken(
    const parser_t &parser ) noexcept
{
    return !parser.bAtEnd && parser.token.kind == token_kind_t::NEWLINE;
}

CYPHER_NODISCARD text_location_t SourceLocationAt(
    string_view_t text,
    usize iTarget ) noexcept
{
    text_location_t location{};
    location.nLine = 1u;
    location.nColumn = 1u;

    usize iByte = 0u;
    while ( iByte < iTarget && iByte < text.cchLength ) {
        const char ch = text.pData[iByte];
        if ( ch == '\r' ) {
            iByte += iByte + 1u < text.cchLength &&
                     text.pData[iByte + 1u] == '\n'
                ? 2u
                : 1u;
            ++location.nLine;
            location.nColumn = 1u;
            continue;
        }
        if ( ch == '\n' ) {
            ++iByte;
            ++location.nLine;
            location.nColumn = 1u;
            continue;
        }

        unicode_code_point_t codePoint = 0u;
        const unicode_result_t decoded = Unicode_DecodeUtf8(
            { text.pData + iByte, text.cchLength - iByte },
            &codePoint );
        const usize cBytes = decoded.status == unicode_status_t::OK
            ? decoded.nInputConsumed
            : 1u;
        iByte += cBytes;
        ++location.nColumn;
    }
    location.iByte = iTarget;
    return location;
}

CYPHER_NODISCARD bool_t ValidateSourceEncoding(
    parser_t &parser,
    string_view_t text ) noexcept
{
    const unicode_result_t utf8 = Unicode_ValidateUtf8( text );
    if ( utf8.status != unicode_status_t::OK ) {
        Fail(
            parser,
            key_value_parse_status_t::INVALID_ENCODING,
            SourceLocationAt( text, utf8.iError ) );
        return CY_FALSE;
    }

    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        const char ch = text.pData[iByte];
        if ( ch == '\0' ||
             ( ch == '\r' &&
               ( iByte + 1u == text.cchLength ||
                 text.pData[iByte + 1u] != '\n' ) ) ) {
            Fail(
                parser,
                key_value_parse_status_t::INVALID_ENCODING,
                SourceLocationAt( text, iByte ) );
            return CY_FALSE;
        }
        if ( ch == '\r' ) {
            ++iByte;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsSchemaId( string_view_t schemaId ) noexcept
{
    if ( !StringView_IsValid( schemaId ) || schemaId.cchLength == 0u ) {
        return CY_FALSE;
    }

    bool_t bAtComponentStart = CY_TRUE;
    bool_t bSawDot = CY_FALSE;
    for ( usize iByte = 0u; iByte < schemaId.cchLength; ++iByte ) {
        const char ch = schemaId.pData[iByte];
        if ( ch == '.' ) {
            if ( bAtComponentStart ) {
                return CY_FALSE;
            }
            bAtComponentStart = CY_TRUE;
            bSawDot = CY_TRUE;
            continue;
        }
        if ( bAtComponentStart ) {
            if ( ch < 'a' || ch > 'z' ) {
                return CY_FALSE;
            }
            bAtComponentStart = CY_FALSE;
            continue;
        }
        if ( ( ch < 'a' || ch > 'z' ) &&
             !Char_IsDigitAscii( ch ) && ch != '_' && ch != '-' ) {
            return CY_FALSE;
        }
    }
    return bSawDot && !bAtComponentStart;
}

CYPHER_NODISCARD bool_t ParseHeaderVersion(
    const token_t &token,
    u32 &nVersion ) noexcept
{
    if ( token.kind != token_kind_t::INTEGER ||
         token.lexeme.cchLength == 0u ||
         token.lexeme.pData[0] == '0' ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < token.lexeme.cchLength; ++iByte ) {
        if ( !Char_IsDigitAscii( token.lexeme.pData[iByte] ) ) {
            return CY_FALSE;
        }
    }
    const string_parse_result_t parsed = StringParse_U32(
        token.lexeme,
        { 10u, STRING_PARSE_FLAG_NONE },
        &nVersion );
    return StringParse_Succeeded( parsed ) && nVersion != 0u;
}

CYPHER_NODISCARD bool_t IsStrictJsonNumber( string_view_t text ) noexcept
{
    if ( !StringView_IsValid( text ) || text.cchLength == 0u ) {
        return CY_FALSE;
    }
    usize iByte = 0u;
    if ( text.pData[iByte] == '-' ) {
        if ( ++iByte == text.cchLength ) return CY_FALSE;
    }
    if ( text.pData[iByte] == '0' ) {
        ++iByte;
        if ( iByte < text.cchLength &&
             Char_IsDigitAscii( text.pData[iByte] ) ) return CY_FALSE;
    } else {
        if ( text.pData[iByte] < '1' || text.pData[iByte] > '9' ) return CY_FALSE;
        do { ++iByte; }
        while ( iByte < text.cchLength &&
                Char_IsDigitAscii( text.pData[iByte] ) );
    }
    if ( iByte < text.cchLength && text.pData[iByte] == '.' ) {
        ++iByte;
        const usize iFraction = iByte;
        while ( iByte < text.cchLength &&
                Char_IsDigitAscii( text.pData[iByte] ) ) ++iByte;
        if ( iByte == iFraction ) return CY_FALSE;
    }
    if ( iByte < text.cchLength &&
         ( text.pData[iByte] == 'e' || text.pData[iByte] == 'E' ) ) {
        ++iByte;
        if ( iByte < text.cchLength &&
             ( text.pData[iByte] == '+' || text.pData[iByte] == '-' ) ) ++iByte;
        const usize iExponent = iByte;
        while ( iByte < text.cchLength &&
                Char_IsDigitAscii( text.pData[iByte] ) ) ++iByte;
        if ( iByte == iExponent ) return CY_FALSE;
    }
    return iByte == text.cchLength;
}

CYPHER_NODISCARD bool_t CykvEscapeSpellingIsValid(
    string_view_t encoded ) noexcept
{
    usize iByte = 0u;
    while ( iByte < encoded.cchLength ) {
        if ( encoded.pData[iByte] != '\\' ) {
            ++iByte;
            continue;
        }
        if ( ++iByte == encoded.cchLength ) {
            return CY_FALSE;
        }

        const char chEscape = encoded.pData[iByte++];
        switch ( chEscape ) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                break;
            case 'u':
            case 'U': {
                const usize cHexDigits = chEscape == 'u' ? 4u : 8u;
                if ( cHexDigits > encoded.cchLength - iByte ) {
                    return CY_FALSE;
                }
                for ( usize iDigit = 0u; iDigit < cHexDigits; ++iDigit ) {
                    if ( !Char_IsHexDigitAscii(
                             encoded.pData[iByte + iDigit] ) ) {
                        return CY_FALSE;
                    }
                }
                iByte += cHexDigits;
                break;
            }
            default:
                return CY_FALSE;
        }
    }
    return CY_TRUE;
}

void ReleaseDecoded( parser_t &parser, decoded_text_t &decoded ) noexcept;

CYPHER_NODISCARD bool_t DecodedCykvStringIsValid(
    string_view_t text,
    bool_t bRejectNul ) noexcept
{
    if ( Unicode_ValidateUtf8( text ).status != unicode_status_t::OK ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( bRejectNul && text.pData[iByte] == '\0' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t NormalizeMultilineText(
    parser_t &parser,
    const token_t &token,
    decoded_text_t &normalized ) noexcept
{
    if ( token.lexeme.cchLength < 7u ) {
        return CY_FALSE;
    }

    const string_view_t raw{
        token.lexeme.pData + 3u,
        token.lexeme.cchLength - 6u
    };
    usize iContentBegin = 0u;
    if ( raw.cchLength >= 2u && raw.pData[0] == '\r' &&
         raw.pData[1] == '\n' ) {
        iContentBegin = 2u;
    } else if ( raw.cchLength >= 1u && raw.pData[0] == '\n' ) {
        iContentBegin = 1u;
    } else {
        return CY_FALSE;
    }

    usize iClosingLine = iContentBegin;
    for ( usize iByte = iContentBegin; iByte < raw.cchLength; ++iByte ) {
        if ( raw.pData[iByte] == '\n' ) {
            iClosingLine = iByte + 1u;
        } else if ( raw.pData[iByte] == '\r' &&
                    iByte + 1u < raw.cchLength &&
                    raw.pData[iByte + 1u] == '\n' ) {
            iClosingLine = iByte + 2u;
            ++iByte;
        }
    }

    usize iContentEnd = iContentBegin;
    if ( iClosingLine > iContentBegin ) {
        iContentEnd = iClosingLine - 1u;
        if ( iContentEnd > 0u && raw.pData[iContentEnd - 1u] == '\r' ) {
            --iContentEnd;
        }
    }
    const string_view_t margin{
        raw.pData + iClosingLine,
        raw.cchLength - iClosingLine
    };
    for ( usize iByte = 0u; iByte < margin.cchLength; ++iByte ) {
        if ( margin.pData[iByte] != ' ' && margin.pData[iByte] != '\t' ) {
            return CY_FALSE;
        }
    }

    if ( raw.cchLength == CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    normalized.cbAllocation = raw.cchLength + 1u;
    normalized.pAllocation = static_cast<char *>( Allocator_Allocate(
        parser.pDocument->pAllocator,
        normalized.cbAllocation,
        alignof( char ) ) );
    if ( normalized.pAllocation == nullptr ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }

    usize cchWritten = 0u;
    usize iLine = iContentBegin;
    while ( iLine < iContentEnd ) {
        usize iLineEnd = iLine;
        while ( iLineEnd < iContentEnd &&
                raw.pData[iLineEnd] != '\r' &&
                raw.pData[iLineEnd] != '\n' ) {
            ++iLineEnd;
        }

        bool_t bBlank = CY_TRUE;
        for ( usize iByte = iLine; iByte < iLineEnd; ++iByte ) {
            if ( raw.pData[iByte] != ' ' && raw.pData[iByte] != '\t' ) {
                bBlank = CY_FALSE;
                break;
            }
        }

        usize iText = iLine;
        if ( bBlank ) {
            usize cchRemove = iLineEnd - iLine;
            if ( cchRemove > margin.cchLength ) {
                cchRemove = margin.cchLength;
            }
            iText += cchRemove;
        } else {
            if ( margin.cchLength > iLineEnd - iLine ) {
                Allocator_Free(
                    parser.pDocument->pAllocator,
                    normalized.pAllocation,
                    normalized.cbAllocation,
                    alignof( char ) );
                normalized = {};
                return CY_FALSE;
            }
            for ( usize iByte = 0u; iByte < margin.cchLength; ++iByte ) {
                if ( raw.pData[iLine + iByte] != margin.pData[iByte] ) {
                    Allocator_Free(
                        parser.pDocument->pAllocator,
                        normalized.pAllocation,
                        normalized.cbAllocation,
                        alignof( char ) );
                    normalized = {};
                    return CY_FALSE;
                }
            }
            iText += margin.cchLength;
        }

        const usize cchLine = iLineEnd - iText;
        if ( cchLine != 0u ) {
            Cy_MemCopy(
                normalized.pAllocation + cchWritten,
                raw.pData + iText,
                cchLine );
            cchWritten += cchLine;
        }

        if ( iLineEnd < iContentEnd ) {
            normalized.pAllocation[cchWritten++] = '\n';
            iLine = iLineEnd + 1u;
            if ( raw.pData[iLineEnd] == '\r' &&
                 iLine < iContentEnd && raw.pData[iLine] == '\n' ) {
                ++iLine;
            }
        } else {
            iLine = iLineEnd;
        }
    }
    normalized.pAllocation[cchWritten] = '\0';
    normalized.text = { normalized.pAllocation, cchWritten };
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t DecodeTokenText(
    parser_t &parser,
    const token_t &token,
    decoded_text_t &decoded ) noexcept
{
    if ( token.kind != token_kind_t::STRING ||
         token.lexeme.cchLength < 2u ) {
        return CY_FALSE;
    }

    decoded_text_t normalized{};
    string_view_t encoded{};
    if ( ( token.flags & TOKEN_FLAG_MULTILINE ) != 0u ) {
        if ( parser.bStrictJson ||
             !NormalizeMultilineText( parser, token, normalized ) ) {
            return CY_FALSE;
        }
        encoded = normalized.text;
    } else {
        encoded = {
            token.lexeme.pData + 1u,
            token.lexeme.cchLength - 2u
        };
    }
    if ( !parser.bStrictJson && !CykvEscapeSpellingIsValid( encoded ) ) {
        ReleaseDecoded( parser, normalized );
        return CY_FALSE;
    }
    const string_escape_style_t style = parser.bStrictJson
        ? string_escape_style_t::JSON
        : string_escape_style_t::CYPHER;
    const string_escape_result_t measured = StringEscape_Decode(
        encoded,
        style,
        CY_KEY_VALUE_ESCAPE_FLAGS,
        nullptr,
        0u );
    if ( measured.status != string_escape_status_t::OK &&
         measured.status != string_escape_status_t::OUTPUT_TRUNCATED ) {
        ReleaseDecoded( parser, normalized );
        return CY_FALSE;
    }
    if ( measured.cchRequired == CY_USIZE_MAX ) {
        ReleaseDecoded( parser, normalized );
        return CY_FALSE;
    }
    decoded.cbAllocation = measured.cchRequired + 1u;
    decoded.pAllocation = static_cast<char *>( Allocator_Allocate(
        parser.pDocument->pAllocator,
        decoded.cbAllocation,
        alignof( char ) ) );
    if ( decoded.pAllocation == nullptr ) {
        ReleaseDecoded( parser, normalized );
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    const string_escape_result_t result = StringEscape_Decode(
        encoded,
        style,
        CY_KEY_VALUE_ESCAPE_FLAGS,
        decoded.pAllocation,
        decoded.cbAllocation );
    ReleaseDecoded( parser, normalized );
    if ( result.status != string_escape_status_t::OK ) {
        Allocator_Free(
            parser.pDocument->pAllocator,
            decoded.pAllocation,
            decoded.cbAllocation,
            alignof( char ) );
        decoded = {};
        return CY_FALSE;
    }
    decoded.text = { decoded.pAllocation, result.cchWritten };
    if ( !DecodedCykvStringIsValid(
             decoded.text,
             !parser.bStrictJson ) ) {
        Allocator_Free(
            parser.pDocument->pAllocator,
            decoded.pAllocation,
            decoded.cbAllocation,
            alignof( char ) );
        decoded = {};
        return CY_FALSE;
    }
    return CY_TRUE;
}

void ReleaseDecoded( parser_t &parser, decoded_text_t &decoded ) noexcept
{
    if ( decoded.pAllocation != nullptr ) {
        Allocator_Free(
            parser.pDocument->pAllocator,
            decoded.pAllocation,
            decoded.cbAllocation,
            alignof( char ) );
    }
    decoded = {};
}

CYPHER_NODISCARD bool_t CheckLimits( parser_t &parser ) noexcept;

CYPHER_NODISCARD bool_t ParseDocumentHeader(
    parser_t &parser,
    string_view_t source ) noexcept
{
    if ( source.cchLength == 0u || source.pData[0] != '@' ||
         !Advance( parser ) || !IsPunctuation( parser, '@' ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::INVALID_HEADER );
        }
        return CY_FALSE;
    }

    if ( !Advance( parser ) || !TokenEqualsIdentifier( parser, "cykv" ) ||
         !Advance( parser ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::INVALID_HEADER );
        }
        return CY_FALSE;
    }

    u32 nLanguageVersion = 0u;
    parser.result.languageVersionLocation = parser.token.range.begin;
    if ( !ParseHeaderVersion( parser.token, nLanguageVersion ) ) {
        FailCurrent( parser, key_value_parse_status_t::INVALID_HEADER );
        return CY_FALSE;
    }
    if ( nLanguageVersion != CYKV_LANGUAGE_VERSION ) {
        FailCurrent( parser, key_value_parse_status_t::UNSUPPORTED_VERSION );
        return CY_FALSE;
    }
    if ( !Advance( parser ) || !IsNewlineToken( parser ) ||
         !Advance( parser ) || !IsPunctuation( parser, '@' ) ||
         !Advance( parser ) || !TokenEqualsIdentifier( parser, "schema" ) ||
         !Advance( parser ) || parser.token.kind != token_kind_t::STRING ||
         ( parser.token.flags & TOKEN_FLAG_MULTILINE ) != 0u ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::INVALID_HEADER );
        }
        return CY_FALSE;
    }

    decoded_text_t decodedSchema{};
    parser.result.schemaIdLocation = parser.token.range.begin;
    if ( !DecodeTokenText( parser, parser.token, decodedSchema ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::INVALID_SCHEMA );
        }
        return CY_FALSE;
    }
    if ( !IsSchemaId( decodedSchema.text ) || !Advance( parser ) ) {
        ReleaseDecoded( parser, decodedSchema );
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::INVALID_SCHEMA );
        }
        return CY_FALSE;
    }

    u32 nSchemaVersion = 0u;
    parser.result.schemaVersionLocation = parser.token.range.begin;
    if ( !ParseHeaderVersion( parser.token, nSchemaVersion ) ) {
        ReleaseDecoded( parser, decodedSchema );
        FailCurrent( parser, key_value_parse_status_t::INVALID_SCHEMA );
        return CY_FALSE;
    }
    if ( !Advance( parser ) || !IsNewlineToken( parser ) ) {
        ReleaseDecoded( parser, decodedSchema );
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::INVALID_HEADER );
        }
        return CY_FALSE;
    }

    const bool_t bHeaderSet = KeyValue_SetDocumentHeader(
        parser.pDocument,
        { nLanguageVersion, decodedSchema.text, nSchemaVersion } );
    ReleaseDecoded( parser, decodedSchema );
    if ( !bHeaderSet ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    if ( !CheckLimits( parser ) ) {
        return CY_FALSE;
    }

    parser.lexer.rules.flags &= ~static_cast<flags32_t>(
        LEXER_FLAG_EMIT_NEWLINES );
    if ( HasFlag( parser, KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS ) ) {
        parser.lexer.rules.flags |= LEXER_FLAG_ALLOW_LINE_COMMENTS |
                                    LEXER_FLAG_ALLOW_BLOCK_COMMENTS |
                                    LEXER_FLAG_ALLOW_NESTED_BLOCK_COMMENT;
    }
    return Advance( parser );
}

CYPHER_NODISCARD bool_t CheckLimits( parser_t &parser ) noexcept
{
    parser.result.nNodesParsed = KeyValue_InternalNodeCount( parser.pDocument );
    parser.result.cbStringData = KeyValue_InternalDataSize( parser.pDocument );
    if ( parser.result.nNodesParsed > parser.options.nMaxNodes ) {
        FailCurrent( parser, key_value_parse_status_t::NODE_LIMIT );
        return CY_FALSE;
    }
    if ( parser.result.cbStringData > parser.options.cbMaxStringData ) {
        FailCurrent( parser, key_value_parse_status_t::STRING_LIMIT );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseValue(
    parser_t &parser,
    key_value_t *pValue,
    usize nDepth ) noexcept;

CYPHER_NODISCARD bool_t ParseObject(
    parser_t &parser,
    key_value_t *pObject,
    usize nDepth ) noexcept
{
    if ( nDepth > parser.options.nMaxDepth ) {
        FailCurrent( parser, key_value_parse_status_t::DEPTH_LIMIT );
        return CY_FALSE;
    }
    if ( !KeyValue_SetContainerType(
             parser.pDocument,
             pObject,
             key_value_type_t::OBJECT ) || !Advance( parser ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK )
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    if ( IsPunctuation( parser, '}' ) ) {
        return Advance( parser );
    }

    while ( !parser.bAtEnd ) {
        decoded_text_t decodedName{};
        string_view_t name{};
        if ( parser.token.kind == token_kind_t::STRING ) {
            if ( !DecodeTokenText( parser, parser.token, decodedName ) ) {
                if ( parser.result.status == key_value_parse_status_t::OK )
                    FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
                return CY_FALSE;
            }
            name = decodedName.text;
        } else if ( !parser.bStrictJson &&
                    HasFlag( parser, KEY_VALUE_PARSE_FLAG_ALLOW_UNQUOTED_KEYS ) &&
                    parser.token.kind == token_kind_t::IDENTIFIER ) {
            name = parser.token.lexeme;
        } else {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }

        if ( ( !parser.bStrictJson ||
               HasFlag(
                   parser,
                   KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS ) ) &&
             KeyValue_Find( pObject, name ) != nullptr ) {
            ReleaseDecoded( parser, decodedName );
            FailCurrent( parser, key_value_parse_status_t::DUPLICATE_KEY );
            return CY_FALSE;
        }
        if ( !Advance( parser ) ||
             !IsPunctuation( parser, parser.bStrictJson ? ':' : '=' ) ) {
            ReleaseDecoded( parser, decodedName );
            if ( parser.result.status == key_value_parse_status_t::OK )
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        if ( !Advance( parser ) ) {
            ReleaseDecoded( parser, decodedName );
            return CY_FALSE;
        }
        if ( KeyValue_InternalNodeCount( parser.pDocument ) >=
             parser.options.nMaxNodes ) {
            ReleaseDecoded( parser, decodedName );
            FailCurrent( parser, key_value_parse_status_t::NODE_LIMIT );
            return CY_FALSE;
        }
        if ( pObject->nChildren >= parser.options.nMaxContainerValues ) {
            ReleaseDecoded( parser, decodedName );
            FailCurrent( parser, key_value_parse_status_t::CONTAINER_LIMIT );
            return CY_FALSE;
        }
        key_value_t *pChild = KeyValue_ObjectInsert(
            parser.pDocument,
            pObject,
            name,
            key_value_type_t::NULL_VALUE );
        ReleaseDecoded( parser, decodedName );
        if ( pChild == nullptr ) {
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
        if ( !CheckLimits( parser ) ||
             !ParseValue( parser, pChild, nDepth + 1u ) ) {
            return CY_FALSE;
        }

        if ( IsPunctuation( parser, '}' ) ) {
            return Advance( parser );
        }
        if ( !parser.bStrictJson ) {
            if ( !HasTriviaBeforeCurrent( parser ) ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
                return CY_FALSE;
            }
            continue;
        }
        if ( !IsPunctuation( parser, ',' ) ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        if ( !Advance( parser ) ) return CY_FALSE;
        if ( IsPunctuation( parser, '}' ) ) {
            if ( !HasFlag(
                     parser,
                     KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA ) ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
                return CY_FALSE;
            }
            return Advance( parser );
        }
    }
    FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ParseArray(
    parser_t &parser,
    key_value_t *pArray,
    usize nDepth ) noexcept
{
    if ( nDepth > parser.options.nMaxDepth ) {
        FailCurrent( parser, key_value_parse_status_t::DEPTH_LIMIT );
        return CY_FALSE;
    }
    if ( !KeyValue_SetContainerType(
             parser.pDocument,
             pArray,
             key_value_type_t::ARRAY ) || !Advance( parser ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK )
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    if ( IsPunctuation( parser, ']' ) ) return Advance( parser );

    while ( !parser.bAtEnd ) {
        if ( KeyValue_InternalNodeCount( parser.pDocument ) >=
             parser.options.nMaxNodes ) {
            FailCurrent( parser, key_value_parse_status_t::NODE_LIMIT );
            return CY_FALSE;
        }
        if ( pArray->nChildren >= parser.options.nMaxContainerValues ) {
            FailCurrent( parser, key_value_parse_status_t::CONTAINER_LIMIT );
            return CY_FALSE;
        }
        key_value_t *pChild = KeyValue_ArrayAppend(
            parser.pDocument,
            pArray,
            key_value_type_t::NULL_VALUE );
        if ( pChild == nullptr ) {
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
        if ( !ParseValue( parser, pChild, nDepth + 1u ) ) return CY_FALSE;
        if ( IsPunctuation( parser, ']' ) ) return Advance( parser );
        if ( !IsPunctuation( parser, ',' ) ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        if ( !Advance( parser ) ) return CY_FALSE;
        if ( IsPunctuation( parser, ']' ) ) {
            if ( !HasFlag(
                     parser,
                     KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA ) ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
                return CY_FALSE;
            }
            return Advance( parser );
        }
    }
    FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ParseStringValue(
    parser_t &parser,
    key_value_t *pValue ) noexcept
{
    decoded_text_t decoded{};
    if ( !DecodeTokenText( parser, parser.token, decoded ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK )
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    const bool_t bSet = KeyValue_SetString(
        parser.pDocument,
        pValue,
        decoded.text );
    ReleaseDecoded( parser, decoded );
    if ( !bSet ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    return CheckLimits( parser ) && Advance( parser );
}

CYPHER_NODISCARD bool_t ParseBinaryValue(
    parser_t &parser,
    key_value_t *pValue ) noexcept
{
    if ( !Advance( parser ) || parser.token.kind != token_kind_t::STRING ||
         ( parser.token.flags &
           ( TOKEN_FLAG_HAS_ESCAPES | TOKEN_FLAG_MULTILINE ) ) != 0u ) {
        if ( parser.result.status == key_value_parse_status_t::OK )
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    decoded_text_t decoded{};
    if ( !DecodeTokenText( parser, parser.token, decoded ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK )
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    const string_convert_result_t measured = StringConvert_HexToBinary(
        decoded.text,
        nullptr,
        0u );
    if ( measured.status != string_convert_status_t::OUTPUT_TRUNCATED &&
         measured.status != string_convert_status_t::OK ) {
        ReleaseDecoded( parser, decoded );
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    byte *pBytes = nullptr;
    if ( measured.cchRequired != 0u ) {
        pBytes = static_cast<byte *>( Allocator_Allocate(
            parser.pDocument->pAllocator,
            measured.cchRequired,
            alignof( byte ) ) );
        if ( pBytes == nullptr ) {
            ReleaseDecoded( parser, decoded );
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
    }
    const string_convert_result_t converted = StringConvert_HexToBinary(
        decoded.text,
        pBytes,
        measured.cchRequired );
    ReleaseDecoded( parser, decoded );
    if ( converted.status != string_convert_status_t::OK ) {
        if ( pBytes != nullptr ) Allocator_Free(
            parser.pDocument->pAllocator,
            pBytes,
            measured.cchRequired,
            alignof( byte ) );
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    const bool_t bSet = KeyValue_SetBinary(
        parser.pDocument,
        pValue,
        { pBytes, measured.cchRequired } );
    if ( pBytes != nullptr ) Allocator_Free(
        parser.pDocument->pAllocator,
        pBytes,
        measured.cchRequired,
        alignof( byte ) );
    if ( !bSet ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    return CheckLimits( parser ) && Advance( parser );
}

CYPHER_NODISCARD bool_t CykvNumberSyntaxIsValid(
    const token_t &token ) noexcept
{
    string_view_t text = token.lexeme;
    if ( !StringView_IsValid( text ) || text.cchLength == 0u ) {
        return CY_FALSE;
    }

    if ( ( token.flags & TOKEN_FLAG_UNSIGNED ) != 0u ) {
        if ( text.pData[text.cchLength - 1u] != 'u' ) {
            return CY_FALSE;
        }
        --text.cchLength;
    }

    usize iByte = 0u;
    if ( iByte < text.cchLength &&
         ( text.pData[iByte] == '+' || text.pData[iByte] == '-' ) ) {
        if ( ( token.flags & TOKEN_FLAG_UNSIGNED ) != 0u ) {
            return CY_FALSE;
        }
        ++iByte;
    }
    if ( iByte == text.cchLength ) {
        return CY_FALSE;
    }

    if ( token.kind == token_kind_t::INTEGER ) {
        if ( ( token.flags & TOKEN_FLAG_BASE_PREFIX ) != 0u ) {
            return iByte + 2u < text.cchLength &&
                   text.pData[iByte] == '0';
        }

        usize cDigits = 0u;
        char chFirstDigit = '\0';
        for ( ; iByte < text.cchLength; ++iByte ) {
            const char ch = text.pData[iByte];
            if ( ch == '_' ) {
                continue;
            }
            if ( !Char_IsDigitAscii( ch ) ) {
                return CY_FALSE;
            }
            if ( cDigits == 0u ) {
                chFirstDigit = ch;
            }
            ++cDigits;
        }
        return cDigits != 0u &&
               ( cDigits == 1u || chFirstDigit != '0' );
    }

    if ( token.kind != token_kind_t::FLOAT ||
         ( token.flags & ( TOKEN_FLAG_BASE_PREFIX |
                           TOKEN_FLAG_UNSIGNED ) ) != 0u ) {
        return CY_FALSE;
    }

    usize cWholeDigits = 0u;
    char chFirstWholeDigit = '\0';
    while ( iByte < text.cchLength &&
            text.pData[iByte] != '.' &&
            text.pData[iByte] != 'e' &&
            text.pData[iByte] != 'E' ) {
        const char ch = text.pData[iByte++];
        if ( ch == '_' ) {
            continue;
        }
        if ( !Char_IsDigitAscii( ch ) ) {
            return CY_FALSE;
        }
        if ( cWholeDigits == 0u ) {
            chFirstWholeDigit = ch;
        }
        ++cWholeDigits;
    }
    if ( cWholeDigits == 0u ||
         ( cWholeDigits > 1u && chFirstWholeDigit == '0' ) ) {
        return CY_FALSE;
    }

    bool_t bHasFraction = CY_FALSE;
    if ( iByte < text.cchLength && text.pData[iByte] == '.' ) {
        bHasFraction = CY_TRUE;
        ++iByte;
        usize cFractionDigits = 0u;
        while ( iByte < text.cchLength &&
                text.pData[iByte] != 'e' &&
                text.pData[iByte] != 'E' ) {
            const char ch = text.pData[iByte++];
            if ( ch == '_' ) {
                continue;
            }
            if ( !Char_IsDigitAscii( ch ) ) {
                return CY_FALSE;
            }
            ++cFractionDigits;
        }
        if ( cFractionDigits == 0u ) {
            return CY_FALSE;
        }
    }

    bool_t bHasExponent = CY_FALSE;
    if ( iByte < text.cchLength &&
         ( text.pData[iByte] == 'e' || text.pData[iByte] == 'E' ) ) {
        bHasExponent = CY_TRUE;
        ++iByte;
        if ( iByte < text.cchLength &&
             ( text.pData[iByte] == '+' || text.pData[iByte] == '-' ) ) {
            ++iByte;
        }
        usize cExponentDigits = 0u;
        for ( ; iByte < text.cchLength; ++iByte ) {
            const char ch = text.pData[iByte];
            if ( ch == '_' ) {
                continue;
            }
            if ( !Char_IsDigitAscii( ch ) ) {
                return CY_FALSE;
            }
            ++cExponentDigits;
        }
        if ( cExponentDigits == 0u ) {
            return CY_FALSE;
        }
    }
    return iByte == text.cchLength && ( bHasFraction || bHasExponent );
}

CYPHER_NODISCARD bool_t CopyNumberWithoutSeparators(
    parser_t &parser,
    string_view_t text,
    decoded_text_t &copy ) noexcept
{
    bool_t bHasSeparator = CY_FALSE;
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        bHasSeparator |= text.pData[iByte] == '_';
    }
    if ( !bHasSeparator ) {
        copy.text = text;
        return CY_TRUE;
    }
    if ( text.cchLength == CY_USIZE_MAX ) {
        return CY_FALSE;
    }

    copy.cbAllocation = text.cchLength + 1u;
    copy.pAllocation = static_cast<char *>( Allocator_Allocate(
        parser.pDocument->pAllocator,
        copy.cbAllocation,
        alignof( char ) ) );
    if ( copy.pAllocation == nullptr ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    usize cchWritten = 0u;
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( text.pData[iByte] != '_' ) {
            copy.pAllocation[cchWritten++] = text.pData[iByte];
        }
    }
    copy.pAllocation[cchWritten] = '\0';
    copy.text = { copy.pAllocation, cchWritten };
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseNumber(
    parser_t &parser,
    key_value_t *pValue ) noexcept
{
    const string_view_t text = parser.token.lexeme;
    if ( parser.bStrictJson && !IsStrictJsonNumber( text ) ) {
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    if ( !parser.bStrictJson && !CykvNumberSyntaxIsValid( parser.token ) ) {
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }

    bool_t bSet = CY_FALSE;
    if ( parser.token.kind == token_kind_t::FLOAT ) {
        decoded_text_t normalized{};
        if ( !CopyNumberWithoutSeparators( parser, text, normalized ) ) {
            return CY_FALSE;
        }
        f64 value = 0.0;
        const string_parse_result_t parsed = StringParse_F64(
            normalized.text,
            STRING_PARSE_FLAG_ALLOW_PLUS_SIGN,
            &value );
        bSet = StringParse_Succeeded( parsed ) &&
               KeyValue_SetF64( parser.pDocument, pValue, value );
        ReleaseDecoded( parser, normalized );
    } else if ( parser.bStrictJson ) {
        if ( text.pData[0] == '-' ) {
            i64 value = 0;
            const string_parse_result_t parsed = StringParse_I64(
                text,
                { 10u, STRING_PARSE_FLAG_NONE },
                &value );
            bSet = StringParse_Succeeded( parsed ) &&
                   KeyValue_SetI64( parser.pDocument, pValue, value );
        } else {
            u64 value = 0u;
            const string_parse_result_t parsed = StringParse_U64(
                text,
                { 10u, STRING_PARSE_FLAG_NONE },
                &value );
            bSet = StringParse_Succeeded( parsed ) &&
                   ( value > static_cast<u64>( CY_I64_MAX )
                     ? KeyValue_SetU64( parser.pDocument, pValue, value )
                     : KeyValue_SetI64(
                         parser.pDocument,
                         pValue,
                         static_cast<i64>( value ) ) );
        }
    } else if ( ( parser.token.flags & TOKEN_FLAG_UNSIGNED ) != 0u ) {
        const string_view_t magnitude{
            text.pData,
            text.cchLength - 1u
        };
        u64 value = 0u;
        const string_parse_result_t parsed = StringParse_U64(
            magnitude,
            {
                0u,
                STRING_PARSE_FLAG_ALLOW_BASE_PREFIX |
                    STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR
            },
            &value );
        bSet = StringParse_Succeeded( parsed ) &&
               KeyValue_SetU64( parser.pDocument, pValue, value );
    } else {
        i64 value = 0;
        const string_parse_result_t parsed = StringParse_I64(
            text,
            {
                0u,
                STRING_PARSE_FLAG_ALLOW_PLUS_SIGN |
                    STRING_PARSE_FLAG_ALLOW_BASE_PREFIX |
                    STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR
            },
            &value );
        bSet = StringParse_Succeeded( parsed ) &&
               KeyValue_SetI64( parser.pDocument, pValue, value );
    }
    if ( !bSet ) {
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    return Advance( parser );
}

bool_t ParseValue(
    parser_t &parser,
    key_value_t *pValue,
    usize nDepth ) noexcept
{
    if ( parser.bAtEnd ) {
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    if ( IsPunctuation( parser, '{' ) ) {
        return ParseObject( parser, pValue, nDepth );
    }
    if ( IsPunctuation( parser, '[' ) ) {
        return ParseArray( parser, pValue, nDepth );
    }
    if ( parser.token.kind == token_kind_t::STRING ) {
        return ParseStringValue( parser, pValue );
    }
    if ( parser.token.kind == token_kind_t::INTEGER ||
         parser.token.kind == token_kind_t::FLOAT ) {
        return ParseNumber( parser, pValue );
    }
    if ( TokenEqualsIdentifier( parser, "null" ) ) {
        if ( !KeyValue_SetNull( parser.pDocument, pValue ) ) return CY_FALSE;
        return Advance( parser );
    }
    if ( TokenEqualsIdentifier( parser, "true" ) ||
         TokenEqualsIdentifier( parser, "false" ) ) {
        const bool_t value = TokenEqualsIdentifier( parser, "true" );
        if ( !KeyValue_SetBool( parser.pDocument, pValue, value ) ) return CY_FALSE;
        return Advance( parser );
    }
    if ( !parser.bStrictJson && TokenEqualsIdentifier( parser, "hex" ) ) {
        return ParseBinaryValue( parser, pValue );
    }
    FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t InitializeParser(
    parser_t &parser,
    string_view_t text ) noexcept
{
    lexer_rules_t rules = Lexer_DefaultRules();
    rules.flags = LEXER_FLAG_ALLOW_ESCAPE_SEQUENCES |
                  LEXER_FLAG_SIGN_IS_NUMBER_PART;
    if ( !parser.bStrictJson ) {
        rules.flags |= LEXER_FLAG_ALLOW_UTF8_IDENTIFIERS |
                       LEXER_FLAG_ALLOW_UNSIGNED_SUFFIX |
                       LEXER_FLAG_ALLOW_MULTILINE_STRING |
                       LEXER_FLAG_EMIT_NEWLINES;
    }
    CharacterSet_AddView(
        &rules.identifierBodyExtra,
        StringView_FromCString( ".-/" ) );
    rules.cchMaxToken = parser.options.cbMaxStringData;
    rules.nMaxCommentDepth = parser.options.nMaxCommentDepth;
    return Lexer_Init( &parser.lexer, text, rules );
}

} // namespace

key_value_parse_result_t KeyValue_InternalParseText(
    string_view_t text,
    const key_value_parse_options_t &options,
    key_value_document_t *pDocument,
    bool_t bStrictJson ) noexcept
{
    key_value_parse_result_t invalid{};
    if ( !StringView_IsValid( text ) ||
         !KeyValue_InternalDocumentIsValid( pDocument ) ||
         ( options.flags & ~CY_KEY_VALUE_PARSE_VALID_FLAGS ) != 0u ||
         options.cbMaxInput == 0u ||
         text.cchLength > options.cbMaxInput ||
         options.nMaxDepth == 0u ||
         options.nMaxDepth > CY_KEY_VALUE_MAX_DEPTH ||
         options.nMaxNodes == 0u ||
         options.nMaxContainerValues == 0u ||
         options.nMaxCommentDepth == 0u ||
         options.cbMaxStringData == 0u ) {
        invalid.status = key_value_parse_status_t::INVALID_ARGUMENT;
        if ( StringView_IsValid( text ) &&
             options.cbMaxInput != 0u &&
             text.cchLength > options.cbMaxInput ) {
            invalid.status = key_value_parse_status_t::INPUT_LIMIT;
        }
        return invalid;
    }

    key_value_document_t *pTemporary = KeyValue_InternalCreateLike( pDocument );
    if ( pTemporary == nullptr ) {
        invalid.status = key_value_parse_status_t::OUT_OF_MEMORY;
        return invalid;
    }
    parser_t parser{};
    parser.options = options;
    parser.pDocument = pTemporary;
    parser.bStrictJson = bStrictJson;
    if ( !ValidateSourceEncoding( parser, text ) ||
         !InitializeParser( parser, text ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK )
            parser.result.status = key_value_parse_status_t::LEXER_ERROR;
        KeyValue_DestroyDocument( pTemporary );
        return parser.result;
    }

    const bool_t bReady = bStrictJson
        ? Advance( parser )
        : ParseDocumentHeader( parser, text );
    if ( !bReady ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            parser.result.status = key_value_parse_status_t::INVALID_HEADER;
        }
        KeyValue_DestroyDocument( pTemporary );
        return parser.result;
    }

    if ( ( !bStrictJson ||
           !HasFlag( parser, KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE ) ) &&
         !IsPunctuation( parser, '{' ) ) {
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
    } else {
        static_cast<void>( ParseValue(
            parser,
            KeyValue_Root( pTemporary ),
            0u ) );
    }
    if ( parser.result.status == key_value_parse_status_t::OK &&
         !parser.bAtEnd ) {
        FailCurrent( parser, key_value_parse_status_t::TRAILING_INPUT );
    }
    parser.result.nNodesParsed = KeyValue_InternalNodeCount( pTemporary );
    parser.result.cbStringData = KeyValue_InternalDataSize( pTemporary );
    if ( parser.result.status == key_value_parse_status_t::OK ) {
        KeyValue_InternalMoveDocumentContents( pDocument, pTemporary );
    }
    KeyValue_DestroyDocument( pTemporary );
    return parser.result;
}

key_value_parse_result_t KeyValue_ParseText(
    string_view_t text,
    const key_value_parse_options_t &options,
    key_value_document_t *pDocument ) noexcept
{
    return KeyValue_InternalParseText( text, options, pDocument, CY_FALSE );
}

const char *KeyValue_ParseStatusName(
    key_value_parse_status_t status ) noexcept
{
    switch ( status ) {
        case key_value_parse_status_t::OK:             return "OK";
        case key_value_parse_status_t::INVALID_ARGUMENT:return "INVALID_ARGUMENT";
        case key_value_parse_status_t::INPUT_LIMIT:    return "INPUT_LIMIT";
        case key_value_parse_status_t::INVALID_ENCODING:return "INVALID_ENCODING";
        case key_value_parse_status_t::INVALID_HEADER: return "INVALID_HEADER";
        case key_value_parse_status_t::UNSUPPORTED_VERSION:
            return "UNSUPPORTED_VERSION";
        case key_value_parse_status_t::INVALID_SCHEMA: return "INVALID_SCHEMA";
        case key_value_parse_status_t::LEXER_ERROR:    return "LEXER_ERROR";
        case key_value_parse_status_t::SYNTAX_ERROR:   return "SYNTAX_ERROR";
        case key_value_parse_status_t::DUPLICATE_KEY:  return "DUPLICATE_KEY";
        case key_value_parse_status_t::DEPTH_LIMIT:    return "DEPTH_LIMIT";
        case key_value_parse_status_t::NODE_LIMIT:     return "NODE_LIMIT";
        case key_value_parse_status_t::CONTAINER_LIMIT:return "CONTAINER_LIMIT";
        case key_value_parse_status_t::COMMENT_DEPTH_LIMIT:
            return "COMMENT_DEPTH_LIMIT";
        case key_value_parse_status_t::STRING_LIMIT:   return "STRING_LIMIT";
        case key_value_parse_status_t::OUT_OF_MEMORY:  return "OUT_OF_MEMORY";
        case key_value_parse_status_t::TRAILING_INPUT: return "TRAILING_INPUT";
    }
    return "UNKNOWN_KEY_VALUE_PARSE_STATUS";
}

} // namespace cypher::common
