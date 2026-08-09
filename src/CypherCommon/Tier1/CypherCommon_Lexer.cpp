//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Lexer.cpp
//  Purpose: Implements the allocation-free lexical scanner used by Cypher text formats.
//  Details: Tokens borrow bounded source storage and retain exact byte, line, and
//           column ranges. The scanner performs lexical validation only; grammar and
//           document semantics remain the responsibility of higher-level parsers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Lexer.h"

#include "CypherCommon_Assert.h"
#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace
{

static bool_t Lexer_HasFlag( const lexer_t &lexer, lexer_flags_t flag ) noexcept
{
    return ( lexer.rules.flags & static_cast<flags32_t>( flag ) ) != 0u;
}

static bool_t Lexer_RulesHaveFlag( const lexer_rules_t &rules, lexer_flags_t flag ) noexcept
{
    return ( rules.flags & static_cast<flags32_t>( flag ) ) != 0u;
}

static bool_t Lexer_HasValidByteAt( const lexer_t &lexer, usize iByte ) noexcept
{
    return lexer.source.pData != nullptr && iByte < lexer.source.cchLength;
}

static bool_t Lexer_IsAtEndInternal( const lexer_t &lexer ) noexcept
{
    return lexer.cursor.iByte >= lexer.source.cchLength;
}

static char Lexer_ByteAt( const lexer_t &lexer, usize iByte ) noexcept
{
    const bool_t bHasByte = Lexer_HasValidByteAt( lexer, iByte );
    CY_ASSERT_MSG( bHasByte, "Lexer_ByteAt requires an existing source byte." );
    return bHasByte ? lexer.source.pData[iByte] : '\0';
}

static text_location_t Lexer_CurrentLocation( const lexer_t &lexer ) noexcept
{
    return { lexer.cursor.iByte, lexer.cursor.nLine, lexer.cursor.nColumn };
}

static void Lexer_Advance( lexer_t *pLexer ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Advance requires a valid lexer." );
    if ( pLexer == nullptr || Lexer_IsAtEndInternal( *pLexer ) ) {
        return;
    }

    const char chCurrent = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
    if ( chCurrent == '\r' ) {
        ++pLexer->cursor.iByte;
        if ( Lexer_HasValidByteAt( *pLexer, pLexer->cursor.iByte ) &&
             Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) == '\n' ) {
            ++pLexer->cursor.iByte;
        }

        ++pLexer->cursor.nLine;
        pLexer->cursor.nColumn = 1u;
        return;
    }

    ++pLexer->cursor.iByte;
    if ( chCurrent == '\n' ) {
        ++pLexer->cursor.nLine;
        pLexer->cursor.nColumn = 1u;
    } else {
        ++pLexer->cursor.nColumn;
    }
}

static void Lexer_AdvanceBytes( lexer_t *pLexer, usize cBytes ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_AdvanceBytes requires a valid lexer." );
    if ( pLexer == nullptr ) {
        return;
    }

    const usize cBytesRemaining = pLexer->source.cchLength - pLexer->cursor.iByte;
    CY_ASSERT_MSG( cBytes <= cBytesRemaining, "Lexer_AdvanceBytes exceeds the source range." );
    const usize iEnd = pLexer->cursor.iByte +
                       ( cBytes < cBytesRemaining ? cBytes : cBytesRemaining );
    while ( pLexer->cursor.iByte < iEnd ) {
        Lexer_Advance( pLexer );
    }
}

static string_view_t Lexer_SourceView(
    const lexer_t &lexer,
    usize iBegin,
    usize iEnd ) noexcept
{
    CY_ASSERT_MSG( iBegin <= iEnd, "Lexer source range must be ordered." );
    CY_ASSERT_MSG( iEnd <= lexer.source.cchLength, "Lexer source range exceeds the input." );

    if ( iBegin > iEnd || iEnd > lexer.source.cchLength || lexer.source.pData == nullptr ) {
        return {};
    }

    return { lexer.source.pData + iBegin, iEnd - iBegin };
}

static void Lexer_WriteToken(
    const lexer_t &lexer,
    token_t *pTokenOut,
    token_kind_t kind,
    usize iBegin,
    text_location_t begin,
    flags32_t flags = TOKEN_FLAG_NONE ) noexcept
{
    CY_ASSERT_MSG( pTokenOut != nullptr, "Lexer_WriteToken requires output storage." );
    if ( pTokenOut == nullptr ) {
        return;
    }

    pTokenOut->kind = kind;
    pTokenOut->lexeme = Lexer_SourceView( lexer, iBegin, lexer.cursor.iByte );
    pTokenOut->range = { begin, Lexer_CurrentLocation( lexer ) };
    pTokenOut->flags = flags;
}

