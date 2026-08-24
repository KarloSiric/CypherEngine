//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StableHash.h
//  Purpose: Declares versioned deterministic hashing for persisted identifiers.
//  Details: The writer canonicalizes type, byte order, lengths, floating-point
//           edge cases, domain, and schema version before producing an XXH3-64 ID.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STABLEHASH_H
#define CYPHER_COMMON_TIER1_STABLEHASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_HashXXH.h"

namespace cypher::common
{

using stable_hash_domain_t = u64;

inline constexpr u32 CY_STABLE_HASH_CONTRACT_VERSION = 1u; // Canonical type-tag encoding revision.

struct stable_hash_builder_t {
    stable_hash_builder_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( stable_hash_builder_t );

    hash_xxh3_stream_t stream{}; // Underlying deterministic XXH3 byte stream.
    bool_t bActive{ CY_FALSE };  // Begin succeeded and End has not consumed state.
};

// Starts a canonical stream for one semantic domain and schema version.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_Begin(
    stable_hash_builder_t *pBuilder,
    stable_hash_domain_t nDomain,
    u32 nSchemaVersion ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_IsActive(
    const stable_hash_builder_t *pBuilder ) noexcept;

// Every write contributes a type tag before its canonical payload.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteBool(
    stable_hash_builder_t *pBuilder,
    bool_t value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteU8(
    stable_hash_builder_t *pBuilder,
    u8 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteU16(
    stable_hash_builder_t *pBuilder,
    u16 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteU32(
    stable_hash_builder_t *pBuilder,
    u32 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteU64(
    stable_hash_builder_t *pBuilder,
    u64 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteI8(
    stable_hash_builder_t *pBuilder,
    i8 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteI16(
    stable_hash_builder_t *pBuilder,
    i16 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteI32(
    stable_hash_builder_t *pBuilder,
    i32 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteI64(
    stable_hash_builder_t *pBuilder,
    i64 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteF32(
    stable_hash_builder_t *pBuilder,
    f32 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteF64(
    stable_hash_builder_t *pBuilder,
    f64 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteBytes(
    stable_hash_builder_t *pBuilder,
    binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteString(
    stable_hash_builder_t *pBuilder,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteHash64(
    stable_hash_builder_t *pBuilder,
    hash64_t hash ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_WriteHash128(
    stable_hash_builder_t *pBuilder,
    hash128_t hash ) noexcept;

// Finalizes one snapshot and closes the builder. Every 64-bit output is valid,
// including zero; success is reported separately from the digest value.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StableHash_End(
    stable_hash_builder_t *pBuilder,
    hash64_t *pHashOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STABLEHASH_H
