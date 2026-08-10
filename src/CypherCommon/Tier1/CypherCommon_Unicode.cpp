//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Unicode.cpp
//  Purpose: Implements strict bounded Unicode encoding primitives.
//  Details: UTF-8 decoding rejects overlong forms, surrogates, invalid continuation
//           bytes, and values beyond U+10FFFF. Transcoders never write partial code
//           points and preserve exact consumed, written, required, and error offsets.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Unicode.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_UNICODE_FLAG_MASK =
    UNICODE_FLAG_REPLACE_INVALID |
    UNICODE_FLAG_REJECT_NUL |
    UNICODE_FLAG_WRITE_TERMINATOR;

struct utf8_decode_t {
    unicode_status_t status{ unicode_status_t::OK };
    unicode_code_point_t codePoint{ 0u };
    usize cbConsumed{ 0u };
    usize cbReplacementAdvance{ 0u };
    usize iError{ 0u };
};

bool_t IsContinuationByte( byte value ) noexcept
{
    return ( value & 0xC0u ) == 0x80u;
}

utf8_decode_t DecodeUtf8At( string_view_t text, usize iStart ) noexcept
{
    if ( iStart >= text.cchLength ) {
        return {
            unicode_status_t::TRUNCATED_SEQUENCE,
            0u,
            0u,
            0u,
            iStart
        };
    }

    const auto *pBytes = reinterpret_cast<const byte *>( text.pData );
    const byte first = pBytes[iStart];
    if ( first <= 0x7Fu ) {
        return { unicode_status_t::OK, first, 1u, 1u, iStart };
    }

    usize cbSequence = 0u;
    unicode_code_point_t codePoint = 0u;
    unicode_code_point_t nMinimum = 0u;
    if ( first >= 0xC2u && first <= 0xDFu ) {
        cbSequence = 2u;
        codePoint = first & 0x1Fu;
        nMinimum = 0x80u;
    } else if ( first >= 0xE0u && first <= 0xEFu ) {
        cbSequence = 3u;
        codePoint = first & 0x0Fu;
        nMinimum = 0x800u;
    } else if ( first >= 0xF0u && first <= 0xF4u ) {
        cbSequence = 4u;
        codePoint = first & 0x07u;
        nMinimum = 0x10000u;
    } else {
        return {
            unicode_status_t::INVALID_SEQUENCE,
            0u,
            0u,
            1u,
            iStart
        };
    }

    const usize cbRemaining = text.cchLength - iStart;
    if ( cbRemaining < cbSequence ) {
        return {
            unicode_status_t::TRUNCATED_SEQUENCE,
            0u,
            0u,
            cbRemaining,
            text.cchLength
        };
    }

    for ( usize iByte = 1u; iByte < cbSequence; ++iByte ) {
        const byte continuation = pBytes[iStart + iByte];
        if ( !IsContinuationByte( continuation ) ) {
            return {
                unicode_status_t::INVALID_SEQUENCE,
                0u,
                0u,
                1u,
                iStart + iByte
            };
        }
        codePoint = ( codePoint << 6u ) | ( continuation & 0x3Fu );
    }

    if ( codePoint < nMinimum || !Unicode_IsScalarValue( codePoint ) ) {
        return {
            unicode_status_t::INVALID_CODE_POINT,
            0u,
            0u,
            cbSequence,
            iStart
        };
    }
    return {
        unicode_status_t::OK,
        codePoint,
        cbSequence,
        cbSequence,
        iStart
    };
}

usize Utf8EncodedLength( unicode_code_point_t codePoint ) noexcept
{
    if ( codePoint <= 0x7Fu ) {
        return 1u;
    }
    if ( codePoint <= 0x7FFu ) {
        return 2u;
    }
    if ( codePoint <= 0xFFFFu ) {
        return 3u;
    }
    return 4u;
}

