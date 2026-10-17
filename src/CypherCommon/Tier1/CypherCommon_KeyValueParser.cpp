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

/*
================
Key Value Parser Implementation Notes

Parses bounded UTF-8 source into caller-owned CYKV storage. Source offsets and diagnostics must
continue to refer to the original input, and malformed text must not escape as a usable partial
document.
================
*/

#include "CypherCommon_KeyValueParserInternal.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_BuildConfig.h"
#include "CypherCommon_Platform.h"
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
    KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE |
    KEY_VALUE_PARSE_FLAG_KEEP_COMMENTS; // Reject unknown option bits at the public boundary.

constexpr flags32_t CY_KEY_VALUE_ESCAPE_FLAGS =
    STRING_ESCAPE_FLAG_QUOTES |
    STRING_ESCAPE_FLAG_BACKSLASH |
    STRING_ESCAPE_FLAG_CONTROL_CHARS |
    STRING_ESCAPE_FLAG_NON_ASCII |
    STRING_ESCAPE_FLAG_PATH_SLASHES; // Complete escape vocabulary accepted by CYKV strings.

struct decoded_text_t {
    string_view_t text{};         // Decoded view; may borrow the token or pAllocation.
    char *pAllocation{ nullptr }; // Temporary allocator-owned decoding storage.
    usize cbAllocation{ 0u };     // Exact byte count needed when releasing storage.
};

// A CYKV 2 operator (#delete, +=) waiting for the inheritance it applies to.
struct parse_op_t {
    key_value_t *pNode{ nullptr };   // The member carrying the operator; its parseMark says which.
    text_location_t location{};      // Where it was written, for an unapplied-operator error.
};

enum parse_mark_t : u8 {
    PARSE_MARK_DELETE = 1u,  // `key = #delete`: drop the inherited member.
    PARSE_MARK_APPEND = 2u,  // `key += [...]`: inherited elements first, then these.
    PARSE_MARK_APPLIED = 4u  // An inheritance source supplied what the operator needs.
};

constexpr usize kMaxExtends = 16u; // #extends lines per object.

