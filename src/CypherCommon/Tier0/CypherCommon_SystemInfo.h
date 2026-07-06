//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_SystemInfo.h
//  Purpose: Declares CypherCommon Tier0 SystemInfo support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_SYSTEMINFO_H
#define CYPHER_COMMON_TIER0_SYSTEMINFO_H
#pragma once

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

constexpr usize CY_SYSTEMINFO_OS_NAME_MAX = 64u;
constexpr usize CY_SYSTEMINFO_OS_VERSION_MAX = 128u;
constexpr usize CY_SYSTEMINFO_CPU_BRAND_MAX = 128u;
constexpr usize CY_SYSTEMINFO_PATH_MAX = 512u;
constexpr usize CY_SYSTEMINFO_REPORT_MAX = 4096u;

// Coarse memory pressure classification derived from available physical memory.
enum cy_system_memory_pressure_t : u32 {
    CY_SYSTEM_MEMORY_PRESSURE_UNKNOWN = 0u,
    CY_SYSTEM_MEMORY_PRESSURE_LOW,
    CY_SYSTEM_MEMORY_PRESSURE_NORMAL,
    CY_SYSTEM_MEMORY_PRESSURE_HIGH,
    CY_SYSTEM_MEMORY_PRESSURE_CRITICAL
};

// Coarse power source state for diagnostics and future laptop/mobile policy.
enum cy_system_power_state_t : u32 {
    CY_SYSTEM_POWER_UNKNOWN = 0u,
    CY_SYSTEM_POWER_AC,
    CY_SYSTEM_POWER_BATTERY,
    CY_SYSTEM_POWER_NO_BATTERY
};

// Runtime CPU feature bits used to choose optimized code paths safely.
enum cy_system_cpu_feature_flags_t : flags64_t {
    CY_SYSTEM_CPU_FEATURE_NONE  = 0ull,
    CY_SYSTEM_CPU_FEATURE_SSE2  = CYPHER_BIT64( 0 ),
    CY_SYSTEM_CPU_FEATURE_SSE3  = CYPHER_BIT64( 1 ),
    CY_SYSTEM_CPU_FEATURE_SSSE3 = CYPHER_BIT64( 2 ),
    CY_SYSTEM_CPU_FEATURE_SSE41 = CYPHER_BIT64( 3 ),
    CY_SYSTEM_CPU_FEATURE_SSE42 = CYPHER_BIT64( 4 ),
    CY_SYSTEM_CPU_FEATURE_AVX   = CYPHER_BIT64( 5 ),
    CY_SYSTEM_CPU_FEATURE_AVX2  = CYPHER_BIT64( 6 ),
    CY_SYSTEM_CPU_FEATURE_NEON  = CYPHER_BIT64( 7 )
};

// Immutable build metadata captured into the SystemInfo snapshot.
struct cy_system_build_info_t {
    const char *pszEngineName;
    const char *pszEngineVersion;
    const char *pszBuildConfig;
    const char *pszBuildDate;
    const char *pszBuildTime;
    const char *pszCompilerName;
    u32 compilerVersion;
};

// Compile target and platform properties known at process startup.
struct cy_system_platform_info_t {
    const char *pszPlatformName;
    const char *pszArchName;

    usize pointerSize;

    bool_t is64Bit;
    bool_t isLittleEndian;
    bool_t hasExceptions;
    bool_t hasRtti;
};

// Operating system name/version strings used by diagnostics and reports.
struct cy_system_os_info_t {
    char szName[CY_SYSTEMINFO_OS_NAME_MAX];
    char szVersion[CY_SYSTEMINFO_OS_VERSION_MAX];
};

// CPU topology, cache line size, brand string, and detected feature flags.
struct cy_system_cpu_info_t {
    char szBrand[CY_SYSTEMINFO_CPU_BRAND_MAX];

    u32 logicalThreadCount;
    u32 physicalCoreCount;

    usize cacheLineSize;

    flags64_t featureFlags;
};

// Page, allocation, and physical memory constants cached at startup.
struct cy_system_memory_info_t {
    usize pageSize;
    usize allocationGranularity;

    u64 totalPhysicalBytes;
};

// Process identity and basic process paths used by logs and crash reports.
struct cy_system_process_info_t {
    u32 processId;

    char szExecutablePath[CY_SYSTEMINFO_PATH_MAX];
    char szWorkingDirectory[CY_SYSTEMINFO_PATH_MAX];
};

// Cached immutable SystemInfo snapshot. Use live query functions for changing state.
struct cy_system_info_t {
    cy_system_build_info_t build;
    cy_system_platform_info_t platform;
    cy_system_os_info_t os;
    cy_system_cpu_info_t cpu;
    cy_system_memory_info_t memory;
    cy_system_process_info_t process;
};

// Live memory status query result. Values may change every call.
struct cy_system_memory_status_t {
    u64 totalPhysicalBytes;
    u64 availablePhysicalBytes;

    u64 processResidentBytes;
    u64 processVirtualBytes;

    cy_system_memory_pressure_t pressure;
};

// Live disk space query result for a path.
struct cy_system_disk_status_t {
    u64 totalBytes;
    u64 freeBytes;
    u64 availableBytes;
};

// Initializes the cached SystemInfo snapshot. Safe to call more than once.
bool_t Cy_SystemInfoInit();

// Clears the cached SystemInfo snapshot. Call during controlled shutdown only.
void Cy_SystemInfoShutdown();

// Returns the cached SystemInfo snapshot, initializing it on first use.
const cy_system_info_t *Cy_SystemInfoGet();

// Queries current physical/process memory state.
cy_system_memory_status_t Cy_SystemInfoQueryMemoryStatus();

// Queries current OS power source state when supported.
cy_system_power_state_t Cy_SystemInfoQueryPowerState();

// Queries disk space for pszPath, or the working directory when pszPath is empty.
cy_system_disk_status_t Cy_SystemInfoQueryDiskStatus( const char *pszPath );

// Returns true when feature is present in a CPU feature bitfield.
bool_t Cy_SystemInfoHasCpuFeature( flags64_t features, cy_system_cpu_feature_flags_t feature );

// Returns a stable diagnostic name for a CPU feature flag.
const char *Cy_SystemInfoCpuFeatureName( cy_system_cpu_feature_flags_t feature );

// Formats a human-readable report. Returns required character count excluding null.
usize Cy_SystemInfoFormatReport( char *pszDst, usize cchDst );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SYSTEMINFO_H
