//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Hash.h
//  Purpose: Declares engine-default non-cryptographic hash helpers.
//  Details: These hashes support tables, tokens, and deterministic identifiers. They
//           must not be used for passwords, signatures, authentication, or untrusted DoS defense.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Hash Contract

Hash operations use explicit byte spans and stable seeds where persistence matters. Table
placement may change, but externally stored content hashes must remain deterministic.
================
*/

#ifndef CYPHER_COMMON_TIER1_HASH_H
#define CYPHER_COMMON_TIER1_HASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

constexpr hash32_t CY_HASH32_DEFAULT_SEED = 0x9E3779B9u;
constexpr hash64_t CY_HASH64_DEFAULT_SEED = 0x9E3779B97F4A7C15ull;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t Hash32_Data(
    binary_block_t data,
    hash32_t seed = CY_HASH32_DEFAULT_SEED ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t Hash64_Data(
    binary_block_t data,
    hash64_t seed = CY_HASH64_DEFAULT_SEED ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t Hash32_String(
    string_view_t text,
    hash32_t seed = CY_HASH32_DEFAULT_SEED ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t Hash64_String(
    string_view_t text,
    hash64_t seed = CY_HASH64_DEFAULT_SEED ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t Hash32_StringInsensitiveAscii(
    string_view_t text,
    hash32_t seed = CY_HASH32_DEFAULT_SEED ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t Hash64_StringInsensitiveAscii(
    string_view_t text,
    hash64_t seed = CY_HASH64_DEFAULT_SEED ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t Hash32_Combine( hash32_t left, hash32_t right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t Hash64_Combine( hash64_t left, hash64_t right ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASH_H
