//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryDebug.cpp
//  Purpose: Implements CypherCommon Tier0 memory debug callbacks.
//  Details: Memory debug events let allocators report low-level allocation
//           activity without coupling to higher diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryDebug.h"

#include <mutex>

namespace cypher::common
{
namespace
{

std::mutex g_memoryDebugMutex;
memory_debug_callback_t g_memoryDebugCallback = nullptr;
void *g_pMemoryDebugContext = nullptr;
thread_local bool_t g_isInsideMemoryDebugCallback = CY_FALSE;

} // namespace

void Cy_MemoryDebugSetCallback(
    memory_debug_callback_t pCallback,
    void *pContext ) noexcept
{
    std::lock_guard<std::mutex> lock( g_memoryDebugMutex );
    g_memoryDebugCallback = pCallback;
    g_pMemoryDebugContext = pCallback != nullptr ? pContext : nullptr;
}

memory_debug_callback_t Cy_MemoryDebugGetCallback( void **ppOutContext ) noexcept
{
    std::lock_guard<std::mutex> lock( g_memoryDebugMutex );
    if ( ppOutContext != nullptr ) {
        *ppOutContext = g_pMemoryDebugContext;
    }
    return g_memoryDebugCallback;
}

void Cy_MemoryDebugReportEvent( const memory_debug_record_t &record ) noexcept
{
    if ( g_isInsideMemoryDebugCallback ) {
        return;
    }

    memory_debug_callback_t pCallback = nullptr;
    void *pContext = nullptr;
    {
        std::lock_guard<std::mutex> lock( g_memoryDebugMutex );
        pCallback = g_memoryDebugCallback;
        pContext = g_pMemoryDebugContext;
    }

    if ( pCallback != nullptr ) {
        g_isInsideMemoryDebugCallback = CY_TRUE;
        pCallback( record, pContext );
        g_isInsideMemoryDebugCallback = CY_FALSE;
    }
}

} // namespace cypher::common