static bool_t Lexer_TokenExceedsLimit( const lexer_t &lexer, usize iBegin ) noexcept
{
    return lexer.rules.cchMaxToken != CY_INVALID_SIZE &&
           lexer.cursor.iByte - iBegin > lexer.rules.cchMaxToken;
}

static lexer_status_t Lexer_Fail(
    lexer_t *pLexer,
    lexer_status_t status,
    usize iBegin,
    text_location_t begin,
    text_location_t errorLocation,
    token_t *pTokenOut ) noexcept
{
    pLexer->status = status;
    pLexer->errorLocation = errorLocation;
    if ( pTokenOut != nullptr ) {
        Lexer_WriteToken( *pLexer, pTokenOut, token_kind_t::ERROR, iBegin, begin );
    }
    return status;
}

static bool_t Lexer_ViewIsValid( string_view_t view ) noexcept
{
    return view.pData != nullptr || view.cchLength == 0u;
}

static bool_t Lexer_MatchesAt(
    const lexer_t &lexer,
    usize iByte,
    string_view_t text ) noexcept
{
    if ( text.pData == nullptr || text.cchLength == 0u ||
         iByte > lexer.source.cchLength ||
         text.cchLength > lexer.source.cchLength - iByte ) {
        return CY_FALSE;
    }

    for ( usize iText = 0u; iText < text.cchLength; ++iText ) {
        if ( lexer.source.pData[iByte + iText] != text.pData[iText] ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

static bool_t Lexer_IsLineCommentAt( const lexer_t &lexer, usize iByte ) noexcept
{
    return Lexer_HasFlag( lexer, LEXER_FLAG_ALLOW_LINE_COMMENTS ) &&
           Lexer_MatchesAt( lexer, iByte, lexer.rules.lineCommentBegin );
}

static bool_t Lexer_IsBlockCommentAt( const lexer_t &lexer, usize iByte ) noexcept
{
    return Lexer_HasFlag( lexer, LEXER_FLAG_ALLOW_BLOCK_COMMENTS ) &&
           Lexer_MatchesAt( lexer, iByte, lexer.rules.blockCommentBegin );
}

static bool_t Lexer_IsIdentifierStartByte( const lexer_t &lexer, char ch ) noexcept
{
    return Char_IsAlphaAscii( ch ) || ch == '_' ||
           CharacterSet_Contains( &lexer.rules.identifierStartExtra, ch );
}

static bool_t Lexer_IsIdentifierBodyByte( const lexer_t &lexer, char ch ) noexcept
{
    return Char_IsAlphaNumericAscii( ch ) || ch == '_' ||
           CharacterSet_Contains( &lexer.rules.identifierBodyExtra, ch );
}

static usize Lexer_ValidUtf8SequenceLength( const lexer_t &lexer, usize iByte ) noexcept
{
    if ( !Lexer_HasValidByteAt( lexer, iByte ) ) {
        return 0u;
    }

    const u8 lead = static_cast<u8>( lexer.source.pData[iByte] );
    usize cBytes = 0u;
    if ( lead >= 0xC2u && lead <= 0xDFu ) {
        cBytes = 2u;
    } else if ( lead >= 0xE0u && lead <= 0xEFu ) {
        cBytes = 3u;
    } else if ( lead >= 0xF0u && lead <= 0xF4u ) {
        cBytes = 4u;
    } else {
        return 0u;
    }

    if ( cBytes > lexer.source.cchLength - iByte ) {
        return 0u;
    }

    for ( usize iContinuation = 1u; iContinuation < cBytes; ++iContinuation ) {
        const u8 value = static_cast<u8>( lexer.source.pData[iByte + iContinuation] );
        if ( ( value & 0xC0u ) != 0x80u ) {
            return 0u;
        }
    }

    const u8 second = static_cast<u8>( lexer.source.pData[iByte + 1u] );
    if ( ( lead == 0xE0u && second < 0xA0u ) ||
         ( lead == 0xEDu && second > 0x9Fu ) ||
         ( lead == 0xF0u && second < 0x90u ) ||
         ( lead == 0xF4u && second > 0x8Fu ) ) {
        return 0u;
    }

    return cBytes;
}

static bool_t Lexer_IsDigitForBase( char ch, u32 nBase ) noexcept
{
    switch ( nBase ) {
        case 2u:  return Char_IsBinaryDigitAscii( ch );
        case 8u:  return Char_IsOctalDigitAscii( ch );
        case 10u: return Char_IsDigitAscii( ch );
        case 16u: return Char_IsHexDigitAscii( ch );
        default:  return CY_FALSE;
    }
}

static bool_t Lexer_IsNumberStart( const lexer_t &lexer ) noexcept
{
    if ( Lexer_IsAtEndInternal( lexer ) ) {
        return CY_FALSE;
    }

    usize iByte = lexer.cursor.iByte;
    char ch = Lexer_ByteAt( lexer, iByte );
    if ( ( ch == '+' || ch == '-' ) && Lexer_HasFlag( lexer, LEXER_FLAG_SIGN_IS_NUMBER_PART ) ) {
        ++iByte;
        if ( !Lexer_HasValidByteAt( lexer, iByte ) ) {
            return CY_FALSE;
        }
        ch = Lexer_ByteAt( lexer, iByte );
    }

    if ( Char_IsDigitAscii( ch ) ) {
        return CY_TRUE;
    }

    return ch == '.' && Lexer_HasValidByteAt( lexer, iByte + 1u ) &&
           Char_IsDigitAscii( Lexer_ByteAt( lexer, iByte + 1u ) );
}

static lexer_status_t Lexer_ScanLineComment(
    lexer_t *pLexer,
    token_t *pTokenOut ) noexcept
{
    const usize iBegin = pLexer->cursor.iByte;
    const text_location_t begin = Lexer_CurrentLocation( *pLexer );
    Lexer_AdvanceBytes( pLexer, pLexer->rules.lineCommentBegin.cchLength );

    while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        if ( Char_IsNewLineAscii( ch ) ) {
            break;
        }
        Lexer_Advance( pLexer );
        if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
            return Lexer_Fail(
                pLexer,
                lexer_status_t::TOKEN_TOO_LONG,
                iBegin,
                begin,
                Lexer_CurrentLocation( *pLexer ),
                pTokenOut );
        }
    }

    if ( pTokenOut != nullptr ) {
        Lexer_WriteToken( *pLexer, pTokenOut, token_kind_t::COMMENT, iBegin, begin );
    }
    return lexer_status_t::OK;
}

static lexer_status_t Lexer_ScanBlockComment(
    lexer_t *pLexer,
    token_t *pTokenOut ) noexcept
{
    const usize iBegin = pLexer->cursor.iByte;
    const text_location_t begin = Lexer_CurrentLocation( *pLexer );
    usize nDepth = 1u;
    Lexer_AdvanceBytes( pLexer, pLexer->rules.blockCommentBegin.cchLength );

    while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        if ( Lexer_MatchesAt(
                 *pLexer,
                 pLexer->cursor.iByte,
                 pLexer->rules.blockCommentEnd ) ) {
            Lexer_AdvanceBytes( pLexer, pLexer->rules.blockCommentEnd.cchLength );
            --nDepth;
            if ( nDepth == 0u ) {
                if ( pTokenOut != nullptr ) {
                    Lexer_WriteToken( *pLexer, pTokenOut, token_kind_t::COMMENT, iBegin, begin );
                }
                return lexer_status_t::OK;
            }
        } else if ( Lexer_HasFlag( *pLexer, LEXER_FLAG_ALLOW_NESTED_BLOCK_COMMENT ) &&
                    Lexer_MatchesAt(
                        *pLexer,
                        pLexer->cursor.iByte,
                        pLexer->rules.blockCommentBegin ) ) {
            Lexer_AdvanceBytes( pLexer, pLexer->rules.blockCommentBegin.cchLength );
            ++nDepth;
        } else {
            Lexer_Advance( pLexer );
        }

        if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
            return Lexer_Fail(
                pLexer,
                lexer_status_t::TOKEN_TOO_LONG,
                iBegin,
                begin,
                Lexer_CurrentLocation( *pLexer ),
                pTokenOut );
        }
    }

    return Lexer_Fail(
        pLexer,
        lexer_status_t::UNTERMINATED_COMMENT,
        iBegin,
        begin,
        Lexer_CurrentLocation( *pLexer ),
        pTokenOut );
}

