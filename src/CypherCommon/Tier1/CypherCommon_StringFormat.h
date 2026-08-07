//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringFormat.h
//  Purpose: Declares bounded formatted-text helpers.
//  Details: Formatting always terminates valid output buffers and reports both the
//           stored and required character counts.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGFORMAT_H
#define CYPHER_COMMON_TIER1_STRINGFORMAT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

#include <cstdarg>

namespace cypher::common
{

enum class string_format_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    FORMAT_ERROR,
    OUTPUT_TRUNCATED
};

struct string_format_result_t {
    string_format_status_t status{ string_format_status_t::OK };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
string_format_result_t StringFormat_VPrintf(
    char *pDest,
    usize cchDest,
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    std::va_list args ) noexcept CY_PRINTF_LIKE( 3, 0 );

CYPHER_NODISCARD CYPHER_COMMON_API
string_format_result_t StringFormat_Printf(
    char *pDest,
    usize cchDest,
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    ... ) noexcept CY_PRINTF_LIKE( 3, 4 );

CYPHER_NODISCARD CYPHER_COMMON_API
string_format_result_t StringFormat_AppendV(
    char *pDest,
    usize cchDest,
    usize *pcchLengthInOut,
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    std::va_list args ) noexcept CY_PRINTF_LIKE( 4, 0 );

CYPHER_NODISCARD CYPHER_COMMON_API
string_format_result_t StringFormat_Append(
    char *pDest,
    usize cchDest,
    usize *pcchLengthInOut,
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    ... ) noexcept CY_PRINTF_LIKE( 4, 5 );

CYPHER_NODISCARD CYPHER_COMMON_API
string_format_result_t StringFormat_GroupedInteger(
    i64 value,
    char chSeparator,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_format_result_t StringFormat_ByteCount(
    u64 cbValue,
    u32 nFractionDigits,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_format_result_t StringFormat_Duration(
    f64 flSeconds,
    u32 nFractionDigits,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGFORMAT_H
