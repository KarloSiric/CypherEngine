//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashXXH.h
//  Purpose: Declares the xxHash-backed high-throughput hash adapter.
//  Details: The implementation wraps the pinned xxHash dependency behind stable
//           Cypher types so third-party headers do not leak into engine interfaces.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HASHXXH_H
#define CYPHER_COMMON_TIER1_HASHXXH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct hash128_t {
    u64 low{ 0u };
    u64 high{ 0u };
};

// Opaque storage for the pinned xxHash streaming state. Third-party types remain
// outside public headers; the implementation verifies size and alignment.
inline constexpr usize CY_XXH3_STREAM_STORAGE_SIZE = 640u;
inline constexpr usize CY_XXH3_STREAM_STORAGE_ALIGNMENT = 64u;

enum class hash_xxh3_stream_mode_t : u8 {
    HASH_64 = 0u,
    HASH_128
};

struct hash_xxh3_stream_t {
    hash_xxh3_stream_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( hash_xxh3_stream_t );

    alignas( CY_XXH3_STREAM_STORAGE_ALIGNMENT )
        byte storage[CY_XXH3_STREAM_STORAGE_SIZE]{};
    hash64_t seed{ 0u };
    hash_xxh3_stream_mode_t mode{ hash_xxh3_stream_mode_t::HASH_64 };
    bool_t bInitialized{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t HashXXH32_Data( binary_block_t data, hash32_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashXXH64_Data( binary_block_t data, hash64_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashXXH3_64_Data( binary_block_t data, hash64_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash128_t HashXXH3_128_Data( binary_block_t data, hash64_t seed = 0u ) noexcept;

// Initializes or resets a caller-owned streaming state.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t HashXXH3_StreamInit(
    hash_xxh3_stream_t *pStream,
    hash_xxh3_stream_mode_t mode,
    hash64_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t HashXXH3_StreamReset(
    hash_xxh3_stream_t *pStream,
    hash64_t seed ) noexcept;

// Appends bytes without finalizing the stream. Empty valid blocks are accepted.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t HashXXH3_StreamUpdate(
    hash_xxh3_stream_t *pStream,
    binary_block_t data ) noexcept;

// Produces a snapshot digest. Calling digest does not consume the stream.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t HashXXH3_StreamDigest64(
    const hash_xxh3_stream_t *pStream,
    hash64_t *pHashOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t HashXXH3_StreamDigest128(
    const hash_xxh3_stream_t *pStream,
    hash128_t *pHashOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t HashXXH3_StreamIsValid(
    const hash_xxh3_stream_t *pStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t HashXXH32_StringInsensitiveAscii(
    string_view_t text,
    hash32_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashXXH3_64_StringInsensitiveAscii(
    string_view_t text,
    hash64_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Hash128_Equals( hash128_t left, hash128_t right ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHXXH_H
