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

namespace cypher::common
{

struct hash128_t {
    u64 low{ 0u };
    u64 high{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
hash32_t HashXXH32_Data( binary_block_t data, hash32_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashXXH64_Data( binary_block_t data, hash64_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash64_t HashXXH3_64_Data( binary_block_t data, hash64_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
hash128_t HashXXH3_128_Data( binary_block_t data, hash64_t seed = 0u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Hash128_Equals( hash128_t left, hash128_t right ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHXXH_H
