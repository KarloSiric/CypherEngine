//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringParse.cpp
//  Purpose: Implements deterministic bounded text-to-value conversion.
//  Details: StringParse converts non-owning text into primitive values without
//           allocation, exceptions, locale state, or null-termination requirements.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringParse.h"

#include "CypherCommon_Assert.h"
#include "CypherCommon_Char.h"

#include <charconv>
#include <cmath>
#include <system_error>

namespace cypher::common
{

namespace
{

// Converts one ASCII digit or letter into its base-36 digit value.
static u8 StringParse_DigitValueAscii( char ch ) noexcept
{
    if ( Char_IsDigitAscii( ch ) ) {
        return static_cast<u8>( ch - '0' );
    }

    if ( Char_IsUpperAscii( ch ) ) {
        return static_cast<u8>( ch - 'A' + 10 );
    }

    if ( Char_IsLowerAscii( ch ) ) {
        return static_cast<u8>( ch - 'a' + 10 );
    }

    return CY_CHAR_INVALID_DIGIT_VALUE;
}

// Advances over ASCII whitespace without reading beyond the bounded view.
static usize StringParse_SkipWhitespace(
    string_view_t text,
    usize iCursor ) noexcept
{
    while ( iCursor < text.cchLength &&
            Char_IsWhitespaceAscii( text.pData[iCursor] ) ) {
        ++iCursor;
    }

    return iCursor;
}

// Locates the first value byte under the parser's leading-whitespace policy.
static usize StringParse_ValueBegin(
    string_view_t text,
    flags32_t flags ) noexcept
{
    if ( ( flags & STRING_PARSE_FLAG_TRIM_WHITESPACE ) == 0u ) {
        return 0u;
    }

    return StringParse_SkipWhitespace( text, 0u );
}

static bool_t StringParse_MatchesBoolLiteral(
    string_view_t text,
    usize iBegin,
    const char *pLiteral,
    usize cchLiteral,
    bool_t bCaseInsensitive ) noexcept
{
    if ( ( text.cchLength - iBegin ) < cchLiteral ) {
        return CY_FALSE;
    }

    for ( usize i = 0u; i < cchLiteral; ++i ) {
        const char chText = bCaseInsensitive
            ? Char_ToLowerAscii( text.pData[iBegin + i] )
            : text.pData[iBegin + i];

        if ( chText != pLiteral[i] ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

static i64 StringParse_SaturatingAddI64( i64 nLeft, i64 nRight ) noexcept
{
    if ( nRight > 0 && nLeft > CY_I64_MAX - nRight ) {
        return CY_I64_MAX;
    }

    if ( nRight < 0 && nLeft < CY_I64_MIN - nRight ) {
        return CY_I64_MIN;
    }

    return nLeft + nRight;
}

static i64 StringParse_SaturatingDifference(
    usize nLeft,
    usize nRight ) noexcept
{
    const usize nI64Max = static_cast<usize>( CY_I64_MAX );

    if ( nLeft >= nRight ) {
        const usize nDifference = nLeft - nRight;
        return nDifference > nI64Max
            ? CY_I64_MAX
            : static_cast<i64>( nDifference );
    }

    const usize nDifference = nRight - nLeft;
    return nDifference > nI64Max
        ? CY_I64_MIN
        : -static_cast<i64>( nDifference );
}

// Extracts a saturated decimal exponent from an already matched float token.
static i64 StringParse_ExplicitDecimalExponent(
    string_view_t text,
    usize iExponent,
    usize iEnd ) noexcept
{
    bool_t bNegative = CY_FALSE;

    if ( iExponent < iEnd &&
         ( text.pData[iExponent] == '+' || text.pData[iExponent] == '-' ) ) {
        bNegative = text.pData[iExponent] == '-';
        ++iExponent;
    }

    i64 nExponent = 0;
    for ( usize i = iExponent; i < iEnd; ++i ) {
        const i64 nDigit = static_cast<i64>( text.pData[i] - '0' );
        if ( nExponent > ( CY_I64_MAX - nDigit ) / 10 ) {
            return bNegative ? CY_I64_MIN : CY_I64_MAX;
        }

        nExponent = ( nExponent * 10 ) + nDigit;
    }

    return bNegative ? -nExponent : nExponent;
}

// Distinguishes values below the representable range from values above it.
static string_parse_status_t StringParse_FloatRangeStatus(
    string_view_t text,
    usize iBegin,
    usize iEnd ) noexcept
{
    usize iCursor = iBegin;
    const bool_t bNegative =
        iCursor < iEnd && text.pData[iCursor] == '-';

    if ( iCursor < iEnd &&
         ( text.pData[iCursor] == '+' || text.pData[iCursor] == '-' ) ) {
        ++iCursor;
    }

    bool_t bBeforePoint = CY_TRUE;
    usize cDigitsBeforePoint = 0u;
    usize cDigits = 0u;
    usize iFirstNonZeroDigit = CY_INVALID_SIZE;
    usize iExponent = iEnd;

    for ( ; iCursor < iEnd; ++iCursor ) {
        const char ch = text.pData[iCursor];

        if ( ch == 'e' || ch == 'E' ) {
            iExponent = iCursor + 1u;
            break;
        }

        if ( ch == '.' ) {
            bBeforePoint = CY_FALSE;
            continue;
        }

        if ( Char_IsDigitAscii( ch ) ) {
            if ( bBeforePoint ) {
                ++cDigitsBeforePoint;
            }

            if ( iFirstNonZeroDigit == CY_INVALID_SIZE && ch != '0' ) {
                iFirstNonZeroDigit = cDigits;
            }

            ++cDigits;
        }
    }

    if ( iFirstNonZeroDigit == CY_INVALID_SIZE ) {
        return string_parse_status_t::NUMERIC_UNDERFLOW;
    }

    const i64 nBaseExponent = StringParse_SaturatingDifference(
        cDigitsBeforePoint,
        iFirstNonZeroDigit + 1u );
    const i64 nExplicitExponent = iExponent < iEnd
        ? StringParse_ExplicitDecimalExponent( text, iExponent, iEnd )
        : 0;
    const i64 nAdjustedExponent = StringParse_SaturatingAddI64(
        nBaseExponent,
        nExplicitExponent );

    if ( nAdjustedExponent < 0 || bNegative ) {
        return string_parse_status_t::NUMERIC_UNDERFLOW;
    }

    return string_parse_status_t::NUMERIC_OVERFLOW;
}

template<typename float_t>
static string_parse_result_t StringParse_Float(
    string_view_t text,
    flags32_t flags,
    float_t *pValueOut ) noexcept
{
    usize iValueBegin = StringParse_ValueBegin( text, flags );
    if ( iValueBegin == text.cchLength ) {
        return {
            string_parse_status_t::EMPTY_INPUT,
            iValueBegin,
            iValueBegin
        };
    }

    usize iConversionBegin = iValueBegin;
    if ( text.pData[iConversionBegin] == '+' ) {
        if ( ( flags & STRING_PARSE_FLAG_ALLOW_PLUS_SIGN ) == 0u ) {
            return {
                string_parse_status_t::INVALID_CHARACTER,
                iConversionBegin,
                iConversionBegin
            };
        }

        ++iConversionBegin;
        if ( iConversionBegin == text.cchLength ||
             text.pData[iConversionBegin] == '+' ||
             text.pData[iConversionBegin] == '-' ) {
            return {
                string_parse_status_t::INVALID_CHARACTER,
                iConversionBegin,
                iConversionBegin
            };
        }
    }

    float_t nValue{};
    const char *pFirst = text.pData + iConversionBegin;
    const char *pLast = text.pData + text.cchLength;
    const std::from_chars_result conversion = std::from_chars(
        pFirst,
        pLast,
        nValue,
        std::chars_format::general );
    const usize iParsedEnd = static_cast<usize>( conversion.ptr - text.pData );

    if ( conversion.ec == std::errc::invalid_argument ) {
        return {
            string_parse_status_t::INVALID_CHARACTER,
            iConversionBegin,
            iConversionBegin
        };
    }

    if ( conversion.ec == std::errc::result_out_of_range ) {
        return {
            StringParse_FloatRangeStatus( text, iValueBegin, iParsedEnd ),
            iParsedEnd,
            iValueBegin
        };
    }

    if ( !std::isfinite( nValue ) &&
         ( flags & STRING_PARSE_FLAG_ALLOW_NON_FINITE_FLOAT ) == 0u ) {
        return {
            string_parse_status_t::NON_FINITE_VALUE,
            iParsedEnd,
            iValueBegin
        };
    }

    usize iCursor = iParsedEnd;
    if ( ( flags & STRING_PARSE_FLAG_TRIM_WHITESPACE ) != 0u ) {
        iCursor = StringParse_SkipWhitespace( text, iCursor );
    }

    if ( iCursor < text.cchLength &&
         ( flags & STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS ) == 0u ) {
        return {
            string_parse_status_t::TRAILING_CHARACTERS,
            iCursor,
            iCursor
        };
    }

    *pValueOut = nValue;
    return {
        string_parse_status_t::OK,
        iCursor,
        CY_INVALID_SIZE
    };
}

} // namespace

bool_t StringParse_Succeeded( string_parse_result_t result ) noexcept
{
    return result.status == string_parse_status_t::OK;
}

const char *StringParse_StatusName( string_parse_status_t status ) noexcept
{
    switch ( status ) {
        case string_parse_status_t::OK:
            return "OK";
        case string_parse_status_t::EMPTY_INPUT:
            return "EMPTY_INPUT";
        case string_parse_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case string_parse_status_t::INVALID_BASE:
            return "INVALID_BASE";
        case string_parse_status_t::INVALID_CHARACTER:
            return "INVALID_CHARACTER";
        case string_parse_status_t::NUMERIC_OVERFLOW:
            return "NUMERIC_OVERFLOW";
        case string_parse_status_t::NUMERIC_UNDERFLOW:
            return "NUMERIC_UNDERFLOW";
        case string_parse_status_t::TRAILING_CHARACTERS:
            return "TRAILING_CHARACTERS";
        case string_parse_status_t::NON_FINITE_VALUE:
            return "NON_FINITE_VALUE";
    }

    // Preserve the non-null API contract for invalid enum values.
    return "UNKNOWN";
}

string_parse_result_t StringParse_U64(
    string_view_t text,
    const string_parse_options_t &options,
    u64 *pValueOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pValueOut != nullptr;
    const bool_t bValidFlags =
        ( options.flags & ~STRING_PARSE_VALID_FLAGS ) == 0u;
    const bool_t bValidBase =
        options.nBase == 0u ||
        ( options.nBase >= 2u && options.nBase <= 36u );

    CY_ASSERT_MSG(
        bValidText,
        "StringParse_U64 requires a valid source view." );
    CY_ASSERT_MSG(
        bValidOutput,
        "StringParse_U64 requires non-null output storage." );
    CY_ASSERT_MSG(
        bValidFlags,
        "StringParse_U64 received unsupported parsing flags." );
    CY_ASSERT_MSG(
        bValidBase,
        "StringParse_U64 requires base zero or a base in [2, 36]." );

    if ( !bValidText || !bValidOutput || !bValidFlags ) {
        return { string_parse_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    if ( !bValidBase ) {
        return { string_parse_status_t::INVALID_BASE, 0u, 0u };
    }

    usize iCursor = StringParse_ValueBegin( text, options.flags );
    if ( iCursor == text.cchLength ) {
        return {
            string_parse_status_t::EMPTY_INPUT,
            iCursor,
            iCursor
        };
    }

    if ( text.pData[iCursor] == '+' ) {
        if ( ( options.flags & STRING_PARSE_FLAG_ALLOW_PLUS_SIGN ) == 0u ) {
            return {
                string_parse_status_t::INVALID_CHARACTER,
                iCursor,
                iCursor
            };
        }

        ++iCursor;
        if ( iCursor == text.cchLength ) {
            return {
                string_parse_status_t::INVALID_CHARACTER,
                iCursor,
                iCursor
            };
        }
    }

    u8 nBase = options.nBase == 0u ? 10u : options.nBase;
    const bool_t bAllowPrefix =
        ( options.flags & STRING_PARSE_FLAG_ALLOW_BASE_PREFIX ) != 0u;
    const bool_t bHasPrefixBytes =
        ( text.cchLength - iCursor ) >= 2u;

    if ( bAllowPrefix && bHasPrefixBytes && text.pData[iCursor] == '0' ) {
        const char chPrefix = Char_ToLowerAscii( text.pData[iCursor + 1u] );
        u8 nPrefixBase = 0u;

        if ( chPrefix == 'b' ) {
            nPrefixBase = 2u;
        } else if ( chPrefix == 'o' ) {
            nPrefixBase = 8u;
        } else if ( chPrefix == 'x' ) {
            nPrefixBase = 16u;
        }

        if ( nPrefixBase != 0u ) {
            if ( options.nBase != 0u && options.nBase != nPrefixBase ) {
                return {
                    string_parse_status_t::INVALID_CHARACTER,
                    iCursor,
                    iCursor + 1u
                };
            }

            nBase = nPrefixBase;
            iCursor += 2u;
            if ( iCursor == text.cchLength ) {
                return {
                    string_parse_status_t::INVALID_CHARACTER,
                    iCursor,
                    iCursor
                };
            }
        }
    }

    const bool_t bAllowDigitSeparator =
        ( options.flags & STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR ) != 0u;
    u64 nValue = 0u;
    bool_t bParsedDigit = CY_FALSE;

    while ( iCursor < text.cchLength ) {
        const char ch = text.pData[iCursor];

        if ( ch == '_' && bAllowDigitSeparator ) {
            const bool_t bHasNextByte = ( iCursor + 1u ) < text.cchLength;
            if ( !bParsedDigit || !bHasNextByte ) {
                return {
                    string_parse_status_t::INVALID_CHARACTER,
                    iCursor,
                    iCursor
                };
            }

            const u8 nNextDigit =
                StringParse_DigitValueAscii( text.pData[iCursor + 1u] );
            if ( nNextDigit == CY_CHAR_INVALID_DIGIT_VALUE ||
                 nNextDigit >= nBase ) {
                return {
                    string_parse_status_t::INVALID_CHARACTER,
                    iCursor,
                    iCursor
                };
            }

            ++iCursor;
            continue;
        }

        const u8 nDigit = StringParse_DigitValueAscii( ch );
        if ( nDigit == CY_CHAR_INVALID_DIGIT_VALUE || nDigit >= nBase ) {
            break;
        }

        const u64 nDigit64 = static_cast<u64>( nDigit );
        const u64 nBase64 = static_cast<u64>( nBase );

        // Guard the multiply-add before it can wrap the accumulator.
        if ( nValue > ( CY_U64_MAX - nDigit64 ) / nBase64 ) {
            return {
                string_parse_status_t::NUMERIC_OVERFLOW,
                iCursor,
                iCursor
            };
        }

        nValue = ( nValue * nBase64 ) + nDigit64;
        bParsedDigit = CY_TRUE;
        ++iCursor;
    }

    if ( !bParsedDigit ) {
        return {
            string_parse_status_t::INVALID_CHARACTER,
            iCursor,
            iCursor
        };
    }

    if ( ( options.flags & STRING_PARSE_FLAG_TRIM_WHITESPACE ) != 0u ) {
        iCursor = StringParse_SkipWhitespace( text, iCursor );
    }

    if ( iCursor < text.cchLength &&
         ( options.flags & STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS ) == 0u ) {
        return {
            string_parse_status_t::TRAILING_CHARACTERS,
            iCursor,
            iCursor
        };
    }

    *pValueOut = nValue;
    return {
        string_parse_status_t::OK,
        iCursor,
        CY_INVALID_SIZE
    };
}

string_parse_result_t StringParse_I64(
    string_view_t text,
    const string_parse_options_t &options,
    i64 *pValueOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pValueOut != nullptr;
    const bool_t bValidFlags =
        ( options.flags & ~STRING_PARSE_VALID_FLAGS ) == 0u;
    const bool_t bValidBase =
        options.nBase == 0u ||
        ( options.nBase >= 2u && options.nBase <= 36u );

    CY_ASSERT_MSG(
        bValidText,
        "StringParse_I64 requires a valid source view." );
    CY_ASSERT_MSG(
        bValidOutput,
        "StringParse_I64 requires non-null output storage." );
    CY_ASSERT_MSG(
        bValidFlags,
        "StringParse_I64 received unsupported parsing flags." );
    CY_ASSERT_MSG(
        bValidBase,
        "StringParse_I64 requires base zero or a base in [2, 36]." );

    if ( !bValidText || !bValidOutput || !bValidFlags ) {
        return { string_parse_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    if ( !bValidBase ) {
        return { string_parse_status_t::INVALID_BASE, 0u, 0u };
    }

    const usize iSign = StringParse_ValueBegin( text, options.flags );
    if ( iSign == text.cchLength ) {
        return {
            string_parse_status_t::EMPTY_INPUT,
            iSign,
            iSign
        };
    }

    const bool_t bNegative = text.pData[iSign] == '-';
    u64 nMagnitude = 0u;

    if ( !bNegative ) {
        const string_parse_result_t result =
            StringParse_U64( text, options, &nMagnitude );
        if ( !StringParse_Succeeded( result ) ) {
            return result;
        }

        if ( nMagnitude > static_cast<u64>( CY_I64_MAX ) ) {
            return {
                string_parse_status_t::NUMERIC_OVERFLOW,
                result.cchConsumed,
                iSign
            };
        }

        *pValueOut = static_cast<i64>( nMagnitude );
        return result;
    }

    const usize iMagnitudeBegin = iSign + 1u;
    if ( iMagnitudeBegin == text.cchLength ||
         Char_IsWhitespaceAscii( text.pData[iMagnitudeBegin] ) ) {
        return {
            string_parse_status_t::INVALID_CHARACTER,
            iMagnitudeBegin,
            iMagnitudeBegin
        };
    }

    const string_view_t magnitudeText = StringView_FromRange(
        text.pData + iMagnitudeBegin,
        text.cchLength - iMagnitudeBegin );
    string_parse_options_t magnitudeOptions = options;
    magnitudeOptions.flags &= ~STRING_PARSE_FLAG_ALLOW_PLUS_SIGN;

    string_parse_result_t magnitudeResult = StringParse_U64(
        magnitudeText,
        magnitudeOptions,
        &nMagnitude );
    magnitudeResult.cchConsumed += iMagnitudeBegin;
    if ( magnitudeResult.iError != CY_INVALID_SIZE ) {
        magnitudeResult.iError += iMagnitudeBegin;
    }

    if ( !StringParse_Succeeded( magnitudeResult ) ) {
        if ( magnitudeResult.status == string_parse_status_t::NUMERIC_OVERFLOW ) {
            magnitudeResult.status = string_parse_status_t::NUMERIC_UNDERFLOW;
        }

        return magnitudeResult;
    }

    const u64 nNegativeLimit = static_cast<u64>( CY_I64_MAX ) + 1u;
    if ( nMagnitude > nNegativeLimit ) {
        return {
            string_parse_status_t::NUMERIC_UNDERFLOW,
            magnitudeResult.cchConsumed,
            iSign
        };
    }

    *pValueOut = nMagnitude == nNegativeLimit
        ? CY_I64_MIN
        : -static_cast<i64>( nMagnitude );
    return magnitudeResult;
}

string_parse_result_t StringParse_U32(
    string_view_t text,
    const string_parse_options_t &options,
    u32 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "StringParse_U32 requires non-null output storage." );

    if ( !bValidOutput ) {
        return { string_parse_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    u64 nWideValue = 0u;
    const string_parse_result_t result =
        StringParse_U64( text, options, &nWideValue );
    if ( !StringParse_Succeeded( result ) ) {
        return result;
    }

    if ( nWideValue > static_cast<u64>( CY_U32_MAX ) ) {
        return {
            string_parse_status_t::NUMERIC_OVERFLOW,
            result.cchConsumed,
            StringParse_ValueBegin( text, options.flags )
        };
    }

    *pValueOut = static_cast<u32>( nWideValue );
    return result;
}

string_parse_result_t StringParse_I32(
    string_view_t text,
    const string_parse_options_t &options,
    i32 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "StringParse_I32 requires non-null output storage." );

    if ( !bValidOutput ) {
        return { string_parse_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    i64 nWideValue = 0;
    const string_parse_result_t result =
        StringParse_I64( text, options, &nWideValue );
    if ( !StringParse_Succeeded( result ) ) {
        return result;
    }

    if ( nWideValue > static_cast<i64>( CY_I32_MAX ) ||
         nWideValue < static_cast<i64>( CY_I32_MIN ) ) {
        return {
            nWideValue < static_cast<i64>( CY_I32_MIN )
                ? string_parse_status_t::NUMERIC_UNDERFLOW
                : string_parse_status_t::NUMERIC_OVERFLOW,
            result.cchConsumed,
            StringParse_ValueBegin( text, options.flags )
        };
    }

    *pValueOut = static_cast<i32>( nWideValue );
    return result;
}

string_parse_result_t StringParse_F64(
    string_view_t text,
    flags32_t flags,
    f64 *pValueOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pValueOut != nullptr;
    const bool_t bValidFlags = ( flags & ~STRING_PARSE_VALID_FLAGS ) == 0u;

    CY_ASSERT_MSG(
        bValidText,
        "StringParse_F64 requires a valid source view." );
    CY_ASSERT_MSG(
        bValidOutput,
        "StringParse_F64 requires non-null output storage." );
    CY_ASSERT_MSG(
        bValidFlags,
        "StringParse_F64 received unsupported parsing flags." );

    if ( !bValidText || !bValidOutput || !bValidFlags ) {
        return { string_parse_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    return StringParse_Float( text, flags, pValueOut );
}

string_parse_result_t StringParse_F32(
    string_view_t text,
    flags32_t flags,
    f32 *pValueOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pValueOut != nullptr;
    const bool_t bValidFlags = ( flags & ~STRING_PARSE_VALID_FLAGS ) == 0u;

    CY_ASSERT_MSG(
        bValidText,
        "StringParse_F32 requires a valid source view." );
    CY_ASSERT_MSG(
        bValidOutput,
        "StringParse_F32 requires non-null output storage." );
    CY_ASSERT_MSG(
        bValidFlags,
        "StringParse_F32 received unsupported parsing flags." );

    if ( !bValidText || !bValidOutput || !bValidFlags ) {
        return { string_parse_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    return StringParse_Float( text, flags, pValueOut );
}

string_parse_result_t StringParse_Bool(
    string_view_t text,
    flags32_t flags,
    bool_t *pValueOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pValueOut != nullptr;
    const bool_t bValidFlags = ( flags & ~STRING_PARSE_VALID_FLAGS ) == 0u;

    CY_ASSERT_MSG(
        bValidText,
        "StringParse_Bool requires a valid source view." );
    CY_ASSERT_MSG(
        bValidOutput,
        "StringParse_Bool requires non-null output storage." );
    CY_ASSERT_MSG(
        bValidFlags,
        "StringParse_Bool received unsupported parsing flags." );

    if ( !bValidText || !bValidOutput || !bValidFlags ) {
        return { string_parse_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    const usize iValueBegin = StringParse_ValueBegin( text, flags );
    if ( iValueBegin == text.cchLength ) {
        return {
            string_parse_status_t::EMPTY_INPUT,
            iValueBegin,
            iValueBegin
        };
    }

    const bool_t bCaseInsensitive =
        ( flags & STRING_PARSE_FLAG_CASE_INSENSITIVE_BOOL ) != 0u;
    bool_t bValue = CY_FALSE;
    usize iCursor = iValueBegin;

    if ( StringParse_MatchesBoolLiteral(
             text, iValueBegin, "true", 4u, bCaseInsensitive ) ) {
        bValue = CY_TRUE;
        iCursor += 4u;
    } else if ( StringParse_MatchesBoolLiteral(
                    text, iValueBegin, "false", 5u, bCaseInsensitive ) ) {
        bValue = CY_FALSE;
        iCursor += 5u;
    } else if ( ( flags & STRING_PARSE_FLAG_ALLOW_NUMERIC_BOOL ) != 0u &&
                ( text.pData[iValueBegin] == '0' ||
                  text.pData[iValueBegin] == '1' ) ) {
        bValue = text.pData[iValueBegin] == '1';
        ++iCursor;
    } else {
        return {
            string_parse_status_t::INVALID_CHARACTER,
            iValueBegin,
            iValueBegin
        };
    }

    if ( ( flags & STRING_PARSE_FLAG_TRIM_WHITESPACE ) != 0u ) {
        iCursor = StringParse_SkipWhitespace( text, iCursor );
    }

    if ( iCursor < text.cchLength &&
         ( flags & STRING_PARSE_FLAG_ALLOW_TRAILING_CHARACTERS ) == 0u ) {
        return {
            string_parse_status_t::TRAILING_CHARACTERS,
            iCursor,
            iCursor
        };
    }

    *pValueOut = bValue;
    return {
        string_parse_status_t::OK,
        iCursor,
        CY_INVALID_SIZE
    };
}

} // namespace cypher::common
