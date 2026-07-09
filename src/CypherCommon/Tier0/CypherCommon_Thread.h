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

Low-level thread helpers shared by runtime systems. This is not the job system;
it only exposes portable identity, sleep, yield, main-thread checks, and
debugger/profiler naming.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using thread_id_t = u64;

constexpr thread_id_t CY_THREAD_INVALID_ID = 0u;

// Initializes thread state and captures the calling thread as the main thread.
bool_t Cy_ThreadInit();

// Resets captured thread state for controlled shutdown and tests.
void Cy_ThreadShutdown();

// Returns whether the thread module has captured a main thread.
bool_t Cy_ThreadIsInitialized();

// Yields the current thread's remaining scheduler time slice.
void Cy_ThreadYield();

// Sleeps the current thread for at least the requested milliseconds.
void Cy_ThreadSleepMs( u32 nMilliseconds );

// Sleeps the current thread for at least the requested microseconds.
void Cy_ThreadSleepUs( u32 nMicroseconds );

// Returns a process-local stable hash for the calling thread id.
thread_id_t Cy_ThreadGetCurrentId();

// Returns a process-local stable hash for the calling thread id.
u64 Cy_ThreadGetCurrentIdHash();

// Returns detected hardware concurrency, falling back to one.
u32 Cy_ThreadGetLogicalCount();

// Captures the current thread as the engine main thread.
void Cy_ThreadCaptureMainThread();

// Returns the captured main thread id, or CY_THREAD_INVALID_ID if unset.
thread_id_t Cy_ThreadGetMainThreadId();

// Returns true when called from the captured main thread.
bool_t Cy_ThreadIsMainThread();

// Best-effort current-thread name for debugger and profiler views.
void Cy_ThreadSetCurrentName( const char *pszName );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_THREAD_H
