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
    URL_ENCODE_FLAG_NONE             = 0u,
    URL_ENCODE_FLAG_SPACE_AS_PLUS    = CYPHER_BIT32( 0 ),
    URL_ENCODE_FLAG_PRESERVE_SLASH   = CYPHER_BIT32( 1 ),
    URL_ENCODE_FLAG_UPPERCASE_HEX    = CYPHER_BIT32( 2 )
};

enum url_decode_flags_t : flags32_t {
    URL_DECODE_FLAG_NONE             = 0u,
    URL_DECODE_FLAG_PLUS_AS_SPACE    = CYPHER_BIT32( 0 ),
    URL_DECODE_FLAG_REJECT_NUL       = CYPHER_BIT32( 1 )
};

enum class url_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_URL,
    INVALID_ESCAPE,
    OUTPUT_TRUNCATED
};

// Bracketed IPv6 hosts are returned without the surrounding '[' and ']'.
struct url_parts_t {
    string_view_t scheme{};
    string_view_t userInfo{};
    string_view_t host{};
    string_view_t port{};
    string_view_t path{};
    string_view_t query{};
    string_view_t fragment{};
    bool_t bHasAuthority{ CY_FALSE };
};

struct url_result_t {
    url_status_t status{ url_status_t::OK };
    usize cchConsumed{ 0u };
    usize cbWritten{ 0u };
    usize cbRequired{ 0u };
    usize iError{ CY_STRING_VIEW_NPOS };
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
