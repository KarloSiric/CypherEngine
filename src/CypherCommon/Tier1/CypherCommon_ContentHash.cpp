//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ContentHash.cpp
//  Purpose: Implements stable 128-bit content fingerprints.
//  Details: Content bytes use XXH3-128; composition and text conversion use fixed
//           field ordering so results remain portable across host endianness.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ContentHash.h"
#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace
{

constexpr char g_contentHashHexDigits[] = "0123456789abcdef";

void ContentHash_StoreU64Little( byte *pDest, u64 value ) noexcept
{
    for ( u32 iByte = 0u; iByte < 8u; ++iByte ) {
        pDest[iByte] = static_cast<byte>( value >> ( iByte * 8u ) );
    }
}

void ContentHash_WriteHexWord( char *pDest, u64 value ) noexcept
{
    for ( usize iDigit = 0u; iDigit < 16u; ++iDigit ) {
        const u32 nShift = static_cast<u32>( ( 15u - iDigit ) * 4u );
        pDest[iDigit] = g_contentHashHexDigits[( value >> nShift ) & 0x0Fu];
    }
}

} // namespace

content_hash_t ContentHash_Data( binary_block_t data ) noexcept
{
    const hash128_t hash = HashXXH3_128_Data( data );
    return { hash.low, hash.high };
}

content_hash_t ContentHash_String( string_view_t text ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "ContentHash_String requires a valid string view." );
    if ( !bValidText ) {
        return CY_CONTENT_HASH_INVALID;
    }
    return ContentHash_Data(
        BinaryBlock_FromData( text.pData, text.cchLength ) );
}

content_hash_t ContentHash_Combine(
    content_hash_t left,
    content_hash_t right ) noexcept
{
    if ( !ContentHash_IsValid( left ) || !ContentHash_IsValid( right ) ) {
        return CY_CONTENT_HASH_INVALID;
    }

    // Serialize both hashes in an explicit byte order before rehashing. Native
    // struct layout would make composition depend on host endianness and padding.
    byte canonical[32]{};
    ContentHash_StoreU64Little( canonical, left.low );
    ContentHash_StoreU64Little( canonical + 8u, left.high );
    ContentHash_StoreU64Little( canonical + 16u, right.low );
    ContentHash_StoreU64Little( canonical + 24u, right.high );
    return ContentHash_Data( { canonical, sizeof( canonical ) } );
}

bool_t ContentHash_IsValid( content_hash_t hash ) noexcept
{
    // All-zero is reserved as the invalid/uncomputed sentinel.
    return hash.low != 0u || hash.high != 0u;
}

bool_t ContentHash_Equals( content_hash_t left, content_hash_t right ) noexcept
{
    return left.low == right.low && left.high == right.high;
}

usize ContentHash_ToHex(
    content_hash_t hash,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest =
        pDest != nullptr && cchDest >= CY_CONTENT_HASH_HEX_CAPACITY;
    CY_ASSERT_MSG(
        bValidDest,
        "ContentHash_ToHex requires storage for 32 digits and a terminator." );
    if ( !bValidDest ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return 0u;
    }

    // Print high word first so lexical text order matches the conventional
    // most-significant-to-least-significant 128-bit representation.
    ContentHash_WriteHexWord( pDest, hash.high );
    ContentHash_WriteHexWord( pDest + 16u, hash.low );
    pDest[CY_CONTENT_HASH_HEX_LENGTH] = '\0';
    return CY_CONTENT_HASH_HEX_LENGTH;
}

bool_t ContentHash_FromHex(
    string_view_t text,
    content_hash_t *pHashOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pHashOut != nullptr;
    CY_ASSERT_MSG( bValidText, "ContentHash_FromHex requires a valid string view." );
    CY_ASSERT_MSG( bValidOutput, "ContentHash_FromHex requires output storage." );
    if ( !bValidText || !bValidOutput ||
         text.cchLength != CY_CONTENT_HASH_HEX_LENGTH ) {
        return CY_FALSE;
    }

    content_hash_t parsed{};
    for ( usize iDigit = 0u; iDigit < CY_CONTENT_HASH_HEX_LENGTH; ++iDigit ) {
        const u8 nDigit = Char_HexValueAscii( text.pData[iDigit] );
        if ( nDigit == CY_CHAR_INVALID_DIGIT_VALUE ) {
            return CY_FALSE;
        }

        u64 *pWord = iDigit < 16u ? &parsed.high : &parsed.low;
        *pWord = ( *pWord << 4u ) | nDigit;
    }

    *pHashOut = parsed;
    return CY_TRUE;
}

} // namespace cypher::common
