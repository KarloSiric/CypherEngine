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
    #include <unistd.h>
    #if !defined( MAP_ANON ) && defined( MAP_ANONYMOUS )
        #define MAP_ANON MAP_ANONYMOUS
    #endif
#endif

namespace cypher::common
{

//-----------------------------------------------------------------------------
// Native virtual-memory translation
//
// Reserve establishes an address range, commit makes pages accessible, decommit
// releases their physical backing, and release destroys the reservation itself.
// Callers must preserve page alignment across every transition.
//-----------------------------------------------------------------------------
namespace
{

usize PlatformMemory_PageSize() noexcept
{
#if CYPHER_PLATFORM_WINDOWS
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo( &systemInfo );
    return static_cast<usize>( systemInfo.dwPageSize );
#elif CYPHER_PLATFORM_POSIX
    const long nPageSize = ::sysconf( _SC_PAGESIZE );
    return nPageSize > 0 ? static_cast<usize>( nPageSize ) : 4096u;
#else
    return 4096u;
#endif
}

bool_t PlatformMemory_AlignSize(
    usize nByteCount,
    usize nAlignment,
    usize &nOutAlignedByteCount ) noexcept
{
    if ( nByteCount == 0u || nAlignment == 0u ) {
        nOutAlignedByteCount = 0u;
        return CY_FALSE;
    }
    return Cy_AlignUpChecked( nByteCount, nAlignment, nOutAlignedByteCount );
}

bool_t PlatformMemory_IsPageAligned( const void *pMemory, usize nPageSize ) noexcept
{
    return pMemory != nullptr &&
           Cy_AlignIsAligned( reinterpret_cast<uintptr>( pMemory ), nPageSize );
}

u64 PlatformMemory_PageCountToBytes( long nPageCount, usize nPageSize ) noexcept
{
    if ( nPageCount <= 0 ) {
        return 0u;
    }

    const u64 nCount = static_cast<u64>( nPageCount );
    const u64 nSize = static_cast<u64>( nPageSize );
    if ( nCount > CY_U64_MAX / nSize ) {
        return CY_U64_MAX;
    }
    return nCount * nSize;
}

} // namespace

platform_memory_info_t Cy_PlatformMemoryGetInfo() noexcept
{
    platform_memory_info_t info{};

#if CYPHER_PLATFORM_WINDOWS
    SYSTEM_INFO systemInfo{};
    ::GetSystemInfo( &systemInfo );
    info.nPageSize = static_cast<usize>( systemInfo.dwPageSize );
    info.nAllocationGranularity =
        static_cast<usize>( systemInfo.dwAllocationGranularity );

    MEMORYSTATUSEX memoryStatus{};
    memoryStatus.dwLength = sizeof( memoryStatus );
    if ( ::GlobalMemoryStatusEx( &memoryStatus ) != FALSE ) {
        info.nTotalPhysicalBytes = static_cast<u64>( memoryStatus.ullTotalPhys );
        info.nAvailablePhysicalBytes = static_cast<u64>( memoryStatus.ullAvailPhys );
    }
#elif CYPHER_PLATFORM_POSIX
    info.nPageSize = PlatformMemory_PageSize();
    // POSIX mmap reservations have page granularity. Windows distinguishes page
    // size from the larger virtual allocation granularity.
    info.nAllocationGranularity = info.nPageSize;

    #if defined( _SC_PHYS_PAGES )
        info.nTotalPhysicalBytes =
            PlatformMemory_PageCountToBytes( ::sysconf( _SC_PHYS_PAGES ), info.nPageSize );
    #endif
    #if defined( _SC_AVPHYS_PAGES )
        info.nAvailablePhysicalBytes =
            PlatformMemory_PageCountToBytes( ::sysconf( _SC_AVPHYS_PAGES ), info.nPageSize );
    #endif
#else
    info.nPageSize = 4096u;
    info.nAllocationGranularity = info.nPageSize;
#endif

    if ( info.nPageSize == 0u ) {
        info.nPageSize = 4096u;
    }
    if ( info.nAllocationGranularity == 0u ) {
        info.nAllocationGranularity = info.nPageSize;
    }
    return info;
}

void *Cy_PlatformMemoryReserve( usize nByteCount ) noexcept
{
    if ( nByteCount == 0u ) {
        return nullptr;
    }

    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    usize nAlignedByteCount = 0u;
    if ( !PlatformMemory_AlignSize(
             nByteCount,
             info.nAllocationGranularity,
             nAlignedByteCount ) ) {
        return nullptr;
    }

#if CYPHER_PLATFORM_WINDOWS
    return ::VirtualAlloc( nullptr, nAlignedByteCount, MEM_RESERVE, PAGE_NOACCESS );
#elif CYPHER_PLATFORM_POSIX
    // PROT_NONE reserves address space and catches accidental access before the
    // caller commits a page range with mprotect.
    void *pMemory = ::mmap(
        nullptr,
        nAlignedByteCount,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0 );
    return pMemory != MAP_FAILED ? pMemory : nullptr;
#else
    return nullptr;
#endif
}

bool_t Cy_PlatformMemoryCommit(
    void *pMemory,
    usize nByteCount ) noexcept
{
    const usize nPageSize = PlatformMemory_PageSize();
    if ( !PlatformMemory_IsPageAligned( pMemory, nPageSize ) ) {
        return CY_FALSE;
    }

    usize nAlignedByteCount = 0u;
    if ( !PlatformMemory_AlignSize( nByteCount, nPageSize, nAlignedByteCount ) ) {
        return CY_FALSE;
    }

#if CYPHER_PLATFORM_WINDOWS
    // VirtualAlloc performs a true commit and obtains backing-store commitment.
    return ::VirtualAlloc(
               pMemory,
               nAlignedByteCount,
               MEM_COMMIT,
               PAGE_READWRITE ) != nullptr;
#elif CYPHER_PLATFORM_POSIX
    // POSIX has no direct reserve/commit split matching Windows. Changing access
    // protection makes the range usable; physical pages remain demand-paged.
    return ::mprotect(
               pMemory,
               nAlignedByteCount,
               PROT_READ | PROT_WRITE ) == 0;
#else
    return CY_FALSE;
#endif
}

bool_t Cy_PlatformMemoryDecommit(
    void *pMemory,
    usize nByteCount ) noexcept
{
    const usize nPageSize = PlatformMemory_PageSize();
    if ( !PlatformMemory_IsPageAligned( pMemory, nPageSize ) ) {
        return CY_FALSE;
    }

    usize nAlignedByteCount = 0u;
    if ( !PlatformMemory_AlignSize( nByteCount, nPageSize, nAlignedByteCount ) ) {
        return CY_FALSE;
    }

#if CYPHER_PLATFORM_WINDOWS
    return ::VirtualFree( pMemory, nAlignedByteCount, MEM_DECOMMIT ) != FALSE;
#elif CYPHER_PLATFORM_POSIX
    // Revoke access first so stale pointers fault consistently. MADV_DONTNEED is
    // best effort and asks the kernel to discard resident backing pages.
    if ( ::mprotect( pMemory, nAlignedByteCount, PROT_NONE ) != 0 ) {
        return CY_FALSE;
    }
    #if defined( MADV_DONTNEED )
        ( void )::madvise( pMemory, nAlignedByteCount, MADV_DONTNEED );
    #endif
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

bool_t Cy_PlatformMemoryRelease(
    void *pMemory,
    usize nByteCount ) noexcept
{
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }

#if CYPHER_PLATFORM_WINDOWS
    // MEM_RELEASE requires the original reservation base and a zero byte count.
    ( void )nByteCount;
    return ::VirtualFree( pMemory, 0u, MEM_RELEASE ) != FALSE;
#elif CYPHER_PLATFORM_POSIX
    // munmap requires the reservation length, so POSIX callers must retain it.
    usize nAlignedByteCount = 0u;
    if ( !PlatformMemory_AlignSize(
             nByteCount,
             PlatformMemory_PageSize(),
             nAlignedByteCount ) ) {
        return CY_FALSE;
    }
    return ::munmap( pMemory, nAlignedByteCount ) == 0;
#else
    ( void )nByteCount;
    return CY_FALSE;
#endif
}

} // namespace cypher::common
