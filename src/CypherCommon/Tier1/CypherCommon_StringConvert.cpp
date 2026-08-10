//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringConvert.cpp
//  Purpose: Implements deterministic primitive and binary text conversion.
//  Details: Integer conversion is allocation-free and locale-independent. Floating
//           conversion uses the C++ character-conversion contract, then applies
//           Cypher casing, sign, and trailing-zero policy deterministically.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringConvert.h"

#include "CypherCommon_Char.h"

#include <charconv>
#include <cmath>
#include <system_error>

namespace cypher::common
{

namespace
{

constexpr flags32_t CY_INTEGER_FORMAT_FLAG_MASK =
    STRING_INTEGER_FORMAT_FLAG_UPPERCASE |
    STRING_INTEGER_FORMAT_FLAG_PREFIX |
    STRING_INTEGER_FORMAT_FLAG_PLUS_SIGN;

constexpr flags32_t CY_FLOAT_FORMAT_FLAG_MASK =
    STRING_FLOAT_FORMAT_FLAG_UPPERCASE |
    STRING_FLOAT_FORMAT_FLAG_PLUS_SIGN |
    STRING_FLOAT_FORMAT_FLAG_TRIM_TRAILING_ZERO;

string_convert_result_t CopyTextResult(
    const char *pSource,
    usize cchSource,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    CY_ASSERT_MSG(
        bValidDestination,
        "String conversion requires a destination when capacity is nonzero." );
    if ( !bValidDestination ) {
        return { string_convert_status_t::INVALID_ARGUMENT, 0u, 0u, 0u };
    }

    const usize cchWritable = cchDest > 0u ? cchDest - 1u : 0u;
    const usize cchCopy = cchSource < cchWritable ? cchSource : cchWritable;
    if ( cchCopy > 0u ) {
        Cy_MemCopy( pDest, pSource, cchCopy );
    }
    if ( cchDest > 0u ) {
        pDest[cchCopy] = '\0';
    }
    return {
        cchCopy == cchSource
            ? string_convert_status_t::OK
            : string_convert_status_t::OUTPUT_TRUNCATED,
        cchCopy,
        cchSource,
        0u
    };
}

string_convert_result_t InvalidTextResult(
    string_convert_status_t status,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[0] = '\0';
    }
    return { status, 0u, 0u, 0u };
}

bool_t IntegerFormatIsValid( const string_integer_format_t &format ) noexcept
{
    if ( format.nBase < 2u || format.nBase > 36u ||
         ( format.flags & ~CY_INTEGER_FORMAT_FLAG_MASK ) != 0u ) {
        return CY_FALSE;
    }
    if ( ( format.flags & STRING_INTEGER_FORMAT_FLAG_PREFIX ) != 0u &&
         format.nBase != 2u && format.nBase != 8u && format.nBase != 16u ) {
        return CY_FALSE;
    }
    return CY_TRUE;
}

string_convert_result_t ConvertUnsignedInteger(
    u64 value,
    bool_t bNegative,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( !IntegerFormatIsValid( format ) ) {
        const string_convert_status_t status =
            format.nBase < 2u || format.nBase > 36u
                ? string_convert_status_t::INVALID_BASE
                : string_convert_status_t::INVALID_ARGUMENT;
        return InvalidTextResult( status, pDest, cchDest );
    }

    const bool_t bUppercase =
        ( format.flags & STRING_INTEGER_FORMAT_FLAG_UPPERCASE ) != 0u;
    const char *pDigits = bUppercase
        ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        : "0123456789abcdefghijklmnopqrstuvwxyz";

    char reverse[256]{};
    usize cchDigits = 0u;
    do {
        reverse[cchDigits++] = pDigits[value % format.nBase];
        value /= format.nBase;
    } while ( value != 0u );
    while ( cchDigits < format.nMinDigits ) {
        reverse[cchDigits++] = '0';
    }

    char converted[260]{};
    usize cchConverted = 0u;
    if ( bNegative ) {
        converted[cchConverted++] = '-';
    } else if ( ( format.flags & STRING_INTEGER_FORMAT_FLAG_PLUS_SIGN ) != 0u ) {
        converted[cchConverted++] = '+';
    }

    if ( ( format.flags & STRING_INTEGER_FORMAT_FLAG_PREFIX ) != 0u ) {
        converted[cchConverted++] = '0';
        if ( format.nBase == 2u ) {
            converted[cchConverted++] = bUppercase ? 'B' : 'b';
        } else if ( format.nBase == 8u ) {
            converted[cchConverted++] = bUppercase ? 'O' : 'o';
        } else {
            converted[cchConverted++] = bUppercase ? 'X' : 'x';
        }
    }

    for ( usize iDigit = 0u; iDigit < cchDigits; ++iDigit ) {
        converted[cchConverted++] = reverse[cchDigits - 1u - iDigit];
    }
    return CopyTextResult( converted, cchConverted, pDest, cchDest );
}

bool_t FloatFormatIsValid( const string_float_format_t &format ) noexcept
{
    const bool_t bValidStyle =
        format.style == string_float_style_t::GENERAL ||
        format.style == string_float_style_t::FIXED ||
        format.style == string_float_style_t::SCIENTIFIC;
    return bValidStyle && ( format.flags & ~CY_FLOAT_FORMAT_FLAG_MASK ) == 0u;
}

std::chars_format ToCharsFormat( string_float_style_t style ) noexcept
{
    switch ( style ) {
        case string_float_style_t::FIXED:
            return std::chars_format::fixed;
        case string_float_style_t::SCIENTIFIC:
            return std::chars_format::scientific;
        case string_float_style_t::GENERAL:
        default:
            return std::chars_format::general;
    }
}

usize TrimFloatTrailingZeros( char *pText, usize cchText ) noexcept
{
    usize iExponent = cchText;
    for ( usize iChar = 0u; iChar < cchText; ++iChar ) {
        if ( pText[iChar] == 'e' || pText[iChar] == 'E' ) {
            iExponent = iChar;
            break;
        }
    }

    usize iDecimal = iExponent;
    for ( usize iChar = 0u; iChar < iExponent; ++iChar ) {
        if ( pText[iChar] == '.' ) {
            iDecimal = iChar;
            break;
        }
    }
    if ( iDecimal == iExponent ) {
        return cchText;
    }

    usize iMantissaEnd = iExponent;
    while ( iMantissaEnd > iDecimal + 1u && pText[iMantissaEnd - 1u] == '0' ) {
        --iMantissaEnd;
    }
    if ( iMantissaEnd == iDecimal + 1u ) {
        iMantissaEnd = iDecimal;
    }
    if ( iMantissaEnd != iExponent ) {
        const usize cchExponent = cchText - iExponent;
        Cy_MemMove( pText + iMantissaEnd, pText + iExponent, cchExponent );
        cchText = iMantissaEnd + cchExponent;
    }
    return cchText;
}

template <typename float_t>
string_convert_result_t ConvertFloat(
    float_t value,
    const string_float_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( !FloatFormatIsValid( format ) ) {
        return InvalidTextResult(
            string_convert_status_t::INVALID_ARGUMENT,
            pDest,
            cchDest );
    }

    char converted[1024]{};
    char *pBegin = converted;
    const bool_t bPlusSign =
        ( format.flags & STRING_FLOAT_FORMAT_FLAG_PLUS_SIGN ) != 0u &&
        !std::signbit( value );
    if ( bPlusSign ) {
        *pBegin++ = '+';
    }

    const std::to_chars_result convertedResult = std::to_chars(
        pBegin,
        converted + sizeof( converted ),
        value,
        ToCharsFormat( format.style ),
        format.nPrecision );
    if ( convertedResult.ec != std::errc{} ) {
        return InvalidTextResult(
            string_convert_status_t::INVALID_ARGUMENT,
            pDest,
            cchDest );
    }

    usize cchConverted = static_cast<usize>( convertedResult.ptr - converted );
    if ( ( format.flags & STRING_FLOAT_FORMAT_FLAG_TRIM_TRAILING_ZERO ) != 0u ) {
        cchConverted = TrimFloatTrailingZeros( converted, cchConverted );
    }
    if ( ( format.flags & STRING_FLOAT_FORMAT_FLAG_UPPERCASE ) != 0u ) {
        for ( usize iChar = 0u; iChar < cchConverted; ++iChar ) {
            converted[iChar] = Char_ToUpperAscii( converted[iChar] );
        }
    }
    return CopyTextResult( converted, cchConverted, pDest, cchDest );
}

} // namespace