static lexer_status_t Lexer_ScanIdentifier(
    lexer_t *pLexer,
    token_t *pTokenOut ) noexcept
{
    const usize iBegin = pLexer->cursor.iByte;
    const text_location_t begin = Lexer_CurrentLocation( *pLexer );
    bool_t bFirstByte = CY_TRUE;

    while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        const bool_t bIdentifierByte = bFirstByte
            ? Lexer_IsIdentifierStartByte( *pLexer, ch )
            : Lexer_IsIdentifierBodyByte( *pLexer, ch );
        if ( bIdentifierByte ) {
            Lexer_Advance( pLexer );
        } else if ( static_cast<u8>( ch ) >= 0x80u &&
                    Lexer_HasFlag( *pLexer, LEXER_FLAG_ALLOW_UTF8_IDENTIFIERS ) ) {
            const usize cBytes = Lexer_ValidUtf8SequenceLength( *pLexer, pLexer->cursor.iByte );
            if ( cBytes == 0u ) {
                const text_location_t errorLocation = Lexer_CurrentLocation( *pLexer );
                Lexer_Advance( pLexer );
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::INVALID_BYTE,
                    iBegin,
                    begin,
                    errorLocation,
                    pTokenOut );
            }
            Lexer_AdvanceBytes( pLexer, cBytes );
        } else {
            break;
        }

        bFirstByte = CY_FALSE;

        if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
            return Lexer_Fail(
                pLexer,
                lexer_status_t::TOKEN_TOO_LONG,
                iBegin,
                begin,
                Lexer_CurrentLocation( *pLexer ),
                pTokenOut );
        }
    }

    Lexer_WriteToken( *pLexer, pTokenOut, token_kind_t::IDENTIFIER, iBegin, begin );
    return lexer_status_t::OK;
}

