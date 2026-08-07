//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashFNV.h
//  Purpose: Declares deterministic FNV-1a hashes and incremental state.
//  Details: FNV is retained for format compatibility and simple compile/runtime IDs;
//           it is not the preferred high-throughput hash for large asset data.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HASHFNV_H
#define CYPHER_COMMON_TIER1_HASHFNV_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

constexpr hash32_t CY_FNV1A32_OFFSET = 2166136261u;
constexpr hash32_t CY_FNV1A32_PRIME = 16777619u;
constexpr hash64_t CY_FNV1A64_OFFSET = 14695981039346656037ull;
constexpr hash64_t CY_FNV1A64_PRIME = 1099511628211ull;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t HashFNV1a32_Update( hash32_t state, binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashFNV1a64_Update( hash64_t state, binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t HashFNV1a32_Data( binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashFNV1a64_Data( binary_block_t data ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t HashFNV1a32_String( string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashFNV1a64_String( string_view_t text ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHFNV_H
