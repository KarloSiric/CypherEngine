//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Thread.h
//  Purpose: Provides portable thread identity, lifecycle, naming, sleep, and yield.
//  Details: This is a low-level platform service, not the engine job scheduler.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_THREAD_H
#define CYPHER_COMMON_TIER0_THREAD_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Thread

Low-level thread helpers shared by runtime systems. This is not the job system;
it only exposes portable identity, sleep, yield, main-thread checks, and
debugger/profiler naming.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

#include <atomic>
#include <thread>

namespace cypher::common
{

using thread_id_t = u64; // Collision-free process-local identifier, not an OS thread handle.

constexpr thread_id_t CY_THREAD_INVALID_ID = 0u; // Assigned IDs begin at one.
constexpr usize CY_THREAD_NAME_CAPACITY = 64u;   // Includes the null terminator.

enum class cy_wait_result_t : u8 {
    Success = 0u, // Wait predicate was satisfied.
    Timeout,      // Deadline expired before the predicate was satisfied.
    Shutdown,     // Primitive was shut down while this thread waited.
    Invalid       // Null object or invalid lifecycle state.
};

using thread_proc_t = i32 ( * )( void *pUserData ) noexcept; // Return value is collected by Join.

struct cy_thread_t {
    std::thread native;                                         // Joinable native thread object.
    std::atomic<thread_id_t> nThreadId{ CY_THREAD_INVALID_ID }; // Published after the thread starts.
    std::atomic<bool_t> isRunning{ CY_FALSE };                  // True only while the procedure is active.
    i32 nResult = 0;                                            // Procedure result, valid after Join.
    char szName[CY_THREAD_NAME_CAPACITY] = {};                  // Owned debugger/profiler name copy.
};

// Initializes thread state and captures the calling thread as the main thread.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadInit() noexcept;

// Resets captured thread state for controlled shutdown and tests.
CYPHER_COMMON_API void Cy_ThreadShutdown() noexcept;

// Returns whether the thread module has captured a main thread.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadIsInitialized() noexcept;

CYPHER_COMMON_API void Cy_ThreadYield() noexcept;

// Sleep durations are minimum requests; scheduler latency may extend them.
CYPHER_COMMON_API void Cy_ThreadSleepMs( u32 nMilliseconds ) noexcept;

CYPHER_COMMON_API void Cy_ThreadSleepUs( u32 nMicroseconds ) noexcept;

// Returns a collision-free process-local ID assigned lazily to the calling thread.
CYPHER_NODISCARD CYPHER_COMMON_API thread_id_t Cy_ThreadGetCurrentId() noexcept;

// Compatibility name for diagnostics that expect a numeric thread token.
CYPHER_NODISCARD CYPHER_COMMON_API u64 Cy_ThreadGetCurrentIdHash() noexcept;

// Hardware concurrency falls back to one when the platform cannot report it.
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_ThreadGetLogicalCount() noexcept;

// Captures the caller as main; refuses to replace a different captured thread.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadCaptureMainThread() noexcept;

// Returns the captured main thread id, or CY_THREAD_INVALID_ID if unset.
CYPHER_NODISCARD CYPHER_COMMON_API thread_id_t Cy_ThreadGetMainThreadId() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadIsMainThread() noexcept;

// Best-effort current-thread name for debugger and profiler views.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadSetCurrentName(
    const char *pszName ) noexcept;

// Starts a joinable engine thread. The object must remain at a stable address and
// must be joined before destruction or reuse.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadCreate(
    cy_thread_t *pThread,
    thread_proc_t pProc,
    void *pUserData,
    const char *pszName = nullptr ) noexcept;

// Joins a thread and optionally returns its procedure result.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadJoin(
    cy_thread_t *pThread,
    i32 *pOutResult = nullptr ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ThreadIsRunning(
    const cy_thread_t *pThread ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API thread_id_t Cy_ThreadGetId(
    const cy_thread_t *pThread ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_THREAD_H
