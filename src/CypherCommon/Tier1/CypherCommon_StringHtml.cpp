//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringHtml.cpp
//  Purpose: Implements bounded HTML entity and plain-text helpers.
//  Details: This intentionally small utility validates UTF-8 and handles basic
//           entities and tags for diagnostics. It is not a parser or sanitizer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
String Html Implementation Notes

Text operations distinguish bounded byte ranges from null-terminated strings. Cursor movement
and conversion validate limits before reading, and failure never relies on ambient locale state.
================
*/

#include "CypherCommon_StringHtml.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

struct html_writer_t {
    char *pDest{ nullptr };             // Optional destination; null selects measurement mode.
    usize cchCapacity{ 0u };            // Writable bytes excluding the final NUL.
    usize cchWritten{ 0u };             // Complete output units copied so far.
    usize cchRequired{ 0u };            // Full length required for an untruncated result.
    bool_t bOverflow{ CY_FALSE };        // Required-length arithmetic exceeded usize.
};

bool_t HtmlDestinationIsValid( char *pDest, usize cchDest ) noexcept
{
    return pDest != nullptr || cchDest == 0u;
}

void HtmlWriter_Write(
    html_writer_t &writer,
    const char *pText,
    usize cchText ) noexcept
{
    if ( cchText > CY_USIZE_MAX - writer.cchRequired ) {
        writer.bOverflow = CY_TRUE;
        return;
    }
    writer.cchRequired += cchText;
    // Never emit a partial entity or UTF-8 sequence. The required count still
    // advances so callers can size a second pass exactly.
    if ( cchText <= writer.cchCapacity - writer.cchWritten ) {
        for ( usize iByte = 0u; iByte < cchText; ++iByte ) {
            writer.pDest[writer.cchWritten + iByte] = pText[iByte];
        }
        writer.cchWritten += cchText;
    }
}

html_text_result_t FinishHtmlWriter(
    html_writer_t &writer,
    usize cchConsumed,
    html_text_status_t failure = html_text_status_t::OK,
    usize iError = CY_STRING_VIEW_NPOS ) noexcept
{
    if ( writer.pDest != nullptr ) {
        writer.pDest[writer.cchWritten] = '\0';
    }
    if ( writer.bOverflow ) {
        return {
            html_text_status_t::INVALID_ARGUMENT,
            cchConsumed,
            writer.cchWritten,
            CY_USIZE_MAX,
            iError
        };
    }
    const html_text_status_t status = failure != html_text_status_t::OK
        ? failure
        : ( writer.cchWritten == writer.cchRequired
            ? html_text_status_t::OK
            : html_text_status_t::OUTPUT_TRUNCATED );
    return {
        status,
        cchConsumed,
        writer.cchWritten,
        writer.cchRequired,
        iError
    };
}

html_text_result_t InvalidHtmlResult(
    html_text_status_t status,
    char *pDest,
    usize cchDest,
    usize iError = CY_STRING_VIEW_NPOS ) noexcept
{
    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[0] = '\0';
    }
    return { status, 0u, 0u, 0u, iError };
}

html_writer_t MakeHtmlWriter( char *pDest, usize cchDest ) noexcept
{
    return {
        pDest,
        cchDest > 0u ? cchDest - 1u : 0u, // Reserve one byte for a stable C-string terminator.
        0u,
        0u,
        CY_FALSE
    };
}

