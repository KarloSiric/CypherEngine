//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryDebug.h
//  Purpose: Declares CypherCommon Tier0 MemoryDebug support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
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
    memory_debug_event_t eventType;
    void *pMemory;
    usize nByteCount;
    usize nAlignment;
    const char *pszTag;
    const char *pszFile;
    u32 nLine;
};

using memory_debug_callback_t = void ( * )(
    const memory_debug_record_t &record,
    void *pContext ) noexcept;

// Installs a callback and opaque context. Passing nullptr disables callbacks.
CYPHER_COMMON_API void Cy_MemoryDebugSetCallback(
    memory_debug_callback_t pCallback,
    void *pContext = nullptr ) noexcept;

// Returns the installed callback and optionally its context.
[[nodiscard]] CYPHER_COMMON_API memory_debug_callback_t Cy_MemoryDebugGetCallback(
    void **ppOutContext = nullptr ) noexcept;

// Reports one allocation event without allocating or invoking callbacks under a lock.
CYPHER_COMMON_API void Cy_MemoryDebugReportEvent(
    const memory_debug_record_t &record ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYDEBUG_H
