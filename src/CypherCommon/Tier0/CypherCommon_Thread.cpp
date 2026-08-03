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
std::atomic<thread_id_t> g_mainThreadId = CY_THREAD_INVALID_ID;
std::atomic<thread_id_t> g_nextThreadId{ 1u };
thread_local thread_id_t g_currentThreadId = CY_THREAD_INVALID_ID;

thread_id_t ThreadGetOrAssignCurrentId() noexcept
{
    if ( g_currentThreadId != CY_THREAD_INVALID_ID ) {
        return g_currentThreadId;
    }

    thread_id_t nThreadId =
        g_nextThreadId.fetch_add( 1u, std::memory_order_relaxed );
    if ( nThreadId == CY_THREAD_INVALID_ID ) {
        nThreadId = g_nextThreadId.fetch_add( 1u, std::memory_order_relaxed );
    }
    g_currentThreadId = nThreadId;
    return g_currentThreadId;
}

void ThreadCaptureMainThreadLocked() noexcept
{
    g_mainThreadId.store(
        ThreadGetOrAssignCurrentId(),
        std::memory_order_release );
    g_threadInitialized.store( true, std::memory_order_release );
}

void ThreadCopyName( char *pszDst, usize nDstCapacity, const char *pszName ) noexcept
{
    if ( pszDst == nullptr || nDstCapacity == 0u ) {
        return;
    }

    pszDst[0] = '\0';
    if ( pszName == nullptr ) {
        return;
    }

    usize nIndex = 0u;
    while ( nIndex + 1u < nDstCapacity && pszName[nIndex] != '\0' ) {
        pszDst[nIndex] = pszName[nIndex];
        ++nIndex;
    }
    pszDst[nIndex] = '\0';
}

} // namespace

bool_t Cy_ThreadInit() noexcept
{
    std::lock_guard<std::mutex> lock( g_threadStateMutex );

    if ( !g_threadInitialized.load( std::memory_order_acquire ) ) {
        ThreadCaptureMainThreadLocked();
    }

    return CY_TRUE;
}

void Cy_ThreadShutdown() noexcept
{
    std::lock_guard<std::mutex> lock( g_threadStateMutex );

    g_threadInitialized.store( false, std::memory_order_release );
    g_mainThreadId.store( CY_THREAD_INVALID_ID, std::memory_order_release );
}

bool_t Cy_ThreadIsInitialized() noexcept
{
    return g_threadInitialized.load( std::memory_order_acquire );
}

void Cy_ThreadYield() noexcept
{
    std::this_thread::yield();
}

void Cy_ThreadSleepMs( u32 nMilliseconds ) noexcept
{
    std::this_thread::sleep_for( std::chrono::milliseconds( nMilliseconds ) );
}

void Cy_ThreadSleepUs( u32 nMicroseconds ) noexcept
{
    std::this_thread::sleep_for( std::chrono::microseconds( nMicroseconds ) );
}

thread_id_t Cy_ThreadGetCurrentId() noexcept
{
    return ThreadGetOrAssignCurrentId();
}

u64 Cy_ThreadGetCurrentIdHash() noexcept
{
    return Cy_ThreadGetCurrentId();
}

u32 Cy_ThreadGetLogicalCount() noexcept
{
    static const u32 nThreadCount = []() {
        const u32 nDetectedThreadCount = std::thread::hardware_concurrency();
        return nDetectedThreadCount != 0u ? nDetectedThreadCount : 1u;
    }();

    return nThreadCount;
}

bool_t Cy_ThreadCaptureMainThread() noexcept
{
    std::lock_guard<std::mutex> lock( g_threadStateMutex );
    const thread_id_t nCurrentThreadId = ThreadGetOrAssignCurrentId();
    if ( g_threadInitialized.load( std::memory_order_acquire ) ) {
        return g_mainThreadId.load( std::memory_order_acquire ) ==
               nCurrentThreadId;
    }
    ThreadCaptureMainThreadLocked();
    return CY_TRUE;
}

thread_id_t Cy_ThreadGetMainThreadId() noexcept
{
    return g_mainThreadId.load( std::memory_order_acquire );
}

