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
#pragma once

/*
================
CypherCommon Thread

Minimal thread helpers. This is not the job system.
================
*/

#include "CypherCommon_BaseTypes.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace cypher::common
{

template <typename type_t>
using atomic_t = std::atomic<type_t>;

using memory_order_t = std::memory_order;

// Yields the current thread's remaining time slice.
inline void ThreadYield()
{
    std::this_thread::yield();
}

// Sleeps the current thread for at least the requested milliseconds.
inline void ThreadSleepMs( u32 milliseconds )
{
    std::this_thread::sleep_for( std::chrono::milliseconds( milliseconds ) );
}

// Returns a stable hash for the current std::thread id.
inline u64 GetCurrentThreadIdHash()
{
    return static_cast<u64>( std::hash<std::thread::id>{}( std::this_thread::get_id() ) );
}

// Returns detected hardware concurrency, falling back to one.
inline u32 GetLogicalThreadCount()
{
    const u32 thread_count = std::thread::hardware_concurrency();
    return thread_count != 0u ? thread_count : 1u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_THREAD_H
