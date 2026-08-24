//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringEscape.cpp
//  Purpose: Implements bounded Unicode-aware escaped-text conversion.
//  Details: JSON follows its restricted escape grammar. C and Cypher styles also
//           support exact two-digit hex and eight-digit Unicode escapes. Writers
//           never emit partial UTF-8 code points or partial escape sequences.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
String Escape Implementation Notes

Text operations distinguish bounded byte ranges from null-terminated strings. Cursor movement
and conversion validate limits before reading, and failure never relies on ambient locale state.
================
*/

#include "CypherCommon_StringEscape.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_STRING_ESCAPE_FLAG_MASK =
    STRING_ESCAPE_FLAG_QUOTES |
    STRING_ESCAPE_FLAG_BACKSLASH |
    STRING_ESCAPE_FLAG_CONTROL_CHARS |
    STRING_ESCAPE_FLAG_NON_ASCII |
    STRING_ESCAPE_FLAG_PATH_SLASHES; // Reject unknown escape-policy bits.

constexpr char g_escapeHexDigits[] = "0123456789abcdef"; // Canonical lowercase escape spelling.

struct escape_writer_t {
    char *pDest{ nullptr };       // Optional output used for write or measure mode.
    usize cchCapacity{ 0u };      // Data bytes available after reserving a terminator.
    usize cchWritten{ 0u };       // Complete code units physically stored.
    usize cchRequired{ 0u };      // Full encoded result length.
    bool_t bTruncated{ CY_FALSE };// A complete unit did not fit in the destination.
    bool_t bOverflow{ CY_FALSE }; // Required-length arithmetic exceeded usize.
};

bool_t EscapeStyleIsValid( string_escape_style_t style ) noexcept
{
    return style == string_escape_style_t::CYPHER ||
           style == string_escape_style_t::C ||
           style == string_escape_style_t::JSON;
}

bool_t EscapeFlagsAreValid( flags32_t flags ) noexcept
{
    return ( flags & ~CY_STRING_ESCAPE_FLAG_MASK ) == 0u;
}

void EscapeWriter_Write(
    escape_writer_t &writer,
    const char *pText,
    usize cchText ) noexcept
{
    // Whole escape/code-point units are written atomically; no partial unit escapes.
    if ( cchText > CY_USIZE_MAX - writer.cchRequired ) {
        writer.bOverflow = CY_TRUE;
        return;
    }
    writer.cchRequired += cchText;
    if ( cchText <= writer.cchCapacity - writer.cchWritten ) {
        if ( cchText > 0u ) {
            Cy_MemCopy( writer.pDest + writer.cchWritten, pText, cchText );
        }
        writer.cchWritten += cchText;
    } else {
        writer.bTruncated = CY_TRUE;
    }
}

void EscapeWriter_Terminate( escape_writer_t &writer ) noexcept
{
    if ( writer.pDest != nullptr ) {
        writer.pDest[writer.cchWritten] = '\0';
    }
}

escape_writer_t MakeEscapeWriter( char *pDest, usize cchDest ) noexcept
{
    return {
        pDest,
        cchDest > 0u ? cchDest - 1u : 0u,
        0u,
        0u,
        CY_FALSE,
        CY_FALSE
    };
}

string_escape_result_t FinishEscapeResult(
    escape_writer_t &writer,
    usize cchConsumed,
    usize iError = CY_STRING_VIEW_NPOS,
    string_escape_status_t failure = string_escape_status_t::OK ) noexcept
{
    EscapeWriter_Terminate( writer );
    if ( writer.bOverflow ) {
        return {
            string_escape_status_t::INVALID_ARGUMENT,
            cchConsumed,
            writer.cchWritten,
            CY_USIZE_MAX,
            iError
        };
    }
    const string_escape_status_t status = failure != string_escape_status_t::OK
        ? failure
        : ( writer.bTruncated || writer.cchWritten != writer.cchRequired
            ? string_escape_status_t::OUTPUT_TRUNCATED
            : string_escape_status_t::OK );
    return {
        status,
        cchConsumed,
        writer.cchWritten,
        writer.cchRequired,
        iError
    };
}

