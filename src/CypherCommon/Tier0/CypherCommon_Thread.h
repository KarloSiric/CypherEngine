//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Thread.h
//  Purpose: Declares CypherCommon Tier0 Thread support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
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

using thread_id_t = u64;

constexpr thread_id_t CY_THREAD_INVALID_ID = 0u;
constexpr usize CY_THREAD_NAME_CAPACITY = 64u;

enum class cy_wait_result_t : u8 {
    Success = 0u,
    Timeout,
    Shutdown,
    Invalid
};

using thread_proc_t = i32 ( * )( void *pUserData ) noexcept;

struct cy_thread_t {
    std::thread native;
    std::atomic<thread_id_t> nThreadId{ CY_THREAD_INVALID_ID };
    std::atomic<bool_t> isRunning{ CY_FALSE };
    i32 nResult = 0;
    char szName[CY_THREAD_NAME_CAPACITY] = {};
};

// Initializes thread state and captures the calling thread as the main thread.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadInit() noexcept;

// Resets captured thread state for controlled shutdown and tests.
CYPHER_COMMON_API void Cy_ThreadShutdown() noexcept;

// Returns whether the thread module has captured a main thread.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadIsInitialized() noexcept;

// Yields the current thread's remaining scheduler time slice.
CYPHER_COMMON_API void Cy_ThreadYield() noexcept;

// Sleeps the current thread for at least the requested milliseconds.
CYPHER_COMMON_API void Cy_ThreadSleepMs( u32 nMilliseconds ) noexcept;

// Sleeps the current thread for at least the requested microseconds.
CYPHER_COMMON_API void Cy_ThreadSleepUs( u32 nMicroseconds ) noexcept;

// Returns a collision-free process-local ID assigned lazily to the calling thread.
[[nodiscard]] CYPHER_COMMON_API thread_id_t Cy_ThreadGetCurrentId() noexcept;

// Compatibility name for diagnostics that expect a numeric thread token.
[[nodiscard]] CYPHER_COMMON_API u64 Cy_ThreadGetCurrentIdHash() noexcept;

// Returns detected hardware concurrency, falling back to one.
[[nodiscard]] CYPHER_COMMON_API u32 Cy_ThreadGetLogicalCount() noexcept;

// Captures the caller as main; refuses to replace a different captured thread.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadCaptureMainThread() noexcept;

// Returns the captured main thread id, or CY_THREAD_INVALID_ID if unset.
[[nodiscard]] CYPHER_COMMON_API thread_id_t Cy_ThreadGetMainThreadId() noexcept;

// Returns true when called from the captured main thread.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadIsMainThread() noexcept;

// Best-effort current-thread name for debugger and profiler views.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadSetCurrentName(
    const char *pszName ) noexcept;

// Starts a joinable engine thread. The thread object must remain at a stable address.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadCreate(
    cy_thread_t *pThread,
    thread_proc_t pProc,
    void *pUserData,
    const char *pszName = nullptr ) noexcept;

// Joins a thread and optionally returns its procedure result.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadJoin(
    cy_thread_t *pThread,
    i32 *pOutResult = nullptr ) noexcept;

// Returns whether the created thread has not yet returned from its procedure.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ThreadIsRunning(
    const cy_thread_t *pThread ) noexcept;

// Returns the assigned process-local ID, or invalid before the thread starts.
[[nodiscard]] CYPHER_COMMON_API thread_id_t Cy_ThreadGetId(
    const cy_thread_t *pThread ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_THREAD_H
