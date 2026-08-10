//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Diff.h
//  Purpose: Declares deterministic binary delta generation and application.
//  Details: Diff data owns its encoded operations through an explicit blob. Applying
//           a diff validates source size/hash and destination capacity before mutation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_DIFF_H
#define CYPHER_COMMON_TIER1_DIFF_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Blob.h"
#include "CypherCommon_ContentHash.h"

namespace cypher::common
{

enum class diff_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    CORRUPT_DIFF,
    SOURCE_MISMATCH,
    OUTPUT_TOO_SMALL,
    OUTPUT_OVERFLOW,
    INTERNAL_ERROR
};

struct binary_diff_t {
    content_hash_t sourceHash{};
    content_hash_t targetHash{};
    usize cbSource{ 0u };
    usize cbTarget{ 0u };
    blob_t encodedOps{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Diff_Init(
    binary_diff_t *pDiff,
    const allocator_t *pAllocator ) noexcept;

CYPHER_COMMON_API void Diff_Shutdown( binary_diff_t *pDiff ) noexcept;
CYPHER_COMMON_API void Diff_Clear( binary_diff_t *pDiff ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
diff_status_t Diff_Generate(
    binary_block_t source,
    binary_block_t target,
    binary_diff_t *pDiffOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
diff_status_t Diff_Apply(
    binary_block_t source,
    const binary_diff_t &diff,
    byte_span_t dest,
    usize *pcbWrittenOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
// Returns the encoded operation-stream size, excluding binary_diff_t metadata.
usize Diff_SerializedSize( const binary_diff_t &diff ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DIFF_H
