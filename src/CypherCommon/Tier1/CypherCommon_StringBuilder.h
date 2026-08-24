//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringBuilder.h
//  Purpose: Declares a non-owning bounded text construction cursor.
//  Details: StringBuilder writes into caller storage, maintains termination when
//           possible, and continues counting required output after truncation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
String Builder Contract

Text operations distinguish bounded byte ranges from null-terminated strings. Cursor movement
and conversion validate limits before reading, and failure never relies on ambient locale state.
================
*/

#ifndef CYPHER_COMMON_TIER1_STRINGBUILDER_H
#define CYPHER_COMMON_TIER1_STRINGBUILDER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringView.h"

#include <cstdarg>

namespace cypher::common
{

enum class string_builder_status_t : u8 {
    OK = 0u,          // Every requested character fit.
    INVALID_ARGUMENT,// Builder state or input range is invalid.
    OUTPUT_TRUNCATED, // Required output exceeded caller capacity.
    FORMAT_ERROR      // printf-style formatting rejected its format or arguments.
};

struct string_builder_t {
    char *pData{ nullptr };       // Caller-owned destination, including NUL space.
    usize cchLength{ 0u };        // Characters physically written, excluding NUL.
    usize cchCapacity{ 0u };      // Total destination bytes, including NUL space.
    usize cchRequired{ 0u };      // Characters requested, even after truncation.
    string_builder_status_t status{ string_builder_status_t::OK }; // Sticky result.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringBuilder_Init(
    string_builder_t *pBuilder,
    char *pBuffer,
    usize cchCapacity ) noexcept;

CYPHER_COMMON_API void StringBuilder_Clear( string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringBuilder_IsValid( const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringBuilder_WasTruncated( const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize StringBuilder_Length( const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize StringBuilder_Required( const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_builder_status_t StringBuilder_Status(
    const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize StringBuilder_Remaining( const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringBuilder_View( const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *StringBuilder_CStr( const string_builder_t *pBuilder ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect StringBuilder_WasTruncated after appending." )
CYPHER_COMMON_API string_builder_status_t StringBuilder_Append(
    string_builder_t *pBuilder,
    string_view_t text ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect StringBuilder_WasTruncated after appending." )
CYPHER_COMMON_API string_builder_status_t StringBuilder_AppendChar(
    string_builder_t *pBuilder,
    char ch ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect StringBuilder_WasTruncated after appending." )
CYPHER_COMMON_API string_builder_status_t StringBuilder_AppendRepeat(
    string_builder_t *pBuilder,
    char ch,
    usize cchCount ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect StringBuilder_WasTruncated after formatting." )
CYPHER_COMMON_API string_builder_status_t StringBuilder_AppendFormatV(
    string_builder_t *pBuilder,
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    std::va_list args ) noexcept CY_PRINTF_LIKE( 2, 0 );

CYPHER_NODISCARD_MSG( "Inspect StringBuilder_WasTruncated after formatting." )
CYPHER_COMMON_API string_builder_status_t StringBuilder_AppendFormat(
    string_builder_t *pBuilder,
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    ... ) noexcept CY_PRINTF_LIKE( 2, 3 );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGBUILDER_H
