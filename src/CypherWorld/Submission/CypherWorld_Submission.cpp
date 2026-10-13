//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Submission/CypherWorld_Submission.cpp
//  Purpose: Owns a fixed-capacity CPU copy of one visible World view.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherWorld_Submission.h"

#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"

#include <atomic>
#include <new>

namespace cypher::engine::world
{

struct world_submission_t {
    const common::allocator_t *pAllocator{ nullptr };
    world_object_snapshot_t *pRecords{ nullptr };
    common::u64 serial{ 0u };
    common::u64 generation{ 0u };
    common::u32 capacity{ 0u };
    common::u32 count{ 0u };
    common::bool_t active{ common::CY_FALSE };
};

namespace
{

std::atomic<common::u64> gLastSubmissionSerial{ 0u };

common::u64 NextSubmissionSerial() noexcept
{
    common::u64 observed = gLastSubmissionSerial.load( std::memory_order_relaxed );
    while ( observed != common::CY_U64_MAX ) {
        if ( gLastSubmissionSerial.compare_exchange_weak(
                 observed, observed + 1u,
                 std::memory_order_relaxed,
                 std::memory_order_relaxed ) ) {
            return observed + 1u;
        }
    }
    return 0u;
}

common::bool_t IsCurrentToken(
    const world_submission_t *pSubmission,
    world_submission_token_t token ) noexcept
{
    return pSubmission->active &&
           token.storageSerial == pSubmission->serial &&
           token.generation != 0u &&
           token.generation == pSubmission->generation;
}

} // namespace

world_submission_error_t WorldSubmission_Init(
    world_submission_t **ppSubmission,
    const common::allocator_t *pAllocator,
    common::u32 capacity ) noexcept
{
    if ( ppSubmission == nullptr || *ppSubmission != nullptr ||
         !common::Allocator_IsValid( pAllocator ) ||
         capacity == 0u ||
         capacity > common::CY_HANDLE_TABLE_MAX_CAPACITY ) {
        return world_submission_error_t::INVALID_ARGUMENT;
    }

    void *pMemory = common::Allocator_Allocate(
        pAllocator, sizeof( world_submission_t ), alignof( world_submission_t ) );
    if ( pMemory == nullptr ) {
        return world_submission_error_t::OUT_OF_MEMORY;
    }

    world_submission_t *pSubmission = ::new ( pMemory ) world_submission_t;
    pSubmission->pAllocator = pAllocator;
    pSubmission->capacity = capacity;
    pSubmission->pRecords = common::Allocator_AllocateArrayStorage<world_object_snapshot_t>(
        pAllocator, capacity );
    if ( pSubmission->pRecords == nullptr ) {
        pSubmission->~world_submission_t();
        common::Allocator_Free(
            pAllocator, pMemory, sizeof( world_submission_t ),
            alignof( world_submission_t ) );
        return world_submission_error_t::OUT_OF_MEMORY;
    }

    for ( common::u32 i = 0u; i < capacity; ++i ) {
        ::new ( pSubmission->pRecords + i ) world_object_snapshot_t;
    }
    pSubmission->serial = NextSubmissionSerial();
    if ( pSubmission->serial == 0u ) {
        for ( common::u32 i = 0u; i < capacity; ++i ) {
            pSubmission->pRecords[i].~world_object_snapshot_t();
        }
        common::Allocator_FreeArrayStorage(
            pAllocator, pSubmission->pRecords, capacity );
        pSubmission->~world_submission_t();
        common::Allocator_Free(
            pAllocator, pMemory, sizeof( world_submission_t ),
            alignof( world_submission_t ) );
        return world_submission_error_t::CAPACITY_EXCEEDED;
    }

    *ppSubmission = pSubmission;
    return world_submission_error_t::OK;
}

void WorldSubmission_Shutdown( world_submission_t **ppSubmission ) noexcept
{
    if ( ppSubmission == nullptr || *ppSubmission == nullptr ) {
        return;
    }
    world_submission_t *pSubmission = *ppSubmission;
    const common::allocator_t *pAllocator = pSubmission->pAllocator;
    for ( common::u32 i = 0u; i < pSubmission->capacity; ++i ) {
        pSubmission->pRecords[i].~world_object_snapshot_t();
    }
    common::Allocator_FreeArrayStorage(
        pAllocator, pSubmission->pRecords, pSubmission->capacity );
    pSubmission->~world_submission_t();
    common::Allocator_Free(
        pAllocator, pSubmission, sizeof( world_submission_t ),
        alignof( world_submission_t ) );
    *ppSubmission = nullptr;
}

world_submission_error_t WorldSubmission_Build(
    world_submission_t *pSubmission,
    const world_t *pWorld,
    const world_view_query_t *pQuery,
    world_submission_token_t *pOutToken ) noexcept
{
    if ( pSubmission == nullptr || pWorld == nullptr ||
         pQuery == nullptr || pOutToken == nullptr ) {
        return world_submission_error_t::INVALID_ARGUMENT;
    }
    if ( pSubmission->active ) {
        return world_submission_error_t::ACTIVE_SUBMISSION;
    }
    *pOutToken = WORLD_SUBMISSION_TOKEN_INVALID;
    if ( pSubmission->generation == common::CY_U64_MAX ) {
        return world_submission_error_t::CAPACITY_EXCEEDED;
    }

    common::u32 copied = 0u;
    const world_error_t queryResult = World_QueryView(
        pWorld, pQuery, pSubmission->pRecords, pSubmission->capacity, &copied );
    // World_QueryView preflights the complete result count and leaves its
    // output untouched when the fixed storage is too small.
    if ( queryResult == world_error_t::CAPACITY_EXCEEDED ) {
        return world_submission_error_t::CAPACITY_EXCEEDED;
    }
    if ( queryResult != world_error_t::OK ) {
        return world_submission_error_t::INVALID_ARGUMENT;
    }

    pSubmission->count = copied;
    ++pSubmission->generation;
    pSubmission->active = common::CY_TRUE;
    *pOutToken = { pSubmission->serial, pSubmission->generation };
    return world_submission_error_t::OK;
}

world_submission_error_t WorldSubmission_Count(
    const world_submission_t *pSubmission,
    world_submission_token_t token,
    common::u32 *pOutCount ) noexcept
{
    if ( pSubmission == nullptr || pOutCount == nullptr ) {
        return world_submission_error_t::INVALID_ARGUMENT;
    }
    if ( !IsCurrentToken( pSubmission, token ) ) {
        return world_submission_error_t::STALE_TOKEN;
    }
    *pOutCount = pSubmission->count;
    return world_submission_error_t::OK;
}

world_submission_error_t WorldSubmission_Get(
    const world_submission_t *pSubmission,
    world_submission_token_t token,
    common::u32 index,
    world_object_snapshot_t *pOutObject ) noexcept
{
    if ( pSubmission == nullptr || pOutObject == nullptr ) {
        return world_submission_error_t::INVALID_ARGUMENT;
    }
    if ( !IsCurrentToken( pSubmission, token ) ) {
        return world_submission_error_t::STALE_TOKEN;
    }
    if ( index >= pSubmission->count ) {
        return world_submission_error_t::INVALID_ARGUMENT;
    }
    *pOutObject = pSubmission->pRecords[index];
    return world_submission_error_t::OK;
}

void WorldSubmission_End( world_submission_t *pSubmission ) noexcept
{
    if ( pSubmission != nullptr ) {
        pSubmission->active = common::CY_FALSE;
        pSubmission->count = 0u;
    }
}

} // namespace cypher::engine::world
