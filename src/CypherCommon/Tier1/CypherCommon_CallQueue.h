//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CallQueue.h
//  Purpose: Declares a bounded FIFO queue of deferred callback records.
//  Details: Enqueued user pointers remain caller-owned and must outlive execution or
//           cancellation. CallQueue itself provides no cross-thread synchronization.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CALLQUEUE_H
#define CYPHER_COMMON_TIER1_CALLQUEUE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"

namespace cypher::common
{

using call_queue_proc_t = void ( * )( void *pUserData ) noexcept;

struct call_queue_entry_t {
    call_queue_proc_t pfnCall{ nullptr };
    void *pUserData{ nullptr };
    u64 nTag{ 0u };
};

struct call_queue_t;

CYPHER_NODISCARD CYPHER_COMMON_API
call_queue_t *CallQueue_Create(
    const allocator_t *pAllocator,
    usize nInitialCapacity = 64u ) noexcept;

CYPHER_COMMON_API void CallQueue_Destroy( call_queue_t *pQueue ) noexcept;
CYPHER_COMMON_API void CallQueue_Clear( call_queue_t *pQueue ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CallQueue_Push(
    call_queue_t *pQueue,
    const call_queue_entry_t &entry ) noexcept;

// Removes matching calls without executing them.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CallQueue_CancelTag( call_queue_t *pQueue, u64 nTag ) noexcept;

// Executes at most nMaxCalls; CY_INVALID_SIZE drains all currently queued entries.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CallQueue_Drain(
    call_queue_t *pQueue,
    usize nMaxCalls = CY_INVALID_SIZE ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CallQueue_Count( const call_queue_t *pQueue ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CALLQUEUE_H
