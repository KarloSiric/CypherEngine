//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringFormat.cpp
//  Purpose: Implements bounded formatted-text helpers.
//  Details: Every valid destination remains terminated, required lengths survive
//           truncation, and convenience formatting uses no dynamic allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringFormat.h"

#include <cmath>
#include <cstdio>

namespace cypher::common
{

namespace
{

constexpr u32 CY_STRING_FORMAT_MAX_FRACTION_DIGITS = 9u;

string_format_result_t CopyFormattedBytes(
    const char *pSource,
    usize cchSource,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    CY_ASSERT_MSG(
        bValidDestination,
        "String formatting requires a destination when capacity is nonzero." );
    if ( !bValidDestination ) {
        return { string_format_status_t::INVALID_ARGUMENT, 0u, 0u };
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
            ? string_format_status_t::OK
            : string_format_status_t::OUTPUT_TRUNCATED,
        cchCopy,
        cchSource
    };
}

} // namespace

string_format_result_t StringFormat_VPrintf(
    char *pDest,
    usize cchDest,
    const char *pFormat,
    std::va_list args ) noexcept
{
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    const bool_t bValidFormat = pFormat != nullptr;
    CY_ASSERT_MSG(
        bValidDestination,
        "StringFormat_VPrintf requires a destination when capacity is nonzero." );
    CY_ASSERT_MSG(
        bValidFormat,
        "StringFormat_VPrintf requires a format string." );

    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[0] = '\0';
    }
    if ( !bValidDestination || !bValidFormat ) {
        return { string_format_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    std::va_list argsCopy;
    va_copy( argsCopy, args );
    const int cchResult = std::vsnprintf(
        pDest,
        cchDest,
        pFormat,
        argsCopy );
    va_end( argsCopy );

    if ( cchResult < 0 ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return { string_format_status_t::FORMAT_ERROR, 0u, 0u };
    }

    const usize cchRequired = static_cast<usize>( cchResult );
    const usize cchWritten = cchDest > 0u
        ? ( cchRequired < cchDest ? cchRequired : cchDest - 1u )
        : 0u;
    return {
        cchWritten == cchRequired
            ? string_format_status_t::OK
            : string_format_status_t::OUTPUT_TRUNCATED,
        cchWritten,
        cchRequired
    };
}

string_format_result_t StringFormat_Printf(
    char *pDest,
    usize cchDest,
    const char *pFormat,
    ... ) noexcept
{
    std::va_list args;
    va_start( args, pFormat );
    const string_format_result_t result =
        StringFormat_VPrintf( pDest, cchDest, pFormat, args );
    va_end( args );
    return result;
}

string_format_result_t StringFormat_AppendV(
    char *pDest,
    usize cchDest,
    usize *pcchLengthInOut,
    const char *pFormat,
    std::va_list args ) noexcept
{
    const bool_t bValidDestination = pDest != nullptr || cchDest == 0u;
    const bool_t bValidLength = pcchLengthInOut != nullptr;
    const usize cchCurrent = bValidLength ? *pcchLengthInOut : 0u;
    const bool_t bLengthInRange =
        bValidLength &&
        ( cchDest == 0u
            ? cchCurrent == 0u
            : cchCurrent < cchDest );
    const bool_t bTerminated =
        !bValidDestination || cchDest == 0u || !bLengthInRange ||
        pDest[cchCurrent] == '\0';
    CY_ASSERT_MSG(
        bValidDestination,
        "StringFormat_AppendV requires a destination when capacity is nonzero." );
    CY_ASSERT_MSG(
        bValidLength,
        "StringFormat_AppendV requires a length pointer." );
    CY_ASSERT_MSG(
        bLengthInRange,
        "StringFormat_AppendV length is outside the destination." );
    CY_ASSERT_MSG(
        bTerminated,
        "StringFormat_AppendV requires a terminated destination." );
    if ( !bValidDestination || !bValidLength ||
         !bLengthInRange || !bTerminated ) {
        return { string_format_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    char *pAppendDest = cchDest > 0u ? pDest + cchCurrent : nullptr;
    const usize cchAppendDest = cchDest > 0u ? cchDest - cchCurrent : 0u;
    const string_format_result_t appended = StringFormat_VPrintf(
        pAppendDest,
        cchAppendDest,
        pFormat,
        args );
    if ( appended.status == string_format_status_t::INVALID_ARGUMENT ||
         appended.status == string_format_status_t::FORMAT_ERROR ) {
        return { appended.status, cchCurrent, cchCurrent };
    }

    if ( appended.cchRequired > CY_USIZE_MAX - cchCurrent ) {
        CY_ASSERT_MSG( CY_FALSE, "Formatted append length overflowed." );
        return {
            string_format_status_t::FORMAT_ERROR,
            cchCurrent,
            CY_USIZE_MAX
        };
    }

    *pcchLengthInOut = cchCurrent + appended.cchWritten;
    return {
        appended.status,
        *pcchLengthInOut,
        cchCurrent + appended.cchRequired
    };
}

string_format_result_t StringFormat_Append(
    char *pDest,
    usize cchDest,
    usize *pcchLengthInOut,
    const char *pFormat,
    ... ) noexcept
{
    std::va_list args;
    va_start( args, pFormat );
    const string_format_result_t result = StringFormat_AppendV(
        pDest,
        cchDest,
        pcchLengthInOut,
        pFormat,
        args );
    va_end( args );
    return result;
}

string_format_result_t StringFormat_GroupedInteger(
    i64 value,
    char chSeparator,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidSeparator = chSeparator != '\0';
    CY_ASSERT_MSG(
        bValidSeparator,
        "StringFormat_GroupedInteger requires a non-null separator." );
    if ( !bValidSeparator ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return { string_format_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    char reverse[32]{};
    usize cchReverse = 0u;
    const bool_t bNegative = value < 0;
    u64 nMagnitude = bNegative
        ? static_cast<u64>( -( value + 1 ) ) + 1u
        : static_cast<u64>( value );

    u32 cDigitsInGroup = 0u;
    do {
        if ( cDigitsInGroup == 3u ) {
            reverse[cchReverse++] = chSeparator;
            cDigitsInGroup = 0u;
        }
        reverse[cchReverse++] =
            static_cast<char>( '0' + ( nMagnitude % 10u ) );
        nMagnitude /= 10u;
        ++cDigitsInGroup;
    } while ( nMagnitude != 0u );

    if ( bNegative ) {
        reverse[cchReverse++] = '-';
    }

    char formatted[32]{};
    for ( usize iIndex = 0u; iIndex < cchReverse; ++iIndex ) {
        formatted[iIndex] = reverse[cchReverse - 1u - iIndex];
    }
    return CopyFormattedBytes( formatted, cchReverse, pDest, cchDest );
}

string_format_result_t StringFormat_ByteCount(
    u64 cbValue,
    u32 nFractionDigits,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidPrecision =
        nFractionDigits <= CY_STRING_FORMAT_MAX_FRACTION_DIGITS;
    CY_ASSERT_MSG(
        bValidPrecision,
        "StringFormat_ByteCount precision exceeds the supported range." );
    if ( !bValidPrecision ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return { string_format_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    constexpr const char *pUnits[] = {
        "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"
    };
    constexpr usize cUnits = sizeof( pUnits ) / sizeof( pUnits[0] );
    f64 flValue = static_cast<f64>( cbValue );
    usize iUnit = 0u;
    while ( flValue >= 1024.0 && iUnit + 1u < cUnits ) {
        flValue /= 1024.0;
        ++iUnit;
    }

    return StringFormat_Printf(
        pDest,
        cchDest,
        "%.*f %s",
        static_cast<int>( nFractionDigits ),
        flValue,
        pUnits[iUnit] );
}

string_format_result_t StringFormat_Duration(
    f64 flSeconds,
    u32 nFractionDigits,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidValue = std::isfinite( flSeconds );
    const bool_t bValidPrecision =
        nFractionDigits <= CY_STRING_FORMAT_MAX_FRACTION_DIGITS;
    CY_ASSERT_MSG(
        bValidValue,
        "StringFormat_Duration requires a finite duration." );
    CY_ASSERT_MSG(
        bValidPrecision,
        "StringFormat_Duration precision exceeds the supported range." );
    if ( !bValidValue || !bValidPrecision ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return { string_format_status_t::INVALID_ARGUMENT, 0u, 0u };
    }

    const f64 flMagnitude = std::fabs( flSeconds );
    f64 flDisplay = flSeconds;
    const char *pUnit = "s";
    if ( flMagnitude < 0.000001 ) {
        flDisplay *= 1000000000.0;
        pUnit = "ns";
    } else if ( flMagnitude < 0.001 ) {
        flDisplay *= 1000000.0;
        pUnit = "us";
    } else if ( flMagnitude < 1.0 ) {
        flDisplay *= 1000.0;
        pUnit = "ms";
    } else if ( flMagnitude >= 3600.0 ) {
        flDisplay /= 3600.0;
        pUnit = "h";
    } else if ( flMagnitude >= 60.0 ) {
        flDisplay /= 60.0;
        pUnit = "min";
    }

    return StringFormat_Printf(
        pDest,
        cchDest,
        "%.*f %s",
        static_cast<int>( nFractionDigits ),
        flDisplay,
        pUnit );
}

} // namespace cypher::common