static lexer_status_t Lexer_ScanNumber(
    lexer_t *pLexer,
    token_t *pTokenOut ) noexcept
{
    const usize iBegin = pLexer->cursor.iByte;
    const text_location_t begin = Lexer_CurrentLocation( *pLexer );
    flags32_t tokenFlags = TOKEN_FLAG_NONE;
    bool_t bFloat = CY_FALSE;

    if ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char chSign = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        if ( chSign == '+' || chSign == '-' ) {
            if ( chSign == '-' ) {
                tokenFlags |= TOKEN_FLAG_NEGATIVE;
            }
            Lexer_Advance( pLexer );
        }
    }

    if ( !Lexer_IsAtEndInternal( *pLexer ) &&
         Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) == '.' ) {
        bFloat = CY_TRUE;
        Lexer_Advance( pLexer );
    } else if ( Lexer_HasValidByteAt( *pLexer, pLexer->cursor.iByte + 1u ) &&
                Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) == '0' ) {
        const char chPrefix = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte + 1u );
        u32 nBase = 0u;
        if ( chPrefix == 'x' || chPrefix == 'X' ) {
            nBase = 16u;
        } else if ( chPrefix == 'b' || chPrefix == 'B' ) {
            nBase = 2u;
        } else if ( chPrefix == 'o' || chPrefix == 'O' ) {
            nBase = 8u;
        }

        if ( nBase != 0u ) {
            tokenFlags |= TOKEN_FLAG_BASE_PREFIX;
            Lexer_AdvanceBytes( pLexer, 2u );
            bool_t bHasDigit = CY_FALSE;
            bool_t bPreviousSeparator = CY_FALSE;

            while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
                const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
                if ( Lexer_IsDigitForBase( ch, nBase ) ) {
                    bHasDigit = CY_TRUE;
                    bPreviousSeparator = CY_FALSE;
                    Lexer_Advance( pLexer );
                } else if ( ch == '_' && bHasDigit && !bPreviousSeparator ) {
                    bPreviousSeparator = CY_TRUE;
                    Lexer_Advance( pLexer );
                } else {
                    break;
                }
            }

            if ( !bHasDigit || bPreviousSeparator ||
                 ( !Lexer_IsAtEndInternal( *pLexer ) &&
                   ( Char_IsAlphaNumericAscii( Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) ) ||
                     Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) == '_' ) ) ) {
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::INVALID_NUMBER,
                    iBegin,
                    begin,
                    Lexer_CurrentLocation( *pLexer ),
                    pTokenOut );
            }

            if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::TOKEN_TOO_LONG,
                    iBegin,
                    begin,
                    Lexer_CurrentLocation( *pLexer ),
                    pTokenOut );
            }

            Lexer_WriteToken(
                *pLexer,
                pTokenOut,
                token_kind_t::INTEGER,
                iBegin,
                begin,
                tokenFlags );
            return lexer_status_t::OK;
        }
    }

    bool_t bHasDigit = CY_FALSE;
    bool_t bPreviousSeparator = CY_FALSE;
    while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        if ( Char_IsDigitAscii( ch ) ) {
            bHasDigit = CY_TRUE;
            bPreviousSeparator = CY_FALSE;
            Lexer_Advance( pLexer );
        } else if ( ch == '_' && bHasDigit && !bPreviousSeparator ) {
            bPreviousSeparator = CY_TRUE;
            Lexer_Advance( pLexer );
        } else {
            break;
        }
    }

    if ( bPreviousSeparator ) {
        return Lexer_Fail(
            pLexer,
            lexer_status_t::INVALID_NUMBER,
            iBegin,
            begin,
            Lexer_CurrentLocation( *pLexer ),
            pTokenOut );
    }

    if ( !bFloat && !Lexer_IsAtEndInternal( *pLexer ) &&
         Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) == '.' ) {
        bFloat = CY_TRUE;
        Lexer_Advance( pLexer );
        bool_t bHasFractionDigit = CY_FALSE;
        bPreviousSeparator = CY_FALSE;
        while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
            const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
            if ( Char_IsDigitAscii( ch ) ) {
                bHasDigit = CY_TRUE;
                bHasFractionDigit = CY_TRUE;
                bPreviousSeparator = CY_FALSE;
                Lexer_Advance( pLexer );
            } else if ( ch == '_' && bHasFractionDigit && !bPreviousSeparator ) {
                bPreviousSeparator = CY_TRUE;
                Lexer_Advance( pLexer );
            } else {
                break;
            }
        }

        if ( bPreviousSeparator ) {
            return Lexer_Fail(
                pLexer,
                lexer_status_t::INVALID_NUMBER,
                iBegin,
                begin,
                Lexer_CurrentLocation( *pLexer ),
                pTokenOut );
        }
    }

    if ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char chExponent = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        if ( chExponent == 'e' || chExponent == 'E' ) {
            bFloat = CY_TRUE;
            Lexer_Advance( pLexer );
            if ( !Lexer_IsAtEndInternal( *pLexer ) ) {
                const char chExponentSign = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
                if ( chExponentSign == '+' || chExponentSign == '-' ) {
                    Lexer_Advance( pLexer );
                }
            }

            bool_t bHasExponentDigit = CY_FALSE;
            bPreviousSeparator = CY_FALSE;
            while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
                const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
                if ( Char_IsDigitAscii( ch ) ) {
                    bHasExponentDigit = CY_TRUE;
                    bPreviousSeparator = CY_FALSE;
                    Lexer_Advance( pLexer );
                } else if ( ch == '_' && bHasExponentDigit && !bPreviousSeparator ) {
                    bPreviousSeparator = CY_TRUE;
                    Lexer_Advance( pLexer );
                } else {
                    break;
                }
            }

            if ( !bHasExponentDigit || bPreviousSeparator ) {
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::INVALID_NUMBER,
                    iBegin,
                    begin,
                    Lexer_CurrentLocation( *pLexer ),
                    pTokenOut );
            }
        }
    }

    if ( !bHasDigit ) {
        return Lexer_Fail(
            pLexer,
            lexer_status_t::INVALID_NUMBER,
            iBegin,
            begin,
            Lexer_CurrentLocation( *pLexer ),
            pTokenOut );
    }

    if ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char chNext = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        if ( Lexer_IsIdentifierStartByte( *pLexer, chNext ) ||
             static_cast<u8>( chNext ) >= 0x80u ) {
            return Lexer_Fail(
                pLexer,
                lexer_status_t::INVALID_NUMBER,
                iBegin,
                begin,
                Lexer_CurrentLocation( *pLexer ),
                pTokenOut );
        }
    }

    if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
        return Lexer_Fail(
            pLexer,
            lexer_status_t::TOKEN_TOO_LONG,
            iBegin,
            begin,
            Lexer_CurrentLocation( *pLexer ),
            pTokenOut );
    }

    Lexer_WriteToken(
        *pLexer,
        pTokenOut,
        bFloat ? token_kind_t::FLOAT : token_kind_t::INTEGER,
        iBegin,
        begin,
        tokenFlags );
    return lexer_status_t::OK;
}