struct parser_t {
    lexer_t lexer{};                       // Lexer and source cursor owned by this parse.
    token_t token{};                       // Current token supplied to recursive productions.
    key_value_parse_options_t options{};   // Caller limits and accepted syntax extensions.
    key_value_document_t *pDocument{ nullptr }; // Document receiving the current value.
    key_value_document_t *pOutputDocument{ nullptr }; // Transactional result document.
    key_value_document_t *pDefinitions{ nullptr }; // Private CYKV 2 definition values.
    key_value_t *pDefinitionBeingParsed{ nullptr }; // Excluded until declaration completes.
    key_value_parse_result_t result{};     // Sticky first error plus resource counters.
    u32 nLanguageVersion{ 0u };            // Selects the active CYKV grammar revision.
    usize nDefinitions{ 0u };              // Completed immutable local definitions.
    usize iPreviousTokenEnd{ 0u };         // Detects required CYKV whitespace between members.
    bool_t bStrictJson{ CY_FALSE };         // Selects JSON grammar instead of CYKV grammar.
    bool_t bAtEnd{ CY_FALSE };              // Lexer has emitted its terminal state.
    bool_t bHasToken{ CY_FALSE };           // token contains a current lexical item.
    key_value_document_t *pScratch{ nullptr }; // Receives skipped @when branches; emptied after each.
    const key_value_build_context_t *pBuildContext{ nullptr }; // What @when conditions read.
    usize nInheritanceDepth{ 0u };         // > 0 inside an #extends object or a document with #base.
    parse_op_t *pOps{ nullptr };           // Operators awaiting inheritance, in source order.
    usize nOps{ 0u };
    usize nOpsCapacity{ 0u };
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
    // Diagnostics are first-failure wins; later cleanup must not hide the cause.
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
    // Preserve the end of the previous token before replacing the current one.
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

CYPHER_NODISCARD bool_t HasHorizontalSpaceBeforeCurrent(
    const parser_t &parser ) noexcept
{
    if ( !HasTriviaBeforeCurrent( parser ) ) {
        return CY_FALSE;
    }
    bool_t bSawHorizontalSpace = CY_FALSE;
    for ( usize iByte = parser.iPreviousTokenEnd;
          iByte < parser.token.range.begin.iByte;
          ++iByte ) {
        const char ch = parser.lexer.source.pData[iByte];
        if ( ch == '\r' || ch == '\n' ) {
            return CY_FALSE;
        }
        bSawHorizontalSpace |= ch == ' ' || ch == '\t';
    }
    return bSawHorizontalSpace;
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

    // Columns count decoded scalars, while iByte remains an exact source offset.
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
    // CYKV is normalized UTF-8 text: embedded NUL and lone CR are forbidden.
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

    // Schema IDs use lower-case dotted components: "cypher.shader", for example.
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

CYPHER_NODISCARD bool_t IsDefinitionName( string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) || name.cchLength == 0u ||
         ( !Char_IsAlphaAscii( name.pData[0] ) && name.pData[0] != '_' ) ) {
        return CY_FALSE;
    }
    for ( usize iByte = 1u; iByte < name.cchLength; ++iByte ) {
        if ( !Char_IsAlphaNumericAscii( name.pData[iByte] ) &&
             name.pData[iByte] != '_' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsDefinitionMemberName( string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) || name.cchLength == 0u ||
         ( !Char_IsAlphaAscii( name.pData[0] ) && name.pData[0] != '_' ) ) {
        return CY_FALSE;
    }
    for ( usize iByte = 1u; iByte < name.cchLength; ++iByte ) {
        if ( !Char_IsAlphaNumericAscii( name.pData[iByte] ) &&
             name.pData[iByte] != '_' && name.pData[iByte] != '-' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t IsDefinitionPath( string_view_t path ) noexcept
{
    if ( !StringView_IsValid( path ) || path.cchLength == 0u ) {
        return CY_FALSE;
    }
    usize iBegin = 0u;
    bool_t bFirst = CY_TRUE;
    while ( iBegin < path.cchLength ) {
        usize iEnd = iBegin;
        while ( iEnd < path.cchLength && path.pData[iEnd] != '.' ) {
            ++iEnd;
        }
        const string_view_t segment{
            path.pData + iBegin,
            iEnd - iBegin
        };
        if ( ( bFirst && !IsDefinitionName( segment ) ) ||
             ( !bFirst && !IsDefinitionMemberName( segment ) ) ) {
            return CY_FALSE;
        }
        bFirst = CY_FALSE;
        if ( iEnd == path.cchLength ) {
            return CY_TRUE;
        }
        iBegin = iEnd + 1u;
    }
    return CY_FALSE;
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
    // Validate JSON's decimal grammar directly; the generic parser accepts CYKV extensions.
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
    // Reject unknown escapes before the shared decoder normalizes their spelling.
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

    // Triple-quoted content starts on the line after the opening delimiter.
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

    // The indentation before the closing delimiter defines the common margin.
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

    // Strip that margin line-by-line and canonicalize every newline to '\n'.
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
    // Decode in two passes so allocation is exact and failure leaves no partial value.
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
    // Every authored CYKV document starts with language and schema directives.
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
    if ( nLanguageVersion != CYKV_LANGUAGE_VERSION_1 &&
         nLanguageVersion != CYKV_LANGUAGE_VERSION_2 ) {
        FailCurrent( parser, key_value_parse_status_t::UNSUPPORTED_VERSION );
        return CY_FALSE;
    }
    parser.nLanguageVersion = nLanguageVersion;
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

    // Newlines are grammar tokens only while parsing the two-line header.
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
    // Definition storage is private parse state, but still counts against the same
    // hostile-input budgets while it is live. Its synthetic root is excluded.
    const usize nOutputNodes = KeyValue_InternalNodeCount(
        parser.pOutputDocument );
    const usize nDefinitionNodes = parser.pDefinitions != nullptr
        ? KeyValue_InternalNodeCount( parser.pDefinitions ) - 1u
        : 0u;
    const usize cbOutputData = KeyValue_InternalDataSize(
        parser.pOutputDocument );
    const usize cbDefinitionData = parser.pDefinitions != nullptr
        ? KeyValue_InternalDataSize( parser.pDefinitions )
        : 0u;
    // A skipped @when branch is parsed in full before it is dropped, so it
    // counts while it is live.
    const usize nScratchNodes = parser.pScratch != nullptr
        ? KeyValue_InternalNodeCount( parser.pScratch ) - 1u
        : 0u;
    const usize cbScratchData = parser.pScratch != nullptr
        ? KeyValue_InternalDataSize( parser.pScratch )
        : 0u;

    const bool_t bNodeOverflow = nOutputNodes > CY_USIZE_MAX - nDefinitionNodes - nScratchNodes;
    const bool_t bDataOverflow = cbOutputData > CY_USIZE_MAX - cbDefinitionData - cbScratchData;
    parser.result.nNodesParsed = bNodeOverflow
        ? CY_USIZE_MAX
        : nOutputNodes + nDefinitionNodes + nScratchNodes;
    parser.result.cbStringData = bDataOverflow
        ? CY_USIZE_MAX
        : cbOutputData + cbDefinitionData + cbScratchData;
    if ( bNodeOverflow || parser.result.nNodesParsed > parser.options.nMaxNodes ) {
        FailCurrent( parser, key_value_parse_status_t::NODE_LIMIT );
        return CY_FALSE;
    }
    if ( bDataOverflow ||
         parser.result.cbStringData > parser.options.cbMaxStringData ) {
        FailCurrent( parser, key_value_parse_status_t::STRING_LIMIT );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t CanAllocateNode( parser_t &parser ) noexcept
{
    if ( !CheckLimits( parser ) ) {
        return CY_FALSE;
    }
    if ( parser.result.nNodesParsed >= parser.options.nMaxNodes ) {
        FailCurrent( parser, key_value_parse_status_t::NODE_LIMIT );
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t EnsureDefinitionStorage(
    parser_t &parser ) noexcept
{
    if ( parser.pDefinitions != nullptr ) {
        return CY_TRUE;
    }
    const key_value_document_t *pOutput = parser.pOutputDocument;
    parser.pDefinitions = KeyValue_CreateDocument({
        pOutput->pAllocator,
        pOutput->nInitialNodes,
        pOutput->cbInitialData,
        // CYKV directive symbols and qualified member paths are always exact-
        // case, independently of a generic document's lookup policy.
        CY_FALSE
    });
    if ( parser.pDefinitions == nullptr ||
         !KeyValue_SetRootType(
             parser.pDefinitions,
             key_value_type_t::OBJECT ) ) {
        if ( parser.pDefinitions != nullptr ) {
            KeyValue_DestroyDocument( parser.pDefinitions );
            parser.pDefinitions = nullptr;
        }
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    return CY_TRUE;
}

struct copied_value_cost_t {
    usize nAdditionalNodes{ 0u };       // Source descendants; destination already exists.
    usize cbAdditionalData{ 0u };      // Copied strings, binary bytes, and object keys.
    usize nMaxContainerDepth{ 0u };    // Deepest container offset from source root.
    bool_t bHasContainer{ CY_FALSE };
};

CYPHER_NODISCARD bool_t AddCopyCost(
    usize &total,
    usize amount ) noexcept
{
    if ( amount > CY_USIZE_MAX - total ) {
        return CY_FALSE;
    }
    total += amount;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t MeasureCopiedValue(
    const key_value_t *pValue,
    usize nRelativeDepth,
    copied_value_cost_t &cost ) noexcept
{
    if ( pValue->type == key_value_type_t::OBJECT ||
         pValue->type == key_value_type_t::ARRAY ) {
        cost.bHasContainer = CY_TRUE;
        if ( nRelativeDepth > cost.nMaxContainerDepth ) {
            cost.nMaxContainerDepth = nRelativeDepth;
        }
    } else if ( pValue->type == key_value_type_t::STRING ) {
        if ( pValue->value.bytes.cbSize == CY_USIZE_MAX ||
             !AddCopyCost(
                 cost.cbAdditionalData,
                 pValue->value.bytes.cbSize + 1u ) ) {
            return CY_FALSE;
        }
    } else if ( pValue->type == key_value_type_t::BINARY &&
                !AddCopyCost(
                    cost.cbAdditionalData,
                    pValue->value.bytes.cbSize ) ) {
        return CY_FALSE;
    }

    for ( const key_value_t *pChild = pValue->pFirstChild;
          pChild != nullptr;
          pChild = pChild->pNext ) {
        if ( !AddCopyCost( cost.nAdditionalNodes, 1u ) ) {
            return CY_FALSE;
        }
        if ( pValue->type == key_value_type_t::OBJECT &&
             ( pChild->cchName == CY_USIZE_MAX ||
               !AddCopyCost(
                   cost.cbAdditionalData,
                   pChild->cchName + 1u ) ) ) {
            return CY_FALSE;
        }
        if ( nRelativeDepth == CY_USIZE_MAX ||
             !MeasureCopiedValue(
                 pChild,
                 nRelativeDepth + 1u,
                 cost ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t CopyDefinitionValue(
    parser_t &parser,
    key_value_t *pDest,
    const key_value_t *pSource,
    usize nDepth ) noexcept
{
    copied_value_cost_t cost{};
    if ( !MeasureCopiedValue( pSource, 0u, cost ) ) {
        FailCurrent( parser, key_value_parse_status_t::NODE_LIMIT );
        return CY_FALSE;
    }
    if ( cost.bHasContainer &&
         ( nDepth > parser.options.nMaxDepth ||
           cost.nMaxContainerDepth > parser.options.nMaxDepth - nDepth ) ) {
        FailCurrent( parser, key_value_parse_status_t::DEPTH_LIMIT );
        return CY_FALSE;
    }
    if ( !CheckLimits( parser ) ) {
        return CY_FALSE;
    }
    if ( cost.nAdditionalNodes >
         parser.options.nMaxNodes - parser.result.nNodesParsed ) {
        FailCurrent( parser, key_value_parse_status_t::NODE_LIMIT );
        return CY_FALSE;
    }
    if ( cost.cbAdditionalData >
         parser.options.cbMaxStringData - parser.result.cbStringData ) {
        FailCurrent( parser, key_value_parse_status_t::STRING_LIMIT );
        return CY_FALSE;
    }
    if ( !KeyValue_InternalCopyValue(
             parser.pDocument,
             pDest,
             pSource ) ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    return CheckLimits( parser );
}

// The value a `$name.member...` path names: a definition or include alias,
// then object members. Null when any segment is missing, or names the
// definition still being declared.
CYPHER_NODISCARD const key_value_t *LookupDefinition(
    const parser_t &parser,
    string_view_t path ) noexcept
{
    usize iSegmentEnd = 0u;
    while ( iSegmentEnd < path.cchLength &&
            path.pData[iSegmentEnd] != '.' ) {
        ++iSegmentEnd;
    }
    const key_value_t *pDefinition = parser.pDefinitions != nullptr
        ? KeyValue_Find(
            KeyValue_Root( parser.pDefinitions ),
            { path.pData, iSegmentEnd } )
        : nullptr;
    if ( pDefinition == parser.pDefinitionBeingParsed ) {
        pDefinition = nullptr;
    }
    usize iSegmentBegin = iSegmentEnd < path.cchLength
        ? iSegmentEnd + 1u
        : path.cchLength;
    while ( pDefinition != nullptr && iSegmentBegin < path.cchLength ) {
        iSegmentEnd = iSegmentBegin;
        while ( iSegmentEnd < path.cchLength &&
                path.pData[iSegmentEnd] != '.' ) {
            ++iSegmentEnd;
        }
        pDefinition = KeyValue_Type( pDefinition ) == key_value_type_t::OBJECT
            ? KeyValue_Find(
                pDefinition,
                {
                    path.pData + iSegmentBegin,
                    iSegmentEnd - iSegmentBegin
                } )
            : nullptr;
        iSegmentBegin = iSegmentEnd < path.cchLength
            ? iSegmentEnd + 1u
            : path.cchLength;
    }
    return pDefinition;
}

CYPHER_NODISCARD bool_t ParseDefinitionReference(
    parser_t &parser,
    key_value_t *pValue,
    usize nDepth ) noexcept
{
    // '$' is deliberately meaningful only in value position and only in CYKV 2.
    if ( !Advance( parser ) || HasTriviaBeforeCurrent( parser ) ||
         parser.token.kind != token_kind_t::IDENTIFIER ||
         !IsDefinitionPath( parser.token.lexeme ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        }
        return CY_FALSE;
    }

    const key_value_t *pDefinition = LookupDefinition( parser, parser.token.lexeme );
    if ( pDefinition == nullptr ) {
        FailCurrent(
            parser,
            key_value_parse_status_t::UNDEFINED_DEFINITION );
        return CY_FALSE;
    }
    if ( !CopyDefinitionValue(
             parser,
             pValue,
             pDefinition,
             nDepth ) ) {
        return CY_FALSE;
    }
    return Advance( parser );
}

CYPHER_NODISCARD bool_t SeedDefinitions(
    parser_t &parser,
    const key_value_definition_seed_t *pSeeds,
    usize nSeeds ) noexcept
{
    if ( nSeeds == 0u ) {
        return CY_TRUE;
    }
    if ( parser.bStrictJson ||
         parser.nLanguageVersion != CYKV_LANGUAGE_VERSION_2 ||
         pSeeds == nullptr ) {
        FailCurrent( parser, key_value_parse_status_t::INVALID_ARGUMENT );
        return CY_FALSE;
    }
    if ( nSeeds > parser.options.nMaxDefinitions ) {
        FailCurrent( parser, key_value_parse_status_t::DEFINITION_LIMIT );
        return CY_FALSE;
    }
    if ( nSeeds != 0u && !EnsureDefinitionStorage( parser ) ) {
        return CY_FALSE;
    }
    for ( usize iSeed = 0u; iSeed < nSeeds; ++iSeed ) {
        const key_value_definition_seed_t &seed = pSeeds[iSeed];
        if ( !IsDefinitionName( seed.name ) ||
             !KeyValue_InternalTreeIsValid( seed.pValue ) ) {
            FailCurrent( parser, key_value_parse_status_t::INVALID_ARGUMENT );
            return CY_FALSE;
        }
        if ( KeyValue_Find(
                 KeyValue_Root( parser.pDefinitions ),
                 seed.name ) != nullptr ) {
            FailCurrent(
                parser,
                key_value_parse_status_t::DUPLICATE_DEFINITION );
            return CY_FALSE;
        }
        if ( !CanAllocateNode( parser ) ) {
            return CY_FALSE;
        }
        key_value_t *pDefinition = KeyValue_ObjectInsert(
            parser.pDefinitions,
            KeyValue_Root( parser.pDefinitions ),
            seed.name,
            key_value_type_t::NULL_VALUE );
        if ( pDefinition == nullptr ) {
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
        if ( !CheckLimits( parser ) ) {
            return CY_FALSE;
        }
        key_value_document_t *pPreviousDocument = parser.pDocument;
        parser.pDocument = parser.pDefinitions;
        const bool_t bCopied = CopyDefinitionValue(
            parser,
            pDefinition,
            seed.pValue,
            0u );
        parser.pDocument = pPreviousDocument;
        if ( !bCopied ) return CY_FALSE;
        ++parser.nDefinitions;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseValue(
    parser_t &parser,
    key_value_t *pValue,
    usize nDepth ) noexcept;

// ---------------------------------------------------------------------------
// Kept comments (KEY_VALUE_PARSE_FLAG_KEEP_COMMENTS)
// ---------------------------------------------------------------------------

CYPHER_NODISCARD bool_t KeepsComments( const parser_t &parser ) noexcept
{
    return !parser.bStrictJson && HasFlag( parser, KEY_VALUE_PARSE_FLAG_KEEP_COMMENTS ) &&
           HasFlag( parser, KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS );
}

// The comments in the source between two tokens. Those before the first
// line break end the line of what came before (trailing); whole lines after
// it belong to what follows (leading), joined with '\n'.
struct gap_comments_t {
    string_view_t trailing{};
    char *pLeading{ nullptr };
    usize cchLeading{ 0u };
    usize cbCapacity{ 0u };
    const allocator_t *pAllocator{ nullptr };

    ~gap_comments_t() noexcept
    {
        if ( pLeading != nullptr ) { Allocator_Free( pAllocator, pLeading, cbCapacity, alignof( char ) ); }
    }

    CYPHER_NODISCARD string_view_t Leading() const noexcept { return { pLeading != nullptr ? pLeading : "", cchLeading }; }

    CYPHER_NODISCARD bool_t AppendLeading( string_view_t text ) noexcept
    {
        const usize cchNeeded = cchLeading + ( cchLeading != 0u ? 1u : 0u ) + text.cchLength;
        if ( cchNeeded > cbCapacity ) {
            usize cbGrown = cbCapacity == 0u ? 128u : cbCapacity * 2u;
            while ( cbGrown < cchNeeded ) { cbGrown *= 2u; }
            char *pGrown = static_cast<char *>( Allocator_Allocate( pAllocator, cbGrown, alignof( char ) ) );
            if ( pGrown == nullptr ) { return CY_FALSE; }
            if ( cchLeading != 0u ) { Cy_MemCopy( pGrown, pLeading, cchLeading ); }
            if ( pLeading != nullptr ) { Allocator_Free( pAllocator, pLeading, cbCapacity, alignof( char ) ); }
            pLeading = pGrown;
            cbCapacity = cbGrown;
        }
        if ( cchLeading != 0u ) { pLeading[cchLeading++] = '\n'; }
        Cy_MemCopy( pLeading + cchLeading, text.pData, text.cchLength );
        cchLeading += text.cchLength;
        return CY_TRUE;
    }
};

CYPHER_NODISCARD bool_t CollectGapComments(
    parser_t &parser,
    usize iBegin,
    usize iEnd,
    gap_comments_t &out ) noexcept
{
    out.pAllocator = parser.pOutputDocument->pAllocator;
    const char *pText = parser.lexer.source.pData;
    // After an opening bracket nothing came before on this line to own a
    // comment, so every comment leads what follows.
    bool_t bNewline = iBegin != 0u && ( pText[iBegin - 1u] == '{' || pText[iBegin - 1u] == '[' || pText[iBegin - 1u] == '\n' );
    for ( usize i = iBegin; i < iEnd; ) {
        if ( pText[i] == '\n' ) {
            bNewline = CY_TRUE;
            ++i;
            continue;
        }
        if ( pText[i] != '/' || i + 1u >= iEnd || ( pText[i + 1u] != '/' && pText[i + 1u] != '*' ) ) {
            ++i;
            continue;
        }
        usize j = i + 2u;
        if ( pText[i + 1u] == '/' ) {
            while ( j < iEnd && pText[j] != '\n' ) { ++j; }
        } else {
            usize nDepth = 1u;
            while ( j < iEnd && nDepth != 0u ) {
                if ( pText[j] == '/' && j + 1u < iEnd && pText[j + 1u] == '*' ) { ++nDepth; j += 2u; continue; }
                if ( pText[j] == '*' && j + 1u < iEnd && pText[j + 1u] == '/' ) { --nDepth; j += 2u; continue; }
                ++j;
            }
        }
        usize jEnd = j;
        while ( jEnd > i && ( pText[jEnd - 1u] == ' ' || pText[jEnd - 1u] == '\t' || pText[jEnd - 1u] == '\r' ) ) { --jEnd; }
        const string_view_t comment{ pText + i, jEnd - i };
        if ( !bNewline ) {
            // Two comments on one line stay one trailing comment.
            out.trailing = out.trailing.cchLength == 0u ? comment : string_view_t{ out.trailing.pData, static_cast<usize>( comment.pData + comment.cchLength - out.trailing.pData ) };
        } else if ( !out.AppendLeading( comment ) ) {
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
        i = j;
    }
    return CY_TRUE;
}

// Adds to a value's kept comments; empty parts leave what is there.
CYPHER_NODISCARD bool_t AddComments(
    parser_t &parser,
    key_value_t *pValue,
    string_view_t leading,
    string_view_t trailing,
    string_view_t inner ) noexcept
{
    if ( pValue == nullptr || ( leading.cchLength == 0u && trailing.cchLength == 0u && inner.cchLength == 0u ) ) {
        return CY_TRUE;
    }
    key_value_comments_t comments = KeyValue_Comments( pValue );
    if ( leading.cchLength != 0u ) { comments.leading = leading; }
    if ( trailing.cchLength != 0u ) { comments.trailing = trailing; }
    if ( inner.cchLength != 0u ) { comments.inner = inner; }
    if ( !KeyValue_SetComments( pValue->pDocument, pValue, comments ) ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    return CheckLimits( parser );
}

// Comments before the next item of a container: the end of the previous
// item's line goes to that item; the lines after lead the next one, which
// the caller attaches once it exists.
CYPHER_NODISCARD bool_t TakeCommentsBeforeItem(
    parser_t &parser,
    key_value_t *pContainer,
    gap_comments_t &gap ) noexcept
{
    if ( !KeepsComments( parser ) || parser.bAtEnd ) { return CY_TRUE; }
    if ( !CollectGapComments( parser, parser.iPreviousTokenEnd, parser.token.range.begin.iByte, gap ) ) { return CY_FALSE; }
    return AddComments( parser, pContainer->pLastChild, {}, gap.trailing, {} );
}

// Comments before a closing bracket: the last item's line end, then lines
// kept inside the container.
CYPHER_NODISCARD bool_t TakeCommentsBeforeClose(
    parser_t &parser,
    key_value_t *pContainer ) noexcept
{
    if ( !KeepsComments( parser ) || parser.bAtEnd ) { return CY_TRUE; }
    gap_comments_t gap{};
    if ( !CollectGapComments( parser, parser.iPreviousTokenEnd, parser.token.range.begin.iByte, gap ) ) { return CY_FALSE; }
    return AddComments( parser, pContainer->pLastChild, {}, gap.trailing, {} ) &&
           AddComments( parser, pContainer, {}, {}, gap.Leading() );
}

CYPHER_NODISCARD bool_t ParseObjectMembers(
    parser_t &parser,
    key_value_t *pObject,
    usize nDepth ) noexcept;

// ---------------------------------------------------------------------------
// CYKV 2 operators and inheritance (#extends, #base, #delete, +=)
// ---------------------------------------------------------------------------

CYPHER_NODISCARD bool_t PushOperator(
    parser_t &parser,
    key_value_t *pNode,
    text_location_t location ) noexcept
{
    if ( parser.nOps == parser.nOpsCapacity ) {
        const allocator_t *pAllocator = parser.pOutputDocument->pAllocator;
        const usize nCapacity = parser.nOpsCapacity == 0u ? 16u : parser.nOpsCapacity * 2u;
        parse_op_t *pGrown = Allocator_AllocateArrayStorage<parse_op_t>( pAllocator, nCapacity );
        if ( pGrown == nullptr ) {
            Fail( parser, key_value_parse_status_t::OUT_OF_MEMORY, location );
            return CY_FALSE;
        }
        for ( usize i = 0u; i < parser.nOps; ++i ) { pGrown[i] = parser.pOps[i]; }
        if ( parser.pOps != nullptr ) {
            Allocator_FreeArrayStorage( pAllocator, parser.pOps, parser.nOpsCapacity );
        }
        parser.pOps = pGrown;
        parser.nOpsCapacity = nCapacity;
    }
    parser.pOps[parser.nOps++] = { pNode, location };
    return CY_TRUE;
}

// Copies source's elements in front of the array's own: inherited first,
// then the ones written with +=.
CYPHER_NODISCARD bool_t PrependCopies(
    parser_t &parser,
    key_value_t *pArray,
    const key_value_t *pSource,
    usize nDepth ) noexcept
{
    key_value_t *pOldFirst = pArray->pFirstChild;
    key_value_t *pOldLast = pArray->pLastChild;
    for ( const key_value_t *pFrom = pSource->pFirstChild; pFrom != nullptr; pFrom = pFrom->pNext ) {
        if ( pArray->nChildren >= parser.options.nMaxContainerValues ) {
            FailCurrent( parser, key_value_parse_status_t::CONTAINER_LIMIT );
            return CY_FALSE;
        }
        if ( !CanAllocateNode( parser ) ) { return CY_FALSE; }
        key_value_t *pCopy = KeyValue_ArrayAppend( parser.pDocument, pArray, key_value_type_t::NULL_VALUE );
        if ( pCopy == nullptr ) {
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
        if ( !CopyDefinitionValue( parser, pCopy, pFrom, nDepth ) ) { return CY_FALSE; }
    }
    if ( pOldFirst != nullptr && pOldLast->pNext != nullptr ) {
        key_value_t *pNewFirst = pOldLast->pNext;
        key_value_t *pNewLast = pArray->pLastChild;
        pOldLast->pNext = nullptr;
        pArray->pLastChild = pOldLast;
        pNewFirst->pPrevious = nullptr;
        pNewLast->pNext = pOldFirst;
        pOldFirst->pPrevious = pNewLast;
        pArray->pFirstChild = pNewFirst;
    }
    return CY_TRUE;
}

// Fills members the local object lacks from an inherited object: local
// values win, objects merge member by member, arrays and scalars are whole
// values, and two different kinds at one path are an error. A member marked
// #delete keeps the inherited one out; one marked += receives the inherited
// elements in front of its own. Shared by #extends and #base.
CYPHER_NODISCARD bool_t FillMissing(
    parser_t &parser,
    key_value_t *pLocal,
    const key_value_t *pInherited,
    usize nDepth ) noexcept
{
    for ( const key_value_t *pFrom = pInherited->pFirstChild; pFrom != nullptr; pFrom = pFrom->pNext ) {
        const string_view_t name{ pFrom->pName, pFrom->cchName };
        key_value_t *pLocalChild = KeyValue_Find( pLocal, name );
        if ( pLocalChild == nullptr ) {
            if ( pLocal->nChildren >= parser.options.nMaxContainerValues ) {
                FailCurrent( parser, key_value_parse_status_t::CONTAINER_LIMIT );
                return CY_FALSE;
            }
            if ( !CanAllocateNode( parser ) ) { return CY_FALSE; }
            key_value_t *pCopy = KeyValue_ObjectInsert( parser.pDocument, pLocal, name, key_value_type_t::NULL_VALUE );
            if ( pCopy == nullptr ) {
                FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
                return CY_FALSE;
            }
            if ( !CopyDefinitionValue( parser, pCopy, pFrom, nDepth + 1u ) ) { return CY_FALSE; }
            continue;
        }
        if ( ( pLocalChild->parseMark & PARSE_MARK_DELETE ) != 0u ) {
            pLocalChild->parseMark |= PARSE_MARK_APPLIED;
            continue;
        }
        if ( ( pLocalChild->parseMark & PARSE_MARK_APPEND ) != 0u ) {
            if ( ( pLocalChild->parseMark & PARSE_MARK_APPLIED ) != 0u ) { continue; } // An earlier source supplied it.
            if ( pFrom->type != key_value_type_t::ARRAY ) {
                FailCurrent( parser, key_value_parse_status_t::APPEND_TYPE_MISMATCH );
                return CY_FALSE;
            }
            if ( !PrependCopies( parser, pLocalChild, pFrom, nDepth + 2u ) ) { return CY_FALSE; }
            pLocalChild->parseMark |= PARSE_MARK_APPLIED;
            continue;
        }
        if ( pLocalChild->type != pFrom->type ) {
            FailCurrent( parser, key_value_parse_status_t::MERGE_CONFLICT );
            return CY_FALSE;
        }
        if ( pFrom->type == key_value_type_t::OBJECT &&
             !FillMissing( parser, pLocalChild, pFrom, nDepth + 1u ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

// Merges a part (a merging #include) into the local object: the part's
// members are added, objects present on both sides merge member by member,
// and any other member both define is an error - parts of one document never
// silently override each other.
CYPHER_NODISCARD bool_t MergePart(
    parser_t &parser,
    key_value_t *pLocal,
    const key_value_t *pPart,
    usize nDepth ) noexcept
{
    for ( const key_value_t *pFrom = pPart->pFirstChild; pFrom != nullptr; pFrom = pFrom->pNext ) {
        const string_view_t name{ pFrom->pName, pFrom->cchName };
        key_value_t *pLocalChild = KeyValue_Find( pLocal, name );
        if ( pLocalChild == nullptr ) {
            if ( pLocal->nChildren >= parser.options.nMaxContainerValues ) {
                FailCurrent( parser, key_value_parse_status_t::CONTAINER_LIMIT );
                return CY_FALSE;
            }
            if ( !CanAllocateNode( parser ) ) { return CY_FALSE; }
            key_value_t *pCopy = KeyValue_ObjectInsert( parser.pDocument, pLocal, name, key_value_type_t::NULL_VALUE );
            if ( pCopy == nullptr ) {
                FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
                return CY_FALSE;
            }
            if ( !CopyDefinitionValue( parser, pCopy, pFrom, nDepth + 1u ) ) { return CY_FALSE; }
            continue;
        }
        if ( pLocalChild->type != key_value_type_t::OBJECT || pFrom->type != key_value_type_t::OBJECT ||
             pLocalChild->parseMark != 0u ) {
            FailCurrent( parser, key_value_parse_status_t::INCLUDE_CONFLICT );
            return CY_FALSE;
        }
        if ( !MergePart( parser, pLocalChild, pFrom, nDepth + 1u ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

// Every operator written since iFirstOp must have found its inherited value.
// Leaving the outermost inheritance, #delete members are removed - not
// earlier, or a later source would supply the value again - and marks are
// cleared so no published node carries one.
CYPHER_NODISCARD bool_t SettleOperators(
    parser_t &parser,
    usize iFirstOp ) noexcept
{
    for ( usize i = iFirstOp; i < parser.nOps; ++i ) {
        const parse_op_t &op = parser.pOps[i];
        if ( ( op.pNode->parseMark & PARSE_MARK_APPLIED ) == 0u ) {
            Fail( parser, key_value_parse_status_t::INHERITED_VALUE_MISSING, op.location );
            return CY_FALSE;
        }
    }
    if ( parser.nInheritanceDepth != 0u ) { return CY_TRUE; }
    for ( usize i = iFirstOp; i < parser.nOps; ++i ) {
        key_value_t *pNode = parser.pOps[i].pNode;
        if ( ( pNode->parseMark & PARSE_MARK_DELETE ) != 0u ) {
            if ( !KeyValue_Remove( pNode->pDocument, pNode->pParent, pNode ) ) {
                FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
                return CY_FALSE;
            }
            continue;
        }
        pNode->parseMark = 0u;
    }
    parser.nOps = iFirstOp;
    return CY_TRUE;
}

// `#extends $template` at the top of an object: the template must be an
// object definition or an imported object.
CYPHER_NODISCARD bool_t ParseExtends(
    parser_t &parser,
    const key_value_t **ppExtends,
    usize &nExtends ) noexcept
{
    if ( !Advance( parser ) || HasTriviaBeforeCurrent( parser ) || !TokenEqualsIdentifier( parser, "extends" ) ||
         !Advance( parser ) || !HasHorizontalSpaceBeforeCurrent( parser ) || !IsPunctuation( parser, '$' ) ||
         !Advance( parser ) || HasTriviaBeforeCurrent( parser ) || parser.token.kind != token_kind_t::IDENTIFIER ||
         !IsDefinitionPath( parser.token.lexeme ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        }
        return CY_FALSE;
    }
    const key_value_t *pTemplate = LookupDefinition( parser, parser.token.lexeme );
    if ( pTemplate == nullptr ) {
        FailCurrent( parser, key_value_parse_status_t::UNDEFINED_DEFINITION );
        return CY_FALSE;
    }
    if ( pTemplate->type != key_value_type_t::OBJECT ) {
        FailCurrent( parser, key_value_parse_status_t::MERGE_CONFLICT );
        return CY_FALSE;
    }
    if ( nExtends == kMaxExtends ) {
        FailCurrent( parser, key_value_parse_status_t::DEFINITION_LIMIT );
        return CY_FALSE;
    }
    ppExtends[nExtends++] = pTemplate;
    return Advance( parser );
}

// ---------------------------------------------------------------------------
// CYKV 2 @when build conditionals
// ---------------------------------------------------------------------------

CYPHER_NODISCARD bool_t IsConditionKeyword( const parser_t &parser ) noexcept
{
    return TokenEqualsIdentifier( parser, "and" ) || TokenEqualsIdentifier( parser, "or" ) ||
           TokenEqualsIdentifier( parser, "not" ) || TokenEqualsIdentifier( parser, "true" ) ||
           TokenEqualsIdentifier( parser, "false" );
}

CYPHER_NODISCARD bool_t ParseConditionOr( parser_t &parser, bool_t &bValue ) noexcept;

CYPHER_NODISCARD bool_t ParseConditionPrimary( parser_t &parser, bool_t &bValue ) noexcept
{
    if ( IsPunctuation( parser, '(' ) ) {
        if ( !Advance( parser ) || !ParseConditionOr( parser, bValue ) ) { return CY_FALSE; }
        if ( !IsPunctuation( parser, ')' ) ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        return Advance( parser );
    }
    if ( parser.bAtEnd || parser.token.kind != token_kind_t::IDENTIFIER || IsConditionKeyword( parser ) ) {
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    // Every property is looked up, in branches already decided too, so a
    // misspelt name is found on the first parse rather than on the target it
    // was meant for.
    const key_value_build_property_t *pProperty = nullptr;
    for ( usize i = 0u; i < parser.pBuildContext->nProperties && pProperty == nullptr; ++i ) {
        if ( StringView_Equals( parser.pBuildContext->pProperties[i].name, parser.token.lexeme ) ) {
            pProperty = &parser.pBuildContext->pProperties[i];
        }
    }
    if ( pProperty == nullptr ) {
        FailCurrent( parser, key_value_parse_status_t::UNKNOWN_CONDITION_PROPERTY );
        return CY_FALSE;
    }
    if ( !Advance( parser ) ) { return CY_FALSE; }
    const bool_t bEqual = IsPunctuation( parser, '=' );
    const bool_t bNotEqual = IsPunctuation( parser, '!' );
    if ( !bEqual && !bNotEqual ) {
        if ( pProperty->kind != key_value_build_value_kind_t::BOOL ) {
            FailCurrent( parser, key_value_parse_status_t::CONDITION_TYPE_MISMATCH );
            return CY_FALSE;
        }
        bValue = pProperty->bValue;
        return CY_TRUE;
    }
    if ( !Advance( parser ) || HasTriviaBeforeCurrent( parser ) || !IsPunctuation( parser, '=' ) || !Advance( parser ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        }
        return CY_FALSE;
    }
    bool_t bMatch = CY_FALSE;
    if ( parser.token.kind == token_kind_t::STRING ) {
        if ( pProperty->kind != key_value_build_value_kind_t::STRING ) {
            FailCurrent( parser, key_value_parse_status_t::CONDITION_TYPE_MISMATCH );
            return CY_FALSE;
        }
        decoded_text_t decoded{};
        if ( !DecodeTokenText( parser, parser.token, decoded ) ) {
            if ( parser.result.status == key_value_parse_status_t::OK ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            }
            return CY_FALSE;
        }
        bMatch = StringView_Equals( decoded.text, pProperty->text );
        ReleaseDecoded( parser, decoded );
    } else if ( parser.token.kind == token_kind_t::INTEGER ) {
        i64 value = 0;
        string_parse_options_t options{};
        options.flags = STRING_PARSE_FLAG_ALLOW_PLUS_SIGN;
        if ( pProperty->kind != key_value_build_value_kind_t::INTEGER ) {
            FailCurrent( parser, key_value_parse_status_t::CONDITION_TYPE_MISMATCH );
            return CY_FALSE;
        }
        if ( !StringParse_Succeeded( StringParse_I64( parser.token.lexeme, options, &value ) ) ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        bMatch = value == pProperty->iValue;
    } else if ( TokenEqualsIdentifier( parser, "true" ) || TokenEqualsIdentifier( parser, "false" ) ) {
        if ( pProperty->kind != key_value_build_value_kind_t::BOOL ) {
            FailCurrent( parser, key_value_parse_status_t::CONDITION_TYPE_MISMATCH );
            return CY_FALSE;
        }
        bMatch = TokenEqualsIdentifier( parser, "true" ) == ( pProperty->bValue != CY_FALSE );
    } else {
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    bValue = bEqual ? bMatch : !bMatch;
    return Advance( parser );
}

CYPHER_NODISCARD bool_t ParseConditionNot( parser_t &parser, bool_t &bValue ) noexcept
{
    if ( !TokenEqualsIdentifier( parser, "not" ) ) { return ParseConditionPrimary( parser, bValue ); }
    if ( !Advance( parser ) || !ParseConditionNot( parser, bValue ) ) { return CY_FALSE; }
    bValue = !bValue;
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t ParseConditionAnd( parser_t &parser, bool_t &bValue ) noexcept
{
    if ( !ParseConditionNot( parser, bValue ) ) { return CY_FALSE; }
    while ( TokenEqualsIdentifier( parser, "and" ) ) {
        bool_t bRight = CY_FALSE;
        if ( !Advance( parser ) || !ParseConditionNot( parser, bRight ) ) { return CY_FALSE; }
        bValue = bValue && bRight;
    }
    return CY_TRUE;
}

bool_t ParseConditionOr( parser_t &parser, bool_t &bValue ) noexcept
{
    if ( !ParseConditionAnd( parser, bValue ) ) { return CY_FALSE; }
    while ( TokenEqualsIdentifier( parser, "or" ) ) {
        bool_t bRight = CY_FALSE;
        if ( !Advance( parser ) || !ParseConditionAnd( parser, bRight ) ) { return CY_FALSE; }
        bValue = bValue || bRight;
    }
    return CY_TRUE;
}

// A branch not taken is still parsed in full - so an error in it is found
// on every target - into scratch storage that is dropped afterwards.
CYPHER_NODISCARD bool_t ParseSkippedMembers(
    parser_t &parser,
    usize nDepth ) noexcept
{
    if ( parser.pScratch == nullptr ) {
        const key_value_document_t *pOutput = parser.pOutputDocument;
        parser.pScratch = KeyValue_CreateDocument( { pOutput->pAllocator, pOutput->nInitialNodes, pOutput->cbInitialData, CY_FALSE } );
        if ( parser.pScratch == nullptr || !KeyValue_SetRootType( parser.pScratch, key_value_type_t::ARRAY ) ) {
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
    }
    if ( !CanAllocateNode( parser ) ) { return CY_FALSE; }
    key_value_t *pScratchRoot = KeyValue_Root( parser.pScratch );
    key_value_t *pBlock = KeyValue_ArrayAppend( parser.pScratch, pScratchRoot, key_value_type_t::OBJECT );
    if ( pBlock == nullptr ) {
        FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
        return CY_FALSE;
    }
    key_value_document_t *pPrevious = parser.pDocument;
    const usize iFirstOp = parser.nOps;
    parser.pDocument = parser.pScratch;
    const bool_t bParsed = ParseObjectMembers( parser, pBlock, nDepth );
    parser.pDocument = pPrevious;
    parser.nOps = iFirstOp; // Operators in a dropped branch apply to nothing.
    const bool_t bRemoved = KeyValue_Remove( parser.pScratch, pScratchRoot, pBlock );
    return bParsed && bRemoved;
}

// `@when <condition> { ... } @else_when <condition> { ... } @else { ... }`
// in an object: the members of the first branch whose condition holds become
// members of the object. Chains may follow one another.
CYPHER_NODISCARD bool_t ParseConditional(
    parser_t &parser,
    key_value_t *pObject,
    usize nDepth ) noexcept
{
    if ( !Advance( parser ) || HasTriviaBeforeCurrent( parser ) || !TokenEqualsIdentifier( parser, "when" ) ) {
        if ( parser.result.status == key_value_parse_status_t::OK ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        }
        return CY_FALSE;
    }
    bool_t bTaken = CY_FALSE;
    bool_t bFinal = CY_FALSE;
    for ( ;; ) {
        bool_t bCondition = CY_TRUE;
        if ( !Advance( parser ) ) { return CY_FALSE; }
        if ( !bFinal && !ParseConditionOr( parser, bCondition ) ) { return CY_FALSE; }
        if ( !IsPunctuation( parser, '{' ) ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        if ( !Advance( parser ) ) { return CY_FALSE; }
        const bool_t bTake = !bTaken && bCondition;
        if ( !( bTake ? ParseObjectMembers( parser, pObject, nDepth ) : ParseSkippedMembers( parser, nDepth ) ) ) {
            return CY_FALSE;
        }
        bTaken = bTaken || bTake;
        if ( bFinal || !IsPunctuation( parser, '@' ) ) { return CY_TRUE; }
        if ( !Advance( parser ) || HasTriviaBeforeCurrent( parser ) ) {
            if ( parser.result.status == key_value_parse_status_t::OK ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            }
            return CY_FALSE;
        }
        if ( TokenEqualsIdentifier( parser, "when" ) ) {
            bTaken = CY_FALSE; // A new chain starts.
        } else if ( TokenEqualsIdentifier( parser, "else" ) ) {
            bFinal = CY_TRUE;
        } else if ( !TokenEqualsIdentifier( parser, "else_when" ) ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
    }
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

// One `name = value` (or, in CYKV 2, `name += [...]` / `name = #delete`).
CYPHER_NODISCARD bool_t ParseObjectMember(
    parser_t &parser,
    key_value_t *pObject,
    usize nDepth ) noexcept
{
    const bool_t bCykv2 = !parser.bStrictJson && parser.nLanguageVersion == CYKV_LANGUAGE_VERSION_2;
    gap_comments_t gap{};
    if ( !TakeCommentsBeforeItem( parser, pObject, gap ) ) {
        return CY_FALSE;
    }
    decoded_text_t decodedName{};
    string_view_t name{};
    const text_location_t memberLocation = parser.token.range.begin;
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

    // Duplicate rejection happens before insertion so the object remains unchanged.
    if ( ( !parser.bStrictJson ||
           HasFlag(
               parser,
               KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS ) ) &&
         KeyValue_Find( pObject, name ) != nullptr ) {
        ReleaseDecoded( parser, decodedName );
        FailCurrent( parser, key_value_parse_status_t::DUPLICATE_KEY );
        return CY_FALSE;
    }
    if ( !Advance( parser ) ) {
        ReleaseDecoded( parser, decodedName );
        return CY_FALSE;
    }
    u8 mark = 0u;
    if ( bCykv2 && IsPunctuation( parser, '+' ) ) {
        if ( !Advance( parser ) || HasTriviaBeforeCurrent( parser ) || !IsPunctuation( parser, '=' ) ) {
            ReleaseDecoded( parser, decodedName );
            if ( parser.result.status == key_value_parse_status_t::OK )
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        mark = PARSE_MARK_APPEND;
    } else if ( !IsPunctuation( parser, parser.bStrictJson ? ':' : '=' ) ) {
        ReleaseDecoded( parser, decodedName );
        FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        return CY_FALSE;
    }
    if ( !Advance( parser ) ) {
        ReleaseDecoded( parser, decodedName );
        return CY_FALSE;
    }
    if ( mark == 0u && bCykv2 && IsPunctuation( parser, '#' ) ) {
        mark = PARSE_MARK_DELETE;
    }
    if ( mark != 0u && parser.nInheritanceDepth == 0u ) {
        ReleaseDecoded( parser, decodedName );
        Fail( parser, key_value_parse_status_t::OPERATOR_WITHOUT_BASE, memberLocation );
        return CY_FALSE;
    }
    if ( !CanAllocateNode( parser ) ) {
        ReleaseDecoded( parser, decodedName );
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
    if ( !CheckLimits( parser ) || !AddComments( parser, pChild, gap.Leading(), {}, {} ) ) {
        return CY_FALSE;
    }
    if ( mark == PARSE_MARK_DELETE ) {
        if ( !Advance( parser ) || HasTriviaBeforeCurrent( parser ) || !TokenEqualsIdentifier( parser, "delete" ) ) {
            if ( parser.result.status == key_value_parse_status_t::OK )
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        if ( !Advance( parser ) ) { return CY_FALSE; }
    } else if ( !ParseValue( parser, pChild, nDepth + 1u ) ) {
        return CY_FALSE;
    }
    if ( mark == PARSE_MARK_APPEND && pChild->type != key_value_type_t::ARRAY ) {
        Fail( parser, key_value_parse_status_t::APPEND_TYPE_MISMATCH, memberLocation );
        return CY_FALSE;
    }
    if ( mark != 0u ) {
        pChild->parseMark = mark;
        return PushOperator( parser, pChild, memberLocation );
    }
    return CY_TRUE;
}

// Members up to and including the closing '}'. CYKV 2 adds @when chains
// between members.
bool_t ParseObjectMembers(
    parser_t &parser,
    key_value_t *pObject,
    usize nDepth ) noexcept
{
    const bool_t bCykv2 = !parser.bStrictJson && parser.nLanguageVersion == CYKV_LANGUAGE_VERSION_2;
    if ( IsPunctuation( parser, '}' ) ) {
        return TakeCommentsBeforeClose( parser, pObject ) && Advance( parser );
    }

    // Each pass consumes one member or conditional and leaves token at '}' or the next one.
    while ( !parser.bAtEnd ) {
        const bool_t bParsed = bCykv2 && IsPunctuation( parser, '@' )
            ? ParseConditional( parser, pObject, nDepth )
            : ParseObjectMember( parser, pObject, nDepth );
        if ( !bParsed ) {
            return CY_FALSE;
        }

        if ( IsPunctuation( parser, '}' ) ) {
            return TakeCommentsBeforeClose( parser, pObject ) && Advance( parser );
        }
        // CYKV separates members with trivia; strict JSON requires an explicit comma.
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
    // CYKV 2 `#extends` lines open the object; members follow.
    const key_value_t *extends[kMaxExtends]{};
    usize nExtends = 0u;
    while ( !parser.bStrictJson && parser.nLanguageVersion == CYKV_LANGUAGE_VERSION_2 && IsPunctuation( parser, '#' ) ) {
        if ( !ParseExtends( parser, extends, nExtends ) ) {
            return CY_FALSE;
        }
    }
    if ( nExtends == 0u ) {
        return ParseObjectMembers( parser, pObject, nDepth );
    }
    const usize iFirstOp = parser.nOps;
    ++parser.nInheritanceDepth;
    bool_t bOk = ParseObjectMembers( parser, pObject, nDepth );
    for ( usize i = 0u; bOk && i < nExtends; ++i ) {
        bOk = FillMissing( parser, pObject, extends[i], nDepth );
    }
    --parser.nInheritanceDepth;
    return bOk && SettleOperators( parser, iFirstOp );
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
    if ( IsPunctuation( parser, ']' ) ) return TakeCommentsBeforeClose( parser, pArray ) && Advance( parser );

    // Arrays always use commas, including CYKV; the option only permits a final comma.
    while ( !parser.bAtEnd ) {
        gap_comments_t gap{};
        if ( !TakeCommentsBeforeItem( parser, pArray, gap ) ) {
            return CY_FALSE;
        }
        if ( !CanAllocateNode( parser ) ) {
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
        if ( !AddComments( parser, pChild, gap.Leading(), {}, {} ) ) return CY_FALSE;
        if ( !ParseValue( parser, pChild, nDepth + 1u ) ) return CY_FALSE;
        if ( IsPunctuation( parser, ']' ) ) return TakeCommentsBeforeClose( parser, pArray ) && Advance( parser );
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
            return TakeCommentsBeforeClose( parser, pArray ) && Advance( parser );
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
    // Binary literals are decoded into temporary bytes, then copied into document storage.
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

    // CYKV uses a trailing 'u'; strip it before validating the numeric body.
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

    // Integer and floating spellings have different leading-zero and suffix rules.
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

    // Token flags select the narrowest lossless scalar representation in the document.
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
    // Recursive descent dispatches on punctuation first, then scalar token spelling.
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
    if ( !parser.bStrictJson &&
         parser.nLanguageVersion == CYKV_LANGUAGE_VERSION_2 &&
         IsPunctuation( parser, '$' ) ) {
        return ParseDefinitionReference( parser, pValue, nDepth );
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

CYPHER_NODISCARD bool_t ParseDefinitionPreamble(
    parser_t &parser ) noexcept
{
    while ( IsPunctuation( parser, '#' ) ) {
        if ( !Advance( parser ) ||
             HasTriviaBeforeCurrent( parser ) ||
             !TokenEqualsIdentifier( parser, "define" ) ||
             !Advance( parser ) ||
             !HasHorizontalSpaceBeforeCurrent( parser ) ||
             parser.token.kind != token_kind_t::IDENTIFIER ||
             !IsDefinitionName( parser.token.lexeme ) ) {
            if ( parser.result.status == key_value_parse_status_t::OK ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            }
            return CY_FALSE;
        }

        const string_view_t name = parser.token.lexeme;
        if ( parser.pDefinitions != nullptr &&
             KeyValue_Find(
                 KeyValue_Root( parser.pDefinitions ),
                 name ) != nullptr ) {
            FailCurrent(
                parser,
                key_value_parse_status_t::DUPLICATE_DEFINITION );
            return CY_FALSE;
        }
        if ( parser.nDefinitions >= parser.options.nMaxDefinitions ) {
            FailCurrent(
                parser,
                key_value_parse_status_t::DEFINITION_LIMIT );
            return CY_FALSE;
        }
        if ( !EnsureDefinitionStorage( parser ) || !Advance( parser ) ||
             !HasHorizontalSpaceBeforeCurrent( parser ) ) {
            if ( parser.result.status == key_value_parse_status_t::OK ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            }
            return CY_FALSE;
        }
        if ( !CanAllocateNode( parser ) ) {
            return CY_FALSE;
        }

        key_value_t *pDefinition = KeyValue_ObjectInsert(
            parser.pDefinitions,
            KeyValue_Root( parser.pDefinitions ),
            name,
            key_value_type_t::NULL_VALUE );
        if ( pDefinition == nullptr ) {
            FailCurrent( parser, key_value_parse_status_t::OUT_OF_MEMORY );
            return CY_FALSE;
        }
        if ( !CheckLimits( parser ) ) {
            return CY_FALSE;
        }

        key_value_document_t *pPreviousDocument = parser.pDocument;
        parser.pDocument = parser.pDefinitions;
        parser.pDefinitionBeingParsed = pDefinition;
        const bool_t bParsed = ParseValue( parser, pDefinition, 0u );
        parser.pDefinitionBeingParsed = nullptr;
        parser.pDocument = pPreviousDocument;
        if ( !bParsed ) {
            return CY_FALSE;
        }
        if ( !parser.bAtEnd && !HasTriviaBeforeCurrent( parser ) ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            return CY_FALSE;
        }
        ++parser.nDefinitions;
    }
    return CY_TRUE;
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

key_value_parse_result_t KeyValue_InternalParseTextWithDefinitions(
    string_view_t text,
    const key_value_parse_options_t &options,
    const key_value_definition_seed_t *pSeeds,
    usize nSeeds,
    key_value_composition_t *pComposition,
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
         options.cbMaxStringData == 0u ||
         ( options.pBuildContext != nullptr && options.pBuildContext->nProperties != 0u &&
           options.pBuildContext->pProperties == nullptr ) ||
         ( nSeeds != 0u && pSeeds == nullptr ) ||
         ( pComposition != nullptr && ( ( pComposition->nParts != 0u && pComposition->ppParts == nullptr ) ||
                                        ( pComposition->nBases != 0u && pComposition->ppBases == nullptr ) ) ) ) {
        invalid.status = key_value_parse_status_t::INVALID_ARGUMENT;
        if ( StringView_IsValid( text ) &&
             options.cbMaxInput != 0u &&
             text.cchLength > options.cbMaxInput ) {
            invalid.status = key_value_parse_status_t::INPUT_LIMIT;
        }
        return invalid;
    }

    // Build transactionally: only a fully valid temporary tree replaces the destination.
    // CYKV and JSON object keys are exact-case language identities. Preserve a
    // destination's optional folded lookup behavior only after publication.
    key_value_document_t *pTemporary = KeyValue_InternalCreateLike(
        pDocument,
        CY_FALSE );
    if ( pTemporary == nullptr ) {
        invalid.status = key_value_parse_status_t::OUT_OF_MEMORY;
        return invalid;
    }
    parser_t parser{};
    parser.options = options;
    parser.pDocument = pTemporary;
    parser.pOutputDocument = pTemporary;
    parser.bStrictJson = bStrictJson;
    parser.pBuildContext = options.pBuildContext != nullptr ? options.pBuildContext : KeyValue_HostBuildContext();
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

    bool_t bPreambleReady = CY_TRUE;
    if ( !bStrictJson &&
         parser.nLanguageVersion == CYKV_LANGUAGE_VERSION_2 ) {
        bPreambleReady = SeedDefinitions( parser, pSeeds, nSeeds ) &&
                         ParseDefinitionPreamble( parser );
    } else if ( nSeeds != 0u ) {
        FailCurrent( parser, key_value_parse_status_t::INVALID_ARGUMENT );
        bPreambleReady = CY_FALSE;
    }
    if ( bPreambleReady ) {
        const bool_t bRequiresObjectRoot =
            !bStrictJson ||
            !HasFlag( parser, KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE );
        const bool_t bCykv2RootReference =
            !bStrictJson &&
            parser.nLanguageVersion == CYKV_LANGUAGE_VERSION_2 &&
            IsPunctuation( parser, '$' );
        if ( bRequiresObjectRoot &&
             !IsPunctuation( parser, '{' ) &&
             !bCykv2RootReference ) {
            FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
        } else {
            // With #base documents, #delete and += are allowed anywhere in
            // the root; they apply when the bases fill in below.
            const bool_t bHasBases = pComposition != nullptr && pComposition->nBases != 0u;
            parser.nInheritanceDepth = bHasBases ? 1u : 0u;
            // Comments between the header and the root lead the document.
            gap_comments_t rootGap{};
            if ( KeepsComments( parser ) &&
                 CollectGapComments( parser, parser.iPreviousTokenEnd, parser.token.range.begin.iByte, rootGap ) ) {
                static_cast<void>( AddComments( parser, KeyValue_Root( pTemporary ), rootGap.Leading(), {}, {} ) );
            }
            static_cast<void>( ParseValue(
                parser,
                KeyValue_Root( pTemporary ),
                0u ) );
            // Comments after the root's closing bracket end the document.
            gap_comments_t endGap{};
            if ( parser.result.status == key_value_parse_status_t::OK && parser.bAtEnd && KeepsComments( parser ) &&
                 CollectGapComments( parser, parser.iPreviousTokenEnd, text.cchLength, endGap ) ) {
                gap_comments_t lines{};
                lines.pAllocator = pTemporary->pAllocator;
                if ( ( endGap.trailing.cchLength == 0u || lines.AppendLeading( endGap.trailing ) ) &&
                     ( endGap.cchLeading == 0u || lines.AppendLeading( endGap.Leading() ) ) ) {
                    static_cast<void>( AddComments( parser, KeyValue_Root( pTemporary ), {}, lines.Leading(), {} ) );
                }
            }
            if ( parser.result.status == key_value_parse_status_t::OK &&
                 bRequiresObjectRoot &&
                 KeyValue_Type( KeyValue_Root( pTemporary ) ) !=
                    key_value_type_t::OBJECT ) {
                FailCurrent( parser, key_value_parse_status_t::SYNTAX_ERROR );
            }
            // Document composition: merging #include parts first (they are
            // part of this document), then #base fills what is still missing.
            key_value_t *pRoot = KeyValue_Root( pTemporary );
            const bool_t bObjectRoot = KeyValue_Type( pRoot ) == key_value_type_t::OBJECT;
            for ( usize i = 0u; pComposition != nullptr && parser.result.status == key_value_parse_status_t::OK &&
                                i < pComposition->nParts; ++i ) {
                if ( !bObjectRoot || KeyValue_Type( pComposition->ppParts[i] ) != key_value_type_t::OBJECT ||
                     !MergePart( parser, pRoot, pComposition->ppParts[i], 0u ) ) {
                    if ( parser.result.status == key_value_parse_status_t::OK ) {
                        FailCurrent( parser, key_value_parse_status_t::INCLUDE_CONFLICT );
                    }
                    pComposition->iFailedPart = i;
                }
            }
            for ( usize i = 0u; bHasBases && parser.result.status == key_value_parse_status_t::OK &&
                                i < pComposition->nBases; ++i ) {
                if ( !bObjectRoot || KeyValue_Type( pComposition->ppBases[i] ) != key_value_type_t::OBJECT ||
                     !FillMissing( parser, pRoot, pComposition->ppBases[i], 0u ) ) {
                    if ( parser.result.status == key_value_parse_status_t::OK ) {
                        FailCurrent( parser, key_value_parse_status_t::MERGE_CONFLICT );
                    }
                    pComposition->iFailedBase = i;
                }
            }
            parser.nInheritanceDepth = 0u;
            if ( parser.result.status == key_value_parse_status_t::OK ) {
                static_cast<void>( SettleOperators( parser, 0u ) );
            }
        }
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
    if ( parser.pDefinitions != nullptr ) {
        KeyValue_DestroyDocument( parser.pDefinitions );
    }
    if ( parser.pScratch != nullptr ) {
        KeyValue_DestroyDocument( parser.pScratch );
    }
    if ( parser.pOps != nullptr ) {
        Allocator_FreeArrayStorage( pTemporary->pAllocator, parser.pOps, parser.nOpsCapacity );
    }
    KeyValue_DestroyDocument( pTemporary );
    return parser.result;
}

key_value_parse_result_t KeyValue_InternalParseText(
    string_view_t text,
    const key_value_parse_options_t &options,
    key_value_document_t *pDocument,
    bool_t bStrictJson ) noexcept
{
    return KeyValue_InternalParseTextWithDefinitions(
        text,
        options,
        nullptr,
        0u,
        nullptr,
        pDocument,
        bStrictJson );
}

const key_value_build_context_t *KeyValue_HostBuildContext() noexcept
{
#if CYPHER_PLATFORM_WINDOWS
    static constexpr const char kPlatform[] = "windows";
#elif CYPHER_PLATFORM_MACOS
    static constexpr const char kPlatform[] = "macos";
#elif CYPHER_PLATFORM_LINUX
    static constexpr const char kPlatform[] = "linux";
#else
    static constexpr const char kPlatform[] = "unknown";
#endif
    static constexpr const char kArchitecture[] = CYPHER_ARCH_NAME;
    static const char *const pProfile = Cy_BuildConfigIsDebug() ? "debug"
        : Cy_BuildConfigGetCurrent() == build_config_t::Development ? "development"
        : Cy_BuildConfigGetCurrent() == build_config_t::Shipping ? "shipping"
        : "release";
    static const key_value_build_property_t properties[]{
        { StringView_FromCString( "target.platform" ), key_value_build_value_kind_t::STRING, CY_FALSE, 0, StringView_FromCString( kPlatform ) },
        { StringView_FromCString( "target.architecture" ), key_value_build_value_kind_t::STRING, CY_FALSE, 0, StringView_FromCString( kArchitecture ) },
        { StringView_FromCString( "build.profile" ), key_value_build_value_kind_t::STRING, CY_FALSE, 0, StringView_FromCString( pProfile ) },
        { StringView_FromCString( "build.debug" ), key_value_build_value_kind_t::BOOL, Cy_BuildConfigIsDebug(), 0, {} },
        { StringView_FromCString( "build.shipping" ), key_value_build_value_kind_t::BOOL,
          Cy_BuildConfigGetCurrent() == build_config_t::Shipping, 0, {} },
    };
    static const key_value_build_context_t context{ properties, sizeof( properties ) / sizeof( properties[0] ) };
    return &context;
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
        case key_value_parse_status_t::DUPLICATE_DEFINITION:
            return "DUPLICATE_DEFINITION";
        case key_value_parse_status_t::UNDEFINED_DEFINITION:
            return "UNDEFINED_DEFINITION";
        case key_value_parse_status_t::DEFINITION_LIMIT:
            return "DEFINITION_LIMIT";
        case key_value_parse_status_t::UNKNOWN_CONDITION_PROPERTY:
            return "UNKNOWN_CONDITION_PROPERTY";
        case key_value_parse_status_t::CONDITION_TYPE_MISMATCH:
            return "CONDITION_TYPE_MISMATCH";
        case key_value_parse_status_t::MERGE_CONFLICT: return "MERGE_CONFLICT";
        case key_value_parse_status_t::INCLUDE_CONFLICT: return "INCLUDE_CONFLICT";
        case key_value_parse_status_t::INHERITED_VALUE_MISSING:
            return "INHERITED_VALUE_MISSING";
        case key_value_parse_status_t::APPEND_TYPE_MISMATCH:
            return "APPEND_TYPE_MISMATCH";
        case key_value_parse_status_t::OPERATOR_WITHOUT_BASE:
            return "OPERATOR_WITHOUT_BASE";
    }
    return "UNKNOWN_KEY_VALUE_PARSE_STATUS";
}

} // namespace cypher::common
