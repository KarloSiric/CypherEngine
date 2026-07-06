//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_SystemInfo.cpp
//  Purpose: Implements CypherCommon Tier0 system information support.
//  Details: This file builds a cached machine/runtime snapshot and exposes
//           dynamic OS queries used by diagnostics, benchmarks, crash reports,
//           allocators, and future editor tooling.
//
//  History:
//  - Created by Karlo Siric on 2026-07-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SystemInfo.h"

#include "CypherCommon_BuildConfig.h"
#include "CypherCommon_Compiler.h"
#include "CypherCommon_CPUDetect.h"
#include "CypherCommon_String.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <system_error>

#if CYPHER_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#elif CYPHER_PLATFORM_LINUX
    #include <sys/statvfs.h>
    #include <sys/sysinfo.h>
    #include <sys/utsname.h>
    #include <unistd.h>
#elif CYPHER_PLATFORM_MACOS
    #include <mach/mach.h>
    #include <mach-o/dyld.h>
    #include <sys/mount.h>
    #include <sys/sysctl.h>
    #include <unistd.h>
#endif

namespace cypher::common
{

namespace
{

constexpr usize CY_SYSTEMINFO_FALLBACK_PAGE_SIZE = 4096u;

cy_system_info_t g_systemInfo = {};
std::mutex g_systemInfoMutex;
std::atomic_bool g_systemInfoInitialized = false;

usize SystemInfo_AppendFormat( char *pszDst, usize cchDst, usize cchCurrent, const char *pszFormat, ... )
{
    va_list args;
    va_start( args, pszFormat );

    int cchWritten = 0;
    if ( pszDst != nullptr && cchDst > cchCurrent ) {
        cchWritten = std::vsnprintf( pszDst + cchCurrent, cchDst - cchCurrent, pszFormat, args );
    } else {
        cchWritten = std::vsnprintf( nullptr, 0u, pszFormat, args );
    }

    va_end( args );

    if ( cchWritten < 0 ) {
        return cchCurrent;
    }

    return cchCurrent + static_cast<usize>( cchWritten );
}

cy_system_memory_pressure_t SystemInfo_CalculateMemoryPressure( u64 totalBytes, u64 availableBytes )
{
    if ( totalBytes == 0u ) {
        return CY_SYSTEM_MEMORY_PRESSURE_UNKNOWN;
    }

    const u64 availablePercent = ( availableBytes * 100u ) / totalBytes;
    if ( availablePercent <= 5u ) {
        return CY_SYSTEM_MEMORY_PRESSURE_CRITICAL;
    }
    if ( availablePercent <= 15u ) {
        return CY_SYSTEM_MEMORY_PRESSURE_HIGH;
    }
    if ( availablePercent <= 50u ) {
        return CY_SYSTEM_MEMORY_PRESSURE_NORMAL;
    }

    return CY_SYSTEM_MEMORY_PRESSURE_LOW;
}

void FillBuildInfo( cy_system_build_info_t &build )
{
    const compiler_info_t compiler = Compiler_GetInfo();

    build.pszEngineName = "CypherEngine";
    build.pszEngineVersion = "0.1.0";
    build.pszBuildConfig = BuildConfig_GetName( BuildConfig_GetCurrent() );
    build.pszBuildDate = __DATE__;
    build.pszBuildTime = __TIME__;
    build.pszCompilerName = compiler.pName;
    build.compilerVersion = compiler.version;
}

void FillPlatformInfo( cy_system_platform_info_t &platform )
{
    platform.pszPlatformName = CYPHER_PLATFORM_NAME;
    platform.pszArchName = CYPHER_ARCH_NAME;
    platform.pointerSize = sizeof( void * );
    platform.is64Bit = CYPHER_TARGET_64BIT != 0;
    platform.isLittleEndian = CYPHER_ENDIAN_LITTLE != 0;
    platform.hasExceptions = CYPHER_CPP_EXCEPTIONS != 0;
    platform.hasRtti = CYPHER_CPP_RTTI != 0;
}

usize Platform_QueryPageSize()
{
#if CYPHER_PLATFORM_WINDOWS
    SYSTEM_INFO info = {};
    ::GetSystemInfo( &info );

    if ( info.dwPageSize == 0u ) {
        return CY_SYSTEMINFO_FALLBACK_PAGE_SIZE;
    }

    return static_cast<usize>( info.dwPageSize );
#elif CYPHER_PLATFORM_LINUX || CYPHER_PLATFORM_MACOS
    const long pageSize = ::sysconf( _SC_PAGESIZE );
    if ( pageSize <= 0 ) {
        return CY_SYSTEMINFO_FALLBACK_PAGE_SIZE;
    }

    return static_cast<usize>( pageSize );
#else
    return CY_SYSTEMINFO_FALLBACK_PAGE_SIZE;
#endif
}

usize Platform_QueryAllocationGranularity()
{
#if CYPHER_PLATFORM_WINDOWS
    SYSTEM_INFO info = {};
    ::GetSystemInfo( &info );

    if ( info.dwAllocationGranularity == 0u ) {
        return Platform_QueryPageSize();
    }

    return static_cast<usize>( info.dwAllocationGranularity );
#else
    return Platform_QueryPageSize();
#endif
}

u64 Platform_QueryTotalPhysicalMemory()
{
#if CYPHER_PLATFORM_WINDOWS
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof( status );

    if ( !::GlobalMemoryStatusEx( &status ) ) {
        return 0u;
    }

    return static_cast<u64>( status.ullTotalPhys );
#elif CYPHER_PLATFORM_LINUX
    struct sysinfo info = {};
    if ( ::sysinfo( &info ) != 0 ) {
        return 0u;
    }

    return static_cast<u64>( info.totalram ) * static_cast<u64>( info.mem_unit );
#elif CYPHER_PLATFORM_MACOS
    u64 memoryBytes = 0u;
    size_t cbMemory = sizeof( memoryBytes );

    if ( ::sysctlbyname( "hw.memsize", &memoryBytes, &cbMemory, nullptr, 0 ) != 0 ) {
        return 0u;
    }

    return memoryBytes;
#else
    return 0u;
#endif
}

void FillMemoryInfo( cy_system_memory_info_t &memory )
{
    memory.pageSize = Platform_QueryPageSize();
    memory.allocationGranularity = Platform_QueryAllocationGranularity();
    memory.totalPhysicalBytes = Platform_QueryTotalPhysicalMemory();
}

void FillOSInfo( cy_system_os_info_t &os )
{
#if CYPHER_PLATFORM_WINDOWS
    Cy_strncpy( os.szName, "Windows", CY_SYSTEMINFO_OS_NAME_MAX );
    Cy_strncpy( os.szVersion, "Unknown", CY_SYSTEMINFO_OS_VERSION_MAX );
#elif CYPHER_PLATFORM_LINUX
    struct utsname name = {};
    if ( ::uname( &name ) == 0 ) {
        Cy_strncpy( os.szName, name.sysname, CY_SYSTEMINFO_OS_NAME_MAX );
        Cy_strncpy( os.szVersion, name.release, CY_SYSTEMINFO_OS_VERSION_MAX );
    } else {
        Cy_strncpy( os.szName, "Linux", CY_SYSTEMINFO_OS_NAME_MAX );
        Cy_strncpy( os.szVersion, "Unknown", CY_SYSTEMINFO_OS_VERSION_MAX );
    }
#elif CYPHER_PLATFORM_MACOS
    Cy_strncpy( os.szName, "macOS", CY_SYSTEMINFO_OS_NAME_MAX );

    char version[CY_SYSTEMINFO_OS_VERSION_MAX] = {};
    size_t cbVersion = sizeof( version );

    if ( ::sysctlbyname( "kern.osproductversion", version, &cbVersion, nullptr, 0 ) == 0 ) {
        Cy_strncpy( os.szVersion, version, CY_SYSTEMINFO_OS_VERSION_MAX );
    } else {
        Cy_strncpy( os.szVersion, "Unknown", CY_SYSTEMINFO_OS_VERSION_MAX );
    }
#endif
}

void FillCPUInfo( cy_system_cpu_info_t &cpu )
{
    const cy_cpu_detect_info_t *pCpu = Cy_CPUDetectGetInfo();
    Cy_strncpy( cpu.szBrand, pCpu->szBrand, CY_SYSTEMINFO_CPU_BRAND_MAX );
    cpu.logicalThreadCount = pCpu->logicalThreadCount;
    cpu.physicalCoreCount = pCpu->physicalCoreCount;
    cpu.cacheLineSize = pCpu->cacheLineSize;
    cpu.featureFlags = pCpu->usableFeatures;
}

void FillProcessInfo( cy_system_process_info_t &process )
{
#if CYPHER_PLATFORM_WINDOWS
    process.processId = static_cast<u32>( ::GetCurrentProcessId() );

    const DWORD cchExecutable = ::GetModuleFileNameA(
        nullptr,
        process.szExecutablePath,
        static_cast<DWORD>( CY_SYSTEMINFO_PATH_MAX )
    );
    if ( cchExecutable == 0u || cchExecutable >= CY_SYSTEMINFO_PATH_MAX ) {
        Cy_strncpy( process.szExecutablePath, "Unknown", CY_SYSTEMINFO_PATH_MAX );
    }

    const DWORD cchWorkingDir = ::GetCurrentDirectoryA(
        static_cast<DWORD>( CY_SYSTEMINFO_PATH_MAX ),
        process.szWorkingDirectory
    );
    if ( cchWorkingDir == 0u || cchWorkingDir >= CY_SYSTEMINFO_PATH_MAX ) {
        Cy_strncpy( process.szWorkingDirectory, "Unknown", CY_SYSTEMINFO_PATH_MAX );
    }
#elif CYPHER_PLATFORM_LINUX
    const pid_t pid = ::getpid();
    process.processId = pid > 0 ? static_cast<u32>( pid ) : 0u;

    const ssize_t cchExecutable = ::readlink(
        "/proc/self/exe",
        process.szExecutablePath,
        CY_SYSTEMINFO_PATH_MAX - 1u
    );
    if ( cchExecutable > 0 ) {
        process.szExecutablePath[cchExecutable] = '\0';
    } else {
        Cy_strncpy( process.szExecutablePath, "Unknown", CY_SYSTEMINFO_PATH_MAX );
    }

    if ( ::getcwd( process.szWorkingDirectory, CY_SYSTEMINFO_PATH_MAX ) == nullptr ) {
        Cy_strncpy( process.szWorkingDirectory, "Unknown", CY_SYSTEMINFO_PATH_MAX );
    }
#elif CYPHER_PLATFORM_MACOS
    const pid_t pid = ::getpid();
    process.processId = pid > 0 ? static_cast<u32>( pid ) : 0u;

    u32 cchExecutable = static_cast<u32>( CY_SYSTEMINFO_PATH_MAX );
    if ( ::_NSGetExecutablePath( process.szExecutablePath, &cchExecutable ) != 0 ) {
        Cy_strncpy( process.szExecutablePath, "Unknown", CY_SYSTEMINFO_PATH_MAX );
    }

    if ( ::getcwd( process.szWorkingDirectory, CY_SYSTEMINFO_PATH_MAX ) == nullptr ) {
        Cy_strncpy( process.szWorkingDirectory, "Unknown", CY_SYSTEMINFO_PATH_MAX );
    }
#endif
}

} // namespace

bool_t Cy_SystemInfoInit()
{
    if ( g_systemInfoInitialized.load( std::memory_order_acquire ) ) {
        return CY_TRUE;
    }

    std::lock_guard<std::mutex> lock( g_systemInfoMutex );
    if ( g_systemInfoInitialized.load( std::memory_order_relaxed ) ) {
        return CY_TRUE;
    }

    g_systemInfo = {};
    FillBuildInfo( g_systemInfo.build );
    FillPlatformInfo( g_systemInfo.platform );
    FillOSInfo( g_systemInfo.os );
    FillCPUInfo( g_systemInfo.cpu );
    FillMemoryInfo( g_systemInfo.memory );
    FillProcessInfo( g_systemInfo.process );

    g_systemInfoInitialized.store( true, std::memory_order_release );
    return CY_TRUE;
}

void Cy_SystemInfoShutdown()
{
    std::lock_guard<std::mutex> lock( g_systemInfoMutex );
    g_systemInfo = {};
    g_systemInfoInitialized.store( false, std::memory_order_release );
}

const cy_system_info_t *Cy_SystemInfoGet()
{
    Cy_SystemInfoInit();
    return &g_systemInfo;
}

cy_system_memory_status_t Cy_SystemInfoQueryMemoryStatus()
{
    cy_system_memory_status_t status = {};

#if CYPHER_PLATFORM_WINDOWS
    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof( memory );
    if ( ::GlobalMemoryStatusEx( &memory ) ) {
        status.totalPhysicalBytes = static_cast<u64>( memory.ullTotalPhys );
        status.availablePhysicalBytes = static_cast<u64>( memory.ullAvailPhys );
    }
#elif CYPHER_PLATFORM_LINUX
    struct sysinfo memory = {};
    if ( ::sysinfo( &memory ) == 0 ) {
        status.totalPhysicalBytes = static_cast<u64>( memory.totalram ) * static_cast<u64>( memory.mem_unit );
        status.availablePhysicalBytes = static_cast<u64>( memory.freeram ) * static_cast<u64>( memory.mem_unit );
    }

    FILE *pStatm = std::fopen( "/proc/self/statm", "r" );
    if ( pStatm != nullptr ) {
        unsigned long long virtualPages = 0ull;
        unsigned long long residentPages = 0ull;
        if ( std::fscanf( pStatm, "%llu %llu", &virtualPages, &residentPages ) == 2 ) {
            const u64 pageSize = static_cast<u64>( Platform_QueryPageSize() );
            status.processVirtualBytes = static_cast<u64>( virtualPages ) * pageSize;
            status.processResidentBytes = static_cast<u64>( residentPages ) * pageSize;
        }
        std::fclose( pStatm );
    }
#elif CYPHER_PLATFORM_MACOS
    u64 totalBytes = 0u;
    size_t cbTotalBytes = sizeof( totalBytes );
    if ( ::sysctlbyname( "hw.memsize", &totalBytes, &cbTotalBytes, nullptr, 0 ) == 0 ) {
        status.totalPhysicalBytes = totalBytes;
    }

    vm_statistics64_data_t vmStats = {};
    mach_msg_type_number_t vmStatsCount = HOST_VM_INFO64_COUNT;
    if ( ::host_statistics64( ::mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>( &vmStats ), &vmStatsCount ) == KERN_SUCCESS ) {
        const u64 pageSize = static_cast<u64>( Platform_QueryPageSize() );
        status.availablePhysicalBytes = static_cast<u64>( vmStats.free_count + vmStats.inactive_count ) * pageSize;
    }

    mach_task_basic_info_data_t taskInfo = {};
    mach_msg_type_number_t taskInfoCount = MACH_TASK_BASIC_INFO_COUNT;
    if ( ::task_info( ::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>( &taskInfo ), &taskInfoCount ) == KERN_SUCCESS ) {
        status.processResidentBytes = static_cast<u64>( taskInfo.resident_size );
        status.processVirtualBytes = static_cast<u64>( taskInfo.virtual_size );
    }
#endif

    if ( status.totalPhysicalBytes == 0u ) {
        status.totalPhysicalBytes = Platform_QueryTotalPhysicalMemory();
    }

    status.pressure = SystemInfo_CalculateMemoryPressure( status.totalPhysicalBytes, status.availablePhysicalBytes );
    return status;
}

cy_system_power_state_t Cy_SystemInfoQueryPowerState()
{
#if CYPHER_PLATFORM_WINDOWS
    SYSTEM_POWER_STATUS power = {};
    if ( !::GetSystemPowerStatus( &power ) ) {
        return CY_SYSTEM_POWER_UNKNOWN;
    }

    if ( power.BatteryFlag == 128u ) {
        return CY_SYSTEM_POWER_NO_BATTERY;
    }
    if ( power.ACLineStatus == 1u ) {
        return CY_SYSTEM_POWER_AC;
    }
    if ( power.ACLineStatus == 0u ) {
        return CY_SYSTEM_POWER_BATTERY;
    }
#endif

    return CY_SYSTEM_POWER_UNKNOWN;
}

cy_system_disk_status_t Cy_SystemInfoQueryDiskStatus( const char *pszPath )
{
    cy_system_disk_status_t status = {};

    std::error_code error;
    const char *pszQueryPath = Cy_strisempty( pszPath ) ? "." : pszPath;
    const std::filesystem::space_info space = std::filesystem::space( pszQueryPath, error );

    if ( error ) {
        return status;
    }

    status.totalBytes = static_cast<u64>( space.capacity );
    status.freeBytes = static_cast<u64>( space.free );
    status.availableBytes = static_cast<u64>( space.available );
    return status;
}

bool_t Cy_SystemInfoHasCpuFeature( flags64_t features, cy_system_cpu_feature_flags_t feature )
{
    return ( features & static_cast<flags64_t>( feature ) ) != 0u;
}

const char *Cy_SystemInfoCpuFeatureName( cy_system_cpu_feature_flags_t feature )
{
    return Cy_CPUDetectFeatureName( static_cast<cy_cpu_feature_flags_t>( feature ) );
}

usize Cy_SystemInfoFormatReport( char *pszDst, usize cchDst )
{
    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    const cy_system_memory_status_t memory = Cy_SystemInfoQueryMemoryStatus();

    if ( pszDst != nullptr && cchDst > 0u ) {
        pszDst[0] = '\0';
    }

    usize cchRequired = 0u;
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "Cypher System Info\n" );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "engine: %s %s\n", pInfo->build.pszEngineName, pInfo->build.pszEngineVersion );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "build: %s %s %s\n", pInfo->build.pszBuildConfig, pInfo->build.pszBuildDate, pInfo->build.pszBuildTime );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "compiler: %s %u\n", pInfo->build.pszCompilerName, pInfo->build.compilerVersion );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "platform: %s %s\n", pInfo->platform.pszPlatformName, pInfo->platform.pszArchName );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "os: %s %s\n", pInfo->os.szName, pInfo->os.szVersion );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "cpu: %s\n", pInfo->cpu.szBrand );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "logical_threads: %u\n", pInfo->cpu.logicalThreadCount );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "physical_cores: %u\n", pInfo->cpu.physicalCoreCount );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "cache_line_size: %zu\n", pInfo->cpu.cacheLineSize );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "page_size: %zu\n", pInfo->memory.pageSize );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "allocation_granularity: %zu\n", pInfo->memory.allocationGranularity );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "total_physical_bytes: %llu\n", static_cast<unsigned long long>( pInfo->memory.totalPhysicalBytes ) );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "available_physical_bytes: %llu\n", static_cast<unsigned long long>( memory.availablePhysicalBytes ) );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "process_resident_bytes: %llu\n", static_cast<unsigned long long>( memory.processResidentBytes ) );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "process_virtual_bytes: %llu\n", static_cast<unsigned long long>( memory.processVirtualBytes ) );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "process_id: %u\n", pInfo->process.processId );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "executable_path: %s\n", pInfo->process.szExecutablePath );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "working_directory: %s\n", pInfo->process.szWorkingDirectory );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "little_endian: %s\n", pInfo->platform.isLittleEndian ? "yes" : "no" );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "is_64_bit: %s\n", pInfo->platform.is64Bit ? "yes" : "no" );
    cchRequired = SystemInfo_AppendFormat( pszDst, cchDst, cchRequired, "cpu_features: 0x%llx\n", static_cast<unsigned long long>( pInfo->cpu.featureFlags ) );

    if ( pszDst != nullptr && cchDst > 0u ) {
        pszDst[cchDst - 1u] = '\0';
    }

    return cchRequired;
}

} // namespace cypher::common
