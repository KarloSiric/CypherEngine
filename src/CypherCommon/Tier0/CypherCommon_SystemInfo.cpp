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
#include "CypherCommon_BuildId.h"
#include "CypherCommon_Compiler.h"
#include "CypherCommon_CPUDetect.h"
#include "CypherCommon_PlatformMemory.h"
#include "CypherCommon_Process.h"

#include <climits>
#include <cstdio>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef PSAPI_VERSION
        #define PSAPI_VERSION 2
    #endif
    #include <psapi.h>
    #include <windows.h>
#elif CYPHER_PLATFORM_LINUX
    #include <sys/statvfs.h>
    #include <sys/utsname.h>
    #include <unistd.h>
#elif CYPHER_PLATFORM_MACOS
    #include <mach/mach.h>
    #include <sys/mount.h>
    #include <sys/sysctl.h>
    #include <unistd.h>
#endif

namespace cypher::common
{

namespace
{

void SystemInfo_CopyString( char *pszDst, usize cchDst, const char *pszSrc ) noexcept
{
    if ( pszDst == nullptr || cchDst == 0u ) {
        return;
    }

    const char *pszRead = pszSrc != nullptr ? pszSrc : "";
    usize cchWrite = 0u;
    while ( cchWrite + 1u < cchDst && pszRead[cchWrite] != '\0' ) {
        pszDst[cchWrite] = pszRead[cchWrite];
        ++cchWrite;
    }

    pszDst[cchWrite] = '\0';
}

bool_t SystemInfo_IsStringEmpty( const char *pszString ) noexcept
{
    return pszString == nullptr || pszString[0] == '\0';
}

#if CYPHER_PLATFORM_WINDOWS
bool_t SystemInfo_Utf8ToWide(
    const char *pszUtf8,
    wchar_t *pwszDst,
    usize cchDst ) noexcept
{
    if ( pszUtf8 == nullptr ||
         pwszDst == nullptr ||
         cchDst == 0u ||
         cchDst > static_cast<usize>( INT_MAX ) ) {
        return CY_FALSE;
    }

    pwszDst[0] = L'\0';
    return ::MultiByteToWideChar(
               CP_UTF8,
               MB_ERR_INVALID_CHARS,
               pszUtf8,
               -1,
               pwszDst,
               static_cast<int>( cchDst ) ) > 0;
}

bool_t SystemInfo_WideToUtf8(
    const wchar_t *pwszWide,
    char *pszDst,
    usize cchDst ) noexcept
{
    if ( pwszWide == nullptr ||
         pszDst == nullptr ||
         cchDst == 0u ||
         cchDst > static_cast<usize>( INT_MAX ) ) {
        return CY_FALSE;
    }

    pszDst[0] = '\0';
    return ::WideCharToMultiByte(
               CP_UTF8,
               WC_ERR_INVALID_CHARS,
               pwszWide,
               -1,
               pszDst,
               static_cast<int>( cchDst ),
               nullptr,
               nullptr ) > 0;
}
#endif

struct system_info_report_writer_t {
    char *pszDst;
    usize cchDst;
    usize cchRequired;
};

void SystemInfo_ReportAppendChar(
    system_info_report_writer_t &writer,
    char value ) noexcept
{
    if ( writer.pszDst != nullptr &&
         writer.cchDst > 0u &&
         writer.cchRequired < writer.cchDst - 1u ) {
        writer.pszDst[writer.cchRequired] = value;
    }

    if ( writer.cchRequired != CY_USIZE_MAX ) {
        ++writer.cchRequired;
    }
}

void SystemInfo_ReportAppendString(
    system_info_report_writer_t &writer,
    const char *pszValue ) noexcept
{
    const char *pszRead = pszValue != nullptr ? pszValue : "";
    while ( *pszRead != '\0' ) {
        SystemInfo_ReportAppendChar( writer, *pszRead );
        ++pszRead;
    }
}

void SystemInfo_ReportAppendU64(
    system_info_report_writer_t &writer,
    u64 value ) noexcept
{
    char szDigits[32] = {};
    usize cchDigits = 0u;
    do {
        szDigits[cchDigits] = static_cast<char>( '0' + ( value % 10u ) );
        ++cchDigits;
        value /= 10u;
    } while ( value != 0u );

    while ( cchDigits > 0u ) {
        --cchDigits;
        SystemInfo_ReportAppendChar( writer, szDigits[cchDigits] );
    }
}

void SystemInfo_ReportAppendHexU64(
    system_info_report_writer_t &writer,
    u64 value ) noexcept
{
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    char szDigits[16] = {};
    usize cchDigits = 0u;
    do {
        szDigits[cchDigits] = HEX_DIGITS[value & 0x0Fu];
        ++cchDigits;
        value >>= 4u;
    } while ( value != 0u );

    SystemInfo_ReportAppendString( writer, "0x" );
    while ( cchDigits > 0u ) {
        --cchDigits;
        SystemInfo_ReportAppendChar( writer, szDigits[cchDigits] );
    }
}

void SystemInfo_ReportAppendStringLine(
    system_info_report_writer_t &writer,
    const char *pszName,
    const char *pszValue ) noexcept
{
    SystemInfo_ReportAppendString( writer, pszName );
    SystemInfo_ReportAppendString( writer, ": " );
    SystemInfo_ReportAppendString( writer, pszValue );
    SystemInfo_ReportAppendChar( writer, '\n' );
}

void SystemInfo_ReportAppendU64Line(
    system_info_report_writer_t &writer,
    const char *pszName,
    u64 value ) noexcept
{
    SystemInfo_ReportAppendString( writer, pszName );
    SystemInfo_ReportAppendString( writer, ": " );
    SystemInfo_ReportAppendU64( writer, value );
    SystemInfo_ReportAppendChar( writer, '\n' );
}

void SystemInfo_ReportTerminate( system_info_report_writer_t &writer ) noexcept
{
    if ( writer.pszDst == nullptr || writer.cchDst == 0u ) {
        return;
    }

    const usize iTerminator =
        writer.cchRequired < writer.cchDst ? writer.cchRequired : writer.cchDst - 1u;
    writer.pszDst[iTerminator] = '\0';
}

u64 SystemInfo_SaturatingMultiply( u64 left, u64 right ) noexcept
{
    if ( left == 0u || right == 0u ) {
        return 0u;
    }
    if ( left > CY_U64_MAX / right ) {
        return CY_U64_MAX;
    }
    return left * right;
}

cy_system_memory_pressure_t SystemInfo_CalculateMemoryPressure(
    u64 totalBytes,
    u64 availableBytes ) noexcept
{
    if ( totalBytes == 0u ) {
        return CY_SYSTEM_MEMORY_PRESSURE_UNKNOWN;
    }

    const f64 availablePercent =
        ( static_cast<f64>( availableBytes ) / static_cast<f64>( totalBytes ) ) * 100.0;
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

void FillBuildInfo( cy_system_build_info_t &build ) noexcept
{
    const compiler_info_t compiler = Cy_CompilerGetInfo();
    const build_id_t *pBuildId = Cy_BuildIdGetEngine();

    build.pszEngineName = pBuildId->pszProductName;
    build.pszEngineVersion = pBuildId->pszVersion;
    build.pszBuildConfig = Cy_BuildConfigGetName( Cy_BuildConfigGetCurrent() );
    build.pszBuildDate = __DATE__;
    build.pszBuildTime = __TIME__;
    build.pszCompilerName = compiler.pName;
    build.compilerVersion = compiler.version;
}

void FillPlatformInfo( cy_system_platform_info_t &platform ) noexcept
{
    platform.pszPlatformName = CYPHER_PLATFORM_NAME;
    platform.pszArchName = CYPHER_ARCH_NAME;
    platform.pointerSize = sizeof( void * );
    platform.is64Bit = CYPHER_TARGET_64BIT != 0;
    platform.isLittleEndian = CYPHER_ENDIAN_LITTLE != 0;
    platform.hasExceptions = CYPHER_CPP_EXCEPTIONS != 0;
    platform.hasRtti = CYPHER_CPP_RTTI != 0;
}

void FillMemoryInfo( cy_system_memory_info_t &memory ) noexcept
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    memory.pageSize = info.nPageSize;
    memory.allocationGranularity = info.nAllocationGranularity;
    memory.totalPhysicalBytes = info.nTotalPhysicalBytes;
}

void FillOSInfo( cy_system_os_info_t &os ) noexcept
{
#if CYPHER_PLATFORM_WINDOWS
    SystemInfo_CopyString( os.szName, CY_SYSTEMINFO_OS_NAME_MAX, "Windows" );
    SystemInfo_CopyString( os.szVersion, CY_SYSTEMINFO_OS_VERSION_MAX, "Unknown" );
#elif CYPHER_PLATFORM_LINUX
    struct utsname name = {};
    if ( ::uname( &name ) == 0 ) {
        SystemInfo_CopyString( os.szName, CY_SYSTEMINFO_OS_NAME_MAX, name.sysname );
        SystemInfo_CopyString( os.szVersion, CY_SYSTEMINFO_OS_VERSION_MAX, name.release );
    } else {
        SystemInfo_CopyString( os.szName, CY_SYSTEMINFO_OS_NAME_MAX, "Linux" );
        SystemInfo_CopyString( os.szVersion, CY_SYSTEMINFO_OS_VERSION_MAX, "Unknown" );
    }
#elif CYPHER_PLATFORM_MACOS
    SystemInfo_CopyString( os.szName, CY_SYSTEMINFO_OS_NAME_MAX, "macOS" );

    char version[CY_SYSTEMINFO_OS_VERSION_MAX] = {};
    size_t cbVersion = sizeof( version );

    if ( ::sysctlbyname( "kern.osproductversion", version, &cbVersion, nullptr, 0 ) == 0 ) {
        SystemInfo_CopyString( os.szVersion, CY_SYSTEMINFO_OS_VERSION_MAX, version );
    } else {
        SystemInfo_CopyString( os.szVersion, CY_SYSTEMINFO_OS_VERSION_MAX, "Unknown" );
    }
#endif
}

void FillCPUInfo( cy_system_cpu_info_t &cpu ) noexcept
{
    const cy_cpu_detect_info_t *pCpu = Cy_CPUDetectGetInfo();
    SystemInfo_CopyString( cpu.szBrand, CY_SYSTEMINFO_CPU_BRAND_MAX, pCpu->szBrand );
    cpu.logicalThreadCount = pCpu->logicalThreadCount;
    cpu.physicalCoreCount = pCpu->physicalCoreCount;
    cpu.cacheLineSize = pCpu->cacheLineSize;
    cpu.featureFlags = pCpu->usableFeatures;
}

void FillProcessInfo( cy_system_process_info_t &process ) noexcept
{
    process.processId = Cy_ProcessGetCurrentId();
    SystemInfo_CopyString(
        process.szExecutablePath,
        CY_SYSTEMINFO_PATH_MAX,
        Cy_ProcessGetExecutablePath() );

#if CYPHER_PLATFORM_WINDOWS
    wchar_t wszWorkingDirectory[CY_SYSTEMINFO_PATH_MAX] = {};
    const DWORD cchWorkingDir = ::GetCurrentDirectoryW(
        static_cast<DWORD>( CYPHER_ARRAY_COUNT( wszWorkingDirectory ) ),
        wszWorkingDirectory );
    if ( cchWorkingDir == 0u ||
         cchWorkingDir >= CYPHER_ARRAY_COUNT( wszWorkingDirectory ) ||
         !SystemInfo_WideToUtf8(
             wszWorkingDirectory,
             process.szWorkingDirectory,
             CYPHER_ARRAY_COUNT( process.szWorkingDirectory ) ) ) {
        SystemInfo_CopyString( process.szWorkingDirectory, CY_SYSTEMINFO_PATH_MAX, "Unknown" );
    }
#elif CYPHER_PLATFORM_LINUX || CYPHER_PLATFORM_MACOS
    if ( ::getcwd( process.szWorkingDirectory, CY_SYSTEMINFO_PATH_MAX ) == nullptr ) {
        SystemInfo_CopyString( process.szWorkingDirectory, CY_SYSTEMINFO_PATH_MAX, "Unknown" );
    }
#endif
}

cy_system_info_t SystemInfo_BuildSnapshot() noexcept
{
    cy_system_info_t info = {};
    FillBuildInfo( info.build );
    FillPlatformInfo( info.platform );
    FillOSInfo( info.os );
    FillCPUInfo( info.cpu );
    FillMemoryInfo( info.memory );
    FillProcessInfo( info.process );
    return info;
}

const cy_system_info_t &SystemInfo_GetCachedSnapshot() noexcept
{
    static const cy_system_info_t info = SystemInfo_BuildSnapshot();
    return info;
}

} // namespace

bool_t Cy_SystemInfoInit() noexcept
{
    CYPHER_UNUSED( SystemInfo_GetCachedSnapshot() );
    return CY_TRUE;
}

const cy_system_info_t *Cy_SystemInfoGet() noexcept
{
    return &SystemInfo_GetCachedSnapshot();
}

cy_system_memory_status_t Cy_SystemInfoQueryMemoryStatus() noexcept
{
    cy_system_memory_status_t status = {};
    const platform_memory_info_t platformMemory = Cy_PlatformMemoryGetInfo();
    status.totalPhysicalBytes = platformMemory.nTotalPhysicalBytes;
    status.availablePhysicalBytes = platformMemory.nAvailablePhysicalBytes;
    status.hasPhysicalMemory = status.totalPhysicalBytes != 0u;

#if CYPHER_PLATFORM_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX processMemory = {};
    processMemory.cb = sizeof( processMemory );
    if ( ::K32GetProcessMemoryInfo(
             ::GetCurrentProcess(),
             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>( &processMemory ),
             sizeof( processMemory ) ) != FALSE ) {
        status.processResidentBytes = static_cast<u64>( processMemory.WorkingSetSize );
        status.processVirtualBytes = static_cast<u64>( processMemory.PrivateUsage );
        status.hasProcessMemory = CY_TRUE;
    }
#elif CYPHER_PLATFORM_LINUX
    FILE *pStatm = std::fopen( "/proc/self/statm", "r" );
    if ( pStatm != nullptr ) {
        unsigned long long virtualPages = 0ull;
        unsigned long long residentPages = 0ull;
        if ( std::fscanf( pStatm, "%llu %llu", &virtualPages, &residentPages ) == 2 ) {
            const u64 pageSize = static_cast<u64>( platformMemory.nPageSize );
            status.processVirtualBytes =
                SystemInfo_SaturatingMultiply( static_cast<u64>( virtualPages ), pageSize );
            status.processResidentBytes =
                SystemInfo_SaturatingMultiply( static_cast<u64>( residentPages ), pageSize );
            status.hasProcessMemory = CY_TRUE;
        }
        std::fclose( pStatm );
    }
#elif CYPHER_PLATFORM_MACOS
    mach_task_basic_info_data_t taskInfo = {};
    mach_msg_type_number_t taskInfoCount = MACH_TASK_BASIC_INFO_COUNT;
    if ( ::task_info( ::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>( &taskInfo ), &taskInfoCount ) == KERN_SUCCESS ) {
        status.processResidentBytes = static_cast<u64>( taskInfo.resident_size );
        status.processVirtualBytes = static_cast<u64>( taskInfo.virtual_size );
        status.hasProcessMemory = CY_TRUE;
    }
#endif

    if ( status.availablePhysicalBytes > status.totalPhysicalBytes ) {
        status.availablePhysicalBytes = status.totalPhysicalBytes;
    }

    status.pressure = SystemInfo_CalculateMemoryPressure( status.totalPhysicalBytes, status.availablePhysicalBytes );
    return status;
}

cy_system_power_state_t Cy_SystemInfoQueryPowerState() noexcept
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

cy_system_disk_status_t Cy_SystemInfoQueryDiskStatus( const char *pszPath ) noexcept
{
    cy_system_disk_status_t status = {};
    const char *pszQueryPath = SystemInfo_IsStringEmpty( pszPath ) ? "." : pszPath;

#if CYPHER_PLATFORM_WINDOWS
    wchar_t wszQueryPath[CY_SYSTEMINFO_PATH_MAX] = {};
    if ( !SystemInfo_Utf8ToWide(
             pszQueryPath,
             wszQueryPath,
             CYPHER_ARRAY_COUNT( wszQueryPath ) ) ) {
        return status;
    }

    ULARGE_INTEGER availableBytes = {};
    ULARGE_INTEGER totalBytes = {};
    ULARGE_INTEGER freeBytes = {};
    if ( ::GetDiskFreeSpaceExW(
             wszQueryPath,
             &availableBytes,
             &totalBytes,
             &freeBytes ) == FALSE ) {
        return status;
    }

    status.totalBytes = static_cast<u64>( totalBytes.QuadPart );
    status.freeBytes = static_cast<u64>( freeBytes.QuadPart );
    status.availableBytes = static_cast<u64>( availableBytes.QuadPart );
    status.isValid = CY_TRUE;
#elif CYPHER_PLATFORM_LINUX
    struct statvfs info = {};
    if ( ::statvfs( pszQueryPath, &info ) != 0 ) {
        return status;
    }

    const u64 nFragmentSize = static_cast<u64>( info.f_frsize );
    status.totalBytes =
        SystemInfo_SaturatingMultiply( static_cast<u64>( info.f_blocks ), nFragmentSize );
    status.freeBytes =
        SystemInfo_SaturatingMultiply( static_cast<u64>( info.f_bfree ), nFragmentSize );
    status.availableBytes =
        SystemInfo_SaturatingMultiply( static_cast<u64>( info.f_bavail ), nFragmentSize );
    status.isValid = CY_TRUE;
#elif CYPHER_PLATFORM_MACOS
    struct statfs info = {};
    if ( ::statfs( pszQueryPath, &info ) != 0 ) {
        return status;
    }

    const u64 nBlockSize = static_cast<u64>( info.f_bsize );
    status.totalBytes =
        SystemInfo_SaturatingMultiply( static_cast<u64>( info.f_blocks ), nBlockSize );
    status.freeBytes =
        SystemInfo_SaturatingMultiply( static_cast<u64>( info.f_bfree ), nBlockSize );
    status.availableBytes =
        SystemInfo_SaturatingMultiply( static_cast<u64>( info.f_bavail ), nBlockSize );
    status.isValid = CY_TRUE;
#endif

    return status;
}

bool_t Cy_SystemInfoHasCpuFeature(
    flags64_t features,
    cy_system_cpu_feature_flags_t feature ) noexcept
{
    return Cy_CPUDetectHasFeature( features, feature );
}

const char *Cy_SystemInfoCpuFeatureName(
    cy_system_cpu_feature_flags_t feature ) noexcept
{
    return Cy_CPUDetectFeatureName( feature );
}

usize Cy_SystemInfoFormatReport( char *pszDst, usize cchDst ) noexcept
{
    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    const cy_system_memory_status_t memory = Cy_SystemInfoQueryMemoryStatus();
    system_info_report_writer_t writer = { pszDst, cchDst, 0u };

    SystemInfo_ReportAppendString( writer, "Cypher System Info\n" );
    SystemInfo_ReportAppendStringLine( writer, "engine", pInfo->build.pszEngineName );
    SystemInfo_ReportAppendStringLine( writer, "engine_version", pInfo->build.pszEngineVersion );
    SystemInfo_ReportAppendStringLine( writer, "build_config", pInfo->build.pszBuildConfig );
    SystemInfo_ReportAppendStringLine( writer, "build_date", pInfo->build.pszBuildDate );
    SystemInfo_ReportAppendStringLine( writer, "build_time", pInfo->build.pszBuildTime );
    SystemInfo_ReportAppendStringLine( writer, "compiler", pInfo->build.pszCompilerName );
    SystemInfo_ReportAppendU64Line( writer, "compiler_version", pInfo->build.compilerVersion );
    SystemInfo_ReportAppendStringLine( writer, "platform", pInfo->platform.pszPlatformName );
    SystemInfo_ReportAppendStringLine( writer, "architecture", pInfo->platform.pszArchName );
    SystemInfo_ReportAppendStringLine( writer, "os", pInfo->os.szName );
    SystemInfo_ReportAppendStringLine( writer, "os_version", pInfo->os.szVersion );
    SystemInfo_ReportAppendStringLine( writer, "cpu", pInfo->cpu.szBrand );
    SystemInfo_ReportAppendU64Line( writer, "logical_threads", pInfo->cpu.logicalThreadCount );
    SystemInfo_ReportAppendU64Line( writer, "physical_cores", pInfo->cpu.physicalCoreCount );
    SystemInfo_ReportAppendU64Line( writer, "cache_line_size", pInfo->cpu.cacheLineSize );
    SystemInfo_ReportAppendU64Line( writer, "page_size", pInfo->memory.pageSize );
    SystemInfo_ReportAppendU64Line(
        writer,
        "allocation_granularity",
        pInfo->memory.allocationGranularity );
    SystemInfo_ReportAppendU64Line(
        writer,
        "total_physical_bytes",
        pInfo->memory.totalPhysicalBytes );
    SystemInfo_ReportAppendU64Line(
        writer,
        "available_physical_bytes",
        memory.availablePhysicalBytes );
    SystemInfo_ReportAppendU64Line(
        writer,
        "process_resident_bytes",
        memory.processResidentBytes );
    SystemInfo_ReportAppendU64Line(
        writer,
        "process_virtual_bytes",
        memory.processVirtualBytes );
    SystemInfo_ReportAppendU64Line( writer, "process_id", pInfo->process.processId );
    SystemInfo_ReportAppendStringLine(
        writer,
        "executable_path",
        pInfo->process.szExecutablePath );
    SystemInfo_ReportAppendStringLine(
        writer,
        "working_directory",
        pInfo->process.szWorkingDirectory );
    SystemInfo_ReportAppendStringLine(
        writer,
        "little_endian",
        pInfo->platform.isLittleEndian ? "yes" : "no" );
    SystemInfo_ReportAppendStringLine(
        writer,
        "is_64_bit",
        pInfo->platform.is64Bit ? "yes" : "no" );
    SystemInfo_ReportAppendString( writer, "cpu_features: " );
    SystemInfo_ReportAppendHexU64( writer, pInfo->cpu.featureFlags );
    SystemInfo_ReportAppendChar( writer, '\n' );

    SystemInfo_ReportTerminate( writer );
    return writer.cchRequired;
}

} // namespace cypher::common
