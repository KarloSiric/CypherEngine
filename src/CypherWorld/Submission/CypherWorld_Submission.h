//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Submission/CypherWorld_Submission.h
//  Purpose: Declares bounded, renderer-neutral view submission storage.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_WORLD_SUBMISSION_H
#define CYPHER_ENGINE_WORLD_SUBMISSION_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherWorld/CypherWorld_Public.h"

namespace cypher::engine::world
{

struct world_submission_t; // Opaque owner of copied view records.

struct world_submission_token_t {
    ::cypher::common::u64 storageSerial{ 0u };
    ::cypher::common::u64 generation{ 0u };
};

inline constexpr world_submission_token_t WORLD_SUBMISSION_TOKEN_INVALID{};

enum class world_submission_error_t {
    OK = 0,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    CAPACITY_EXCEEDED,
    ACTIVE_SUBMISSION,
    STALE_TOKEN
};

// Allocates the owner and record array once. The borrowed allocator must
// outlive the storage. Capacity is fixed at 1..65536 records.
CYPHER_NODISCARD world_submission_error_t WorldSubmission_Init(
    world_submission_t **ppSubmission,
    const ::cypher::common::allocator_t *pAllocator,
    ::cypher::common::u32 capacity ) noexcept;

// Invalidates outstanding tokens and frees storage. Callers must stop using
// the opaque pointer before shutdown; an old pointer itself is not a token.
void WorldSubmission_Shutdown( world_submission_t **ppSubmission ) noexcept;

// Copies one view's logical objects into fixed storage. A previous submission
// must be ended before another build. Failure publishes no token or records;
// CAPACITY_EXCEEDED never exposes a partial view. The source World need only
// remain alive during this call. Concurrent World mutation is unsupported.
CYPHER_NODISCARD world_submission_error_t WorldSubmission_Build(
    world_submission_t *pSubmission,
    const world_t *pWorld,
    const world_view_query_t *pQuery,
    world_submission_token_t *pOutToken ) noexcept;

// Access through a generation-checked token and value copies. Neither call
// exposes a pointer into storage. Output is unchanged on failure.
CYPHER_NODISCARD world_submission_error_t WorldSubmission_Count(
    const world_submission_t *pSubmission,
    world_submission_token_t token,
    ::cypher::common::u32 *pOutCount ) noexcept;
CYPHER_NODISCARD world_submission_error_t WorldSubmission_Get(
    const world_submission_t *pSubmission,
    world_submission_token_t token,
    ::cypher::common::u32 index,
    world_object_snapshot_t *pOutObject ) noexcept;

// Ends the currently active submission and invalidates its token. Safe to
// call more than once; this does not change copied World objects.
void WorldSubmission_End( world_submission_t *pSubmission ) noexcept;

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_SUBMISSION_H
