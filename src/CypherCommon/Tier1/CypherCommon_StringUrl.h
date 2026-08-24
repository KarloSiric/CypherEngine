//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringUrl.h
//  Purpose: Declares bounded URL parsing and percent encoding helpers.
//  Details: The API performs deterministic URI text processing only; DNS, HTTP,
//           sockets, internationalized domains, and trust policy belong elsewhere.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGURL_H
#define CYPHER_COMMON_TIER1_STRINGURL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum url_encode_flags_t : flags32_t {
    URL_ENCODE_FLAG_NONE             = 0u,                // Apply strict percent encoding only.
    URL_ENCODE_FLAG_SPACE_AS_PLUS    = CYPHER_BIT32( 0 ), // Emit form-style '+' for an ASCII space.
    URL_ENCODE_FLAG_PRESERVE_SLASH   = CYPHER_BIT32( 1 ), // Keep path separators readable.
    URL_ENCODE_FLAG_UPPERCASE_HEX    = CYPHER_BIT32( 2 )  // Use uppercase digits in percent escapes.
};

enum url_decode_flags_t : flags32_t {
    URL_DECODE_FLAG_NONE             = 0u,                // Decode percent escapes without form rules.
    URL_DECODE_FLAG_PLUS_AS_SPACE    = CYPHER_BIT32( 0 ), // Interpret '+' as an ASCII space.
    URL_DECODE_FLAG_REJECT_NUL       = CYPHER_BIT32( 1 )  // Reject escapes that would inject a NUL byte.
};

enum class url_status_t : u8 {
    OK = 0u,          // Parse or conversion completed successfully.
    INVALID_ARGUMENT,// Pointer, capacity, or input contract was invalid.
    INVALID_URL,     // URL structure is malformed.
    INVALID_ESCAPE,  // Percent escape is incomplete or contains a non-hex digit.
    OUTPUT_TRUNCATED // Destination received only a prefix of the required output.
};

// Bracketed IPv6 hosts are returned without the surrounding '[' and ']'.
struct url_parts_t {
    string_view_t scheme{};   // Text before ':', excluding the delimiter.
    string_view_t userInfo{}; // Optional authority user-info before '@'.
    string_view_t host{};     // Host text; IPv6 brackets are removed.
    string_view_t port{};     // Optional decimal port text after ':'.
    string_view_t path{};     // Hierarchical path, still percent encoded.
    string_view_t query{};    // Optional text after '?', excluding delimiter.
    string_view_t fragment{}; // Optional text after '#', excluding delimiter.
    bool_t bHasAuthority{ CY_FALSE }; // True when URL contains a leading "//" authority.
};

struct url_result_t {
    url_status_t status{ url_status_t::OK }; // Final parse or conversion state.
    usize cchConsumed{ 0u }; // Source characters accepted.
    usize cbWritten{ 0u };   // Destination bytes physically stored.
    usize cbRequired{ 0u };  // Complete destination byte count.
    usize iError{ CY_STRING_VIEW_NPOS }; // Source byte responsible for failure.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringUrl_IsUnreservedByte( u8 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringUrl_IsValidScheme( string_view_t scheme ) noexcept;

// Parts borrow the original URL storage.
CYPHER_NODISCARD CYPHER_COMMON_API
url_result_t StringUrl_Parse( string_view_t url, url_parts_t *pPartsOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringUrl_HostEquals(
    const url_parts_t &parts,
    string_view_t expectedHost ) noexcept;

// PercentEncode reserves one byte for a null terminator. PercentDecode writes raw
// bytes and therefore does not append a terminator.
CYPHER_NODISCARD_MSG( "Inspect cbRequired to detect URL encoding truncation." )
CYPHER_COMMON_API url_result_t StringUrl_PercentEncode(
    const_byte_span_t source,
    flags32_t flags,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD_MSG( "Inspect cbRequired to detect URL decoding truncation." )
CYPHER_COMMON_API url_result_t StringUrl_PercentDecode(
    string_view_t source,
    flags32_t flags,
    byte_span_t dest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGURL_H