usize WriteHexDigits(
    char *pDest,
    unicode_code_point_t value,
    usize nDigits ) noexcept
{
    for ( usize iDigit = 0u; iDigit < nDigits; ++iDigit ) {
        const u32 nShift = static_cast<u32>( ( nDigits - 1u - iDigit ) * 4u );
        pDest[iDigit] = g_escapeHexDigits[( value >> nShift ) & 0x0Fu];
    }
    return nDigits;
}

usize EncodeCodePointEscape(
    unicode_code_point_t codePoint,
    string_escape_style_t style,
    char *pEscape ) noexcept
{
    // JSON represents non-BMP scalars as a UTF-16 surrogate pair.
    if ( style == string_escape_style_t::JSON && codePoint > 0xFFFFu ) {
        const unicode_code_point_t adjusted = codePoint - 0x10000u;
        const u16 high = static_cast<u16>( 0xD800u + ( adjusted >> 10u ) );
        const u16 low = static_cast<u16>( 0xDC00u + ( adjusted & 0x3FFu ) );
        pEscape[0] = '\\';
        pEscape[1] = 'u';
        WriteHexDigits( pEscape + 2u, high, 4u );
        pEscape[6] = '\\';
        pEscape[7] = 'u';
        WriteHexDigits( pEscape + 8u, low, 4u );
        return 12u;
    }

    pEscape[0] = '\\';
    if ( codePoint <= 0xFFFFu ) {
        pEscape[1] = 'u';
        WriteHexDigits( pEscape + 2u, codePoint, 4u );
        return 6u;
    }
    pEscape[1] = 'U';
    WriteHexDigits( pEscape + 2u, codePoint, 8u );
    return 10u;
}

usize EncodeControlEscape(
    unicode_code_point_t codePoint,
    string_escape_style_t style,
    char *pEscape ) noexcept
{
    pEscape[0] = '\\';
    switch ( codePoint ) {
        case '\b': pEscape[1] = 'b'; return 2u;
        case '\f': pEscape[1] = 'f'; return 2u;
        case '\n': pEscape[1] = 'n'; return 2u;
        case '\r': pEscape[1] = 'r'; return 2u;
        case '\t': pEscape[1] = 't'; return 2u;
        case '\v':
            if ( style != string_escape_style_t::JSON ) {
                pEscape[1] = 'v';
                return 2u;
            }
            break;
        case '\0':
            if ( style != string_escape_style_t::JSON ) {
                pEscape[1] = '0';
                return 2u;
            }
            break;
        default:
            break;
    }

    if ( style == string_escape_style_t::JSON ) {
        pEscape[1] = 'u';
        pEscape[2] = '0';
        pEscape[3] = '0';
        WriteHexDigits( pEscape + 4u, codePoint, 2u );
        return 6u;
    }
    pEscape[1] = 'x';
    WriteHexDigits( pEscape + 2u, codePoint, 2u );
    return 4u;
}

bool_t ParseHexDigits(
    string_view_t text,
    usize iStart,
    usize nDigits,
    unicode_code_point_t &valueOut,
    usize &iErrorOut ) noexcept
{
    if ( iStart > text.cchLength || nDigits > text.cchLength - iStart ) {
        iErrorOut = text.cchLength;
        return CY_FALSE;
    }

    unicode_code_point_t value = 0u;
    for ( usize iDigit = 0u; iDigit < nDigits; ++iDigit ) {
        const u8 digit = Char_HexValueAscii( text.pData[iStart + iDigit] );
        if ( digit == CY_CHAR_INVALID_DIGIT_VALUE ) {
            iErrorOut = iStart + iDigit;
            return CY_FALSE;
        }
        value = ( value << 4u ) | digit;
    }
    valueOut = value;
    return CY_TRUE;
}

