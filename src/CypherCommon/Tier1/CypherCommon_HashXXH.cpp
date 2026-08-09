//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashXXH.cpp
//  Purpose: Implements the xxHash-backed high-throughput hash adapter.
//  Details: Third-party declarations remain confined to this translation unit;
//           callers receive stable Cypher value types and validity behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_HashXXH.h"
#include "CypherCommon_Char.h"

#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace cypher::common
{

namespace
{

constexpr byte g_emptyHashInput = 0u;
constexpr usize CY_XXHASH_CASE_FOLD_CHUNK_SIZE = 256u;

CYPHER_NODISCARD const void *HashXXH_Input( binary_block_t data ) noexcept
{
    return data.cbSize == 0u ? &g_emptyHashInput : data.pData;
}

CYPHER_NODISCARD bool_t HashXXH_Validate( binary_block_t data ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    CY_ASSERT_MSG( bValidData, "xxHash requires a valid binary block." );
    return bValidData;
}

} // namespace

hash32_t HashXXH32_Data( binary_block_t data, hash32_t seed ) noexcept
{
    if ( !HashXXH_Validate( data ) ) {
        return 0u;
    }
    return static_cast<hash32_t>(
        XXH32( HashXXH_Input( data ), data.cbSize, seed ) );
}

hash64_t HashXXH64_Data( binary_block_t data, hash64_t seed ) noexcept
{
    if ( !HashXXH_Validate( data ) ) {
        return 0u;
    }
    return static_cast<hash64_t>(
        XXH64( HashXXH_Input( data ), data.cbSize, seed ) );
}

hash64_t HashXXH3_64_Data( binary_block_t data, hash64_t seed ) noexcept
{
    if ( !HashXXH_Validate( data ) ) {
        return 0u;
    }
    return static_cast<hash64_t>(
        XXH3_64bits_withSeed( HashXXH_Input( data ), data.cbSize, seed ) );
}

hash128_t HashXXH3_128_Data( binary_block_t data, hash64_t seed ) noexcept
{
    if ( !HashXXH_Validate( data ) ) {
        return {};
    }

    const XXH128_hash_t hash =
        XXH3_128bits_withSeed( HashXXH_Input( data ), data.cbSize, seed );
    return { hash.low64, hash.high64 };
}

hash32_t HashXXH32_StringInsensitiveAscii(
    string_view_t text,
    hash32_t seed ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "xxHash string hashing requires a valid string view." );
    if ( !bValidText ) {
        return 0u;
    }

    XXH32_state_t state{};
    if ( XXH32_reset( &state, seed ) != XXH_OK ) {
        CY_ASSERT_MSG( CY_FALSE, "XXH32 state reset failed." );
        return 0u;
    }

    byte folded[CY_XXHASH_CASE_FOLD_CHUNK_SIZE]{};
    usize iOffset = 0u;
    while ( iOffset < text.cchLength ) {
        const usize cchRemaining = text.cchLength - iOffset;
        const usize cchChunk = cchRemaining < CY_XXHASH_CASE_FOLD_CHUNK_SIZE
            ? cchRemaining
            : CY_XXHASH_CASE_FOLD_CHUNK_SIZE;
        for ( usize iChar = 0u; iChar < cchChunk; ++iChar ) {
            folded[iChar] = static_cast<byte>(
                Char_ToLowerAscii( text.pData[iOffset + iChar] ) );
        }
        if ( XXH32_update( &state, folded, cchChunk ) != XXH_OK ) {
            CY_ASSERT_MSG( CY_FALSE, "XXH32 state update failed." );
            return 0u;
        }
        iOffset += cchChunk;
    }
    return static_cast<hash32_t>( XXH32_digest( &state ) );
}

hash64_t HashXXH3_64_StringInsensitiveAscii(
    string_view_t text,
    hash64_t seed ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "xxHash string hashing requires a valid string view." );
    if ( !bValidText ) {
        return 0u;
    }

    XXH3_state_t state{};
    if ( XXH3_64bits_reset_withSeed( &state, seed ) != XXH_OK ) {
        CY_ASSERT_MSG( CY_FALSE, "XXH3-64 state reset failed." );
        return 0u;
    }

    byte folded[CY_XXHASH_CASE_FOLD_CHUNK_SIZE]{};
    usize iOffset = 0u;
    while ( iOffset < text.cchLength ) {
        const usize cchRemaining = text.cchLength - iOffset;
        const usize cchChunk = cchRemaining < CY_XXHASH_CASE_FOLD_CHUNK_SIZE
            ? cchRemaining
            : CY_XXHASH_CASE_FOLD_CHUNK_SIZE;
        for ( usize iChar = 0u; iChar < cchChunk; ++iChar ) {
            folded[iChar] = static_cast<byte>(
                Char_ToLowerAscii( text.pData[iOffset + iChar] ) );
        }
        if ( XXH3_64bits_update( &state, folded, cchChunk ) != XXH_OK ) {
            CY_ASSERT_MSG( CY_FALSE, "XXH3-64 state update failed." );
            return 0u;
        }
        iOffset += cchChunk;
    }
    return static_cast<hash64_t>( XXH3_64bits_digest( &state ) );
}

bool_t Hash128_Equals( hash128_t left, hash128_t right ) noexcept
{
    return left.low == right.low && left.high == right.high;
}

} // namespace cypher::common