string_convert_result_t StringConvert_U64(
    u64 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    return ConvertUnsignedInteger(
        value,
        CY_FALSE,
        format,
        pDest,
        cchDest );
}

string_convert_result_t StringConvert_I64(
    i64 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bNegative = value < 0;
    const u64 magnitude = bNegative
        ? static_cast<u64>( -( value + 1 ) ) + 1u
        : static_cast<u64>( value );
    return ConvertUnsignedInteger(
        magnitude,
        bNegative,
        format,
        pDest,
        cchDest );
}

string_convert_result_t StringConvert_U32(
    u32 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    return StringConvert_U64( value, format, pDest, cchDest );
}

string_convert_result_t StringConvert_I32(
    i32 value,
    const string_integer_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    return StringConvert_I64( value, format, pDest, cchDest );
}

string_convert_result_t StringConvert_F64(
    f64 value,
    const string_float_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    return ConvertFloat( value, format, pDest, cchDest );
}

string_convert_result_t StringConvert_F32(
    f32 value,
    const string_float_format_t &format,
    char *pDest,
    usize cchDest ) noexcept
{
    return ConvertFloat( value, format, pDest, cchDest );
}

string_convert_result_t StringConvert_Bool(
    bool_t value,
    char *pDest,
    usize cchDest ) noexcept
{
    const char *pText = value ? "true" : "false";
    const usize cchText = value ? 4u : 5u;
    return CopyTextResult( pText, cchText, pDest, cchDest );
}