void EncodeUtf8Unchecked(
    unicode_code_point_t codePoint,
    char *pDest ) noexcept
{
    if ( codePoint <= 0x7Fu ) {
        pDest[0] = static_cast<char>( codePoint );
    } else if ( codePoint <= 0x7FFu ) {
        pDest[0] = static_cast<char>( 0xC0u | ( codePoint >> 6u ) );
        pDest[1] = static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    } else if ( codePoint <= 0xFFFFu ) {
        pDest[0] = static_cast<char>( 0xE0u | ( codePoint >> 12u ) );
        pDest[1] = static_cast<char>(
            0x80u | ( ( codePoint >> 6u ) & 0x3Fu ) );
        pDest[2] = static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    } else {
        pDest[0] = static_cast<char>( 0xF0u | ( codePoint >> 18u ) );
        pDest[1] = static_cast<char>(
            0x80u | ( ( codePoint >> 12u ) & 0x3Fu ) );
        pDest[2] = static_cast<char>(
            0x80u | ( ( codePoint >> 6u ) & 0x3Fu ) );
        pDest[3] = static_cast<char>( 0x80u | ( codePoint & 0x3Fu ) );
    }
}

bool_t UnicodeFlagsAreValid( flags32_t flags ) noexcept
{
    return ( flags & ~CY_UNICODE_FLAG_MASK ) == 0u;
}

usize DataCapacity( usize nCapacity, flags32_t flags ) noexcept
{
    return ( flags & UNICODE_FLAG_WRITE_TERMINATOR ) != 0u
        ? ( nCapacity > 0u ? nCapacity - 1u : 0u )
        : nCapacity;
}

unicode_status_t FinalStatus(
    bool_t bTruncated,
    usize nRequired,
    usize nCapacity,
    flags32_t flags ) noexcept
{
    const usize nTerminator =
        ( flags & UNICODE_FLAG_WRITE_TERMINATOR ) != 0u ? 1u : 0u;
    const bool_t bCapacityOverflow = nRequired > CY_USIZE_MAX - nTerminator;
    return bTruncated || bCapacityOverflow ||
           nCapacity < nRequired + nTerminator
        ? unicode_status_t::OUTPUT_TRUNCATED
        : unicode_status_t::OK;
}

void TerminateUtf8(
    char *pDest,
    usize cchDest,
    usize cchWritten,
    flags32_t flags ) noexcept
{
    if ( ( flags & UNICODE_FLAG_WRITE_TERMINATOR ) != 0u &&
         pDest != nullptr && cchDest > 0u ) {
        pDest[cchWritten] = '\0';
    }
}

void TerminateUtf16(
    span_t<utf16_unit_t> dest,
    usize nWritten,
    flags32_t flags ) noexcept
{
    if ( ( flags & UNICODE_FLAG_WRITE_TERMINATOR ) != 0u &&
         dest.pData != nullptr && dest.nCount > 0u ) {
        dest.pData[nWritten] = 0u;
    }
}

void TerminateUtf32(
    span_t<unicode_code_point_t> dest,
    usize nWritten,
    flags32_t flags ) noexcept
{
    if ( ( flags & UNICODE_FLAG_WRITE_TERMINATOR ) != 0u &&
         dest.pData != nullptr && dest.nCount > 0u ) {
        dest.pData[nWritten] = 0u;
    }
}

} // namespace

bool_t Unicode_IsScalarValue( unicode_code_point_t codePoint ) noexcept
{
    return codePoint <= CY_UNICODE_MAX && !Unicode_IsSurrogate( codePoint );
}

bool_t Unicode_IsSurrogate( unicode_code_point_t codePoint ) noexcept
{
    return codePoint >= 0xD800u && codePoint <= 0xDFFFu;
}

unicode_result_t Unicode_ValidateUtf8( string_view_t text ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "Unicode_ValidateUtf8 requires a valid string view." );
    if ( !bValidText ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    usize iByte = 0u;
    while ( iByte < text.cchLength ) {
        const utf8_decode_t decoded = DecodeUtf8At( text, iByte );
        if ( decoded.status != unicode_status_t::OK ) {
            return {
                decoded.status,
                iByte,
                0u,
                0u,
                decoded.iError
            };
        }
        iByte += decoded.cbConsumed;
    }
    return { unicode_status_t::OK, text.cchLength, 0u, 0u, CY_STRING_VIEW_NPOS };
}

