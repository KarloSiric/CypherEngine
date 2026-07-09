//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Thread.cpp
//  Purpose: Implements CypherCommon Tier0 Thread helpers.
//  Details: This file provides the lowest thread identity, sleep, yield, and
//           naming primitives used by async IO, profiling, memory diagnostics,
//           and future worker/job systems.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Thread.h"

#include "CypherCommon_Defines.h"
#include "CypherCommon_Platform.h"

#include <chrono>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif CYPHER_PLATFORM_LINUX || CYPHER_PLATFORM_MACOS
    #include <pthread.h>
#endif

namespace cypher::common
{
namespace
{

std::mutex g_threadStateMutex;
std::atomic_bool g_threadInitialized = false;
std::atomic<thread_id_t> g_mainThreadHash = CY_THREAD_INVALID_ID;

thread_id_t ThreadHashId( const std::thread::id &id )
{
    thread_id_t nHash = static_cast<thread_id_t>( std::hash<std::thread::id>{}( id ) );
    return nHash != CY_THREAD_INVALID_ID ? nHash : 1u;
}

void ThreadCaptureMainThreadLocked()
{
    g_mainThreadHash.store( ThreadHashId( std::this_thread::get_id() ), std::memory_order_release );
    g_threadInitialized.store( true, std::memory_order_release );
}

} // namespace

bool_t Cy_ThreadInit()
{
    std::lock_guard<std::mutex> lock( g_threadStateMutex );

    if ( !g_threadInitialized.load( std::memory_order_acquire ) ) {
        ThreadCaptureMainThreadLocked();
    }

    return CY_TRUE;
}

void Cy_ThreadShutdown()
{
    std::lock_guard<std::mutex> lock( g_threadStateMutex );

    g_threadInitialized.store( false, std::memory_order_release );
    g_mainThreadHash.store( CY_THREAD_INVALID_ID, std::memory_order_release );
}

bool_t Cy_ThreadIsInitialized()
{
    return g_threadInitialized.load( std::memory_order_acquire );
}

void Cy_ThreadYield()
{
    std::this_thread::yield();
}

void Cy_ThreadSleepMs( u32 nMilliseconds )
{
    std::this_thread::sleep_for( std::chrono::milliseconds( nMilliseconds ) );
}

void Cy_ThreadSleepUs( u32 nMicroseconds )
{
    std::this_thread::sleep_for( std::chrono::microseconds( nMicroseconds ) );
}

thread_id_t Cy_ThreadGetCurrentId()
{
    return ThreadHashId( std::this_thread::get_id() );
}

u64 Cy_ThreadGetCurrentIdHash()
{
    return Cy_ThreadGetCurrentId();
}

u32 Cy_ThreadGetLogicalCount()
{
    static const u32 nThreadCount = []() {
        const u32 nDetectedThreadCount = std::thread::hardware_concurrency();
        return nDetectedThreadCount != 0u ? nDetectedThreadCount : 1u;
    }();

    return nThreadCount;
}

void Cy_ThreadCaptureMainThread()
{
    std::lock_guard<std::mutex> lock( g_threadStateMutex );
    ThreadCaptureMainThreadLocked();
}

thread_id_t Cy_ThreadGetMainThreadId()
{
    std::lock_guard<std::mutex> lock( g_threadStateMutex );
    return g_mainThreadHash.load( std::memory_order_acquire );
}

bool_t Cy_ThreadIsMainThread()
{
    if ( !g_threadInitialized.load( std::memory_order_acquire ) ) {
        return CY_FALSE;
    }

    return Cy_ThreadGetCurrentId() == g_mainThreadHash.load( std::memory_order_acquire );
}

void Cy_ThreadSetCurrentName( const char *pszName )
{
    if ( pszName == nullptr || pszName[0] == '\0' ) {
        return;
    }

#if CYPHER_PLATFORM_WINDOWS
    wchar_t wszName[64] = {};
    const int cchWritten = MultiByteToWideChar( CP_UTF8, 0, pszName, -1, wszName, static_cast<int>( CYPHER_ARRAY_COUNT( wszName ) ) );
    if ( cchWritten > 0 ) {
        SetThreadDescription( GetCurrentThread(), wszName );
    }
#elif CYPHER_PLATFORM_MACOS
    pthread_setname_np( pszName );
#elif CYPHER_PLATFORM_LINUX
    char szName[16] = {};
    for ( usize i = 0u; i + 1u < CYPHER_ARRAY_COUNT( szName ) && pszName[i] != '\0'; ++i ) {
        szName[i] = pszName[i];
    }
    pthread_setname_np( pthread_self(), szName );
#else
    CYPHER_UNUSED( pszName );
#endif
}

} // namespace cypher::common
