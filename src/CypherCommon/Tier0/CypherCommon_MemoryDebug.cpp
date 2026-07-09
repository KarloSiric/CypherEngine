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

} // namespace

void MemoryDebug_SetCallback( memory_debug_callback_t callback )
{
    std::lock_guard<std::mutex> lock( g_memoryDebugMutex );
    g_memoryDebugCallback = callback;
}

void MemoryDebug_ReportEvent( memory_debug_event_t event_type, void *pMemory, usize cbSize, const char *pTag )
{
    std::lock_guard<std::mutex> lock( g_memoryDebugMutex );
    if ( g_memoryDebugCallback != nullptr ) {
        g_memoryDebugCallback( event_type, pMemory, cbSize, pTag );
    }
}

} // namespace cypher::common
