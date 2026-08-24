//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_Hash.h
//  Purpose: Declares cryptographic digest and keyed short-hash services.
//  Details: BLAKE2b provides streaming digests and keyed authentication material;
//           SipHash protects short attacker-controlled hash-table keys from flooding.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_HASH_H
#define CYPHER_SECURITY_HASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSecurity_Types.h"
#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_Defines.h"

namespace cypher::security
{

using common::binary_block_t;

inline constexpr usize CY_SECURITY_DIGEST_MIN_SIZE = 16u;
inline constexpr usize CY_SECURITY_DIGEST_DEFAULT_SIZE = 32u;
inline constexpr usize CY_SECURITY_DIGEST_MAX_SIZE = 64u;
inline constexpr usize CY_SECURITY_DIGEST_KEY_MIN_SIZE = 16u;
inline constexpr usize CY_SECURITY_DIGEST_KEY_MAX_SIZE = 64u;
inline constexpr usize CY_SECURITY_SHORT_HASH_KEY_SIZE = 16u;
inline constexpr usize CY_SECURITY_SHORT_HASH_SIZE = 8u;
inline constexpr usize CY_SECURITY_DIGEST_STREAM_STORAGE_SIZE = 512u;
inline constexpr usize CY_SECURITY_DIGEST_STREAM_STORAGE_ALIGNMENT = 64u;

struct security_digest_t {
    byte bytes[CY_SECURITY_DIGEST_MAX_SIZE]{}; // Digest bytes; only the leading cbSize are valid.
    usize cbSize{ 0u };                        // Selected BLAKE2b output size in bytes.
};

struct security_short_hash_key_t {
    byte bytes[CY_SECURITY_SHORT_HASH_KEY_SIZE]{}; // Secret SipHash key material.
};

struct security_short_hash_t {
    byte bytes[CY_SECURITY_SHORT_HASH_SIZE]{}; // Fixed-width SipHash result bytes.
};

struct security_digest_stream_t {
    security_digest_stream_t() noexcept = default;
    CYPHER_SECURITY_API ~security_digest_stream_t() noexcept;
    CYPHER_NO_COPY_MOVE( security_digest_stream_t );

    alignas( CY_SECURITY_DIGEST_STREAM_STORAGE_ALIGNMENT )
        byte storage[CY_SECURITY_DIGEST_STREAM_STORAGE_SIZE]{}; // Opaque libsodium state.
    usize cbDigest{ 0u };                                    // Requested final digest width.
    bool_t bActive{ CY_FALSE };                              // Begin succeeded and End is pending.
};

// Computes an unkeyed or keyed BLAKE2b digest. An empty key selects unkeyed mode.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityDigest_Data(
    binary_block_t data,
    binary_block_t key,
    usize cbDigest,
    security_digest_t *pDigestOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityDigest_Begin(
    security_digest_stream_t *pStream,
    binary_block_t key,
    usize cbDigest = CY_SECURITY_DIGEST_DEFAULT_SIZE ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityDigest_Update(
    security_digest_stream_t *pStream,
    binary_block_t data ) noexcept;

// Abandons an active digest and securely clears all backend state.
CYPHER_SECURITY_API void SecurityDigest_Cancel(
    security_digest_stream_t *pStream ) noexcept;

// Finalization consumes and securely clears the stream state.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityDigest_End(
    security_digest_stream_t *pStream,
    security_digest_t *pDigestOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityDigest_IsValid(
    const security_digest_t &digest ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityDigest_Equals(
    const security_digest_t &left,
    const security_digest_t &right ) noexcept;

// Generates a random SipHash key from the OS CSPRNG. Callers normally keep one
// key for the lifetime of the protected hash-table domain.
CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityShortHash_GenerateKey(
    security_short_hash_key_t *pKeyOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
security_status_t SecurityShortHash_Data(
    binary_block_t data,
    const security_short_hash_key_t &key,
    security_short_hash_t *pHashOut ) noexcept;

CYPHER_NODISCARD CYPHER_SECURITY_API
bool_t SecurityShortHash_Equals(
    const security_short_hash_t &left,
    const security_short_hash_t &right ) noexcept;

// Converts SipHash output to a numeric table hash using fixed little-endian order.
CYPHER_NODISCARD CYPHER_SECURITY_API
hash64_t SecurityShortHash_ToU64(
    const security_short_hash_t &hash ) noexcept;

} // namespace cypher::security

#endif // CYPHER_SECURITY_HASH_H
