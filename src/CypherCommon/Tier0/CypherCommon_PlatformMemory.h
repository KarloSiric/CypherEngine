//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PlatformMemory.h
//  Purpose: Wraps operating-system virtual memory reservation and page commitment.
//  Details: Byte ranges must obey the reported page geometry; reservations and
//           committed physical pages remain separate lifecycle operations.
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
    usize nPageSize;              // Commit/decommit granularity in bytes.
    usize nAllocationGranularity; // Reservation alignment in bytes.
    u64 nTotalPhysicalBytes;      // Installed physical memory reported by the OS.
    u64 nAvailablePhysicalBytes;  // Approximate bytes currently available to the system.
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
