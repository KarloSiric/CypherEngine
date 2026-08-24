//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryDebug.h
//  Purpose: Defines allocation events consumed by memory diagnostics and tools.
//  Details: Records borrow all string and memory pointers for the synchronous
//           callback only; reporting itself must not allocate recursively.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_MEMORYDEBUG_H
#define CYPHER_COMMON_TIER0_MEMORYDEBUG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Memory Debug

Memory debug event declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

enum class memory_debug_event_t : u32 {
    Alloc = 0u,
    Free,
    Realloc,
    Leak
};

struct memory_debug_record_t {
    memory_debug_event_t eventType; // Allocation operation represented by this record.
    void *pMemory;                  // Affected allocation; null is valid for failed allocs.
    usize nByteCount;               // Requested or tracked allocation size in bytes.
    usize nAlignment;               // Requested alignment in bytes; zero means unspecified.
    const char *pszTag;             // Borrowed allocation category; may be null.
    const char *pszFile;            // Borrowed source path; may be null.
    u32 nLine;                      // One-based source line; zero means unavailable.
};

using memory_debug_callback_t = void ( * )(
    const memory_debug_record_t &record,
    void *pContext ) noexcept; // Opaque value installed with the callback.

// Installs a callback and opaque context. Passing nullptr disables callbacks.
// Callbacks may run concurrently on producer threads. Replacing a callback does
// not drain calls already in flight, so the caller owns context synchronization.
CYPHER_COMMON_API void Cy_MemoryDebugSetCallback(
    memory_debug_callback_t pCallback,
    void *pContext = nullptr ) noexcept;

// Returns the installed callback and optionally its context.
CYPHER_NODISCARD CYPHER_COMMON_API memory_debug_callback_t Cy_MemoryDebugGetCallback(
    void **ppOutContext = nullptr ) noexcept;

// Reports one allocation event without allocating or invoking callbacks under a lock.
CYPHER_COMMON_API void Cy_MemoryDebugReportEvent(
    const memory_debug_record_t &record ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYDEBUG_H
