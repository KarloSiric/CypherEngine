//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PlatformMemory.h
//  Purpose: Declares CypherCommon Tier0 PlatformMemory support.
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

#ifndef CYPHER_COMMON_TIER0_PLATFORMMEMORY_H
#define CYPHER_COMMON_TIER0_PLATFORMMEMORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Platform Memory

Operating-system memory declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct platform_memory_info_t {
    usize nPageSize;
    usize nAllocationGranularity;
    u64 nTotalPhysicalBytes;
    u64 nAvailablePhysicalBytes;
};

// Queries page geometry and current physical-memory totals directly from the OS.
CYPHER_NODISCARD CYPHER_COMMON_API platform_memory_info_t Cy_PlatformMemoryGetInfo() noexcept;

// Reserves inaccessible virtual address space without committing physical pages.
CYPHER_NODISCARD CYPHER_COMMON_API void *Cy_PlatformMemoryReserve(
    usize nByteCount ) noexcept;

// Commits read/write pages inside a reservation.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_PlatformMemoryCommit(
    void *pMemory,
    usize nByteCount ) noexcept;

// Revokes access to committed pages and requests physical-page reclamation.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_PlatformMemoryDecommit(
    void *pMemory,
    usize nByteCount ) noexcept;

// Releases a complete reservation. POSIX callers must provide its original size.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_PlatformMemoryRelease(
    void *pMemory,
    usize nByteCount ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PLATFORMMEMORY_H
