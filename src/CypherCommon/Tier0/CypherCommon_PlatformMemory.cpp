//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_PlatformMemory.cpp
//  Purpose: Implements CypherCommon Tier0 OS virtual memory helpers.
//  Details: This file gives allocators one portable page-level reserve, commit,
//           decommit, and release surface.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_PlatformMemory.h"

#include "CypherCommon_Align.h"
#include "CypherCommon_Platform.h"
#include "CypherCommon_SystemInfo.h"

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif CYPHER_PLATFORM_POSIX
    #include <sys/mman.h>
    #if !defined( MAP_ANON ) && defined( MAP_ANONYMOUS )
        #define MAP_ANON MAP_ANONYMOUS
    #endif
#endif

namespace cypher::common
{

platform_memory_info_t PlatformMemory_GetInfo()
{
    Cy_SystemInfoInit();

    const cy_system_info_t *pSystemInfo = Cy_SystemInfoGet();
    const cy_system_memory_status_t memoryStatus = Cy_SystemInfoQueryMemoryStatus();

    platform_memory_info_t info{};
    info.page_size = pSystemInfo != nullptr ? pSystemInfo->memory.pageSize : CY_KB * 4u;
    info.allocation_granularity = pSystemInfo != nullptr ? pSystemInfo->memory.allocationGranularity : info.page_size;
    info.total_physical_memory = memoryStatus.totalPhysicalBytes;
    info.available_physical_memory = memoryStatus.availablePhysicalBytes;
    return info;
}

void *PlatformMemory_Reserve( usize cbSize )
{
    if ( cbSize == 0u ) {
        return nullptr;
    }

    const platform_memory_info_t info = PlatformMemory_GetInfo();
    const usize cbAlignedSize = AlignUp( cbSize, info.page_size );

#if CYPHER_PLATFORM_WINDOWS
    return ::VirtualAlloc( nullptr, cbAlignedSize, MEM_RESERVE, PAGE_READWRITE );
#elif CYPHER_PLATFORM_POSIX
    void *pMemory = ::mmap( nullptr, cbAlignedSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0 );
    return pMemory != MAP_FAILED ? pMemory : nullptr;
#else
    return nullptr;
#endif
}

bool_t PlatformMemory_Commit( void *pMemory, usize cbSize )
{
    if ( pMemory == nullptr || cbSize == 0u ) {
        return CY_FALSE;
    }

    const platform_memory_info_t info = PlatformMemory_GetInfo();
    const usize cbAlignedSize = AlignUp( cbSize, info.page_size );

#if CYPHER_PLATFORM_WINDOWS
    return ::VirtualAlloc( pMemory, cbAlignedSize, MEM_COMMIT, PAGE_READWRITE ) != nullptr;
#elif CYPHER_PLATFORM_POSIX
    return ::mprotect( pMemory, cbAlignedSize, PROT_READ | PROT_WRITE ) == 0;
#else
    return CY_FALSE;
#endif
}

void PlatformMemory_Decommit( void *pMemory, usize cbSize )
{
    if ( pMemory == nullptr || cbSize == 0u ) {
        return;
    }

    const platform_memory_info_t info = PlatformMemory_GetInfo();
    const usize cbAlignedSize = AlignUp( cbSize, info.page_size );

#if CYPHER_PLATFORM_WINDOWS
    ::VirtualFree( pMemory, cbAlignedSize, MEM_DECOMMIT );
#elif CYPHER_PLATFORM_POSIX
    ::mprotect( pMemory, cbAlignedSize, PROT_NONE );
    #if defined( MADV_DONTNEED )
        ::madvise( pMemory, cbAlignedSize, MADV_DONTNEED );
    #endif
#endif
}

void PlatformMemory_Release( void *pMemory, usize cbSize )
{
    if ( pMemory == nullptr ) {
        return;
    }

#if CYPHER_PLATFORM_WINDOWS
    ( void )cbSize;
    ::VirtualFree( pMemory, 0u, MEM_RELEASE );
#elif CYPHER_PLATFORM_POSIX
    if ( cbSize != 0u ) {
        const platform_memory_info_t info = PlatformMemory_GetInfo();
        ::munmap( pMemory, AlignUp( cbSize, info.page_size ) );
    }
#endif
}

} // namespace cypher::common
