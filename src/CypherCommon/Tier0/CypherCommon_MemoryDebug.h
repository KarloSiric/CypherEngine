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

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

enum class memory_debug_event_t : u32 {
    Alloc = 0u,
    Free,
    Realloc,
    Leak
};

using memory_debug_callback_t = void ( * )( memory_debug_event_t event_type,
                                            void *pMemory,
                                            usize cbSize,
                                            const char *pTag );

void MemoryDebug_SetCallback( memory_debug_callback_t callback );
void MemoryDebug_ReportEvent( memory_debug_event_t event_type, void *pMemory, usize cbSize, const char *pTag );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYDEBUG_H
