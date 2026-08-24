//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_SystemInfo.h
//  Purpose: Declares cached host identity and live host-resource queries.
//  Details: Immutable build/platform information is captured once. Memory, disk,
//           and power information is queried live because it can change while the
//           engine or tools are running.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_SYSTEMINFO_H
#define CYPHER_COMMON_TIER0_SYSTEMINFO_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_CPUDetect.h"
#include "CypherCommon_Defines.h"
#include "CypherCommon_Platform.h"
#include "CypherCommon_Process.h"

namespace cypher::common
{

constexpr usize CY_SYSTEMINFO_OS_NAME_MAX = 64u;       // OS family string capacity.
constexpr usize CY_SYSTEMINFO_OS_VERSION_MAX = 128u;  // OS release string capacity.
constexpr usize CY_SYSTEMINFO_CPU_BRAND_MAX = 128u;   // CPU brand string capacity.
constexpr usize CY_SYSTEMINFO_PATH_MAX = CY_PROCESS_PATH_MAX; // Native path capacity.
constexpr usize CY_SYSTEMINFO_REPORT_MAX = 4096u;     // Recommended text report buffer.

// Coarse memory pressure classification derived from available physical memory.
enum cy_system_memory_pressure_t : u32 {
    CY_SYSTEM_MEMORY_PRESSURE_UNKNOWN = 0u, // Total memory was unavailable.
    CY_SYSTEM_MEMORY_PRESSURE_LOW,          // More than half remains available.
    CY_SYSTEM_MEMORY_PRESSURE_NORMAL,       // 15-50 percent remains available.
    CY_SYSTEM_MEMORY_PRESSURE_HIGH,         // 5-15 percent remains available.
    CY_SYSTEM_MEMORY_PRESSURE_CRITICAL      // At most 5 percent remains available.
};

// Coarse power source state for diagnostics and future laptop/mobile policy.
enum cy_system_power_state_t : u32 {
    CY_SYSTEM_POWER_UNKNOWN = 0u, // Host query failed or is unsupported.
    CY_SYSTEM_POWER_AC,           // External power is connected.
    CY_SYSTEM_POWER_BATTERY,      // Running from battery power.
    CY_SYSTEM_POWER_NO_BATTERY    // Desktop or battery not installed.
};

// SystemInfo uses CPUDetect's canonical feature type rather than duplicating bits.
using cy_system_cpu_feature_flags_t = cy_cpu_feature_flags_t;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_NONE = CY_CPU_FEATURE_NONE;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_SSE2 = CY_CPU_FEATURE_SSE2;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_SSE3 = CY_CPU_FEATURE_SSE3;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_SSSE3 = CY_CPU_FEATURE_SSSE3;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_SSE41 = CY_CPU_FEATURE_SSE41;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_SSE42 = CY_CPU_FEATURE_SSE42;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_AVX = CY_CPU_FEATURE_AVX;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_AVX2 = CY_CPU_FEATURE_AVX2;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_NEON = CY_CPU_FEATURE_NEON;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_AES = CY_CPU_FEATURE_AES;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_FMA = CY_CPU_FEATURE_FMA;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_BMI1 = CY_CPU_FEATURE_BMI1;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_BMI2 = CY_CPU_FEATURE_BMI2;
constexpr cy_system_cpu_feature_flags_t CY_SYSTEM_CPU_FEATURE_POPCNT = CY_CPU_FEATURE_POPCNT;

// Immutable build metadata captured into the SystemInfo snapshot.
struct cy_system_build_info_t {
    const char *pszEngineName;    // Static storage owned by BuildId.
    const char *pszEngineVersion; // Static storage owned by BuildId.
    const char *pszBuildConfig;   // debug, development, profile, or shipping.
    const char *pszBuildDate;     // Translation-unit build date.
    const char *pszBuildTime;     // Translation-unit build time.
    const char *pszCompilerName;  // Stable compiler family name.
    u32 compilerVersion;          // Packed compiler version for comparisons.
};

// Compile target and platform properties known at process startup.
struct cy_system_platform_info_t {
    const char *pszPlatformName; // Static normalized operating-system name.
    const char *pszArchName;     // Static normalized target architecture.

    usize pointerSize;           // Native pointer width in bytes.

