//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ContentHash.h
//  Purpose: Declares stable 128-bit content fingerprints.
//  Details: ContentHash supports asset cache keys and change detection using XXH3-128.
//           It is fast and portable but not an adversarial cryptographic digest.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CONTENTHASH_H
#define CYPHER_COMMON_TIER1_CONTENTHASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_HashXXH.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct content_hash_t {
    u64 low{ 0u };
    u64 high{ 0u };
};

constexpr content_hash_t CY_CONTENT_HASH_INVALID{};
constexpr usize CY_CONTENT_HASH_HEX_LENGTH = 32u;
constexpr usize CY_CONTENT_HASH_HEX_CAPACITY = CY_CONTENT_HASH_HEX_LENGTH + 1u;

CYPHER_NODISCARD CYPHER_COMMON_API
content_hash_t ContentHash_Data( binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
content_hash_t ContentHash_String( string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
content_hash_t ContentHash_Combine(
    content_hash_t left,
    content_hash_t right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ContentHash_IsValid( content_hash_t hash ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ContentHash_Equals( content_hash_t left, content_hash_t right ) noexcept;

// Writes 32 lowercase hexadecimal digits followed by a null terminator.
// Returns 32 on success and zero when the destination contract is not satisfied.
CYPHER_NODISCARD CYPHER_COMMON_API
usize ContentHash_ToHex(
    content_hash_t hash,
    char *pDest,
    usize cchDest ) noexcept;

// Parses exactly 32 hexadecimal digits. Output is unchanged when parsing fails.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ContentHash_FromHex(
    string_view_t text,
    content_hash_t *pHashOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONTENTHASH_H