string_convert_result_t StringConvert_BinaryToHex(
    const void *pData,
    usize cbData,
    bool_t bUppercase,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidData = pData != nullptr || cbData == 0u;
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    CY_ASSERT_MSG( bValidData, "BinaryToHex requires data for a non-empty range." );
    CY_ASSERT_MSG(
        bValidDestination,
        "BinaryToHex requires a destination when capacity is nonzero." );
    if ( !bValidData || !bValidDestination || cbData > CY_USIZE_MAX / 2u ) {
        return InvalidTextResult(
            string_convert_status_t::INVALID_ARGUMENT,
            pDest,
            cchDest );
    }

    const char *pDigits = bUppercase
        ? "0123456789ABCDEF"
        : "0123456789abcdef";
    const usize cchRequired = cbData * 2u;
    const usize cchWritable = cchDest > 0u ? cchDest - 1u : 0u;
    const usize cchWritten = cchRequired < cchWritable ? cchRequired : cchWritable;
    const auto *pBytes = static_cast<const byte *>( pData );
    for ( usize iChar = 0u; iChar < cchWritten; ++iChar ) {
        const byte value = pBytes[iChar / 2u];
        pDest[iChar] = ( iChar & 1u ) == 0u
            ? pDigits[value >> 4u]
            : pDigits[value & 0x0Fu];
    }
    if ( cchDest > 0u ) {
        pDest[cchWritten] = '\0';
    }
    return {
        cchWritten == cchRequired
            ? string_convert_status_t::OK
            : string_convert_status_t::OUTPUT_TRUNCATED,
        cchWritten,
        cchRequired,
        cbData
    };
}

string_convert_result_t StringConvert_HexToBinary(
    string_view_t text,
    void *pDest,
    usize cbDest ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidDestination = pDest != nullptr || cbDest == 0u;
    CY_ASSERT_MSG( bValidText, "HexToBinary requires a valid string view." );
    CY_ASSERT_MSG(
        bValidDestination,
        "HexToBinary requires a destination when capacity is nonzero." );
    if ( !bValidText || !bValidDestination ) {
        return { string_convert_status_t::INVALID_ARGUMENT, 0u, 0u, 0u };
    }
    if ( ( text.cchLength & 1u ) != 0u ) {
        return { string_convert_status_t::INVALID_TEXT, 0u, 0u, 0u };
    }

    for ( usize iChar = 0u; iChar < text.cchLength; ++iChar ) {
        if ( Char_HexValueAscii( text.pData[iChar] ) ==
             CY_CHAR_INVALID_DIGIT_VALUE ) {
            return {
                string_convert_status_t::INVALID_TEXT,
                0u,
                text.cchLength / 2u,
                iChar
            };
        }
    }

    const usize cbRequired = text.cchLength / 2u;
    const usize cbWritten = cbRequired < cbDest ? cbRequired : cbDest;
    auto *pBytes = static_cast<byte *>( pDest );
    for ( usize iByte = 0u; iByte < cbWritten; ++iByte ) {
        const u8 high = Char_HexValueAscii( text.pData[iByte * 2u] );
        const u8 low = Char_HexValueAscii( text.pData[iByte * 2u + 1u] );
        pBytes[iByte] = static_cast<byte>( ( high << 4u ) | low );
    }
    return {
        cbWritten == cbRequired
            ? string_convert_status_t::OK
            : string_convert_status_t::OUTPUT_TRUNCATED,
        cbWritten,
        cbRequired,
        text.cchLength
    };
}

} // namespace cypher::common
