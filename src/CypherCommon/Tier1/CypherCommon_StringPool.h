//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringPool.h
//  Purpose: Declares allocator-backed stable string interning.
//  Details: Interned addresses remain stable until the pool is cleared or destroyed.
//           The pool resolves hash collisions by comparing complete string bytes.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGPOOL_H
#define CYPHER_COMMON_TIER1_STRINGPOOL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum string_pool_flags_t : flags32_t {
    STRING_POOL_FLAG_NONE                    = 0u,                // Preserve byte-sensitive identity.
    STRING_POOL_FLAG_CASE_INSENSITIVE_ASCII  = CYPHER_BIT32( 0 )  // Fold ASCII during hash and equality.
};

struct string_pool_desc_t {
    const allocator_t *pAllocator{ nullptr }; // Owns all pool blocks and hash storage.
    usize nInitialBuckets{ 256u };            // Initial hash-table slot count.
    usize cbInitialBlock{ 16u * CY_KIB };     // Minimum first character-data block.
    flags32_t flags{ STRING_POOL_FLAG_NONE }; // Identity policy fixed at creation.
};

struct string_pool_stats_t {
    usize nStrings{ 0u };    // Distinct interned values.
    usize cbStringData{ 0u };// Payload bytes, including one NUL per string.
    usize cbReserved{ 0u };  // Reserved payload bytes; excludes headers and hash slots.
    usize nCollisions{ 0u }; // Equal hashes rejected by complete byte comparison.
};

struct string_pool_t;

CYPHER_NODISCARD CYPHER_COMMON_API
string_pool_t *StringPool_Create( const string_pool_desc_t &desc ) noexcept;

CYPHER_COMMON_API void StringPool_Destroy( string_pool_t *pPool ) noexcept;

CYPHER_COMMON_API void StringPool_Clear( string_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringPool_IsValid( const string_pool_t *pPool ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *StringPool_Intern(
    string_pool_t *pPool,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *StringPool_Find(
    const string_pool_t *pPool,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringPool_Contains(
    const string_pool_t *pPool,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_pool_stats_t StringPool_Stats( const string_pool_t *pPool ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGPOOL_H
