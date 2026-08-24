//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Encoding.cpp
//  Purpose: Implements strict hexadecimal and Base64 conversions.
//  Details: Syntax is validated before decoding so capacity failures never
//           consume partial input and successful decodes are canonical.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity_Encoding.h"

#include "CypherSecurity.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryOps.h"

#include <sodium.h>

namespace cypher::security
{

namespace
{

constexpr char g_emptyEncodedInput = '\0';

CYPHER_NODISCARD bool_t Base64Variant_IsValid(
    base64_variant_t variant ) noexcept
{
    return variant == base64_variant_t::ORIGINAL ||
           variant == base64_variant_t::ORIGINAL_NO_PADDING ||
           variant == base64_variant_t::URL_SAFE ||
           variant == base64_variant_t::URL_SAFE_NO_PADDING;
}

CYPHER_NODISCARD bool_t Base64Variant_UsesPadding(
    base64_variant_t variant ) noexcept
{
    return variant == base64_variant_t::ORIGINAL ||
           variant == base64_variant_t::URL_SAFE;
}

CYPHER_NODISCARD bool_t Base64Variant_IsUrlSafe(
    base64_variant_t variant ) noexcept
{
    return variant == base64_variant_t::URL_SAFE ||
           variant == base64_variant_t::URL_SAFE_NO_PADDING;
}

CYPHER_NODISCARD int Base64Variant_Native(
    base64_variant_t variant ) noexcept
{
    switch ( variant ) {
        case base64_variant_t::ORIGINAL:
            return sodium_base64_VARIANT_ORIGINAL;
        case base64_variant_t::ORIGINAL_NO_PADDING:
            return sodium_base64_VARIANT_ORIGINAL_NO_PADDING;
        case base64_variant_t::URL_SAFE:
            return sodium_base64_VARIANT_URLSAFE;
        case base64_variant_t::URL_SAFE_NO_PADDING:
            return sodium_base64_VARIANT_URLSAFE_NO_PADDING;
    }
    return 0;
}

CYPHER_NODISCARD int Base64_CharacterValue(
    char character,
    bool_t bUrlSafe ) noexcept
{
    if ( character >= 'A' && character <= 'Z' ) {
        return character - 'A';
    }
    if ( character >= 'a' && character <= 'z' ) {
        return character - 'a' + 26;
    }
    if ( character >= '0' && character <= '9' ) {
        return character - '0' + 52;
    }
    if ( character == ( bUrlSafe ? '-' : '+' ) ) {
        return 62;
    }
    if ( character == ( bUrlSafe ? '_' : '/' ) ) {
        return 63;
    }
    return -1;
}

CYPHER_NODISCARD bool_t Hex_CharacterIsValid(
    char character ) noexcept
{
    return ( character >= '0' && character <= '9' ) ||
           ( character >= 'a' && character <= 'f' ) ||
           ( character >= 'A' && character <= 'F' );
}

} // namespace

bool_t SecurityHex_EncodedSize(
    usize cbBinary,
    usize *pSizeOut ) noexcept
{
    // Two printable characters per byte plus the terminating null expected by
    // the encoding API.
    if ( pSizeOut == nullptr ||
         cbBinary > ( common::CY_USIZE_MAX - 1u ) / 2u ) {
        return CY_FALSE;
    }
    *pSizeOut = cbBinary * 2u + 1u;
    return CY_TRUE;
}

bool_t SecurityHex_DecodedSize(
    string_view_t encoded,
    usize *pSizeOut ) noexcept
{
    if ( pSizeOut == nullptr || !common::StringView_IsValid( encoded ) ||
         ( encoded.cchLength & 1u ) != 0u ) {
        return CY_FALSE;
    }
    for ( usize iCharacter = 0u;
          iCharacter < encoded.cchLength;
          ++iCharacter ) {
        if ( !Hex_CharacterIsValid( encoded.pData[iCharacter] ) ) {
            return CY_FALSE;
        }
    }
    *pSizeOut = encoded.cchLength / 2u;
    return CY_TRUE;
}

security_status_t SecurityHex_Encode(
    binary_block_t binary,
    char *pEncodedOut,
    usize cchEncodedCapacity,
    usize *pCharactersWrittenOut ) noexcept
{
    const bool_t bValidInput = common::BinaryBlock_IsValid( binary );
    CY_ASSERT_MSG( bValidInput, "Hex encoding requires a valid binary range." );
    if ( !bValidInput || pCharactersWrittenOut == nullptr ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    usize cchRequired = 0u;
    if ( !SecurityHex_EncodedSize( binary.cbSize, &cchRequired ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pEncodedOut == nullptr || cchEncodedCapacity < cchRequired ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }
    if ( common::Cy_MemRangesOverlap(
             pEncodedOut,
             cchRequired,
             binary.pData,
             binary.cbSize ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    if ( sodium_bin2hex(
             pEncodedOut,
             cchEncodedCapacity,
             binary.pData,
             binary.cbSize ) == nullptr ) {
        Security_ZeroMemory( pEncodedOut, cchRequired );
        return security_status_t::OPERATION_FAILED;
    }
    *pCharactersWrittenOut = cchRequired - 1u;
    return security_status_t::OK;
}

security_status_t SecurityHex_Decode(
    string_view_t encoded,
    void *pBinaryOut,
    usize cbBinaryCapacity,
    usize *pBytesWrittenOut ) noexcept
{
    if ( pBytesWrittenOut == nullptr ||
         !common::StringView_IsValid( encoded ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    // Validate the complete spelling and determine exact output size before
    // touching caller storage.
    usize cbRequired = 0u;
    if ( !SecurityHex_DecodedSize( encoded, &cbRequired ) ) {
        return security_status_t::INVALID_ENCODING;
    }
    if ( cbBinaryCapacity < cbRequired ||
         ( cbRequired > 0u && pBinaryOut == nullptr ) ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }
    if ( common::Cy_MemRangesOverlap(
             pBinaryOut,
             cbRequired,
             encoded.pData,
             encoded.cchLength ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    byte emptyOutput = 0u;
    byte *pOutput = cbRequired == 0u
        ? &emptyOutput
        : static_cast<byte *>( pBinaryOut );
    const char *pInput = encoded.cchLength == 0u
        ? &g_emptyEncodedInput
        : encoded.pData;
    usize cbWritten = 0u;
    const char *pEnd = nullptr;
    const int result = sodium_hex2bin(
        pOutput,
        cbBinaryCapacity,
        pInput,
        encoded.cchLength,
        nullptr,
        &cbWritten,
        &pEnd );
    if ( result != 0 || cbWritten != cbRequired ||
         pEnd != pInput + encoded.cchLength ) {
        Security_ZeroMemory( pBinaryOut, cbRequired );
        return security_status_t::INVALID_ENCODING;
    }
    *pBytesWrittenOut = cbWritten;
    return security_status_t::OK;
}

bool_t SecurityBase64_EncodedSize(
    usize cbBinary,
    base64_variant_t variant,
    usize *pSizeOut ) noexcept
{
    if ( pSizeOut == nullptr || !Base64Variant_IsValid( variant ) ) {
        return CY_FALSE;
    }

    // Every complete three-byte group becomes four characters. The tail differs
    // only by whether this variant retains '=' padding.
    const usize cGroups = cbBinary / 3u;
    const usize cRemainder = cbBinary % 3u;
    if ( cGroups > ( common::CY_USIZE_MAX - 1u ) / 4u ) {
        return CY_FALSE;
    }
    usize cchEncoded = cGroups * 4u;
    const usize cchTail = cRemainder == 0u
        ? 0u
        : ( Base64Variant_UsesPadding( variant ) ? 4u : cRemainder + 1u );
    if ( cchEncoded > common::CY_USIZE_MAX - cchTail - 1u ) {
        return CY_FALSE;
    }
    *pSizeOut = cchEncoded + cchTail + 1u;
    return CY_TRUE;
}

bool_t SecurityBase64_DecodedSize(
    string_view_t encoded,
    base64_variant_t variant,
    usize *pSizeOut ) noexcept
{
    if ( pSizeOut == nullptr || !Base64Variant_IsValid( variant ) ||
         !common::StringView_IsValid( encoded ) ) {
        return CY_FALSE;
    }
    if ( encoded.cchLength == 0u ) {
        *pSizeOut = 0u;
        return CY_TRUE;
    }

    const bool_t bPadded = Base64Variant_UsesPadding( variant );
    const bool_t bUrlSafe = Base64Variant_IsUrlSafe( variant );
    usize cPadding = 0u;
    if ( bPadded ) {
        if ( encoded.cchLength % 4u != 0u ) {
            return CY_FALSE;
        }
        if ( encoded.pData[encoded.cchLength - 1u] == '=' ) {
            ++cPadding;
        }
        if ( encoded.cchLength > 1u &&
             encoded.pData[encoded.cchLength - 2u] == '=' ) {
            ++cPadding;
        }
    } else if ( encoded.cchLength % 4u == 1u ) {
        return CY_FALSE;
    }

    const usize cDataCharacters = encoded.cchLength - cPadding;
    for ( usize iCharacter = 0u;
          iCharacter < cDataCharacters;
          ++iCharacter ) {
        if ( Base64_CharacterValue(
                 encoded.pData[iCharacter],
                 bUrlSafe ) < 0 ) {
            return CY_FALSE;
        }
    }
    for ( usize iCharacter = cDataCharacters;
          iCharacter < encoded.cchLength;
          ++iCharacter ) {
        if ( encoded.pData[iCharacter] != '=' ) {
            return CY_FALSE;
        }
    }
    if ( !bPadded ) {
        for ( usize iCharacter = 0u;
              iCharacter < encoded.cchLength;
              ++iCharacter ) {
            if ( encoded.pData[iCharacter] == '=' ) {
                return CY_FALSE;
            }
        }
    }

    const usize cRemainder = cDataCharacters % 4u;
    if ( cRemainder == 1u ||
         ( bPadded &&
           ( ( cPadding == 1u && cRemainder != 3u ) ||
             ( cPadding == 2u && cRemainder != 2u ) ) ) ) {
        return CY_FALSE;
    }
    // Unused low bits in the final sextet must be zero. Rejecting alternate
    // spellings keeps one canonical Base64 representation for each byte string.
    if ( cRemainder == 2u ) {
        const int nLast = Base64_CharacterValue(
            encoded.pData[cDataCharacters - 1u],
            bUrlSafe );
        if ( ( nLast & 0x0f ) != 0 ) {
            return CY_FALSE;
        }
    } else if ( cRemainder == 3u ) {
        const int nLast = Base64_CharacterValue(
            encoded.pData[cDataCharacters - 1u],
            bUrlSafe );
        if ( ( nLast & 0x03 ) != 0 ) {
            return CY_FALSE;
        }
    }

    const usize cCompleteGroups = cDataCharacters / 4u;
    if ( cCompleteGroups > common::CY_USIZE_MAX / 3u ) {
        return CY_FALSE;
    }
    *pSizeOut = cCompleteGroups * 3u +
                ( cRemainder == 2u ? 1u : cRemainder == 3u ? 2u : 0u );
    return CY_TRUE;
}

security_status_t SecurityBase64_Encode(
    binary_block_t binary,
    base64_variant_t variant,
    char *pEncodedOut,
    usize cchEncodedCapacity,
    usize *pCharactersWrittenOut ) noexcept
{
    const bool_t bValidInput = common::BinaryBlock_IsValid( binary );
    const bool_t bValidVariant = Base64Variant_IsValid( variant );
    CY_ASSERT_MSG( bValidInput, "Base64 encoding requires a valid binary range." );
    CY_ASSERT_MSG( bValidVariant, "Base64 encoding requires a valid variant." );
    if ( !bValidInput || !bValidVariant || pCharactersWrittenOut == nullptr ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    usize cchRequired = 0u;
    if ( !SecurityBase64_EncodedSize( binary.cbSize, variant, &cchRequired ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    if ( pEncodedOut == nullptr || cchEncodedCapacity < cchRequired ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }
    if ( common::Cy_MemRangesOverlap(
             pEncodedOut,
             cchRequired,
             binary.pData,
             binary.cbSize ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    if ( sodium_bin2base64(
             pEncodedOut,
             cchEncodedCapacity,
             binary.pData,
             binary.cbSize,
             Base64Variant_Native( variant ) ) == nullptr ) {
        Security_ZeroMemory( pEncodedOut, cchRequired );
        return security_status_t::OPERATION_FAILED;
    }
    *pCharactersWrittenOut = cchRequired - 1u;
    return security_status_t::OK;
}

security_status_t SecurityBase64_Decode(
    string_view_t encoded,
    base64_variant_t variant,
    void *pBinaryOut,
    usize cbBinaryCapacity,
    usize *pBytesWrittenOut ) noexcept
{
    if ( pBytesWrittenOut == nullptr ||
         !Base64Variant_IsValid( variant ) ||
         !common::StringView_IsValid( encoded ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }
    usize cbRequired = 0u;
    if ( !SecurityBase64_DecodedSize( encoded, variant, &cbRequired ) ) {
        return security_status_t::INVALID_ENCODING;
    }
    if ( cbBinaryCapacity < cbRequired ||
         ( cbRequired > 0u && pBinaryOut == nullptr ) ) {
        return security_status_t::BUFFER_TOO_SMALL;
    }
    if ( common::Cy_MemRangesOverlap(
             pBinaryOut,
             cbRequired,
             encoded.pData,
             encoded.cchLength ) ) {
        return security_status_t::INVALID_ARGUMENT;
    }

    byte emptyOutput = 0u;
    byte *pOutput = cbRequired == 0u
        ? &emptyOutput
        : static_cast<byte *>( pBinaryOut );
    const char *pInput = encoded.cchLength == 0u
        ? &g_emptyEncodedInput
        : encoded.pData;
    usize cbWritten = 0u;
    const char *pEnd = nullptr;
    const int result = sodium_base642bin(
        pOutput,
        cbBinaryCapacity,
        pInput,
        encoded.cchLength,
        nullptr,
        &cbWritten,
        &pEnd,
        Base64Variant_Native( variant ) );
    // Requiring pEnd at the exact view boundary rejects valid prefixes followed
    // by trailing garbage.
    if ( result != 0 || cbWritten != cbRequired ||
         pEnd != pInput + encoded.cchLength ) {
        Security_ZeroMemory( pBinaryOut, cbRequired );
        return security_status_t::INVALID_ENCODING;
    }
    *pBytesWrittenOut = cbWritten;
    return security_status_t::OK;
}

} // namespace cypher::security