bool_t ViewEqualsLiteral(
    string_view_t view,
    const char *pLiteral,
    usize cchLiteral ) noexcept
{
    if ( view.cchLength != cchLiteral ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < cchLiteral; ++iByte ) {
        if ( view.pData[iByte] != pLiteral[iByte] ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t ParseNumericEntity(
    string_view_t entity,
    unicode_code_point_t &codePointOut ) noexcept
{
    if ( entity.cchLength < 2u || entity.pData[0] != '#' ) {
        return CY_FALSE;
    }
    usize iCursor = 1u;
    u32 nBase = 10u;
    if ( iCursor < entity.cchLength &&
         ( entity.pData[iCursor] == 'x' || entity.pData[iCursor] == 'X' ) ) {
        nBase = 16u;
        ++iCursor;
    }
    if ( iCursor == entity.cchLength ) {
        return CY_FALSE;
    }

    unicode_code_point_t value = 0u;
    // Accumulate with a pre-multiply bound check; entities must resolve to a
    // Unicode scalar and may not smuggle an embedded NUL into engine text.
    for ( ; iCursor < entity.cchLength; ++iCursor ) {
        const u8 digit = nBase == 16u
            ? Char_HexValueAscii( entity.pData[iCursor] )
            : Char_DigitValueAscii( entity.pData[iCursor] );
        if ( digit == CY_CHAR_INVALID_DIGIT_VALUE || digit >= nBase ||
             value > ( CY_UNICODE_MAX - digit ) / nBase ) {
            return CY_FALSE;
        }
        value = value * nBase + digit;
    }
    if ( value == 0u || !Unicode_IsScalarValue( value ) ) {
        return CY_FALSE;
    }
    codePointOut = value;
    return CY_TRUE;
}

bool_t TagCreatesLineBreak( string_view_t tag ) noexcept
{
    usize iCursor = 0u;
    while ( iCursor < tag.cchLength && Char_IsWhitespaceAscii( tag.pData[iCursor] ) ) {
        ++iCursor;
    }
    if ( iCursor < tag.cchLength && tag.pData[iCursor] == '/' ) {
        ++iCursor;
    }
    while ( iCursor < tag.cchLength && Char_IsWhitespaceAscii( tag.pData[iCursor] ) ) {
        ++iCursor;
    }
    const usize iNameStart = iCursor;
    while ( iCursor < tag.cchLength && Char_IsAlphaAscii( tag.pData[iCursor] ) ) {
        ++iCursor;
    }
    const string_view_t name{
        tag.pData + iNameStart,
        iCursor - iNameStart
    };
    return StringView_EqualsInsensitiveAscii( name, StringView_FromCString( "br" ) ) ||
           StringView_EqualsInsensitiveAscii( name, StringView_FromCString( "p" ) ) ||
           StringView_EqualsInsensitiveAscii( name, StringView_FromCString( "div" ) ) ||
           StringView_EqualsInsensitiveAscii( name, StringView_FromCString( "li" ) );
}

} // namespace

html_text_result_t StringHtml_EncodeEntities(
    string_view_t text,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( !StringView_IsValid( text ) || !HtmlDestinationIsValid( pDest, cchDest ) ||
         ( flags & ~HTML_TEXT_VALID_FLAGS ) != 0u ) {
        return InvalidHtmlResult(
            html_text_status_t::INVALID_ARGUMENT,
            pDest,
            cchDest );
    }

    html_writer_t writer = MakeHtmlWriter( pDest, cchDest );
    usize iCursor = 0u;
    while ( iCursor < text.cchLength ) {
        unicode_code_point_t codePoint = 0u;
        const unicode_result_t decoded = Unicode_DecodeUtf8(
            { text.pData + iCursor, text.cchLength - iCursor },
            &codePoint );
        if ( decoded.status != unicode_status_t::OK ) {
            return FinishHtmlWriter(
                writer,
                iCursor,
                html_text_status_t::INVALID_CODE_POINT,
                iCursor );
        }

        const char *pEntity = nullptr;
        usize cchEntity = 0u;
        if ( codePoint == '&' ) {
            pEntity = "&amp;";
            cchEntity = 5u;
        } else if ( codePoint == '<' ) {
            pEntity = "&lt;";
            cchEntity = 4u;
        } else if ( codePoint == '>' ) {
            pEntity = "&gt;";
            cchEntity = 4u;
        } else if ( codePoint == '"' &&
                    ( flags & HTML_TEXT_FLAG_ENCODE_QUOTES ) != 0u ) {
            pEntity = "&quot;";
            cchEntity = 6u;
        } else if ( codePoint == '\'' &&
                    ( flags & HTML_TEXT_FLAG_ENCODE_QUOTES ) != 0u ) {
            pEntity = "&apos;";
            cchEntity = 6u;
        }

        if ( pEntity != nullptr ) {
            HtmlWriter_Write( writer, pEntity, cchEntity );
        } else {
            HtmlWriter_Write(
                writer,
                text.pData + iCursor,
                decoded.nInputConsumed );
        }
        iCursor += decoded.nInputConsumed;
    }
    return FinishHtmlWriter( writer, text.cchLength );
}

html_text_result_t StringHtml_DecodeEntities(
    string_view_t text,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( !StringView_IsValid( text ) || !HtmlDestinationIsValid( pDest, cchDest ) ||
         ( flags & ~HTML_TEXT_VALID_FLAGS ) != 0u ) {
        return InvalidHtmlResult(
            html_text_status_t::INVALID_ARGUMENT,
            pDest,
            cchDest );
    }

    html_writer_t writer = MakeHtmlWriter( pDest, cchDest );
    usize iCursor = 0u;
    while ( iCursor < text.cchLength ) {
        if ( text.pData[iCursor] != '&' ) {
            unicode_code_point_t codePoint = 0u;
            const unicode_result_t decoded = Unicode_DecodeUtf8(
                { text.pData + iCursor, text.cchLength - iCursor },
                &codePoint );
            if ( decoded.status != unicode_status_t::OK ) {
                return FinishHtmlWriter(
                    writer,
                    iCursor,
                    html_text_status_t::INVALID_CODE_POINT,
                    iCursor );
            }
            HtmlWriter_Write(
                writer,
                text.pData + iCursor,
                decoded.nInputConsumed );
            iCursor += decoded.nInputConsumed;
            continue;
        }

        const usize iEntityStart = iCursor;
        // HTML entities are bounded by their semicolon. This utility intentionally
        // accepts only the small deterministic entity vocabulary listed below.
        usize iSemicolon = iCursor + 1u;
        while ( iSemicolon < text.cchLength && text.pData[iSemicolon] != ';' ) {
            ++iSemicolon;
        }
        if ( iSemicolon >= text.cchLength ) {
            return FinishHtmlWriter(
                writer,
                iEntityStart,
                html_text_status_t::INVALID_ENTITY,
                iEntityStart );
        }

        const string_view_t entity{
            text.pData + iEntityStart + 1u,
            iSemicolon - iEntityStart - 1u
        };
        unicode_code_point_t codePoint = 0u;
        bool_t bKnown = CY_TRUE;
        if ( ViewEqualsLiteral( entity, "amp", 3u ) ) {
            codePoint = '&';
        } else if ( ViewEqualsLiteral( entity, "lt", 2u ) ) {
            codePoint = '<';
        } else if ( ViewEqualsLiteral( entity, "gt", 2u ) ) {
            codePoint = '>';
        } else if ( ViewEqualsLiteral( entity, "quot", 4u ) ) {
            codePoint = '"';
        } else if ( ViewEqualsLiteral( entity, "apos", 4u ) ) {
            codePoint = '\'';
        } else if ( ( flags & HTML_TEXT_FLAG_DECODE_NUMERIC ) != 0u &&
                    ParseNumericEntity( entity, codePoint ) ) {
        } else {
            bKnown = CY_FALSE;
        }

        if ( !bKnown ) {
            const html_text_status_t status =
                entity.cchLength > 0u && entity.pData[0] == '#'
                    ? html_text_status_t::INVALID_CODE_POINT
                    : html_text_status_t::INVALID_ENTITY;
            return FinishHtmlWriter( writer, iEntityStart, status, iEntityStart );
        }

        char encoded[4]{};
        const unicode_result_t encodedResult = Unicode_EncodeUtf8(
            codePoint,
            encoded,
            sizeof( encoded ) );
        if ( encodedResult.status != unicode_status_t::OK ) {
            return FinishHtmlWriter(
                writer,
                iEntityStart,
                html_text_status_t::INVALID_CODE_POINT,
                iEntityStart );
        }
        HtmlWriter_Write( writer, encoded, encodedResult.nOutputWritten );
        iCursor = iSemicolon + 1u;
    }
    return FinishHtmlWriter( writer, text.cchLength );
}

html_text_result_t StringHtml_StripTags(
    string_view_t text,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( !StringView_IsValid( text ) || !HtmlDestinationIsValid( pDest, cchDest ) ||
         ( flags & ~HTML_TEXT_VALID_FLAGS ) != 0u ) {
        return InvalidHtmlResult(
            html_text_status_t::INVALID_ARGUMENT,
            pDest,
            cchDest );
    }

    const unicode_result_t validated = Unicode_ValidateUtf8( text );
    if ( validated.status != unicode_status_t::OK ) {
        return InvalidHtmlResult(
            html_text_status_t::INVALID_CODE_POINT,
            pDest,
            cchDest,
            validated.iError );
    }

    html_writer_t writer = MakeHtmlWriter( pDest, cchDest );
    usize iCursor = 0u;
    bool_t bPreviousWhitespace = CY_FALSE;
    bool_t bPreviousLineBreak = CY_FALSE;
    while ( iCursor < text.cchLength ) {
        if ( text.pData[iCursor] == '<' ) {
            const usize iTagStart = iCursor;
            // Quote tracking prevents a '>' inside an attribute value from ending
            // the tag early. This is tag stripping, not a general HTML parser.
            char chQuote = '\0';
            ++iCursor;
            while ( iCursor < text.cchLength ) {
                const char ch = text.pData[iCursor];
                if ( chQuote != '\0' ) {
                    if ( ch == chQuote ) {
                        chQuote = '\0';
                    }
                } else if ( ch == '"' || ch == '\'' ) {
                    chQuote = ch;
                } else if ( ch == '>' ) {
                    break;
                }
                ++iCursor;
            }
            if ( iCursor >= text.cchLength ) {
                return FinishHtmlWriter(
                    writer,
                    iTagStart,
                    html_text_status_t::UNTERMINATED_TAG,
                    iTagStart );
            }

            const string_view_t tag{
                text.pData + iTagStart + 1u,
                iCursor - iTagStart - 1u
            };
            if ( ( flags & HTML_TEXT_FLAG_PRESERVE_LINE_BREAKS ) != 0u &&
                 TagCreatesLineBreak( tag ) && !bPreviousLineBreak ) {
                constexpr char newline = '\n';
                HtmlWriter_Write( writer, &newline, 1u );
                bPreviousLineBreak = CY_TRUE;
                bPreviousWhitespace = CY_TRUE;
            }
            ++iCursor;
            continue;
        }

        const char ch = text.pData[iCursor];
        if ( Char_IsWhitespaceAscii( ch ) ) {
            const bool_t bLineBreak = ch == '\r' || ch == '\n';
            if ( bLineBreak &&
                 ( flags & HTML_TEXT_FLAG_PRESERVE_LINE_BREAKS ) != 0u ) {
                if ( !bPreviousLineBreak ) {
                    constexpr char newline = '\n';
                    HtmlWriter_Write( writer, &newline, 1u );
                }
                bPreviousLineBreak = CY_TRUE;
                bPreviousWhitespace = CY_TRUE;
            } else if ( ( flags & HTML_TEXT_FLAG_COLLAPSE_WHITESPACE ) != 0u ) {
                if ( !bPreviousWhitespace ) {
                    constexpr char space = ' ';
                    HtmlWriter_Write( writer, &space, 1u );
                }
                bPreviousWhitespace = CY_TRUE;
                bPreviousLineBreak = CY_FALSE;
            } else {
                HtmlWriter_Write( writer, &ch, 1u );
                bPreviousWhitespace = CY_TRUE;
                bPreviousLineBreak = bLineBreak;
            }
            ++iCursor;
            continue;
        }

        unicode_code_point_t codePoint = 0u;
        const unicode_result_t decoded = Unicode_DecodeUtf8(
            { text.pData + iCursor, text.cchLength - iCursor },
            &codePoint );
        HtmlWriter_Write( writer, text.pData + iCursor, decoded.nInputConsumed );
        iCursor += decoded.nInputConsumed;
        bPreviousWhitespace = CY_FALSE;
        bPreviousLineBreak = CY_FALSE;
    }
    return FinishHtmlWriter( writer, text.cchLength );
}

} // namespace cypher::common