static bool_t Lexer_IsSimpleEscape( char ch ) noexcept
{
    switch ( ch ) {
        case '\'':
        case '"':
        case '?':
        case '\\':
        case '/':
        case '0':
        case 'a':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
        case 'v':
            return CY_TRUE;
        default:
            return CY_FALSE;
    }
}

static lexer_status_t Lexer_ScanQuoted(
    lexer_t *pLexer,
    token_t *pTokenOut,
    char chQuote,
    token_kind_t kind ) noexcept
{
    const usize iBegin = pLexer->cursor.iByte;
    const text_location_t begin = Lexer_CurrentLocation( *pLexer );
    flags32_t tokenFlags = TOKEN_FLAG_QUOTED;
    Lexer_Advance( pLexer );

    while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        if ( ch == chQuote ) {
            Lexer_Advance( pLexer );
            if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::TOKEN_TOO_LONG,
                    iBegin,
                    begin,
                    Lexer_CurrentLocation( *pLexer ),
                    pTokenOut );
            }

            Lexer_WriteToken( *pLexer, pTokenOut, kind, iBegin, begin, tokenFlags );
            return lexer_status_t::OK;
        }

        if ( Char_IsNewLineAscii( ch ) ) {
            return Lexer_Fail(
                pLexer,
                lexer_status_t::UNTERMINATED_STRING,
                iBegin,
                begin,
                Lexer_CurrentLocation( *pLexer ),
                pTokenOut );
        }

        if ( ch == '\\' && Lexer_HasFlag( *pLexer, LEXER_FLAG_ALLOW_ESCAPE_SEQUENCES ) ) {
            tokenFlags |= TOKEN_FLAG_HAS_ESCAPES;
            const text_location_t escapeLocation = Lexer_CurrentLocation( *pLexer );
            Lexer_Advance( pLexer );
            if ( Lexer_IsAtEndInternal( *pLexer ) ) {
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::INVALID_ESCAPE,
                    iBegin,
                    begin,
                    escapeLocation,
                    pTokenOut );
            }

            const char chEscape = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
            if ( Lexer_IsSimpleEscape( chEscape ) ) {
                Lexer_Advance( pLexer );
            } else if ( chEscape == 'x' || chEscape == 'u' || chEscape == 'U' ) {
                const usize cHexDigits = chEscape == 'x' ? 2u : ( chEscape == 'u' ? 4u : 8u );
                Lexer_Advance( pLexer );
                for ( usize iDigit = 0u; iDigit < cHexDigits; ++iDigit ) {
                    if ( Lexer_IsAtEndInternal( *pLexer ) ||
                         !Char_IsHexDigitAscii( Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) ) ) {
                        return Lexer_Fail(
                            pLexer,
                            lexer_status_t::INVALID_ESCAPE,
                            iBegin,
                            begin,
                            escapeLocation,
                            pTokenOut );
                    }
                    Lexer_Advance( pLexer );
                }
            } else {
                Lexer_Advance( pLexer );
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::INVALID_ESCAPE,
                    iBegin,
                    begin,
                    escapeLocation,
                    pTokenOut );
            }
        } else {
            if ( static_cast<u8>( ch ) < 0x20u ) {
                const text_location_t errorLocation = Lexer_CurrentLocation( *pLexer );
                Lexer_Advance( pLexer );
                return Lexer_Fail(
                    pLexer,
                    lexer_status_t::INVALID_BYTE,
                    iBegin,
                    begin,
                    errorLocation,
                    pTokenOut );
            }
            Lexer_Advance( pLexer );
        }

        if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
            return Lexer_Fail(
                pLexer,
                lexer_status_t::TOKEN_TOO_LONG,
                iBegin,
                begin,
                Lexer_CurrentLocation( *pLexer ),
                pTokenOut );
        }
    }

    return Lexer_Fail(
        pLexer,
        lexer_status_t::UNTERMINATED_STRING,
        iBegin,
        begin,
        Lexer_CurrentLocation( *pLexer ),
        pTokenOut );
}

