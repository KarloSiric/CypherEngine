//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Encoding.h
//  Purpose: Declares strict hexadecimal and Base64 conversions.
//  Details: Decoders consume the complete input and reject whitespace,
//           malformed padding, and non-canonical trailing bits. These helpers
//           encode cryptographic material but do not encrypt it.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_ENCODING_H
#define CYPHER_SECURITY_ENCODING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_Types.h"
#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"

namespace cypher::security
{

using common::binary_block_t;
using common::string_view_t;

enum class base64_variant_t : u8 {
    ORIGINAL = 0u,
    ORIGINAL_NO_PADDING,
    URL_SAFE,
    URL_SAFE_NO_PADDING
};

// Encoded size includes the terminating null byte required by Hex_Encode.
CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityHex_EncodedSize(
    usize cbBinary,
    usize *pSizeOut ) noexcept;

// Returns false when the complete view is not strict hexadecimal text.
CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityHex_DecodedSize(
    string_view_t encoded,
    usize *pSizeOut ) noexcept;

// Writes lowercase hexadecimal and a terminating null byte. cchWrittenOut
// excludes that terminator.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityHex_Encode(
    binary_block_t binary,
    char *pEncodedOut,
    usize cchEncodedCapacity,
    usize *pCharactersWrittenOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityHex_Decode(
    string_view_t encoded,
    void *pBinaryOut,
    usize cbBinaryCapacity,
    usize *pBytesWrittenOut ) noexcept;

// Encoded size includes the terminating null byte required by Base64_Encode.
CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityBase64_EncodedSize(
    usize cbBinary,
    base64_variant_t variant,
    usize *pSizeOut ) noexcept;

// Returns false for malformed or non-canonical text for the selected variant.
CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityBase64_DecodedSize(
    string_view_t encoded,
    base64_variant_t variant,
    usize *pSizeOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityBase64_Encode(
    binary_block_t binary,
    base64_variant_t variant,
    char *pEncodedOut,
    usize cchEncodedCapacity,
    usize *pCharactersWrittenOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityBase64_Decode(
    string_view_t encoded,
    base64_variant_t variant,
    void *pBinaryOut,
    usize cbBinaryCapacity,
    usize *pBytesWrittenOut ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_ENCODING_H
