//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Hash.cpp
//  Purpose: Implements engine-default non-cryptographic hashes.
//  Details: The facade selects XXH32 and XXH3-64 for fast deterministic keys while
//           preserving allocation-free ASCII case folding and stable combination.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Hash.h"
#include "CypherCommon_HashXXH.h"

namespace cypher::common
{

namespace
{

CYPHER_NODISCARD hash32_t Hash32_Avalanche( hash32_t value ) noexcept
{
    value ^= value >> 16u;
    value *= 0x85EBCA6Bu;
    value ^= value >> 13u;
    value *= 0xC2B2AE35u;
    return value ^ ( value >> 16u );
}

CYPHER_NODISCARD hash64_t Hash64_Avalanche( hash64_t value ) noexcept
{
    value ^= value >> 30u;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27u;
    value *= 0x94D049BB133111EBull;
    return value ^ ( value >> 31u );
}

} // namespace

hash32_t Hash32_Data( binary_block_t data, hash32_t seed ) noexcept
{
    return HashXXH32_Data( data, seed );
}

hash64_t Hash64_Data( binary_block_t data, hash64_t seed ) noexcept
{
    return HashXXH3_64_Data( data, seed );
}

hash32_t Hash32_String( string_view_t text, hash32_t seed ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "Hash32_String requires a valid string view." );
    if ( !bValidText ) {
        return 0u;
    }
    return Hash32_Data( BinaryBlock_FromData( text.pData, text.cchLength ), seed );
}

hash64_t Hash64_String( string_view_t text, hash64_t seed ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "Hash64_String requires a valid string view." );
    if ( !bValidText ) {
        return 0u;
    }
    return Hash64_Data( BinaryBlock_FromData( text.pData, text.cchLength ), seed );
}

hash32_t Hash32_StringInsensitiveAscii(
    string_view_t text,
    hash32_t seed ) noexcept
{
    return HashXXH32_StringInsensitiveAscii( text, seed );
}

hash64_t Hash64_StringInsensitiveAscii(
    string_view_t text,
    hash64_t seed ) noexcept
{
    return HashXXH3_64_StringInsensitiveAscii( text, seed );
}

hash32_t Hash32_Combine( hash32_t left, hash32_t right ) noexcept
{
    const hash32_t combined =
        left ^ ( right + 0x9E3779B9u + ( left << 6u ) + ( left >> 2u ) );
    return Hash32_Avalanche( combined );
}

hash64_t Hash64_Combine( hash64_t left, hash64_t right ) noexcept
{
    const hash64_t combined =
        left ^ ( right + 0x9E3779B97F4A7C15ull +
                 ( left << 6u ) + ( left >> 2u ) );
    return Hash64_Avalanche( combined );
}

} // namespace cypher::common