unicode_result_t Unicode_DecodeUtf8(
    string_view_t text,
    unicode_code_point_t *pCodePointOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pCodePointOut != nullptr;
    CY_ASSERT_MSG( bValidText, "Unicode_DecodeUtf8 requires a valid string view." );
    CY_ASSERT_MSG( bValidOutput, "Unicode_DecodeUtf8 requires output storage." );
    if ( !bValidText || !bValidOutput ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    const utf8_decode_t decoded = DecodeUtf8At( text, 0u );
    if ( decoded.status != unicode_status_t::OK ) {
        return { decoded.status, 0u, 0u, 1u, decoded.iError };
    }
    *pCodePointOut = decoded.codePoint;
    return {
        unicode_status_t::OK,
        decoded.cbConsumed,
        1u,
        1u,
        CY_STRING_VIEW_NPOS
    };
}

unicode_result_t Unicode_EncodeUtf8(
    unicode_code_point_t codePoint,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    CY_ASSERT_MSG(
        bValidDestination,
        "Unicode_EncodeUtf8 requires a destination when capacity is nonzero." );
    if ( !bValidDestination ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }
    if ( !Unicode_IsScalarValue( codePoint ) ) {
        return { unicode_status_t::INVALID_CODE_POINT, 0u, 0u, 0u, 0u };
    }

    const usize cchRequired = Utf8EncodedLength( codePoint );
    if ( cchDest < cchRequired ) {
        return {
            unicode_status_t::OUTPUT_TRUNCATED,
            1u,
            0u,
            cchRequired,
            CY_STRING_VIEW_NPOS
        };
    }
    EncodeUtf8Unchecked( codePoint, pDest );
    return {
        unicode_status_t::OK,
        1u,
        cchRequired,
        cchRequired,
        CY_STRING_VIEW_NPOS
    };
}

unicode_result_t Unicode_CountUtf8CodePoints( string_view_t text ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG(
        bValidText,
        "Unicode_CountUtf8CodePoints requires a valid string view." );
    if ( !bValidText ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    usize iByte = 0u;
    usize nCodePoints = 0u;
    while ( iByte < text.cchLength ) {
        const utf8_decode_t decoded = DecodeUtf8At( text, iByte );
        if ( decoded.status != unicode_status_t::OK ) {
            return {
                decoded.status,
                iByte,
                nCodePoints,
                nCodePoints,
                decoded.iError
            };
        }
        iByte += decoded.cbConsumed;
        ++nCodePoints;
    }
    return {
        unicode_status_t::OK,
        text.cchLength,
        nCodePoints,
        nCodePoints,
        CY_STRING_VIEW_NPOS
    };
}

unicode_result_t Unicode_Utf8ToUtf16(
    string_view_t source,
    flags32_t flags,
    span_t<utf16_unit_t> dest ) noexcept
{
    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidDest = Span_IsValid( dest );
    const bool_t bValidFlags = UnicodeFlagsAreValid( flags );
    if ( !bValidSource || !bValidDest || !bValidFlags ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    const usize nDataCapacity = DataCapacity( dest.nCount, flags );
    usize iByte = 0u;
    usize nWritten = 0u;
    usize nRequired = 0u;
    bool_t bTruncated = CY_FALSE;
    while ( iByte < source.cchLength ) {
        utf8_decode_t decoded = DecodeUtf8At( source, iByte );
        if ( decoded.status != unicode_status_t::OK ) {
            if ( ( flags & UNICODE_FLAG_REPLACE_INVALID ) == 0u ) {
                TerminateUtf16( dest, nWritten, flags );
                return {
                    decoded.status,
                    iByte,
                    nWritten,
                    nRequired,
                    decoded.iError
                };
            }
            decoded.codePoint = CY_UNICODE_REPLACEMENT;
            decoded.cbConsumed = decoded.cbReplacementAdvance;
        }
        if ( decoded.codePoint == 0u &&
             ( flags & UNICODE_FLAG_REJECT_NUL ) != 0u ) {
            TerminateUtf16( dest, nWritten, flags );
            return {
                unicode_status_t::INVALID_CODE_POINT,
                iByte,
                nWritten,
                nRequired,
                iByte
            };
        }

        const usize nUnits = decoded.codePoint <= 0xFFFFu ? 1u : 2u;
        if ( nWritten <= nDataCapacity && nUnits <= nDataCapacity - nWritten ) {
            if ( nUnits == 1u ) {
                dest.pData[nWritten] = static_cast<utf16_unit_t>( decoded.codePoint );
            } else {
                const unicode_code_point_t adjusted = decoded.codePoint - 0x10000u;
                dest.pData[nWritten] = static_cast<utf16_unit_t>(
                    0xD800u + ( adjusted >> 10u ) );
                dest.pData[nWritten + 1u] = static_cast<utf16_unit_t>(
                    0xDC00u + ( adjusted & 0x3FFu ) );
            }
            nWritten += nUnits;
        } else {
            bTruncated = CY_TRUE;
        }
        nRequired += nUnits;
        iByte += decoded.cbConsumed;
    }

    TerminateUtf16( dest, nWritten, flags );
    return {
        FinalStatus( bTruncated, nRequired, dest.nCount, flags ),
        source.cchLength,
        nWritten,
        nRequired,
        CY_STRING_VIEW_NPOS
    };
}

unicode_result_t Unicode_Utf16ToUtf8(
    span_t<const utf16_unit_t> source,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidSource = Span_IsValid( source );
    const bool_t bValidDest = pDest != nullptr || cchDest == 0u;
    const bool_t bValidFlags = UnicodeFlagsAreValid( flags );
    if ( !bValidSource || !bValidDest || !bValidFlags ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    const usize cchDataCapacity = DataCapacity( cchDest, flags );
    usize iUnit = 0u;
    usize cchWritten = 0u;
    usize cchRequired = 0u;
    bool_t bTruncated = CY_FALSE;
    while ( iUnit < source.nCount ) {
        unicode_code_point_t codePoint = source.pData[iUnit];
        usize nUnitsConsumed = 1u;
        unicode_status_t sequenceStatus = unicode_status_t::OK;
        usize iError = iUnit;
        if ( codePoint >= 0xD800u && codePoint <= 0xDBFFu ) {
            if ( iUnit + 1u >= source.nCount ) {
                sequenceStatus = unicode_status_t::TRUNCATED_SEQUENCE;
                iError = source.nCount;
            } else {
                const utf16_unit_t low = source.pData[iUnit + 1u];
                if ( low < 0xDC00u || low > 0xDFFFu ) {
                    sequenceStatus = unicode_status_t::INVALID_SEQUENCE;
                    iError = iUnit + 1u;
                } else {
                    codePoint = 0x10000u +
                        ( ( codePoint - 0xD800u ) << 10u ) +
                        ( low - 0xDC00u );
                    nUnitsConsumed = 2u;
                }
            }
        } else if ( codePoint >= 0xDC00u && codePoint <= 0xDFFFu ) {
            sequenceStatus = unicode_status_t::INVALID_SEQUENCE;
        }

        if ( sequenceStatus != unicode_status_t::OK ) {
            if ( ( flags & UNICODE_FLAG_REPLACE_INVALID ) == 0u ) {
                TerminateUtf8( pDest, cchDest, cchWritten, flags );
                return {
                    sequenceStatus,
                    iUnit,
                    cchWritten,
                    cchRequired,
                    iError
                };
            }
            codePoint = CY_UNICODE_REPLACEMENT;
        }
        if ( codePoint == 0u &&
             ( flags & UNICODE_FLAG_REJECT_NUL ) != 0u ) {
            TerminateUtf8( pDest, cchDest, cchWritten, flags );
            return {
                unicode_status_t::INVALID_CODE_POINT,
                iUnit,
                cchWritten,
                cchRequired,
                iUnit
            };
        }

        const usize cchCodePoint = Utf8EncodedLength( codePoint );
        if ( cchWritten <= cchDataCapacity &&
             cchCodePoint <= cchDataCapacity - cchWritten ) {
            EncodeUtf8Unchecked( codePoint, pDest + cchWritten );
            cchWritten += cchCodePoint;
        } else {
            bTruncated = CY_TRUE;
        }
        cchRequired += cchCodePoint;
        iUnit += nUnitsConsumed;
    }

    TerminateUtf8( pDest, cchDest, cchWritten, flags );
    return {
        FinalStatus( bTruncated, cchRequired, cchDest, flags ),
        source.nCount,
        cchWritten,
        cchRequired,
        CY_STRING_VIEW_NPOS
    };
}

unicode_result_t Unicode_Utf8ToUtf32(
    string_view_t source,
    flags32_t flags,
    span_t<unicode_code_point_t> dest ) noexcept
{
    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidDest = Span_IsValid( dest );
    const bool_t bValidFlags = UnicodeFlagsAreValid( flags );
    if ( !bValidSource || !bValidDest || !bValidFlags ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    const usize nDataCapacity = DataCapacity( dest.nCount, flags );
    usize iByte = 0u;
    usize nWritten = 0u;
    usize nRequired = 0u;
    bool_t bTruncated = CY_FALSE;
    while ( iByte < source.cchLength ) {
        utf8_decode_t decoded = DecodeUtf8At( source, iByte );
        if ( decoded.status != unicode_status_t::OK ) {
            if ( ( flags & UNICODE_FLAG_REPLACE_INVALID ) == 0u ) {
                TerminateUtf32( dest, nWritten, flags );
                return {
                    decoded.status,
                    iByte,
                    nWritten,
                    nRequired,
                    decoded.iError
                };
            }
            decoded.codePoint = CY_UNICODE_REPLACEMENT;
            decoded.cbConsumed = decoded.cbReplacementAdvance;
        }
        if ( decoded.codePoint == 0u &&
             ( flags & UNICODE_FLAG_REJECT_NUL ) != 0u ) {
            TerminateUtf32( dest, nWritten, flags );
            return {
                unicode_status_t::INVALID_CODE_POINT,
                iByte,
                nWritten,
                nRequired,
                iByte
            };
        }

        if ( nWritten < nDataCapacity ) {
            dest.pData[nWritten++] = decoded.codePoint;
        } else {
            bTruncated = CY_TRUE;
        }
        ++nRequired;
        iByte += decoded.cbConsumed;
    }

    TerminateUtf32( dest, nWritten, flags );
    return {
        FinalStatus( bTruncated, nRequired, dest.nCount, flags ),
        source.cchLength,
        nWritten,
        nRequired,
        CY_STRING_VIEW_NPOS
    };
}

unicode_result_t Unicode_Utf32ToUtf8(
    span_t<const unicode_code_point_t> source,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidSource = Span_IsValid( source );
    const bool_t bValidDest = pDest != nullptr || cchDest == 0u;
    const bool_t bValidFlags = UnicodeFlagsAreValid( flags );
    if ( !bValidSource || !bValidDest || !bValidFlags ) {
        return { unicode_status_t::INVALID_ARGUMENT, 0u, 0u, 0u, 0u };
    }

    const usize cchDataCapacity = DataCapacity( cchDest, flags );
    usize cchWritten = 0u;
    usize cchRequired = 0u;
    bool_t bTruncated = CY_FALSE;
    for ( usize iCodePoint = 0u; iCodePoint < source.nCount; ++iCodePoint ) {
        unicode_code_point_t codePoint = source.pData[iCodePoint];
        if ( !Unicode_IsScalarValue( codePoint ) ) {
            if ( ( flags & UNICODE_FLAG_REPLACE_INVALID ) == 0u ) {
                TerminateUtf8( pDest, cchDest, cchWritten, flags );
                return {
                    unicode_status_t::INVALID_CODE_POINT,
                    iCodePoint,
                    cchWritten,
                    cchRequired,
                    iCodePoint
                };
            }
            codePoint = CY_UNICODE_REPLACEMENT;
        }
        if ( codePoint == 0u &&
             ( flags & UNICODE_FLAG_REJECT_NUL ) != 0u ) {
            TerminateUtf8( pDest, cchDest, cchWritten, flags );
            return {
                unicode_status_t::INVALID_CODE_POINT,
                iCodePoint,
                cchWritten,
                cchRequired,
                iCodePoint
            };
        }

        const usize cchCodePoint = Utf8EncodedLength( codePoint );
        if ( cchWritten <= cchDataCapacity &&
             cchCodePoint <= cchDataCapacity - cchWritten ) {
            EncodeUtf8Unchecked( codePoint, pDest + cchWritten );
            cchWritten += cchCodePoint;
        } else {
            bTruncated = CY_TRUE;
        }
        cchRequired += cchCodePoint;
    }

    TerminateUtf8( pDest, cchDest, cchWritten, flags );
    return {
        FinalStatus( bTruncated, cchRequired, cchDest, flags ),
        source.nCount,
        cchWritten,
        cchRequired,
        CY_STRING_VIEW_NPOS
    };
}

} // namespace cypher::common