bool_t EscapeClassAllowed(
    unicode_code_point_t codePoint,
    flags32_t flags ) noexcept
{
    if ( codePoint == '"' ) {
        return ( flags & STRING_ESCAPE_FLAG_QUOTES ) != 0u;
    }
    if ( codePoint == '\\' ) {
        return ( flags & STRING_ESCAPE_FLAG_BACKSLASH ) != 0u;
    }
    if ( codePoint == '/' ) {
        return ( flags & STRING_ESCAPE_FLAG_PATH_SLASHES ) != 0u;
    }
    if ( codePoint < 0x20u || codePoint == 0x7Fu ) {
        return ( flags & STRING_ESCAPE_FLAG_CONTROL_CHARS ) != 0u;
    }
    if ( codePoint > 0x7Fu ) {
        return ( flags & STRING_ESCAPE_FLAG_NON_ASCII ) != 0u;
    }
    return CY_TRUE;
}

} // namespace

bool_t StringEscape_NeedsEscaping(
    string_view_t text,
    string_escape_style_t style,
    flags32_t flags ) noexcept
{
    if ( !StringView_IsValid( text ) ||
         !EscapeStyleIsValid( style ) ||
         !EscapeFlagsAreValid( flags ) ) {
        return CY_FALSE;
    }

    // Decode first so non-ASCII policy operates on scalar values, not UTF-8 bytes.
    usize iByte = 0u;
    while ( iByte < text.cchLength ) {
        unicode_code_point_t codePoint = 0u;
        const unicode_result_t decoded = Unicode_DecodeUtf8(
            StringView_Subview( text, iByte, CY_STRING_VIEW_NPOS ),
            &codePoint );
        if ( decoded.status != unicode_status_t::OK ) {
            return CY_TRUE;
        }
        if ( ( codePoint == '"' && ( flags & STRING_ESCAPE_FLAG_QUOTES ) != 0u ) ||
             ( codePoint == '\\' && ( flags & STRING_ESCAPE_FLAG_BACKSLASH ) != 0u ) ||
             ( codePoint == '/' && ( flags & STRING_ESCAPE_FLAG_PATH_SLASHES ) != 0u ) ||
             ( ( codePoint < 0x20u || codePoint == 0x7Fu ) &&
               ( flags & STRING_ESCAPE_FLAG_CONTROL_CHARS ) != 0u ) ||
             ( codePoint > 0x7Fu &&
               ( flags & STRING_ESCAPE_FLAG_NON_ASCII ) != 0u ) ) {
            return CY_TRUE;
        }
        iByte += decoded.nInputConsumed;
    }
    return CY_FALSE;
}