    bool_t is64Bit;              // Build targets a 64-bit address space.
    bool_t isLittleEndian;       // Native byte order is little endian.
    bool_t hasExceptions;        // C++ exception support is enabled.
    bool_t hasRtti;              // C++ RTTI support is enabled.
};

// Operating system name/version strings used by diagnostics and reports.
struct cy_system_os_info_t {
    char szName[CY_SYSTEMINFO_OS_NAME_MAX];       // Host OS family.
    char szVersion[CY_SYSTEMINFO_OS_VERSION_MAX]; // Host OS release.
};

// CPU topology, cache line size, brand string, and detected feature flags.
struct cy_system_cpu_info_t {
    char szBrand[CY_SYSTEMINFO_CPU_BRAND_MAX]; // Human-readable processor model.

    u32 logicalThreadCount;                   // Scheduler-visible hardware threads.
    u32 physicalCoreCount;                    // Best available physical-core count.

    usize cacheLineSize;                      // L1 data cache line size in bytes.

    flags64_t featureFlags;                   // Features safe for this process to use.
};

// Page, allocation, and physical memory constants cached at startup.
struct cy_system_memory_info_t {
    usize pageSize;              // Commit/protection granularity in bytes.
    usize allocationGranularity; // Virtual reservation granularity in bytes.

    u64 totalPhysicalBytes;      // Installed physical memory in bytes.
};

// Process identity and basic process paths used by logs and crash reports.
struct cy_system_process_info_t {
    process_id_t processId; // Current native process identifier.

    char szExecutablePath[CY_SYSTEMINFO_PATH_MAX];  // Absolute executable path.
    char szWorkingDirectory[CY_SYSTEMINFO_PATH_MAX]; // Current process directory.
};

// Cached immutable SystemInfo snapshot. Use live query functions for changing state.
struct cy_system_info_t {
    cy_system_build_info_t build;       // Build identity fixed at compile time.
    cy_system_platform_info_t platform; // Compile-target capabilities.
    cy_system_os_info_t os;             // Host OS identity.
    cy_system_cpu_info_t cpu;           // Host processor identity/topology.
    cy_system_memory_info_t memory;      // Stable page and installed-memory facts.
    cy_system_process_info_t process;   // Process identity and startup paths.
};

// Live memory status query result. Values may change every call.
struct cy_system_memory_status_t {
    u64 totalPhysicalBytes;     // Physical memory visible to the host.
    u64 availablePhysicalBytes; // Memory currently available without reclaim.

    u64 processResidentBytes;   // Current process working/resident set.
    u64 processVirtualBytes;    // Current process private/virtual footprint.

    cy_system_memory_pressure_t pressure; // Coarse available-memory category.
    bool_t hasPhysicalMemory;              // Physical counters are valid.
    bool_t hasProcessMemory;               // Process counters are valid.
};

// Live disk space query result for a path.
struct cy_system_disk_status_t {
    u64 totalBytes;     // Total filesystem capacity.
    u64 freeBytes;      // All free blocks, including privileged reservations.
    u64 availableBytes; // Free blocks available to this process/user.
    bool_t isValid;     // Host query completed successfully.
};

// Initializes the immutable process-lifetime SystemInfo snapshot.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_SystemInfoInit() noexcept;

// Returns the cached SystemInfo snapshot, initializing it on first use.
CYPHER_NODISCARD CYPHER_COMMON_API const cy_system_info_t *Cy_SystemInfoGet() noexcept;

// Queries current physical/process memory state.
CYPHER_NODISCARD CYPHER_COMMON_API cy_system_memory_status_t
Cy_SystemInfoQueryMemoryStatus() noexcept;

// Queries current OS power source state when supported.
CYPHER_NODISCARD CYPHER_COMMON_API cy_system_power_state_t
Cy_SystemInfoQueryPowerState() noexcept;

// Queries disk space for pszPath, or the working directory when pszPath is empty.
CYPHER_NODISCARD CYPHER_COMMON_API cy_system_disk_status_t
Cy_SystemInfoQueryDiskStatus( const char *pszPath ) noexcept;

// Returns true when feature is present in a CPU feature bitfield.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_SystemInfoHasCpuFeature(
    flags64_t features,
    cy_system_cpu_feature_flags_t feature ) noexcept;

// Returns a stable diagnostic name for a CPU feature flag.
CYPHER_NODISCARD CYPHER_COMMON_API const char *Cy_SystemInfoCpuFeatureName(
    cy_system_cpu_feature_flags_t feature ) noexcept;

// Formats a human-readable report. Returns required character count excluding null.
CYPHER_NODISCARD CYPHER_COMMON_API usize Cy_SystemInfoFormatReport(
    char *pszDst,
    usize cchDst ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_SYSTEMINFO_H
