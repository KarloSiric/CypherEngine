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

#include <new>

namespace cypher::common
{

namespace
{

constexpr byte g_emptyHashInput = 0u; // xxHash still requires an address for an empty block.
constexpr usize CY_XXHASH_CASE_FOLD_CHUNK_SIZE = 256u; // Bounds temporary stack use.

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

CYPHER_NODISCARD XXH3_state_t *HashXXH_StreamState(
    hash_xxh3_stream_t *pStream ) noexcept
{
    // The public header keeps the third-party type opaque. Its lifetime begins
    // in this byte storage through placement construction below.
    return std::launder(
        reinterpret_cast<XXH3_state_t *>( pStream->storage ) );
}

CYPHER_NODISCARD const XXH3_state_t *HashXXH_StreamState(
    const hash_xxh3_stream_t *pStream ) noexcept
{
    return std::launder(
        reinterpret_cast<const XXH3_state_t *>( pStream->storage ) );
}

CYPHER_NODISCARD bool_t HashXXH_StreamModeIsValid(
    hash_xxh3_stream_mode_t mode ) noexcept
{
    return mode == hash_xxh3_stream_mode_t::HASH_64 ||
           mode == hash_xxh3_stream_mode_t::HASH_128;
}

} // namespace

static_assert(
    sizeof( XXH3_state_t ) <= CY_XXH3_STREAM_STORAGE_SIZE,
    "Pinned xxHash streaming state exceeds Cypher's opaque storage." );
static_assert(
    alignof( XXH3_state_t ) <= CY_XXH3_STREAM_STORAGE_ALIGNMENT,
    "Pinned xxHash streaming state exceeds Cypher's opaque alignment." );

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

bool_t HashXXH3_StreamInit(
    hash_xxh3_stream_t *pStream,
    hash_xxh3_stream_mode_t mode,
    hash64_t seed ) noexcept
{
    const bool_t bValidStream = pStream != nullptr;
    const bool_t bValidMode = HashXXH_StreamModeIsValid( mode );
    CY_ASSERT_MSG( bValidStream, "xxHash stream initialization requires storage." );
    CY_ASSERT_MSG( bValidMode, "xxHash stream initialization requires a valid mode." );
    if ( !bValidStream || !bValidMode ) {
        return CY_FALSE;
    }

    ::new ( static_cast<void *>( pStream->storage ) ) XXH3_state_t{};
    pStream->mode = mode;
    pStream->seed = seed;
    pStream->bInitialized = CY_TRUE;

    const XXH_errorcode result = mode == hash_xxh3_stream_mode_t::HASH_64
        ? XXH3_64bits_reset_withSeed( HashXXH_StreamState( pStream ), seed )
        : XXH3_128bits_reset_withSeed( HashXXH_StreamState( pStream ), seed );
    if ( result != XXH_OK ) {
        pStream->bInitialized = CY_FALSE;
        CY_ASSERT_MSG( CY_FALSE, "xxHash stream reset failed." );
        return CY_FALSE;
    }
    return CY_TRUE;
}

bool_t HashXXH3_StreamReset(
    hash_xxh3_stream_t *pStream,
    hash64_t seed ) noexcept
{
    const bool_t bValidStream = HashXXH3_StreamIsValid( pStream );
    CY_ASSERT_MSG( bValidStream, "xxHash stream reset requires initialized state." );
    return bValidStream
        ? HashXXH3_StreamInit( pStream, pStream->mode, seed )
        : CY_FALSE;
}

bool_t HashXXH3_StreamUpdate(
    hash_xxh3_stream_t *pStream,
    binary_block_t data ) noexcept
{
    const bool_t bValidStream = HashXXH3_StreamIsValid( pStream );
    const bool_t bValidData = HashXXH_Validate( data );
    CY_ASSERT_MSG( bValidStream, "xxHash stream update requires initialized state." );
    if ( !bValidStream || !bValidData ) {
        return CY_FALSE;
    }

    const XXH_errorcode result =
        pStream->mode == hash_xxh3_stream_mode_t::HASH_64
            ? XXH3_64bits_update(
                  HashXXH_StreamState( pStream ),
                  HashXXH_Input( data ),
                  data.cbSize )
            : XXH3_128bits_update(
                  HashXXH_StreamState( pStream ),
                  HashXXH_Input( data ),
                  data.cbSize );
    if ( result != XXH_OK ) {
        CY_ASSERT_MSG( CY_FALSE, "xxHash stream update failed." );
        return CY_FALSE;
    }
    return CY_TRUE;
}

bool_t HashXXH3_StreamDigest64(
    const hash_xxh3_stream_t *pStream,
    hash64_t *pHashOut ) noexcept
{
    const bool_t bValidStream =
        HashXXH3_StreamIsValid( pStream ) &&
        pStream->mode == hash_xxh3_stream_mode_t::HASH_64;
    const bool_t bValidOutput = pHashOut != nullptr;
    CY_ASSERT_MSG( bValidStream, "xxHash 64-bit digest requires a 64-bit stream." );
    CY_ASSERT_MSG( bValidOutput, "xxHash digest requires output storage." );
    if ( !bValidStream || !bValidOutput ) {
        return CY_FALSE;
    }

    // xxHash digest calls do not consume the state; callers may continue
    // updating the same stream after taking an intermediate digest.
    *pHashOut = static_cast<hash64_t>(
        XXH3_64bits_digest( HashXXH_StreamState( pStream ) ) );
    return CY_TRUE;
}

bool_t HashXXH3_StreamDigest128(
    const hash_xxh3_stream_t *pStream,
    hash128_t *pHashOut ) noexcept
{
    const bool_t bValidStream =
        HashXXH3_StreamIsValid( pStream ) &&
        pStream->mode == hash_xxh3_stream_mode_t::HASH_128;
    const bool_t bValidOutput = pHashOut != nullptr;
    CY_ASSERT_MSG( bValidStream, "xxHash 128-bit digest requires a 128-bit stream." );
    CY_ASSERT_MSG( bValidOutput, "xxHash digest requires output storage." );
    if ( !bValidStream || !bValidOutput ) {
        return CY_FALSE;
    }

    const XXH128_hash_t hash =
        XXH3_128bits_digest( HashXXH_StreamState( pStream ) );
    *pHashOut = { hash.low64, hash.high64 };
    return CY_TRUE;
}

bool_t HashXXH3_StreamIsValid(
    const hash_xxh3_stream_t *pStream ) noexcept
{
    return pStream != nullptr &&
           pStream->bInitialized &&
           HashXXH_StreamModeIsValid( pStream->mode );
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

    // Fold a bounded chunk at a time so hashing long paths never allocates or
    // requires a second full-size lowercase copy.
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

    // Keep ASCII case-insensitive identity independent of the process locale.
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