bool_t Cy_ThreadIsMainThread() noexcept
{
    if ( !g_threadInitialized.load( std::memory_order_acquire ) ) {
        return CY_FALSE;
    }

    return Cy_ThreadGetCurrentId() ==
           g_mainThreadId.load( std::memory_order_acquire );
}

bool_t Cy_ThreadSetCurrentName( const char *pszName ) noexcept
{
    if ( pszName == nullptr || pszName[0] == '\0' ) {
        return CY_FALSE;
    }

#if CYPHER_PLATFORM_WINDOWS
    wchar_t wszName[64] = {};
    const int cchWritten = MultiByteToWideChar( CP_UTF8, 0, pszName, -1, wszName, static_cast<int>( CYPHER_ARRAY_COUNT( wszName ) ) );
    if ( cchWritten > 0 ) {
        return SetThreadDescription( GetCurrentThread(), wszName ) >= 0;
    }
    return CY_FALSE;
#elif CYPHER_PLATFORM_MACOS
    char szName[CY_THREAD_NAME_CAPACITY] = {};
    ThreadCopyName( szName, CYPHER_ARRAY_COUNT( szName ), pszName );
    return pthread_setname_np( szName ) == 0;
#elif CYPHER_PLATFORM_LINUX
    char szName[16] = {};
    for ( usize i = 0u; i + 1u < CYPHER_ARRAY_COUNT( szName ) && pszName[i] != '\0'; ++i ) {
        szName[i] = pszName[i];
    }
    return pthread_setname_np( pthread_self(), szName ) == 0;
#else
    CYPHER_UNUSED( pszName );
    return CY_FALSE;
#endif
}

bool_t Cy_ThreadCreate(
    cy_thread_t *pThread,
    thread_proc_t pProc,
    void *pUserData,
    const char *pszName ) noexcept
{
    if ( pThread == nullptr || pProc == nullptr || pThread->native.joinable() ) {
        return CY_FALSE;
    }

    pThread->nThreadId.store( CY_THREAD_INVALID_ID, std::memory_order_relaxed );
    pThread->isRunning.store( CY_TRUE, std::memory_order_release );
    pThread->nResult = 0;
    ThreadCopyName( pThread->szName, CY_THREAD_NAME_CAPACITY, pszName );

    try {
        pThread->native = std::thread( [pThread, pProc, pUserData]() noexcept {
            pThread->nThreadId.store(
                Cy_ThreadGetCurrentId(),
                std::memory_order_release );
            if ( pThread->szName[0] != '\0' ) {
                ( void )Cy_ThreadSetCurrentName( pThread->szName );
            }
            pThread->nResult = pProc( pUserData );
            pThread->isRunning.store( CY_FALSE, std::memory_order_release );
        } );
    } catch ( ... ) {
        pThread->nThreadId.store( CY_THREAD_INVALID_ID, std::memory_order_relaxed );
        pThread->isRunning.store( CY_FALSE, std::memory_order_relaxed );
        return CY_FALSE;
    }

    return CY_TRUE;
}

bool_t Cy_ThreadJoin( cy_thread_t *pThread, i32 *pOutResult ) noexcept
{
    if ( pThread == nullptr || !pThread->native.joinable() ||
         pThread->native.get_id() == std::this_thread::get_id() ) {
        return CY_FALSE;
    }

    try {
        pThread->native.join();
    } catch ( ... ) {
        return CY_FALSE;
    }

    pThread->isRunning.store( CY_FALSE, std::memory_order_release );
    if ( pOutResult != nullptr ) {
        *pOutResult = pThread->nResult;
    }
    return CY_TRUE;
}

bool_t Cy_ThreadIsRunning( const cy_thread_t *pThread ) noexcept
{
    return pThread != nullptr &&
           pThread->isRunning.load( std::memory_order_acquire );
}

thread_id_t Cy_ThreadGetId( const cy_thread_t *pThread ) noexcept
{
    return pThread != nullptr
        ? pThread->nThreadId.load( std::memory_order_acquire )
        : CY_THREAD_INVALID_ID;
}

} // namespace cypher::common