static lexer_status_t Lexer_ScanPunctuation(
    lexer_t *pLexer,
    token_t *pTokenOut ) noexcept
{
    const usize iBegin = pLexer->cursor.iByte;
    const text_location_t begin = Lexer_CurrentLocation( *pLexer );
    usize cBestMatch = 0u;

    for ( usize iPunctuation = 0u;
          iPunctuation < pLexer->rules.nPunctuationCount;
          ++iPunctuation ) {
        const string_view_t punctuation = pLexer->rules.pPunctuations[iPunctuation];
        if ( punctuation.cchLength > cBestMatch &&
             Lexer_MatchesAt( *pLexer, iBegin, punctuation ) ) {
            cBestMatch = punctuation.cchLength;
        }
    }

    if ( cBestMatch == 0u ) {
        cBestMatch = 1u;
    }
    Lexer_AdvanceBytes( pLexer, cBestMatch );

    if ( Lexer_TokenExceedsLimit( *pLexer, iBegin ) ) {
        return Lexer_Fail(
            pLexer,
            lexer_status_t::TOKEN_TOO_LONG,
            iBegin,
            begin,
            Lexer_CurrentLocation( *pLexer ),
            pTokenOut );
    }

    Lexer_WriteToken( *pLexer, pTokenOut, token_kind_t::PUNCTUATION, iBegin, begin );
    return lexer_status_t::OK;
}