string_escape_result_t StringEscape_Encode(
    string_view_t text,
    string_escape_style_t style,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidStyle = EscapeStyleIsValid( style );
    const bool_t bValidFlags = EscapeFlagsAreValid( flags );
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    if ( !bValidText || !bValidStyle || !bValidFlags || !bValidDestination ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return { string_escape_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    escape_writer_t writer = MakeEscapeWriter( pDest, cchDest );
    usize iByte = 0u;
    while ( iByte < text.cchLength ) {
        unicode_code_point_t codePoint = 0u;
        const unicode_result_t decoded = Unicode_DecodeUtf8(
            StringView_Subview( text, iByte, CY_STRING_VIEW_NPOS ),
            &codePoint );
        if ( decoded.status != unicode_status_t::OK ) {
            return FinishEscapeResult(
                writer,
                iByte,
                iByte + decoded.iError,
                string_escape_status_t::INVALID_CODE_POINT );
        }

        // Twelve bytes is the longest spelling: two JSON \uXXXX escapes.
        char escaped[12]{};
        usize cchEscaped = 0u;
        if ( codePoint == '"' && ( flags & STRING_ESCAPE_FLAG_QUOTES ) != 0u ) {
            escaped[0] = '\\';
            escaped[1] = '"';
            cchEscaped = 2u;
        } else if ( codePoint == '\\' &&
                    ( flags & STRING_ESCAPE_FLAG_BACKSLASH ) != 0u ) {
            escaped[0] = '\\';
            escaped[1] = '\\';
            cchEscaped = 2u;
        } else if ( codePoint == '/' &&
                    ( flags & STRING_ESCAPE_FLAG_PATH_SLASHES ) != 0u ) {
            escaped[0] = '\\';
            escaped[1] = '/';
            cchEscaped = 2u;
        } else if ( ( codePoint < 0x20u || codePoint == 0x7Fu ) &&
                    ( flags & STRING_ESCAPE_FLAG_CONTROL_CHARS ) != 0u ) {
            cchEscaped = EncodeControlEscape( codePoint, style, escaped );
        } else if ( codePoint > 0x7Fu &&
                    ( flags & STRING_ESCAPE_FLAG_NON_ASCII ) != 0u ) {
            cchEscaped = EncodeCodePointEscape( codePoint, style, escaped );
        }

        if ( cchEscaped > 0u ) {
            EscapeWriter_Write( writer, escaped, cchEscaped );
        } else {
            EscapeWriter_Write(
                writer,
                text.pData + iByte,
                decoded.nInputConsumed );
        }
        iByte += decoded.nInputConsumed;
    }
    return FinishEscapeResult( writer, text.cchLength );
}

string_escape_result_t StringEscape_Decode(
    string_view_t text,
    string_escape_style_t style,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidStyle = EscapeStyleIsValid( style );
    const bool_t bValidFlags = EscapeFlagsAreValid( flags );
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    if ( !bValidText || !bValidStyle || !bValidFlags || !bValidDestination ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return { string_escape_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    escape_writer_t writer = MakeEscapeWriter( pDest, cchDest );
    usize iByte = 0u;
    while ( iByte < text.cchLength ) {
        if ( text.pData[iByte] != '\\' ) {
            unicode_code_point_t codePoint = 0u;
            const unicode_result_t decoded = Unicode_DecodeUtf8(
                StringView_Subview( text, iByte, CY_STRING_VIEW_NPOS ),
                &codePoint );
            if ( decoded.status != unicode_status_t::OK ) {
                return FinishEscapeResult(
                    writer,
                    iByte,
                    iByte + decoded.iError,
                    string_escape_status_t::INVALID_CODE_POINT );
            }
            EscapeWriter_Write(
                writer,
                text.pData + iByte,
                decoded.nInputConsumed );
            iByte += decoded.nInputConsumed;
            continue;
        }

        // Keep the leading slash offset for precise invalid-escape diagnostics.
        const usize iEscape = iByte;
        if ( iByte + 1u >= text.cchLength ) {
            return FinishEscapeResult(
                writer,
                iByte,
                text.cchLength,
                string_escape_status_t::INVALID_ESCAPE );
        }

        const char chEscape = text.pData[iByte + 1u];
        unicode_code_point_t codePoint = 0u;
        usize cchEscape = 2u;
        bool_t bRecognized = CY_TRUE;
        switch ( chEscape ) {
            case '"': codePoint = '"'; break;
            case '\\': codePoint = '\\'; break;
            case '/': codePoint = '/'; break;
            case 'b': codePoint = '\b'; break;
            case 'f': codePoint = '\f'; break;
            case 'n': codePoint = '\n'; break;
            case 'r': codePoint = '\r'; break;
            case 't': codePoint = '\t'; break;
            case 'v':
                if ( style == string_escape_style_t::JSON ) {
                    bRecognized = CY_FALSE;
                } else {
                    codePoint = '\v';
                }
                break;
            case '0':
                if ( style == string_escape_style_t::JSON ) {
                    bRecognized = CY_FALSE;
                } else {
                    codePoint = 0u;
                }
                break;
            case 'x': {
                if ( style == string_escape_style_t::JSON ) {
                    bRecognized = CY_FALSE;
                    break;
                }
                usize iError = iEscape;
                if ( !ParseHexDigits( text, iByte + 2u, 2u, codePoint, iError ) ) {
                    return FinishEscapeResult(
                        writer,
                        iByte,
                        iError,
                        string_escape_status_t::INVALID_ESCAPE );
                }
                cchEscape = 4u;
                break;
            }
            case 'u': {
                usize iError = iEscape;
                if ( !ParseHexDigits( text, iByte + 2u, 4u, codePoint, iError ) ) {
                    return FinishEscapeResult(
                        writer,
                        iByte,
                        iError,
                        string_escape_status_t::INVALID_ESCAPE );
                }
                cchEscape = 6u;
                if ( codePoint >= 0xD800u && codePoint <= 0xDBFFu ) {
                    // A high surrogate is valid only when immediately paired with a low one.
                    if ( iByte + 12u > text.cchLength ||
                         text.pData[iByte + 6u] != '\\' ||
                         text.pData[iByte + 7u] != 'u' ) {
                        return FinishEscapeResult(
                            writer,
                            iByte,
                            iByte + 6u,
                            string_escape_status_t::INVALID_CODE_POINT );
                    }
                    unicode_code_point_t low = 0u;
                    if ( !ParseHexDigits(
                             text,
                             iByte + 8u,
                             4u,
                             low,
                             iError ) ||
                         low < 0xDC00u || low > 0xDFFFu ) {
                        return FinishEscapeResult(
                            writer,
                            iByte,
                            iError,
                            string_escape_status_t::INVALID_CODE_POINT );
                    }
                    codePoint = 0x10000u +
                        ( ( codePoint - 0xD800u ) << 10u ) +
                        ( low - 0xDC00u );
                    cchEscape = 12u;
                } else if ( codePoint >= 0xDC00u && codePoint <= 0xDFFFu ) {
                    return FinishEscapeResult(
                        writer,
                        iByte,
                        iByte + 2u,
                        string_escape_status_t::INVALID_CODE_POINT );
                }
                break;
            }
            case 'U': {
                if ( style == string_escape_style_t::JSON ) {
                    bRecognized = CY_FALSE;
                    break;
                }
                usize iError = iEscape;
                if ( !ParseHexDigits( text, iByte + 2u, 8u, codePoint, iError ) ) {
                    return FinishEscapeResult(
                        writer,
                        iByte,
                        iError,
                        string_escape_status_t::INVALID_ESCAPE );
                }
                cchEscape = 10u;
                break;
            }
            default:
                bRecognized = CY_FALSE;
                break;
        }

        if ( !bRecognized || !EscapeClassAllowed( codePoint, flags ) ) {
            return FinishEscapeResult(
                writer,
                iByte,
                iEscape,
                string_escape_status_t::INVALID_ESCAPE );
        }
        if ( !Unicode_IsScalarValue( codePoint ) ) {
            return FinishEscapeResult(
                writer,
                iByte,
                iEscape,
                string_escape_status_t::INVALID_CODE_POINT );
        }

        // Convert the validated scalar back to canonical UTF-8 output.
        char encoded[4]{};
        const unicode_result_t encodedResult = Unicode_EncodeUtf8(
            codePoint,
            encoded,
            sizeof( encoded ) );
        if ( encodedResult.status != unicode_status_t::OK ) {
            return FinishEscapeResult(
                writer,
                iByte,
                iEscape,
                string_escape_status_t::INVALID_CODE_POINT );
        }
        EscapeWriter_Write(
            writer,
            encoded,
            encodedResult.nOutputWritten );
        iByte += cchEscape;
    }
    return FinishEscapeResult( writer, text.cchLength );
}

} // namespace cypher::common
