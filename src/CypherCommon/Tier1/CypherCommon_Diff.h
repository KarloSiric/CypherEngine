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
    OK = 0u,          // Delta generation or application completed.
    INVALID_ARGUMENT,// Input block, destination, or diff state is invalid.
    OUT_OF_MEMORY,   // Encoding storage could not be allocated.
    CORRUPT_DIFF,    // Encoded operations or target hash failed validation.
    SOURCE_MISMATCH, // Source size or content hash does not match the delta.
    OUTPUT_TOO_SMALL,// Destination cannot hold the reconstructed target.
    OUTPUT_OVERFLOW, // Encoded output lengths overflow the host size type.
    INTERNAL_ERROR   // Hash stream or another internal primitive failed.
};

struct binary_diff_t {
    content_hash_t sourceHash{}; // Exact source identity required by apply.
    content_hash_t targetHash{}; // Expected identity after reconstruction.
    usize cbSource{ 0u };        // Required source byte count.
    usize cbTarget{ 0u };        // Reconstructed target byte count.
    blob_t encodedOps{};         // Owned versioned operation stream.
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