static bool_t Lexer_RulesAreValid( const lexer_rules_t &rules ) noexcept
{
    if ( rules.nPunctuationCount > 0u && rules.pPunctuations == nullptr ) {
        return CY_FALSE;
    }

    if ( Lexer_RulesHaveFlag( rules, LEXER_FLAG_ALLOW_LINE_COMMENTS ) &&
         ( !Lexer_ViewIsValid( rules.lineCommentBegin ) ||
           rules.lineCommentBegin.cchLength == 0u ) ) {
        return CY_FALSE;
    }

    if ( Lexer_RulesHaveFlag( rules, LEXER_FLAG_ALLOW_BLOCK_COMMENTS ) &&
         ( !Lexer_ViewIsValid( rules.blockCommentBegin ) ||
           !Lexer_ViewIsValid( rules.blockCommentEnd ) ||
           rules.blockCommentBegin.cchLength == 0u ||
           rules.blockCommentEnd.cchLength == 0u ) ) {
        return CY_FALSE;
    }

    for ( usize iPunctuation = 0u; iPunctuation < rules.nPunctuationCount; ++iPunctuation ) {
        const string_view_t punctuation = rules.pPunctuations[iPunctuation];
        if ( !Lexer_ViewIsValid( punctuation ) || punctuation.cchLength == 0u ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

} // namespace

lexer_rules_t Lexer_DefaultRules() noexcept
{
    return {};
}

bool_t Lexer_Init(
    lexer_t *pLexer,
    string_view_t source,
    const lexer_rules_t &rules ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Init requires a valid lexer." );
    if ( pLexer == nullptr ) {
        return CY_FALSE;
    }

    *pLexer = {};
    if ( !Lexer_ViewIsValid( source ) || !Lexer_RulesAreValid( rules ) ) {
        pLexer->status = lexer_status_t::INVALID_ARGUMENT;
        return CY_FALSE;
    }

    pLexer->source = source;
    pLexer->rules = rules;
    Lexer_Reset( pLexer );
    return CY_TRUE;
}

void Lexer_Reset( lexer_t *pLexer ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Reset requires a valid lexer." );
    if ( pLexer == nullptr ) {
        return;
    }

    pLexer->cursor = {};
    pLexer->status = lexer_status_t::OK;
    pLexer->errorLocation = Lexer_CurrentLocation( *pLexer );
}

lexer_status_t Lexer_SkipTrivia( lexer_t *pLexer ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_SkipTrivia requires a valid lexer." );
    if ( pLexer == nullptr ) {
        return lexer_status_t::INVALID_ARGUMENT;
    }
    if ( pLexer->status != lexer_status_t::OK ) {
        return pLexer->status;
    }

    while ( !Lexer_IsAtEndInternal( *pLexer ) ) {
        const char ch = Lexer_ByteAt( *pLexer, pLexer->cursor.iByte );
        if ( Char_IsNewLineAscii( ch ) ) {
            if ( Lexer_HasFlag( *pLexer, LEXER_FLAG_EMIT_NEWLINES ) ) {
                return lexer_status_t::OK;
            }
            Lexer_Advance( pLexer );
            continue;
        }

        if ( Char_IsWhitespaceAscii( ch ) ) {
            Lexer_Advance( pLexer );
            continue;
        }

        if ( Lexer_IsLineCommentAt( *pLexer, pLexer->cursor.iByte ) ) {
            if ( Lexer_HasFlag( *pLexer, LEXER_FLAG_EMIT_COMMENTS ) ) {
                return lexer_status_t::OK;
            }
            const lexer_status_t status = Lexer_ScanLineComment( pLexer, nullptr );
            if ( status != lexer_status_t::OK ) {
                return status;
            }
            continue;
        }

        if ( Lexer_IsBlockCommentAt( *pLexer, pLexer->cursor.iByte ) ) {
            if ( Lexer_HasFlag( *pLexer, LEXER_FLAG_EMIT_COMMENTS ) ) {
                return lexer_status_t::OK;
            }
            const lexer_status_t status = Lexer_ScanBlockComment( pLexer, nullptr );
            if ( status != lexer_status_t::OK ) {
                return status;
            }
            continue;
        }

        break;
    }

    return Lexer_IsAtEndInternal( *pLexer )
        ? lexer_status_t::END_OF_INPUT
        : lexer_status_t::OK;
}

lexer_status_t Lexer_Read( lexer_t *pLexer, token_t *pTokenOut ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Read requires a valid lexer." );
    CY_ASSERT_MSG( pTokenOut != nullptr, "Lexer_Read requires token output storage." );
    if ( pLexer == nullptr ) {
        return lexer_status_t::INVALID_ARGUMENT;
    }
    if ( pTokenOut == nullptr ) {
        pLexer->status = lexer_status_t::INVALID_ARGUMENT;
        pLexer->errorLocation = Lexer_CurrentLocation( *pLexer );
        return pLexer->status;
    }

    *pTokenOut = {};
    if ( pLexer->status != lexer_status_t::OK ) {
        return pLexer->status;
    }

    const lexer_status_t triviaStatus = Lexer_SkipTrivia( pLexer );
    if ( triviaStatus != lexer_status_t::OK &&
         triviaStatus != lexer_status_t::END_OF_INPUT ) {
        pTokenOut->kind = token_kind_t::ERROR;
        return triviaStatus;
    }

    if ( Lexer_IsAtEndInternal( *pLexer ) ) {
        const text_location_t location = Lexer_CurrentLocation( *pLexer );
        Lexer_WriteToken(
            *pLexer,
            pTokenOut,
            token_kind_t::END_OF_INPUT,
            pLexer->cursor.iByte,
            location );
        pLexer->status = lexer_status_t::END_OF_INPUT;
        return pLexer->status;
    }

    const usize iBegin = pLexer->cursor.iByte;
    const text_location_t begin = Lexer_CurrentLocation( *pLexer );
    const char ch = Lexer_ByteAt( *pLexer, iBegin );

    if ( Char_IsNewLineAscii( ch ) ) {
        Lexer_Advance( pLexer );
        Lexer_WriteToken( *pLexer, pTokenOut, token_kind_t::NEWLINE, iBegin, begin );
        return lexer_status_t::OK;
    }

    if ( Lexer_IsLineCommentAt( *pLexer, iBegin ) ) {
        return Lexer_ScanLineComment( pLexer, pTokenOut );
    }
    if ( Lexer_IsBlockCommentAt( *pLexer, iBegin ) ) {
        return Lexer_ScanBlockComment( pLexer, pTokenOut );
    }

    if ( ch == '"' ) {
        return Lexer_ScanQuoted( pLexer, pTokenOut, ch, token_kind_t::STRING );
    }
    if ( ch == '\'' ) {
        const token_kind_t kind = Lexer_HasFlag(
            *pLexer,
            LEXER_FLAG_ALLOW_SINGLE_QUOTED_STRING )
            ? token_kind_t::STRING
            : token_kind_t::CHARACTER;
        return Lexer_ScanQuoted( pLexer, pTokenOut, ch, kind );
    }

    if ( Lexer_IsNumberStart( *pLexer ) ) {
        return Lexer_ScanNumber( pLexer, pTokenOut );
    }

    if ( Lexer_IsIdentifierStartByte( *pLexer, ch ) ) {
        return Lexer_ScanIdentifier( pLexer, pTokenOut );
    }

    if ( static_cast<u8>( ch ) >= 0x80u &&
         Lexer_HasFlag( *pLexer, LEXER_FLAG_ALLOW_UTF8_IDENTIFIERS ) ) {
        const usize cBytes = Lexer_ValidUtf8SequenceLength( *pLexer, iBegin );
        if ( cBytes != 0u ) {
            return Lexer_ScanIdentifier( pLexer, pTokenOut );
        }
    }

    if ( Char_IsPunctuationAscii( ch ) ) {
        return Lexer_ScanPunctuation( pLexer, pTokenOut );
    }

    const text_location_t errorLocation = Lexer_CurrentLocation( *pLexer );
    Lexer_Advance( pLexer );
    return Lexer_Fail(
        pLexer,
        lexer_status_t::INVALID_BYTE,
        iBegin,
        begin,
        errorLocation,
        pTokenOut );
}

bool_t Lexer_IsAtEnd( const lexer_t *pLexer ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_IsAtEnd requires a valid lexer." );
    return pLexer == nullptr || Lexer_IsAtEndInternal( *pLexer );
}

text_location_t Lexer_Location( const lexer_t *pLexer ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Location requires a valid lexer." );
    return pLexer != nullptr ? Lexer_CurrentLocation( *pLexer ) : text_location_t{};
}

lexer_checkpoint_t Lexer_Save( const lexer_t *pLexer ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Save requires a valid lexer." );
    return pLexer != nullptr ? pLexer->cursor : lexer_checkpoint_t{};
}

void Lexer_Restore( lexer_t *pLexer, lexer_checkpoint_t checkpoint ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Restore requires a valid lexer." );
    if ( pLexer == nullptr ) {
        return;
    }

    const bool_t bValid = checkpoint.iByte <= pLexer->source.cchLength &&
                          checkpoint.nLine > 0u && checkpoint.nColumn > 0u;
    CY_ASSERT_MSG( bValid, "Lexer_Restore received an invalid checkpoint." );
    if ( !bValid ) {
        return;
    }

    pLexer->cursor = checkpoint;
    pLexer->status = lexer_status_t::OK;
    pLexer->errorLocation = Lexer_CurrentLocation( *pLexer );
}

const char *Lexer_StatusName( lexer_status_t status ) noexcept
{
    switch ( status ) {
        case lexer_status_t::OK:                   return "OK";
        case lexer_status_t::END_OF_INPUT:         return "END_OF_INPUT";
        case lexer_status_t::INVALID_ARGUMENT:     return "INVALID_ARGUMENT";
        case lexer_status_t::INVALID_BYTE:         return "INVALID_BYTE";
        case lexer_status_t::INVALID_NUMBER:       return "INVALID_NUMBER";
        case lexer_status_t::INVALID_ESCAPE:       return "INVALID_ESCAPE";
        case lexer_status_t::UNTERMINATED_STRING:  return "UNTERMINATED_STRING";
        case lexer_status_t::UNTERMINATED_COMMENT: return "UNTERMINATED_COMMENT";
        case lexer_status_t::TOKEN_TOO_LONG:       return "TOKEN_TOO_LONG";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
