//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherHost/CypherHost.cpp
//  Purpose: Implements the CypherHost Host module.
//  Details: This file participates in engine host startup, frame flow, and shutdown
//           ordering. Keep it thin enough that subsystem initialization remains
//           visible and debuggable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommand.h"
#include "CypherCVar.h"
#include "Engine/CypherCommon_Print.h"
#include "CypherHost_Local.h"
#include "CypherConfig.h"
#include "CypherFileSystem.h"
#include "CypherLog.h"
#include "CypherMemory.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherSystem_Public.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_BuildConfig.h"
#include "CypherCommon_BuildId.h"
#include "CypherCommon_Compiler.h"
#include "CypherCommon_CPUDetect.h"
#include "CypherCommon_Platform.h"

#include <chrono>      // Monotonic startup timing.
#include <cstdio>      // Bounded diagnostic formatting.
#include <cstring>     // strncpy for log path cvars.

namespace rc = cypher::engine::common;
namespace mem = cypher::engine::memory;

namespace {

constexpr int HOST_LOG_PATH_LIMIT = 640;
constexpr rc::usize HOST_DIAGNOSTIC_TEXT_CAPACITY = 256u;
constexpr rc::usize HOST_REPORT_RULE_WIDTH = 80u;
constexpr rc::usize HOST_REPORT_VALUE_WIDTH = 84u;

#define HOST_LOG_FIELD( CHANNEL, LABEL, FORMAT, ... )                                            \
    LOG_INFO( ( CHANNEL ), "  %-24s : " FORMAT, ( LABEL ) __VA_OPT__( , ) __VA_ARGS__ )

#define HOST_LOG_DETAIL( CHANNEL, LABEL, FORMAT, ... )                                           \
    LOG_INFO( ( CHANNEL ), "    %-22s : " FORMAT, ( LABEL ) __VA_OPT__( , ) __VA_ARGS__ )

struct host_startup_timings_t {
    double coreServicesMilliseconds{ 0.0 };
    double mountMilliseconds{ 0.0 };
    double cvarRegistrationMilliseconds{ 0.0 };
    double commandRegistrationMilliseconds{ 0.0 };
    double configLoadMilliseconds{ 0.0 };
    double logApplyMilliseconds{ 0.0 };
    double runtimePolicyMilliseconds{ 0.0 };
    double windowMilliseconds{ 0.0 };
    double finishMilliseconds{ 0.0 };
};

double Host_ElapsedMilliseconds( const std::chrono::steady_clock::time_point begin )
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin ).count();
}

void Host_LogRule( const cypher::engine::log::channel_t channel, const char fill )
{
    char rule[HOST_REPORT_RULE_WIDTH + 1u]{};
    std::memset( rule, fill, HOST_REPORT_RULE_WIDTH );
    rule[HOST_REPORT_RULE_WIDTH] = '\0';
    LOG_INFO_MESSAGE( channel, rule );
}

void Host_LogSection(
    const cypher::engine::log::channel_t channel,
    const char *title )
{
    char heading[HOST_REPORT_RULE_WIDTH + 1u]{};
    const int written = std::snprintf(
        heading,
        sizeof( heading ),
        "----- %s ",
        title != nullptr && title[0] != '\0' ? title : "STARTUP" );

    if ( written < 0 ) {
        Host_LogRule( channel, '-' );
        return;
    }

    const rc::usize used = static_cast<rc::usize>( written );
    if ( used < HOST_REPORT_RULE_WIDTH ) {
        std::memset( heading + used, '-', HOST_REPORT_RULE_WIDTH - used );
        heading[HOST_REPORT_RULE_WIDTH] = '\0';
    }

    LOG_INFO_MESSAGE( channel, heading );
}

void Host_LogStartupBanner()
{
    using namespace cypher::engine;

    const sys::system_info_t *systemInfo = sys::Sys_GetSystemInfo();
    const char *platformName = systemInfo != nullptr
        ? systemInfo->platform.pszPlatformName
        : "unknown-platform";
    const char *architectureName = systemInfo != nullptr
        ? systemInfo->platform.pszArchName
        : "unknown-architecture";

    Host_LogRule( log::channel_t::HOST, '=' );
    LOG_INFO(
        log::channel_t::HOST,
        "  %s %u.%u.%u.%u  |  %s %u.%u.%u.%u",
        common::COM_ENGINE_INFO.name,
        common::COM_ENGINE_INFO.version.major,
        common::COM_ENGINE_INFO.version.minor,
        common::COM_ENGINE_INFO.version.patch,
        common::COM_ENGINE_INFO.version.build,
        common::COM_GAME_INFO.name,
        common::COM_GAME_INFO.version.major,
        common::COM_GAME_INFO.version.minor,
        common::COM_GAME_INFO.version.patch,
        common::COM_GAME_INFO.version.build );
    LOG_INFO(
        log::channel_t::HOST,
        "  %s build  |  %s %s  |  starting",
        ::cypher::common::Cy_BuildConfigGetName( ::cypher::common::Cy_BuildConfigGetCurrent() ),
        platformName,
        architectureName );
    Host_LogRule( log::channel_t::HOST, '=' );
}

void Host_LogWrappedDetail(
    const cypher::engine::log::channel_t channel,
    const char *label,
    const char *value )
{
    const char *cursor = value != nullptr ? value : "";
    const char *safeLabel = label != nullptr ? label : "";
    bool firstLine = true;

    do {
        while ( *cursor == ' ' ) {
            ++cursor;
        }

        const rc::usize remaining = std::strlen( cursor );
        rc::usize length = remaining < HOST_REPORT_VALUE_WIDTH
            ? remaining
            : HOST_REPORT_VALUE_WIDTH;

        if ( length < remaining ) {
            rc::usize wordBoundary = length;
            while ( wordBoundary > 0u && cursor[wordBoundary] != ' ' ) {
                --wordBoundary;
            }
            if ( wordBoundary > 0u ) {
                length = wordBoundary;
            }
        }

        LOG_INFO(
            channel,
            firstLine ? "    %-22s : %.*s" : "    %-22s   %.*s",
            firstLine ? safeLabel : "",
            static_cast<int>( length ),
            cursor );

        cursor += length;
        firstLine = false;
    } while ( *cursor != '\0' );
}

const char *Host_TextOrUnavailable( const char *value )
{
    return value != nullptr && value[0] != '\0' ? value : "<not_embedded>";
}

template <rc::usize Capacity>
void Host_FormatByteCount( const rc::u64 bytes, char ( &textOut )[Capacity] )
{
    static_assert( Capacity > 0u );

    constexpr const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    constexpr rc::usize unitCount = sizeof( units ) / sizeof( units[0] );

    rc::usize unitIndex = 0u;
    double value = static_cast<double>( bytes );
    while ( value >= 1024.0 && unitIndex + 1u < unitCount ) {
        value /= 1024.0;
        ++unitIndex;
    }

    const int written = unitIndex == 0u
        ? std::snprintf(
            textOut,
            Capacity,
            "%llu B",
            static_cast<unsigned long long>( bytes ) )
        : std::snprintf( textOut, Capacity, "%.2f %s", value, units[unitIndex] );

    if ( written < 0 || static_cast<rc::usize>( written ) >= Capacity ) {
        textOut[0] = '\0';
    }
}

template <rc::usize Capacity>
void Host_AppendListItem( char ( &text )[Capacity], const char *item )
{
    if ( item == nullptr || item[0] == '\0' ) {
        return;
    }

    const rc::usize length = std::strlen( text );
    if ( length >= Capacity - 1u ) {
        return;
    }

    const int written = std::snprintf(
        text + length,
        Capacity - length,
        "%s%s",
        length == 0u ? "" : " | ",
        item );
    if ( written < 0 ) {
        text[length] = '\0';
    }
}

const char *Host_BoolText( const bool value )
{
    return value ? "true" : "false";
}

const char *Host_PresentModeName( const cypher::engine::render::render_present_mode_t mode )
{
    using cypher::engine::render::render_present_mode_t;

    switch ( mode ) {
    case render_present_mode_t::IMMEDIATE:
        return "immediate";
    case render_present_mode_t::FIFO:
        return "fifo";
    case render_present_mode_t::FIFO_RELAXED:
        return "fifo relaxed";
    case render_present_mode_t::MAILBOX:
        return "mailbox";
    case render_present_mode_t::COUNT:
    default:
        return "unknown";
    }
}

const char *Host_WindowModeName( const cypher::engine::sys::window_mode_t mode )
{
    using cypher::engine::sys::window_mode_t;

    switch ( mode ) {
    case window_mode_t::WINDOWED:
        return "windowed";
    case window_mode_t::BORDERLESS_FULLSCREEN:
        return "borderless fullscreen";
    case window_mode_t::EXCLUSIVE_FULLSCREEN:
        return "exclusive fullscreen";
    case window_mode_t::COUNT:
    default:
        return "unknown";
    }
}

const char *Host_WindowGraphicsApiName( const cypher::engine::sys::window_graphics_api_t graphicsApi )
{
    using cypher::engine::sys::window_graphics_api_t;

    switch ( graphicsApi ) {
    case window_graphics_api_t::NONE:
        return "none";
    case window_graphics_api_t::OPENGL:
        return "OpenGL";
    case window_graphics_api_t::VULKAN:
        return "Vulkan";
    case window_graphics_api_t::COUNT:
    default:
        return "unknown";
    }
}

const char *Host_MemoryPressureName( const ::cypher::common::cy_system_memory_pressure_t pressure )
{
    using namespace ::cypher::common;

    switch ( pressure ) {
    case CY_SYSTEM_MEMORY_PRESSURE_LOW:
        return "low";
    case CY_SYSTEM_MEMORY_PRESSURE_NORMAL:
        return "normal";
    case CY_SYSTEM_MEMORY_PRESSURE_HIGH:
        return "high";
    case CY_SYSTEM_MEMORY_PRESSURE_CRITICAL:
        return "critical";
    case CY_SYSTEM_MEMORY_PRESSURE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *Host_PowerStateName( const cypher::engine::sys::system_power_state_t powerState )
{
    using namespace ::cypher::common;

    switch ( powerState ) {
    case CY_SYSTEM_POWER_AC:
        return "ac";
    case CY_SYSTEM_POWER_BATTERY:
        return "battery";
    case CY_SYSTEM_POWER_NO_BATTERY:
        return "no battery";
    case CY_SYSTEM_POWER_UNKNOWN:
    default:
        return "unavailable";
    }
}

const char *Host_DisplayOrientationName( const cypher::engine::sys::display_orientation_t orientation )
{
    using cypher::engine::sys::display_orientation_t;

    switch ( orientation ) {
    case display_orientation_t::LANDSCAPE:
        return "landscape";
    case display_orientation_t::LANDSCAPE_FLIPPED:
        return "landscape flipped";
    case display_orientation_t::PORTRAIT:
        return "portrait";
    case display_orientation_t::PORTRAIT_FLIPPED:
        return "portrait flipped";
    case display_orientation_t::UNKNOWN:
    case display_orientation_t::COUNT:
    default:
        return "unknown";
    }
}

const char *Host_LogFormatName( const cypher::engine::log::format_mode_t format )
{
    using cypher::engine::log::format_mode_t;
    switch ( format ) {
    case format_mode_t::COMPACT:
        return "compact";
    case format_mode_t::DETAILED:
        return "detailed";
    case format_mode_t::CONSOLE:
        return "console";
    default:
        return "unknown";
    }
}

const char *Host_LogFlushName( const cypher::engine::log::flush_policy_t flush )
{
    using cypher::engine::log::flush_policy_t;

    switch ( flush ) {
    case flush_policy_t::NEVER:
        return "never";
    case flush_policy_t::ERRORS_AND_ABOVE:
        return "errors and above";
    case flush_policy_t::EVERY_MESSAGE:
        return "every message";
    default:
        return "unknown";
    }
}

const char *Host_LogFileModeName( const cypher::engine::log::file_mode_t fileMode )
{
    using cypher::engine::log::file_mode_t;
    return fileMode == file_mode_t::APPEND ? "append" : "truncate";
}

const char *Host_LogSourcePathName( const cypher::engine::log::source_path_mode_t sourcePath )
{
    using cypher::engine::log::source_path_mode_t;
    return sourcePath == source_path_mode_t::FULL_PATH ? "full path" : "basename";
}

const char *Host_FileSystemMountTypeName( const cypher::engine::fs::mount_type_t type )
{
    using cypher::engine::fs::mount_type_t;
    return type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE ? "package" : "directory";
}

void Host_FormatCpuFeatures(
    const ::cypher::common::flags64_t features,
    char ( &featuresOut )[HOST_DIAGNOSTIC_TEXT_CAPACITY] )
{
    using namespace ::cypher::common;

    constexpr cy_cpu_feature_flags_t knownFeatures[] = {
        CY_CPU_FEATURE_SSE2,
        CY_CPU_FEATURE_SSE3,
        CY_CPU_FEATURE_SSSE3,
        CY_CPU_FEATURE_SSE41,
        CY_CPU_FEATURE_SSE42,
        CY_CPU_FEATURE_AVX,
        CY_CPU_FEATURE_AVX2,
        CY_CPU_FEATURE_NEON,
        CY_CPU_FEATURE_AES,
        CY_CPU_FEATURE_FMA,
        CY_CPU_FEATURE_BMI1,
        CY_CPU_FEATURE_BMI2,
        CY_CPU_FEATURE_POPCNT
    };

    featuresOut[0] = '\0';
    for ( const cy_cpu_feature_flags_t feature : knownFeatures ) {
        if ( Cy_CPUDetectHasFeature( features, feature ) ) {
            Host_AppendListItem( featuresOut, Cy_CPUDetectFeatureName( feature ) );
        }
    }

    if ( featuresOut[0] == '\0' ) {
        std::snprintf( featuresOut, sizeof( featuresOut ), "none" );
    }
}

void Host_FormatSanitizers( char ( &sanitizersOut )[HOST_DIAGNOSTIC_TEXT_CAPACITY] )
{
    sanitizersOut[0] = '\0';
#if CYPHER_SANITIZER_ADDRESS
    Host_AppendListItem( sanitizersOut, "address" );
#endif
#if CYPHER_SANITIZER_THREAD
    Host_AppendListItem( sanitizersOut, "thread" );
#endif
#if CYPHER_SANITIZER_MEMORY
    Host_AppendListItem( sanitizersOut, "memory" );
#endif
#if CYPHER_SANITIZER_UNDEFINED
    Host_AppendListItem( sanitizersOut, "undefined" );
#endif
    if ( sanitizersOut[0] == '\0' ) {
        std::snprintf( sanitizersOut, sizeof( sanitizersOut ), "none" );
    }
}

void Host_FormatCvarFlags(
    const cypher::engine::cvar::flags_t flags,
    char ( &flagsOut )[HOST_DIAGNOSTIC_TEXT_CAPACITY] )
{
    using namespace cypher::engine::cvar;

    const rc::u32 bits = static_cast<rc::u32>( flags );
    flagsOut[0] = '\0';
    if ( ( bits & CYPHER_CVAR_ARCHIVE ) != 0u ) {
        Host_AppendListItem( flagsOut, "archive" );
    }
    if ( ( bits & CYPHER_CVAR_READONLY ) != 0u ) {
        Host_AppendListItem( flagsOut, "read-only" );
    }
    if ( ( bits & CYPHER_CVAR_CHEAT ) != 0u ) {
        Host_AppendListItem( flagsOut, "cheat" );
    }
    if ( ( bits & CYPHER_CVAR_DEV ) != 0u ) {
        Host_AppendListItem( flagsOut, "developer-only" );
    }
    if ( ( bits & CYPHER_CVAR_MODIFIED ) != 0u ) {
        Host_AppendListItem( flagsOut, "modified" );
    }
    if ( flagsOut[0] == '\0' ) {
        std::snprintf( flagsOut, sizeof( flagsOut ), "none" );
    }
}

void Host_LogRuntimePath( const char *label, const char *value )
{
    const char *safeValue = value != nullptr ? value : "";
    LOG_INFO(
        cypher::engine::log::channel_t::SYSTEM,
        "    %-22s : %.*s%s",
        label ? label : "Unknown",
        HOST_LOG_PATH_LIMIT,
        safeValue,
        std::strlen( safeValue ) > static_cast<rc::usize>( HOST_LOG_PATH_LIMIT )
            ? "  [truncated]"
            : "" );
}

void Host_LogDiskStatus( const char *rootName, const char *path )
{
    const cypher::engine::sys::system_disk_status_t disk =
        cypher::engine::sys::Sys_QueryDiskStatus( path );
    if ( !disk.isValid ) {
        LOG_INFO(
            cypher::engine::log::channel_t::SYSTEM,
            "  Storage / %-14s : unavailable  (%.*s)",
            rootName ? rootName : "unknown",
            HOST_LOG_PATH_LIMIT,
            path ? path : "" );
        return;
    }

    char totalText[32]{};
    char freeText[32]{};
    char availableText[32]{};
    Host_FormatByteCount( disk.totalBytes, totalText );
    Host_FormatByteCount( disk.freeBytes, freeText );
    Host_FormatByteCount( disk.availableBytes, availableText );
    LOG_INFO(
        cypher::engine::log::channel_t::SYSTEM,
        "  Storage / %s",
        rootName ? rootName : "unknown" );
    HOST_LOG_DETAIL(
        cypher::engine::log::channel_t::SYSTEM,
        "Capacity",
        "%s  (%llu bytes)",
        totalText,
        static_cast<unsigned long long>( disk.totalBytes ) );
    HOST_LOG_DETAIL(
        cypher::engine::log::channel_t::SYSTEM,
        "Free",
        "%s  (%llu bytes)",
        freeText,
        static_cast<unsigned long long>( disk.freeBytes ) );
    HOST_LOG_DETAIL(
        cypher::engine::log::channel_t::SYSTEM,
        "Available",
        "%s  (%llu bytes)",
        availableText,
        static_cast<unsigned long long>( disk.availableBytes ) );
}

void Host_LogIdentityAndSystemReport( const int argumentCount )
{
    using namespace cypher::engine;

    const ::cypher::common::build_id_t *engineBuild = ::cypher::common::Cy_BuildIdGetEngine();
    const ::cypher::common::build_id_t *gameBuild = ::cypher::common::Cy_BuildIdGetGame();
    const ::cypher::common::compiler_info_t compiler = ::cypher::common::Cy_CompilerGetInfo();
    const sys::system_info_t *systemInfo = sys::Sys_GetSystemInfo();
    const ::cypher::common::cy_cpu_detect_info_t *cpuInfo = ::cypher::common::Cy_CPUDetectGetInfo();

    Host_LogSection( log::channel_t::HOST, "PRODUCT & BUILD" );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Engine",
        "%s %u.%u.%u.%u",
        common::COM_ENGINE_INFO.name,
        common::COM_ENGINE_INFO.version.major,
        common::COM_ENGINE_INFO.version.minor,
        common::COM_ENGINE_INFO.version.patch,
        common::COM_ENGINE_INFO.version.build );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "Internal name",
        "%s",
        common::COM_ENGINE_INFO.szInternalName );
    Host_LogWrappedDetail(
        log::channel_t::HOST,
        "Description",
        common::COM_ENGINE_INFO.description );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Game",
        "%s %u.%u.%u.%u",
        common::COM_GAME_INFO.name,
        common::COM_GAME_INFO.version.major,
        common::COM_GAME_INFO.version.minor,
        common::COM_GAME_INFO.version.patch,
        common::COM_GAME_INFO.version.build );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "Internal name",
        "%s",
        common::COM_GAME_INFO.szInternalName );
    Host_LogWrappedDetail(
        log::channel_t::HOST,
        "Description",
        common::COM_GAME_INFO.description );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Ownership",
        "%s  |  copyright %s",
        common::COM_ENGINE_INFO.szOrganizationName,
        common::COM_ENGINE_INFO.szCopyrightOwner );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "License",
        "%s  (%s)",
        common::COM_ENGINE_INFO.szLicenseName,
        common::COM_ENGINE_INFO.szLicenseSpdx );

    if ( engineBuild != nullptr && gameBuild != nullptr ) {
        HOST_LOG_FIELD(
            log::channel_t::HOST,
            "Binary versions",
            "engine %s  |  game %s",
            Host_TextOrUnavailable( engineBuild->pszVersion ),
            Host_TextOrUnavailable( gameBuild->pszVersion ) );
        HOST_LOG_FIELD(
            log::channel_t::HOST,
            "Source revision",
            "branch %s  |  commit %s  |  build %u",
            Host_TextOrUnavailable( engineBuild->pszBranchName ),
            Host_TextOrUnavailable( engineBuild->pszCommitHash ),
            engineBuild->version.nBuild );
        HOST_LOG_FIELD(
            log::channel_t::HOST,
            "Compiled",
            "%s %s",
            Host_TextOrUnavailable( engineBuild->pszBuildDate ),
            Host_TextOrUnavailable( engineBuild->pszBuildTime ) );
    }

    char sanitizerText[HOST_DIAGNOSTIC_TEXT_CAPACITY]{};
    Host_FormatSanitizers( sanitizerText );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Build policy",
        "%s  |  optimized %s",
        ::cypher::common::Cy_BuildConfigGetName( ::cypher::common::Cy_BuildConfigGetCurrent() ),
        Host_BoolText( ::cypher::common::Cy_BuildConfigIsOptimized() ) );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "Runtime checks",
        "assertions %s  |  exceptions %s  |  RTTI %s  |  sanitizers %s",
        Host_BoolText( CYPHER_ASSERTS_ENABLED != 0 ),
        Host_BoolText( compiler.hasExceptions ),
        Host_BoolText( compiler.hasRtti ),
        sanitizerText );

    Host_LogSection( log::channel_t::SYSTEM, "SYSTEM & PROCESS" );
    if ( systemInfo == nullptr || cpuInfo == nullptr ) {
        LOG_WARNING( log::channel_t::SYSTEM, "System report unavailable: immutable host snapshot is missing" );
        return;
    }

    HOST_LOG_FIELD(
        log::channel_t::SYSTEM,
        "Operating system",
        "%s %s",
        systemInfo->os.szName,
        systemInfo->os.szVersion );
    HOST_LOG_FIELD(
        log::channel_t::SYSTEM,
        "Target",
        "%s %s  |  %zu-bit pointers  |  %s address model  |  %s-endian",
        systemInfo->platform.pszPlatformName,
        systemInfo->platform.pszArchName,
        systemInfo->platform.pointerSize * 8u,
        systemInfo->platform.is64Bit ? "64-bit" : "32-bit",
        systemInfo->platform.isLittleEndian ? "little" : "big" );
    HOST_LOG_FIELD(
        log::channel_t::SYSTEM,
        "Compiler",
        "%s %u.%u.%u  |  ABI %s",
        compiler.pName,
        compiler.versionMajor,
        compiler.versionMinor,
        compiler.versionPatch,
        compiler.usesMsvcAbi ? "msvc" : "itanium" );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Compiler metadata",
        "packed %u  |  clang-cl %s",
        compiler.version,
        Host_BoolText( compiler.isClangCl ) );
    HOST_LOG_FIELD(
        log::channel_t::SYSTEM,
        "Process",
        "PID %llu  |  arguments %d",
        static_cast<unsigned long long>( systemInfo->process.processId ),
        argumentCount );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Runtime capacities",
        "events %u  |  text input %u bytes",
        sys::SYS_EVENT_QUEUE_CAPACITY,
        sys::SYS_TEXT_INPUT_CAPACITY );
    HOST_LOG_FIELD(
        log::channel_t::SYSTEM,
        "Processor",
        "%s",
        cpuInfo->szBrand );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Vendor",
        "%s  (raw: %s)",
        ::cypher::common::Cy_CPUDetectVendorName( cpuInfo->vendor ),
        cpuInfo->szVendor );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Topology",
        "%u physical cores  |  %u logical threads  |  %zu-byte cache line",
        cpuInfo->physicalCoreCount,
        cpuInfo->logicalThreadCount,
        cpuInfo->cacheLineSize );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Identity",
        "family %u  |  model %u  |  stepping %u",
        cpuInfo->family,
        cpuInfo->model,
        cpuInfo->stepping );

    char cpuFeatures[HOST_DIAGNOSTIC_TEXT_CAPACITY]{};
    Host_FormatCpuFeatures( cpuInfo->usableFeatures, cpuFeatures );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Feature masks",
        "hardware 0x%016llx  |  usable 0x%016llx",
        static_cast<unsigned long long>( cpuInfo->hardwareFeatures ),
        static_cast<unsigned long long>( cpuInfo->usableFeatures ) );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Usable features",
        "%s",
        cpuFeatures );

    char installedMemoryText[32]{};
    char pageSizeText[32]{};
    char allocationGranularityText[32]{};
    Host_FormatByteCount( systemInfo->memory.totalPhysicalBytes, installedMemoryText );
    Host_FormatByteCount( systemInfo->memory.pageSize, pageSizeText );
    Host_FormatByteCount( systemInfo->memory.allocationGranularity, allocationGranularityText );
    HOST_LOG_FIELD(
        log::channel_t::SYSTEM,
        "Installed memory",
        "%s  (%llu bytes)",
        installedMemoryText,
        static_cast<unsigned long long>( systemInfo->memory.totalPhysicalBytes ) );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Page size",
        "%s  (%zu bytes)",
        pageSizeText,
        systemInfo->memory.pageSize );
    HOST_LOG_DETAIL(
        log::channel_t::SYSTEM,
        "Allocation granularity",
        "%s  (%zu bytes)",
        allocationGranularityText,
        systemInfo->memory.allocationGranularity );

    const sys::system_memory_status_t memoryStatus = sys::Sys_QueryMemoryStatus();
    if ( memoryStatus.hasPhysicalMemory ) {
        char totalText[32]{};
        char availableText[32]{};
        Host_FormatByteCount( memoryStatus.totalPhysicalBytes, totalText );
        Host_FormatByteCount( memoryStatus.availablePhysicalBytes, availableText );
        HOST_LOG_FIELD(
            log::channel_t::SYSTEM,
            "Physical memory",
            "%s total  |  %s available  |  pressure %s",
            totalText,
            availableText,
            Host_MemoryPressureName( memoryStatus.pressure ) );
        HOST_LOG_DETAIL(
            log::channel_t::SYSTEM,
            "Exact bytes",
            "total %llu  |  available %llu",
            static_cast<unsigned long long>( memoryStatus.totalPhysicalBytes ),
            static_cast<unsigned long long>( memoryStatus.availablePhysicalBytes ) );
    } else {
        HOST_LOG_FIELD( log::channel_t::SYSTEM, "Physical memory", "unavailable  |  pressure unknown" );
    }

    if ( memoryStatus.hasProcessMemory ) {
        char residentText[32]{};
        char virtualText[32]{};
        Host_FormatByteCount( memoryStatus.processResidentBytes, residentText );
        Host_FormatByteCount( memoryStatus.processVirtualBytes, virtualText );
        HOST_LOG_FIELD(
            log::channel_t::SYSTEM,
            "Process memory",
            "%s resident  |  %s virtual",
            residentText,
            virtualText );
        HOST_LOG_DETAIL(
            log::channel_t::SYSTEM,
            "Exact bytes",
            "resident %llu  |  virtual %llu",
            static_cast<unsigned long long>( memoryStatus.processResidentBytes ),
            static_cast<unsigned long long>( memoryStatus.processVirtualBytes ) );
    } else {
        HOST_LOG_FIELD( log::channel_t::SYSTEM, "Process memory", "unavailable" );
    }

    HOST_LOG_FIELD(
        log::channel_t::SYSTEM,
        "Power state",
        "%s  (platform query)",
        Host_PowerStateName( sys::Sys_QueryPowerState() ) );

    const sys::paths_t &paths = sys::Sys_Paths();
    LOG_INFO( log::channel_t::SYSTEM, "  Runtime paths" );
    Host_LogRuntimePath( "Executable", paths.executablePath );
    Host_LogRuntimePath( "Executable directory", paths.executableDir );
    Host_LogRuntimePath( "Working directory", paths.workingDir );
    Host_LogRuntimePath( "Content root", paths.basePath );
    Host_LogRuntimePath( "User data", paths.userPath );
    Host_LogDiskStatus( "content", paths.basePath );
    Host_LogDiskStatus( "user data", paths.userPath );
}

void Host_LogFileSystemReport()
{
    using namespace cypher::engine;

    Host_LogSection( log::channel_t::FS, "FILESYSTEM" );
    HOST_LOG_FIELD( log::channel_t::FS, "State", "READY" );
    HOST_LOG_FIELD(
        log::channel_t::FS,
        "Runtime capacities",
        "mounts %u  |  async requests %u  |  watches %u",
        fs::CYPHER_FILESYSTEM_MAX_MOUNTS,
        fs::CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS,
        fs::CYPHER_FILESYSTEM_MAX_WATCHES );
    HOST_LOG_DETAIL(
        log::channel_t::FS,
        "Watch storage",
        "events %u  |  snapshot entries %u",
        fs::CYPHER_FILESYSTEM_MAX_WATCH_EVENTS,
        fs::CYPHER_FILESYSTEM_MAX_WATCH_SNAPSHOT_ENTRIES );
    HOST_LOG_FIELD(
        log::channel_t::FS,
        "Path policy",
        "'/' separator  |  lowercase  |  relative paths only" );
    HOST_LOG_DETAIL(
        log::channel_t::FS,
        "Rejected forms",
        "parent traversal  |  absolute paths  |  drive paths" );
    HOST_LOG_DETAIL(
        log::channel_t::FS,
        "Path capacities",
        "path %u  |  virtual root %u  |  pattern %u bytes",
        fs::CYPHER_FILESYSTEM_MAX_PATH_LENGTH,
        fs::CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH,
        fs::CYPHER_FILESYSTEM_MAX_PATTERN_LENGTH );

    const common::u32 mountCount = fs::FS_MountCount();
    common::u32 directoryCount = 0u;
    common::u32 packageCount = 0u;
    HOST_LOG_FIELD(
        log::channel_t::FS,
        "Mount table",
        "%u / %u  |  priority descending  |  insertion order on ties",
        mountCount,
        fs::CYPHER_FILESYSTEM_MAX_MOUNTS );

    for ( common::u32 mountIndex = 0u; mountIndex < mountCount; ++mountIndex ) {
        fs::mount_info_t mountInfo{};
        const fs::fs_error_t infoResult = fs::FS_GetMountInfo( mountIndex, mountInfo );
        if ( infoResult != fs::fs_error_t::OK ) {
            LOG_WARNING(
                log::channel_t::FS,
                "Mount [%u] diagnostics unavailable: %s",
                mountIndex,
                fs::FS_ErrorDesc( infoResult ) );
            continue;
        }

        const bool readOnly = ( mountInfo.flags & fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY ) != 0u;
        const bool writable = ( mountInfo.flags & fs::CYPHER_FILESYSTEM_MOUNT_WRITABLE ) != 0u;
        const bool optional = ( mountInfo.flags & fs::CYPHER_FILESYSTEM_MOUNT_OPTIONAL ) != 0u;
        const char *access = writable ? "writable" : ( readOnly ? "read-only" : "unspecified" );
        const char *virtualRoot = mountInfo.szVirtualRoot[0] != '\0' ? mountInfo.szVirtualRoot : "<root>";

        if ( mountInfo.type == fs::mount_type_t::CYPHER_FILESYSTEM_PACKAGE ) {
            ++packageCount;
            fs::package_info_t packageInfo{};
            const fs::fs_error_t packageResult =
                fs::FS_GetPackageInfo( mountInfo.szPhysicalRoot, packageInfo );
            LOG_INFO(
                log::channel_t::FS,
                "  Mount [%u]  %s  ->  %s",
                mountIndex,
                virtualRoot,
                mountInfo.szPhysicalRoot );
            HOST_LOG_DETAIL(
                log::channel_t::FS,
                "Identity",
                "handle %u  |  %s  |  priority %u",
                mountInfo.handle,
                Host_FileSystemMountTypeName( mountInfo.type ),
                mountInfo.priority );
            HOST_LOG_DETAIL(
                log::channel_t::FS,
                "Access",
                "%s  |  optional %s  |  flags 0x%08x",
                access,
                Host_BoolText( optional ),
                mountInfo.flags );
            HOST_LOG_DETAIL(
                log::channel_t::FS,
                "Package",
                "%u entries  |  metadata %s",
                packageInfo.nFileCount,
                packageResult == fs::fs_error_t::OK ? "available" : "unavailable" );
        } else {
            ++directoryCount;
            LOG_INFO(
                log::channel_t::FS,
                "  Mount [%u]  %s  ->  %s",
                mountIndex,
                virtualRoot,
                mountInfo.szPhysicalRoot );
            HOST_LOG_DETAIL(
                log::channel_t::FS,
                "Identity",
                "handle %u  |  %s  |  priority %u",
                mountInfo.handle,
                Host_FileSystemMountTypeName( mountInfo.type ),
                mountInfo.priority );
            HOST_LOG_DETAIL(
                log::channel_t::FS,
                "Access",
                "%s  |  optional %s  |  flags 0x%08x",
                access,
                Host_BoolText( optional ),
                mountInfo.flags );
        }
    }

    const char *writePath = fs::FS_GetWritePath();
    HOST_LOG_FIELD(
        log::channel_t::FS,
        "Write route",
        "%s",
        writePath != nullptr && writePath[0] != '\0' ? writePath : "<unset>" );
    HOST_LOG_DETAIL(
        log::channel_t::FS,
        "Write policy",
        "dedicated root  |  configured %s",
        Host_BoolText( writePath != nullptr && writePath[0] != '\0' ) );
    HOST_LOG_FIELD(
        log::channel_t::FS,
        "Summary",
        "READY  |  %u mounts  (%u directory, %u package)  |  write route %s",
        mountCount,
        directoryCount,
        packageCount,
        Host_BoolText( writePath != nullptr && writePath[0] != '\0' ) );
}

void Host_LogFileSystemStartupIo()
{
    using namespace cypher::engine;

    fs::stats_t stats{};
    const fs::fs_error_t result = fs::FS_GetStats( stats );
    if ( result != fs::fs_error_t::OK ) {
        LOG_WARNING(
            log::channel_t::FS,
            "Startup I/O snapshot unavailable: %s",
            fs::FS_ErrorDesc( result ) );
        return;
    }

    char bytesReadText[32]{};
    char bytesWrittenText[32]{};
    Host_FormatByteCount( stats.nBytesRead, bytesReadText );
    Host_FormatByteCount( stats.nBytesWritten, bytesWrittenText );
    HOST_LOG_FIELD(
        log::channel_t::FS,
        "Startup I/O calls",
        "open %llu  |  close %llu  |  read %llu  |  write %llu",
        static_cast<unsigned long long>( stats.nOpenCount ),
        static_cast<unsigned long long>( stats.nCloseCount ),
        static_cast<unsigned long long>( stats.nReadCount ),
        static_cast<unsigned long long>( stats.nWriteCount ) );
    HOST_LOG_DETAIL(
        log::channel_t::FS,
        "Bytes read",
        "%s  (%llu bytes)",
        bytesReadText,
        static_cast<unsigned long long>( stats.nBytesRead ) );
    HOST_LOG_DETAIL(
        log::channel_t::FS,
        "Bytes written",
        "%s  (%llu bytes)",
        bytesWrittenText,
        static_cast<unsigned long long>( stats.nBytesWritten ) );
    HOST_LOG_DETAIL(
        log::channel_t::FS,
        "Unresolved lookups",
        "%llu",
        static_cast<unsigned long long>( stats.nFailedLookupCount ) );
}

void Host_LogCommandRegistryReport()
{
    using namespace cypher::engine;

    Host_LogSection( log::channel_t::CMD, "COMMAND REGISTRY" );
    HOST_LOG_FIELD(
        log::channel_t::CMD,
        "Registry",
        "%s  |  %u / %u commands",
        cmd::Cmd_IsInitialized() ? "READY" : "NOT INITIALIZED",
        cmd::Cmd_Count(),
        CYPHER_COMMAND_MAX_COMMANDS );
    HOST_LOG_DETAIL(
        log::channel_t::CMD,
        "Parser limits",
        "%u arguments  |  %u-byte line  |  %u-byte payload",
        CYPHER_COMMAND_MAX_ARGUMENTS,
        CYPHER_COMMAND_MAX_LINE_LENGTH,
        CYPHER_COMMAND_MAX_LINE_LENGTH - 1u );
    LOG_INFO(
        log::channel_t::CMD,
        "  %-3s %-18s %-10s %-10s %s",
        "#",
        "Command",
        "Callback",
        "Context",
        "Description" );
    LOG_INFO(
        log::channel_t::CMD,
        "  %-3s %-18s %-10s %-10s %s",
        "---",
        "------------------",
        "----------",
        "----------",
        "----------------------------------------------" );

    for ( common::u32 index = 0u; index < cmd::Cmd_Count(); ++index ) {
        const cmd::cmd_t *command = cmd::Cmd_GetByIndex( index );
        if ( command == nullptr ) {
            LOG_WARNING( log::channel_t::CMD, "Command [%u] diagnostics unavailable", index );
            continue;
        }
        LOG_INFO(
            log::channel_t::CMD,
            "  %-3u %-18s %-10s %-10s %s",
            index,
            command->name ? command->name : "<unnamed>",
            command->pCallbackFn != nullptr ? "bound" : "missing",
            command->pExtraData != nullptr ? "bound" : "none",
            command->description ? command->description : "" );
    }
}

void Host_LogCvarRegistryReport()
{
    using namespace cypher::engine;

    common::u32 archiveCount = 0u;
    common::u32 readOnlyCount = 0u;
    common::u32 cheatCount = 0u;
    common::u32 developerCount = 0u;
    common::u32 modifiedCount = 0u;
    for ( common::u32 index = 0u; index < cvar::Cvar_Count(); ++index ) {
        const cvar::cvar_t *variable = cvar::Cvar_GetByIndex( index );
        if ( variable == nullptr ) {
            continue;
        }
        const common::u32 flags = static_cast<common::u32>( variable->flags );
        archiveCount += ( flags & cvar::CYPHER_CVAR_ARCHIVE ) != 0u ? 1u : 0u;
        readOnlyCount += ( flags & cvar::CYPHER_CVAR_READONLY ) != 0u ? 1u : 0u;
        cheatCount += ( flags & cvar::CYPHER_CVAR_CHEAT ) != 0u ? 1u : 0u;
        developerCount += ( flags & cvar::CYPHER_CVAR_DEV ) != 0u ? 1u : 0u;
        modifiedCount += ( flags & cvar::CYPHER_CVAR_MODIFIED ) != 0u ? 1u : 0u;
    }

    Host_LogSection( log::channel_t::CVAR, "CONSOLE VARIABLES" );
    HOST_LOG_FIELD(
        log::channel_t::CVAR,
        "Registry",
        "%s  |  %u / %u variables  |  %zu-byte values",
        cvar::Cvar_IsInitialized() ? "READY" : "NOT INITIALIZED",
        cvar::Cvar_Count(),
        CYPHER_CVAR_MAX_CVARS,
        sizeof( cvar::cvar_t{}.valueString ) - 1u );
    HOST_LOG_DETAIL(
        log::channel_t::CVAR,
        "Flag totals",
        "archive %u  |  read-only %u  |  cheat %u  |  developer %u  |  modified %u",
        archiveCount,
        readOnlyCount,
        cheatCount,
        developerCount,
        modifiedCount );

    for ( common::u32 index = 0u; index < cvar::Cvar_Count(); ++index ) {
        const cvar::cvar_t *variable = cvar::Cvar_GetByIndex( index );
        if ( variable == nullptr ) {
            LOG_WARNING( log::channel_t::CVAR, "CVar [%u] diagnostics unavailable", index );
            continue;
        }

        char flagText[HOST_DIAGNOSTIC_TEXT_CAPACITY]{};
        Host_FormatCvarFlags( variable->flags, flagText );
        LOG_INFO(
            log::channel_t::CVAR,
            "  [%02u] %-24s : %s  (default: %s)",
            index,
            variable->name ? variable->name : "<unnamed>",
            variable->valueString,
            variable->defaultString );
        LOG_INFO(
            log::channel_t::CVAR,
            "       %-22s : int %u  |  float %.6g  |  bool %s  |  %s  (0x%08x)",
            "Cached / flags",
            variable->valueInt,
            static_cast<double>( variable->valueFloat ),
            Host_BoolText( variable->valueBool ),
            flagText,
            static_cast<common::u32>( variable->flags ) );
    }
}

void Host_LogSinkRecord(
    const char *name,
    const cypher::engine::log::sink_config_t &sink,
    const bool fileBacked )
{
    using namespace cypher::engine;

    LOG_INFO(
        log::channel_t::CFG,
        "  %-24s : %s  |  %s+  |  %s  |  flush %s",
        name ? name : "unknown",
        sink.enabled ? "ENABLED " : "DISABLED",
        log::Log_LevelName( sink.nMinLevel ),
        Host_LogFormatName( sink.format ),
        Host_LogFlushName( sink.flush ) );
    if ( fileBacked ) {
        LOG_INFO(
            log::channel_t::CFG,
            "    %-22s : %s  |  %s",
            "File",
            sink.path[0] != '\0' ? sink.path : "<unset>",
            Host_LogFileModeName( sink.file ) );
    }
    LOG_INFO(
        log::channel_t::CFG,
        "    %-22s : time %s  |  source %s  |  function %s  |  color %s",
        "Fields",
        Host_BoolText( sink.bIncludeTimestamps ),
        Host_BoolText( sink.bIncludeSourceLocation ),
        Host_BoolText( sink.bIncludeFunctionName ),
        Host_BoolText( sink.bColorEnabled ) );
}

void Host_LogLoggingReport( const cypher::engine::log::config_t &config )
{
    using namespace cypher::engine;

    const common::u32 enabledSinkCount =
        ( config.terminal.enabled ? 1u : 0u ) +
        ( config.engineFile.enabled ? 1u : 0u ) +
        ( config.errorFile.enabled ? 1u : 0u ) +
        ( config.consoleFile.enabled ? 1u : 0u ) +
        ( config.editorFile.enabled ? 1u : 0u ) +
        ( config.gameFile.enabled ? 1u : 0u );

    Host_LogSection( log::channel_t::CFG, "LOGGING" );
    HOST_LOG_FIELD(
        log::channel_t::CFG,
        "State",
        "READY  |  %u / 6 stream sinks enabled",
        enabledSinkCount );
    HOST_LOG_FIELD(
        log::channel_t::CFG,
        "Global filter",
        "%s+  |  channels 0x%08x  |  source paths %s",
        log::Log_LevelName( config.nMinLevel ),
        config.nChannelMask,
        Host_LogSourcePathName( config.szSourcePath ) );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "Capacities",
        "%zu-byte message  |  %zu-byte file path",
        log::CYPHER_LOG_MESSAGE_MAX - 1u,
        log::CYPHER_LOG_FILE_PATH_MAX - 1u );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "Crash buffer",
        "route reserved  |  storage not implemented" );
    HOST_LOG_FIELD(
        log::channel_t::CFG,
        "Default sink mask",
        "all levels request terminal + engine file" );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "WARNING adds",
        "error file" );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "ERROR / FATAL add",
        "error file + reserved crash route" );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "Delivery",
        "subject to global, channel, and per-sink filters" );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "Optional streams",
        "console, editor, and game require an explicit record mask" );
    LOG_INFO( log::channel_t::CFG, "  Configured sinks" );
    Host_LogSinkRecord( "terminal", config.terminal, false );
    Host_LogSinkRecord( "engine file", config.engineFile, true );
    Host_LogSinkRecord( "error file", config.errorFile, true );
    Host_LogSinkRecord( "console file", config.consoleFile, true );
    Host_LogSinkRecord( "editor file", config.editorFile, true );
    Host_LogSinkRecord( "game file", config.gameFile, true );
}

void Host_LogWindowAndDisplayReport(
    const cypher::engine::sys::window_desc_t &request,
    const cypher::engine::sys::window_t &window )
{
    using namespace cypher::engine;

    Host_LogSection( log::channel_t::PLATFORM, "PLATFORM, WINDOW & DISPLAY" );
    sys::platform_backend_info_t backendInfo{};
    const sys::sys_error_t backendResult = sys::Sys_GetPlatformBackendInfo( backendInfo );
    if ( backendResult == sys::sys_error_t::OK ) {
        HOST_LOG_FIELD(
            log::channel_t::PLATFORM,
            "Platform backend",
            "%s  |  video driver %s",
            backendInfo.name,
            backendInfo.videoDriver );
        HOST_LOG_DETAIL(
            log::channel_t::PLATFORM,
            "Library versions",
            "compiled %u.%u.%u  |  runtime %u.%u.%u",
            backendInfo.compiledVersionMajor,
            backendInfo.compiledVersionMinor,
            backendInfo.compiledVersionPatch,
            backendInfo.runtimeVersionMajor,
            backendInfo.runtimeVersionMinor,
            backendInfo.runtimeVersionPatch );
        HOST_LOG_DETAIL(
            log::channel_t::PLATFORM,
            "Library revision",
            "%s",
            backendInfo.revision );
    } else {
        LOG_WARNING(
            log::channel_t::PLATFORM,
            "Platform backend diagnostics unavailable: %s",
            sys::Sys_ErrorDesc( backendResult ) );
    }
    HOST_LOG_FIELD(
        log::channel_t::PLATFORM,
        "Window request",
        "%s  |  %ux%u logical  |  %s  |  display %u",
        request.title ? request.title : "<untitled>",
        request.width,
        request.height,
        Host_WindowModeName( request.mode ),
        request.displayId );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Graphics / flags",
        "%s  |  0x%08x  |  compatibility fullscreen %s",
        Host_WindowGraphicsApiName( request.graphicsApi ),
        request.flags,
        Host_BoolText( request.fullscreen ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Requested behavior",
        "resizable %s  |  high-DPI %s  |  hidden %s",
        Host_BoolText( ( request.flags & sys::SYS_WINDOW_RESIZABLE ) != 0u ),
        Host_BoolText( ( request.flags & sys::SYS_WINDOW_HIGH_PIXEL_DENSITY ) != 0u ),
        Host_BoolText( ( request.flags & sys::SYS_WINDOW_HIDDEN ) != 0u ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Requested style",
        "borderless %s  |  always-on-top %s",
        Host_BoolText( ( request.flags & sys::SYS_WINDOW_BORDERLESS ) != 0u ),
        Host_BoolText( ( request.flags & sys::SYS_WINDOW_ALWAYS_ON_TOP ) != 0u ) );
    HOST_LOG_FIELD(
        log::channel_t::PLATFORM,
        "Window created",
        "%s  |  id %u  |  display %u  |  position %d,%d",
        window.valid ? "READY" : "INVALID",
        window.id,
        window.displayId,
        window.x,
        window.y );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Dimensions",
        "%ux%u logical  |  %ux%u drawable",
        window.logicalWidth,
        window.logicalHeight,
        window.width,
        window.height );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Scale",
        "pixel density %.3f  |  display %.3f",
        static_cast<double>( window.pixelDensity ),
        static_cast<double>( window.displayScale ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Mode / graphics",
        "%s  |  %s",
        Host_WindowModeName( window.mode ),
        Host_WindowGraphicsApiName( window.graphicsApi ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "State",
        "flags 0x%08x  |  visible %s  |  focused %s  |  mouse focus %s  |  close %s",
        window.flags,
        Host_BoolText( window.visible ),
        Host_BoolText( window.focused ),
        Host_BoolText( window.mouseFocused ),
        Host_BoolText( window.shouldClose ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Geometry state",
        "fullscreen %s  |  minimized %s  |  maximized %s",
        Host_BoolText( window.fullscreen ),
        Host_BoolText( window.minimized ),
        Host_BoolText( window.maximized ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Window style",
        "resizable %s  |  borderless %s  |  always-on-top %s",
        Host_BoolText( ( window.flags & sys::SYS_WINDOW_RESIZABLE ) != 0u ),
        Host_BoolText( ( window.flags & sys::SYS_WINDOW_BORDERLESS ) != 0u ),
        Host_BoolText( ( window.flags & sys::SYS_WINDOW_ALWAYS_ON_TOP ) != 0u ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Window input",
        "mouse grabbed %s  |  relative mouse %s  |  text input %s",
        Host_BoolText( window.mouseGrabbed ),
        Host_BoolText( window.relativeMouseEnabled ),
        Host_BoolText( window.textInputEnabled ) );

    sys::display_list_result_t displays{};
    sys::sys_display_id_t primaryDisplay = sys::SYS_INVALID_DISPLAY_ID;
    const sys::sys_error_t displaysResult = sys::Sys_GetDisplays( nullptr, 0u, displays );
    const sys::sys_error_t primaryResult = sys::Sys_GetPrimaryDisplay( primaryDisplay );
    if ( displaysResult == sys::sys_error_t::OK && primaryResult == sys::sys_error_t::OK ) {
        HOST_LOG_FIELD(
            log::channel_t::PLATFORM,
            "Display topology",
            "%u connected  |  primary %u  |  window on %u",
            displays.displaysRequired,
            primaryDisplay,
            window.displayId );
    } else {
        LOG_WARNING(
            log::channel_t::PLATFORM,
            "Display topology incomplete: enumerate=%s, primary=%s",
            sys::Sys_ErrorDesc( displaysResult ),
            sys::Sys_ErrorDesc( primaryResult ) );
    }

    sys::display_info_t displayInfo{};
    const sys::sys_error_t displayInfoResult =
        sys::Sys_GetDisplayInfo( window.displayId, displayInfo );
    if ( displayInfoResult != sys::sys_error_t::OK ) {
        LOG_WARNING(
            log::channel_t::PLATFORM,
            "Display %u diagnostics unavailable: %s",
            window.displayId,
            sys::Sys_ErrorDesc( displayInfoResult ) );
        return;
    }

    common::u32 exclusiveModeCount = 0u;
    const sys::sys_error_t modeCountResult =
        sys::Sys_GetDisplayModeCount( window.displayId, exclusiveModeCount );
    HOST_LOG_FIELD(
        log::channel_t::PLATFORM,
        "Active display",
        "%s  |  id %u  |  primary %s",
        displayInfo.name,
        displayInfo.id,
        Host_BoolText( displayInfo.primary ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Orientation / HDR",
        "%s  |  desktop HDR %s",
        Host_DisplayOrientationName( displayInfo.orientation ),
        Host_BoolText( displayInfo.hdrEnabled ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Bounds",
        "%d,%d  %ux%u  |  content scale %.3f",
        displayInfo.bounds.x,
        displayInfo.bounds.y,
        displayInfo.bounds.width,
        displayInfo.bounds.height,
        static_cast<double>( displayInfo.contentScale ) );
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Usable bounds",
        "%d,%d  %ux%u",
        displayInfo.usableBounds.x,
        displayInfo.usableBounds.y,
        displayInfo.usableBounds.width,
        displayInfo.usableBounds.height );
    if ( modeCountResult == sys::sys_error_t::OK ) {
        HOST_LOG_DETAIL(
            log::channel_t::PLATFORM,
            "Current mode",
            "%ux%u @ %.3f Hz  (%d/%d)  |  density %.3f  |  %u exclusive modes",
            displayInfo.currentMode.width,
            displayInfo.currentMode.height,
            static_cast<double>( displayInfo.currentMode.refreshRateHz ),
            displayInfo.currentMode.refreshRateNumerator,
            displayInfo.currentMode.refreshRateDenominator,
            static_cast<double>( displayInfo.currentMode.pixelDensity ),
            exclusiveModeCount );
    } else {
        HOST_LOG_DETAIL(
            log::channel_t::PLATFORM,
            "Current mode",
            "%ux%u @ %.3f Hz  (%d/%d)  |  density %.3f  |  exclusive modes unavailable (%s)",
            displayInfo.currentMode.width,
            displayInfo.currentMode.height,
            static_cast<double>( displayInfo.currentMode.refreshRateHz ),
            displayInfo.currentMode.refreshRateNumerator,
            displayInfo.currentMode.refreshRateDenominator,
            static_cast<double>( displayInfo.currentMode.pixelDensity ),
            sys::Sys_ErrorDesc( modeCountResult ) );
    }
    HOST_LOG_DETAIL(
        log::channel_t::PLATFORM,
        "Desktop mode",
        "%ux%u @ %.3f Hz  (%d/%d)  |  density %.3f",
        displayInfo.desktopMode.width,
        displayInfo.desktopMode.height,
        static_cast<double>( displayInfo.desktopMode.refreshRateHz ),
        displayInfo.desktopMode.refreshRateNumerator,
        displayInfo.desktopMode.refreshRateDenominator,
        static_cast<double>( displayInfo.desktopMode.pixelDensity ) );
}

void Host_PrintArenaStats( const mem::arena_stats_t &arenaStats )
{
    COM_PRINTF(
        "%-16s used=%zu committed=%zu capacity=%zu peak=%zu allocs=%llu failed=%llu\n",
        arenaStats.name ? arenaStats.name : "<unnamed>",
        arenaStats.used,
        arenaStats.committed,
        arenaStats.capacity,
        arenaStats.nPeakUsed,
        static_cast<unsigned long long>( arenaStats.nAllocationCount ),
        static_cast<unsigned long long>( arenaStats.nFailedAllocationCount ) );
}

/*
================
Host Builtin Commands
================
*/
void Host_CmdEcho( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    for ( rc::u32 i = 1u; i < argc; ++i ) {
        COM_PRINTF( "%s%s", argv[i], ( i + 1u < argc ) ? " " : "\n" );
    }
    if ( argc <= 1u ) {
        COM_PRINTF( "\n" );
    }
}

void Host_CmdVersion( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    const rc::version_t &nEngineVersion = rc::COM_ENGINE_INFO.version;
    const rc::version_t &nGameVersion = rc::COM_GAME_INFO.version;

    COM_PRINTF(
        "%s %u.%u.%u.%u | %s %u.%u.%u.%u\n",
        rc::COM_ENGINE_INFO.name,
        nEngineVersion.major,
        nEngineVersion.minor,
        nEngineVersion.patch,
        nEngineVersion.build,
        rc::COM_GAME_INFO.name,
        nGameVersion.major,
        nGameVersion.minor,
        nGameVersion.patch,
        nGameVersion.build );
}

void Host_CmdQuit( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )argc;
    ( void )argv;

    cypher::engine::host::state_t *pHostState = static_cast<cypher::engine::host::state_t *>( pExtraData );

    if ( pHostState == nullptr ) {
        return ;
    }

    Host_RequestShutdown( *pHostState );
}

void Host_CmdLogApply( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    const auto result = cypher::engine::host::Host_ApplyLogCvars();

    if ( result != cypher::engine::host::host_error_t::OK ) {
        COM_ERRORF(
            cypher::engine::host::Host_ErrorCode( result ),
            "log_apply failed." );
        return;
    }

    COM_PRINTF( "log config applied.\n" );
}

void Host_CmdLogConfig( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    const auto &config = cypher::engine::log::Log_GetConfig();

    COM_PRINTF(
        "log: global=%s terminal=%u/%s engine_file=%u/%s error_file=%u/%s\n",
        cypher::engine::log::Log_LevelName( config.nMinLevel ),
        config.terminal.enabled ? 1u : 0u,
        cypher::engine::log::Log_LevelName( config.terminal.nMinLevel ),
        config.engineFile.enabled ? 1u : 0u,
        cypher::engine::log::Log_LevelName( config.engineFile.nMinLevel ),
        config.errorFile.enabled ? 1u : 0u,
        cypher::engine::log::Log_LevelName( config.errorFile.nMinLevel ) );
}

void Host_CmdMemReport( void *pExtraData, rc::u32 argc, char **argv ) {
    ( void )pExtraData;
    ( void )argc;
    ( void )argv;

    if ( !mem::Mem_IsInitialized() ) {
        COM_PRINTF( "memory system is not initialized.\n" );
        return;
    }

    const mem::memory_stats_t stats = mem::Mem_Stats();

    COM_PRINTF(
        "memory: used=%zu committed=%zu capacity=%zu peak=%zu\n",
        stats.nTotalUsed,
        stats.totalCommitted,
        stats.nTotalCapacity,
        stats.nPeakUsed );

    Host_PrintArenaStats( stats.permanentStats );
    Host_PrintArenaStats( stats.frameStats );
    Host_PrintArenaStats( stats.scratchStats );
    Host_PrintArenaStats( stats.resourceStats );
    Host_PrintArenaStats( stats.worldStats );
    Host_PrintArenaStats( stats.renderStats );
    Host_PrintArenaStats( stats.editorStats );
}

}       // namespace

namespace cypher::engine::host {

/*
================
Host_PrepareStateForInit
================
*/
void Host_PrepareStateForInit( state_t &pHostState ) {
    pHostState.stage = stage_t::INITIALIZING;
    pHostState.running = false;
    pHostState.bHasFocus = true;
    pHostState.frame = {};
}

/*
================
Host_InitCoreEngineSystems

Brings up low-level systems in dependency order.
================
*/
host_error_t Host_InitCoreEngineSystems( state_t &pHostState ) {
    const auto coreServicesBegin = std::chrono::steady_clock::now();
    auto serviceBegin = coreServicesBegin;
    double systemInitMilliseconds = 0.0;
    double logInitMilliseconds = 0.0;
    double systemReportMilliseconds = 0.0;
    double memoryInitMilliseconds = 0.0;
    double filesystemInitMilliseconds = 0.0;
    double commandInitMilliseconds = 0.0;
    double cvarInitMilliseconds = 0.0;
    double configInitMilliseconds = 0.0;

    sys::init_info_t sysInfo {
        .argc = pHostState.config.argc,
        .argv = pHostState.config.argv,
        .appName = common::COM_GAME_INFO.szInternalName,
        .organizationName = common::COM_GAME_INFO.szOrganizationName
    };

    serviceBegin = std::chrono::steady_clock::now();
    const auto sysResult = sys::Sys_Init( sysInfo );
    systemInitMilliseconds = Host_ElapsedMilliseconds( serviceBegin );
    if ( sysResult != sys::sys_error_t::OK ) {
        COM_ERRORF( Sys_ErrorCode( sysResult ) , "Host_Init: Sys_Init failed: %s", sys::Sys_ErrorDesc( sysResult ) );

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    serviceBegin = std::chrono::steady_clock::now();
    const auto logResult = log::Log_Init();
    logInitMilliseconds = Host_ElapsedMilliseconds( serviceBegin );
    if ( logResult != log::log_error_t::OK ) {
        COM_ERRORF( log::Log_ErrorCode( logResult ), "Host_Init: Log_Init failed: %s", log::Log_ErrorDesc( logResult ) );

        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    Host_LogStartupBanner();
    serviceBegin = std::chrono::steady_clock::now();
    Host_LogIdentityAndSystemReport( pHostState.config.argc );
    systemReportMilliseconds = Host_ElapsedMilliseconds( serviceBegin );

    Host_LogSection( log::channel_t::MEMORY, "MEMORY" );
    serviceBegin = std::chrono::steady_clock::now();
    const auto memoryResult = mem::Mem_Init( mem::Mem_DefaultConfig() );
    memoryInitMilliseconds = Host_ElapsedMilliseconds( serviceBegin );
    if ( memoryResult != mem::mem_error_t::OK ) {
        LOG_ERROR( log::channel_t::MEMORY, "memory system initialization failed: %s.", mem::Mem_ErrorDesc( memoryResult ) );
        COM_ERRORF( mem::Mem_ErrorCode( memoryResult ), "Host_Init: Mem_Init failed: %s", mem::Mem_ErrorDesc( memoryResult ) );
        log::Log_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    serviceBegin = std::chrono::steady_clock::now();
    const auto fsResult = fs::FS_Init();
    filesystemInitMilliseconds = Host_ElapsedMilliseconds( serviceBegin );
    if( fsResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::FS, "filesystem initialization failed: %s.", fs::FS_ErrorDesc( fsResult ) );
        COM_ERRORF( FS_ErrorCode( fsResult ), "Host_Init: FS_Init failed: %s", fs::FS_ErrorDesc( fsResult ) );
        mem::Mem_Shutdown();
        log::Log_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }
    serviceBegin = std::chrono::steady_clock::now();
    const auto cmdResult = cmd::Cmd_Init();
    commandInitMilliseconds = Host_ElapsedMilliseconds( serviceBegin );
    if ( cmdResult != cmd::cmd_error_t::OK )
    {
        LOG_ERROR( log::channel_t::CMD, "command system initialization failed: %s.", Cmd_ErrorDesc( cmdResult ) );
        COM_ERRORF( Cmd_ErrorCode( cmdResult ), "Host_Init: Cmd_Init failed: %s", Cmd_ErrorDesc( cmdResult ) );

        fs::FS_Shutdown();
        mem::Mem_Shutdown();
        log::Log_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    serviceBegin = std::chrono::steady_clock::now();
    const auto cvarResult = cvar::Cvar_Init();
    cvarInitMilliseconds = Host_ElapsedMilliseconds( serviceBegin );

    if ( cvarResult != cvar::cvar_error_t::OK )
    {
        LOG_ERROR( log::channel_t::CVAR, "cvar system initialization failed: %s.", cvar::Cvar_ErrorDesc( cvarResult ) );
        COM_ERRORF( Cvar_ErrorCode( cvarResult ), "Host_Init: Cvar_Init failed: %s", cvar::Cvar_ErrorDesc( cvarResult ) );

        cmd::Cmd_Shutdown();
        fs::FS_Shutdown();
        mem::Mem_Shutdown();
        log::Log_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    serviceBegin = std::chrono::steady_clock::now();
    const auto cfgResult = cfg::Cfg_Init();
    configInitMilliseconds = Host_ElapsedMilliseconds( serviceBegin );

    if ( cfgResult != cfg::cfg_error_t::OK )
    {
        LOG_ERROR( log::channel_t::CFG, "config system initialization failed: %s.", cfg::Cfg_ErrorDesc( cfgResult ) );
        COM_ERRORF( Cfg_ErrorCode( cfgResult ), "Host_Init: Cfg_Init failed: %s", cfg::Cfg_ErrorDesc( cfgResult ) );

        cvar::Cvar_Shutdown();
        cmd::Cmd_Shutdown();
        fs::FS_Shutdown();
        mem::Mem_Shutdown();
        log::Log_Shutdown();
        sys::Sys_Shutdown();

        pHostState.running = false;
        pHostState.stage = stage_t::SHUTDOWN;
        return host_error_t::ERR_INITIALIZING;
    }

    const double coreServicesMilliseconds = Host_ElapsedMilliseconds( coreServicesBegin );
    Host_LogSection( log::channel_t::HOST, "CORE SERVICES" );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %12s", "Service", "State", "Elapsed" );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %12s", "------------------------", "----------", "------------" );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "System", "READY", systemInitMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "Logging", "READY", logInitMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "System report", "READY", systemReportMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "Memory", "READY", memoryInitMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "FileSystem", "READY", filesystemInitMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "Commands", "READY", commandInitMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "CVars", "READY", cvarInitMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %-10s %9.3f ms", "Configuration", "READY", configInitMilliseconds );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Core services",
        "READY in %.3f ms",
        coreServicesMilliseconds );
    return host_error_t::OK;
}

/*
================
Host_MountFileSystem
================
*/
host_error_t Host_MountFileSystem( void ) {
    const sys::paths_t &paths = sys::Sys_Paths();

    const auto baseMountResult = fs::FS_MountDirectory(
        "",
        paths.basePath,
        fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY,
        0u );

    if ( baseMountResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::FS, "filesystem base mount failed: base='%s', error=%s.", paths.basePath, fs::FS_ErrorDesc( baseMountResult ) );
        COM_ERRORF(
            fs::FS_ErrorCode( baseMountResult ),
            "Host_Init: filesystem base mount failed: %s",
            fs::FS_ErrorDesc( baseMountResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    const auto writePathResult = fs::FS_SetWritePath( paths.userPath );
    if ( writePathResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::FS, "filesystem write path failed: user='%s', error=%s.", paths.userPath, fs::FS_ErrorDesc( writePathResult ) );
        COM_ERRORF(
            fs::FS_ErrorCode( writePathResult ),
            "Host_Init: filesystem write path failed: %s",
            fs::FS_ErrorDesc( writePathResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    Host_LogFileSystemReport();
    return host_error_t::OK;
}

/*
================
Host_RegisterBuiltinCvars
================
*/
host_error_t Host_RegisterBuiltinCvars( void ) {
    struct builtin_cvar_t {
        const char *name;
        const char *defaultValue;
        cvar::flags_t flags;
    };

    const builtin_cvar_t builtinCvars[] = {
        { "r_width", "1280", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_height", "720", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_fullscreen", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_vsync", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_fov", "90", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_near", "0.1", cvar::CYPHER_CVAR_ARCHIVE },
        { "r_far", "1000", cvar::CYPHER_CVAR_ARCHIVE },

        { "host_target_fps", "60", cvar::CYPHER_CVAR_ARCHIVE },
        { "host_timescale", "1.0", cvar::CYPHER_CVAR_ARCHIVE },
        { "host_max_delta_time", "0.25", cvar::CYPHER_CVAR_ARCHIVE },

        { "developer", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "con_show", "0", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_global_level", "trace", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal_color", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_terminal_timestamps", "0", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_engine_file", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_engine_file_level", "trace", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_engine_file_path", "CypherEngine.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_error_file", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_error_file_level", "warning", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_error_file_path", "CypherEngine_errors.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_console_file", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_console_file_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_console_file_path", "Console.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_editor_file", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_editor_file_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_editor_file_path", "Editor.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_game_file", "0", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_game_file_level", "info", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_game_file_path", "Game.log", cvar::CYPHER_CVAR_ARCHIVE },

        { "log_file_timestamps", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_file_source", "1", cvar::CYPHER_CVAR_ARCHIVE },
        { "log_file_function", "1", cvar::CYPHER_CVAR_ARCHIVE },

        { "sys_app_name", rc::COM_GAME_INFO.szInternalName, cvar::CYPHER_CVAR_READONLY },
    };

    for ( const builtin_cvar_t &builtinCvar : builtinCvars ) {
        const auto result = cvar::Cvar_Register(
            builtinCvar.name,
            builtinCvar.defaultValue,
            builtinCvar.flags );

        if ( result != cvar::cvar_error_t::OK ) {
            COM_ERRORF(
                cvar::Cvar_ErrorCode( result ),
                "Host_Init: failed to register cvar '%s': %s",
                builtinCvar.name,
                cvar::Cvar_ErrorDesc( result ) );
            return host_error_t::ERR_INITIALIZING;
        }
    }

    LOG_DEBUG( log::channel_t::CVAR, "built-in cvars registered: count=%zu.", sizeof( builtinCvars ) / sizeof( builtinCvars[0] ) );
    return host_error_t::OK;
}

/*
================
Host_RegisterBuiltinCommands
================
*/
host_error_t Host_RegisterBuiltinCommands( state_t &pHostState ) {
    struct builtin_command_t {
        const char *name;
        cmd::command_fn_t callback;
        void *pExtraData;
        const char *description;
    };

    const builtin_command_t builtinCommands[] = {
        { "echo", Host_CmdEcho, nullptr, "prints text to the engine console" },
        { "version", Host_CmdVersion, nullptr, "prints engine and game version information" },
        { "quit", Host_CmdQuit, &pHostState, "requests engine shutdown" },
        { "log_apply", Host_CmdLogApply, nullptr, "applies log cvars to active log sinks" },
        { "log_config", Host_CmdLogConfig, nullptr, "prints active log sink configuration" },
        { "mem_report", Host_CmdMemReport, nullptr, "prints memory arena usage" }
    };

    for ( const builtin_command_t &builtinCommand : builtinCommands ) {
        const auto result = cmd::Cmd_Register(
            builtinCommand.name,
            builtinCommand.callback,
            builtinCommand.pExtraData,
            builtinCommand.description );

        if ( result != cmd::cmd_error_t::OK ) {
            COM_ERRORF(
                cmd::Cmd_ErrorCode( result ),
                "Host_Init: failed to register command '%s': %s",
                builtinCommand.name,
                cmd::Cmd_ErrorDesc( result ) );
            return host_error_t::ERR_INITIALIZING;
        }
    }

    Host_LogCommandRegistryReport();
    return host_error_t::OK;
}

/*
================
Host_LoadStartupConfig
================
*/
host_error_t Host_LoadStartupConfig( void ) {
    constexpr const char *defaultPath = "config/default.cfg";
    constexpr const char *autoexecPath = "config/autoexec.cfg";
    bool defaultLoaded = false;
    bool autoexecLoaded = false;

    Host_LogSection( log::channel_t::CFG, "CONFIGURATION" );
    HOST_LOG_FIELD( log::channel_t::CFG, "State", "LOADING" );
    HOST_LOG_FIELD(
        log::channel_t::CFG,
        "Parser limits",
        "%llu-byte file  |  %u-byte line  |  %u-byte payload",
        static_cast<unsigned long long>( cfg::CYPHER_CONFIG_MAX_FILE_SIZE ),
        cfg::CYPHER_CONFIG_MAX_LINE_LENGTH,
        cfg::CYPHER_CONFIG_MAX_LINE_LENGTH - 1u );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "Execution limits",
        "%u-byte path  |  %u nested exec levels",
        cfg::CYPHER_CONFIG_MAX_PATH_LENGTH,
        cfg::CYPHER_CONFIG_MAX_EXEC_DEPTH );

    const auto defaultResult = cfg::Cfg_LoadFile( defaultPath, false, &defaultLoaded );
    if ( defaultResult != cfg::cfg_error_t::OK ) {
        LOG_ERROR( log::channel_t::CFG, "default startup config failed: %s.", cfg::Cfg_ErrorDesc( defaultResult ) );
        COM_ERRORF(
            cfg::Cfg_ErrorCode( defaultResult ),
            "Host_Init: default config load failed: %s",
            cfg::Cfg_ErrorDesc( defaultResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    const auto autoexecResult = cfg::Cfg_LoadFile( autoexecPath, false, &autoexecLoaded );
    if ( autoexecResult != cfg::cfg_error_t::OK ) {
        LOG_ERROR( log::channel_t::CFG, "autoexec startup config failed: %s.", cfg::Cfg_ErrorDesc( autoexecResult ) );
        COM_ERRORF(
            cfg::Cfg_ErrorCode( autoexecResult ),
            "Host_Init: autoexec config load failed: %s",
            cfg::Cfg_ErrorDesc( autoexecResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    fs::file_info_t defaultInfo{};
    fs::file_info_t autoexecInfo{};
    if ( defaultLoaded ) {
        (void)fs::FS_GetFileInfo( defaultPath, defaultInfo );
    }
    if ( autoexecLoaded ) {
        (void)fs::FS_GetFileInfo( autoexecPath, autoexecInfo );
    }

    HOST_LOG_FIELD(
        log::channel_t::CFG,
        "Defaults",
        "%s  |  %s  |  %llu bytes",
        defaultPath,
        defaultLoaded ? "LOADED" : "SKIPPED (not found)",
        static_cast<unsigned long long>( defaultLoaded ? defaultInfo.nFileSize : 0u ) );
    HOST_LOG_DETAIL(
        log::channel_t::CFG,
        "Defaults fallback",
        "%s",
        defaultLoaded ? "none" : "built-in defaults" );
    HOST_LOG_FIELD(
        log::channel_t::CFG,
        "Autoexec",
        "%s  |  %s  |  %llu bytes",
        autoexecPath,
        autoexecLoaded ? "LOADED" : "SKIPPED (not found)",
        static_cast<unsigned long long>( autoexecLoaded ? autoexecInfo.nFileSize : 0u ) );
    HOST_LOG_FIELD(
        log::channel_t::CFG,
        "Summary",
        "READY  |  defaults %s  |  autoexec %s",
        defaultLoaded ? "file" : "built-in",
        autoexecLoaded ? "file" : "not present" );

    return host_error_t::OK;
}

/*
================
Host_LogLevelFromCvar
================
*/
log::level_t Host_LogLevelFromCvar( const char *szCvarName, const log::level_t fallback )
{
    log::level_t parsedLevel = fallback;
    const char *szLevelName = cvar::Cvar_GetString( szCvarName );
    const auto parseResult = log::Log_LevelFromString( szLevelName, parsedLevel );

    if ( parseResult != log::log_error_t::OK ) {
        LOG_WARNING( log::channel_t::CFG, "invalid log level cvar '%s'='%s'; keeping '%s'.", szCvarName, szLevelName ? szLevelName : "<null>", log::Log_LevelName( fallback ) );
        return fallback;
    }

    return parsedLevel;
}

/*
================
Host_CopyLogPathFromCvar
================
*/
void Host_CopyLogPathFromCvar( char *outPath, const common::usize outPathSize, const char *szCvarName, const char *fallback )
{
    if ( outPath == nullptr || outPathSize == 0u ) {
        return;
    }

    const char *path = cvar::Cvar_GetString( szCvarName );

    if ( path == nullptr || path[0] == '\0' ) {
        path = fallback;
    }

    std::strncpy( outPath, path, outPathSize - 1u );
    outPath[outPathSize - 1u] = '\0';
}

/*
================
Host_ApplyLogCvars

Converts registered log cvars into the active logger sink configuration.
================
*/
host_error_t Host_ApplyLogCvars( void )
{
    log::config_t logConfig = log::Log_GetConfig();

    logConfig.nMinLevel = Host_LogLevelFromCvar( "log_global_level", logConfig.nMinLevel );

    logConfig.terminal.enabled = cvar::Cvar_GetBool( "log_terminal" );
    logConfig.terminal.nMinLevel = Host_LogLevelFromCvar( "log_terminal_level", logConfig.terminal.nMinLevel );
    logConfig.terminal.format = log::format_mode_t::CONSOLE;
    logConfig.terminal.bIncludeTimestamps = cvar::Cvar_GetBool( "log_terminal_timestamps" );
    logConfig.terminal.bIncludeSourceLocation = false;
    logConfig.terminal.bIncludeFunctionName = false;
    logConfig.terminal.bColorEnabled = cvar::Cvar_GetBool( "log_terminal_color" );

    logConfig.engineFile.enabled = cvar::Cvar_GetBool( "log_engine_file" );
    logConfig.engineFile.nMinLevel = Host_LogLevelFromCvar( "log_engine_file_level", logConfig.engineFile.nMinLevel );
    logConfig.engineFile.format = log::format_mode_t::DETAILED;
    logConfig.engineFile.bIncludeTimestamps = cvar::Cvar_GetBool( "log_file_timestamps" );
    logConfig.engineFile.bIncludeSourceLocation = cvar::Cvar_GetBool( "log_file_source" );
    logConfig.engineFile.bIncludeFunctionName = cvar::Cvar_GetBool( "log_file_function" );
    logConfig.engineFile.bColorEnabled = false;
    Host_CopyLogPathFromCvar( logConfig.engineFile.path, sizeof( logConfig.engineFile.path ), "log_engine_file_path", "CypherEngine.log" );

    logConfig.errorFile.enabled = cvar::Cvar_GetBool( "log_error_file" );
    logConfig.errorFile.nMinLevel = Host_LogLevelFromCvar( "log_error_file_level", logConfig.errorFile.nMinLevel );
    logConfig.errorFile.format = log::format_mode_t::DETAILED;
    logConfig.errorFile.bIncludeTimestamps = cvar::Cvar_GetBool( "log_file_timestamps" );
    logConfig.errorFile.bIncludeSourceLocation = cvar::Cvar_GetBool( "log_file_source" );
    logConfig.errorFile.bIncludeFunctionName = cvar::Cvar_GetBool( "log_file_function" );
    logConfig.errorFile.bColorEnabled = false;
    Host_CopyLogPathFromCvar( logConfig.errorFile.path, sizeof( logConfig.errorFile.path ), "log_error_file_path", "CypherEngine_errors.log" );

    logConfig.consoleFile.enabled = cvar::Cvar_GetBool( "log_console_file" );
    logConfig.consoleFile.nMinLevel = Host_LogLevelFromCvar( "log_console_file_level", logConfig.consoleFile.nMinLevel );
    logConfig.consoleFile.format = log::format_mode_t::COMPACT;
    logConfig.consoleFile.bIncludeTimestamps = true;
    logConfig.consoleFile.bIncludeSourceLocation = false;
    logConfig.consoleFile.bIncludeFunctionName = false;
    logConfig.consoleFile.bColorEnabled = false;
    Host_CopyLogPathFromCvar( logConfig.consoleFile.path, sizeof( logConfig.consoleFile.path ), "log_console_file_path", "Console.log" );

    logConfig.editorFile.enabled = cvar::Cvar_GetBool( "log_editor_file" );
    logConfig.editorFile.nMinLevel = Host_LogLevelFromCvar( "log_editor_file_level", logConfig.editorFile.nMinLevel );
    logConfig.editorFile.format = log::format_mode_t::DETAILED;
    logConfig.editorFile.bIncludeTimestamps = cvar::Cvar_GetBool( "log_file_timestamps" );
    logConfig.editorFile.bIncludeSourceLocation = cvar::Cvar_GetBool( "log_file_source" );
    logConfig.editorFile.bIncludeFunctionName = cvar::Cvar_GetBool( "log_file_function" );
    logConfig.editorFile.bColorEnabled = false;
    Host_CopyLogPathFromCvar( logConfig.editorFile.path, sizeof( logConfig.editorFile.path ), "log_editor_file_path", "Editor.log" );

    logConfig.gameFile.enabled = cvar::Cvar_GetBool( "log_game_file" );
    logConfig.gameFile.nMinLevel = Host_LogLevelFromCvar( "log_game_file_level", logConfig.gameFile.nMinLevel );
    logConfig.gameFile.format = log::format_mode_t::DETAILED;
    logConfig.gameFile.bIncludeTimestamps = cvar::Cvar_GetBool( "log_file_timestamps" );
    logConfig.gameFile.bIncludeSourceLocation = cvar::Cvar_GetBool( "log_file_source" );
    logConfig.gameFile.bIncludeFunctionName = cvar::Cvar_GetBool( "log_file_function" );
    logConfig.gameFile.bColorEnabled = false;
    Host_CopyLogPathFromCvar( logConfig.gameFile.path, sizeof( logConfig.gameFile.path ), "log_game_file_path", "Game.log" );

    const auto setResult = log::Log_SetConfig( logConfig );

    if ( setResult != log::log_error_t::OK ) {
        COM_ERRORF( log::Log_ErrorCode( setResult ), "Host_ApplyLogCvars: Log_SetConfig failed: %s", log::Log_ErrorDesc( setResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    Host_LogCvarRegistryReport();
    Host_LogLoggingReport( logConfig );

    return host_error_t::OK;
}

/*
================
Host_ApplyCvarsToConfig
================
*/
host_error_t Host_ApplyCvarsToConfig( state_t &pHostState ) {
    window_config_t &windowConfig = pHostState.config.windowConfig;
    render::render_config_t &renderConfig = pHostState.config.renderConfig;

    // Start from the renderer's canonical defaults on every startup pass, then
    // apply user policy. This prevents stale state after a failed/retried init.
    renderConfig = render::R_DefaultConfig();

    const common::u32 width = cvar::Cvar_GetInt( "r_width" );
    const common::u32 height = cvar::Cvar_GetInt( "r_height" );
    const common::u32 nTargetFps = cvar::Cvar_GetInt( "host_target_fps" );

    if ( width != 0u ) {
        windowConfig.viewport.width = width;
    }

    if ( height != 0u ) {
        windowConfig.viewport.height = height;
    }

    if ( nTargetFps != 0u ) {
        windowConfig.nTargetFps = nTargetFps;
    }

    windowConfig.fullscreen = cvar::Cvar_GetBool( "r_fullscreen" );
    renderConfig.presentMode = cvar::Cvar_GetBool( "r_vsync" )
        ? render::render_present_mode_t::FIFO
        : render::render_present_mode_t::IMMEDIATE;

    Host_LogSection( log::channel_t::HOST, "RUNTIME POLICY" );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Window",
        "%s  |  %ux%u logical  |  %s",
        windowConfig.title ? windowConfig.title : "<untitled>",
        windowConfig.viewport.width,
        windowConfig.viewport.height,
        windowConfig.fullscreen ? "borderless fullscreen" : "windowed" );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "Presentation",
        "%s  |  target %u FPS  (%.3f ms)",
        Host_PresentModeName( renderConfig.presentMode ),
        windowConfig.nTargetFps,
        windowConfig.nTargetFps > 0u ? 1000.0 / static_cast<double>( windowConfig.nTargetFps ) : 0.0 );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Simulation",
        "timescale %.3f  |  max delta %.3f s",
        static_cast<double>( cvar::Cvar_GetFloat( "host_timescale" ) ),
        static_cast<double>( cvar::Cvar_GetFloat( "host_max_delta_time" ) ) );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "Runtime flags",
        "developer %s  |  console visible %s  |  application %s",
        Host_BoolText( cvar::Cvar_GetBool( "developer" ) ),
        Host_BoolText( cvar::Cvar_GetBool( "con_show" ) ),
        cvar::Cvar_GetString( "sys_app_name" ) );

    return host_error_t::OK;
}

/*
================
Host_CreateWindow
================
*/
host_error_t Host_CreateWindow( state_t &pHostState )
{
    sys::window_desc_t windowDescription{};

    windowDescription.title        = pHostState.config.windowConfig.title;
    windowDescription.width        = pHostState.config.windowConfig.viewport.width;
    windowDescription.height       = pHostState.config.windowConfig.viewport.height;
    windowDescription.fullscreen   = pHostState.config.windowConfig.fullscreen;

    const auto windowResult = sys::Sys_CreateWindow( windowDescription, pHostState.window );
    if ( windowResult != sys::sys_error_t::OK ) {
        LOG_ERROR( log::channel_t::HOST, "window creation failed: %s.", sys::Sys_ErrorDesc( windowResult ) );
        COM_ERRORF( sys::Sys_ErrorCode( windowResult ), "Host_CreateWindow: Sys_CreateWindow failed: %s", sys::Sys_ErrorDesc( windowResult ) );
        return host_error_t::ERR_INITIALIZING;
    }

    Host_LogWindowAndDisplayReport( windowDescription, pHostState.window );

    return host_error_t::OK;
}

/*
================
Host_FinishInit
================
*/
host_error_t Host_FinishInit( state_t &pHostState ) {
    pHostState.running = true;
    pHostState.stage = stage_t::RUNNING;

    const common::f64 now = sys::Sys_TimeNowSeconds();

    pHostState.frame.nCurrentTimeSeconds = now;
    pHostState.frame.nPreviousTimeSeconds = now;

    return host_error_t::OK;
}

/*
================
Host_RequestShutdown
================
*/
void Host_RequestShutdown( state_t &pHostState )
{
    if ( pHostState.stage == stage_t::SHUTDOWN ) {
        return ;
    }

    pHostState.running = false;
    pHostState.stage = stage_t::SHUTTINGDOWN;

    LOG_INFO( log::channel_t::HOST, "shutdown requested." );

    return ;
}

/*
================
Host_Init

Main engine startup sequence.
================
*/
host_error_t Host_Init( state_t &pHostState ) {
    const auto startupBegin = std::chrono::steady_clock::now();
    auto stageBegin = startupBegin;
    host_startup_timings_t timings{};
    host_error_t result{};

    Host_PrepareStateForInit( pHostState );

    stageBegin = std::chrono::steady_clock::now();
    result = Host_InitCoreEngineSystems( pHostState );
    timings.coreServicesMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_MountFileSystem();
    timings.mountMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_RegisterBuiltinCvars();
    timings.cvarRegistrationMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_RegisterBuiltinCommands( pHostState );
    timings.commandRegistrationMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_LoadStartupConfig();
    timings.configLoadMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_ApplyLogCvars();
    timings.logApplyMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_ApplyCvarsToConfig( pHostState );
    timings.runtimePolicyMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_CreateWindow( pHostState );
    timings.windowMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    stageBegin = std::chrono::steady_clock::now();
    result = Host_FinishInit( pHostState );
    timings.finishMilliseconds = Host_ElapsedMilliseconds( stageBegin );
    if ( result != host_error_t::OK ) {
        Host_Shutdown( pHostState );
        return result;
    }

    const double startupMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startupBegin ).count();

    Host_LogSection( log::channel_t::HOST, "STARTUP SUMMARY" );
    Host_LogFileSystemStartupIo();
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Runtime services",
        "8 / 8 READY" );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "Core",
        "System | Logging | Memory | FileSystem" );
    HOST_LOG_DETAIL(
        log::channel_t::HOST,
        "Runtime",
        "Commands | CVars | Configuration | Window" );
    HOST_LOG_FIELD(
        log::channel_t::HOST,
        "Registries",
        "%u mounts  |  %u commands  |  %u CVars",
        fs::FS_MountCount(),
        cmd::Cmd_Count(),
        cvar::Cvar_Count() );
    LOG_INFO( log::channel_t::HOST, "  %-24s %12s", "Startup phase", "Elapsed" );
    LOG_INFO( log::channel_t::HOST, "  %-24s %12s", "------------------------", "------------" );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Core services", timings.coreServicesMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Mounts", timings.mountMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "CVar registration", timings.cvarRegistrationMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Command registration", timings.commandRegistrationMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Configuration load", timings.configLoadMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Logging policy", timings.logApplyMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Runtime policy", timings.runtimePolicyMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Window", timings.windowMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "Finish", timings.finishMilliseconds );
    LOG_INFO( log::channel_t::HOST, "  %-24s %9.3f ms", "TOTAL", startupMilliseconds );

    Host_LogRule( log::channel_t::HOST, '=' );
    LOG_INFO(
        log::channel_t::HOST,
        "  READY  |  %s  |  %s  |  %s  |  %.3f ms",
        common::COM_ENGINE_INFO.name,
        common::COM_GAME_INFO.name,
        ::cypher::common::Cy_BuildConfigGetName( ::cypher::common::Cy_BuildConfigGetCurrent() ),
        startupMilliseconds );
    LOG_INFO(
        log::channel_t::HOST,
        "  Window %ux%u logical  |  %ux%u drawable  |  %s",
        pHostState.window.logicalWidth,
        pHostState.window.logicalHeight,
        pHostState.window.width,
        pHostState.window.height,
        Host_WindowModeName( pHostState.window.mode ) );
    Host_LogRule( log::channel_t::HOST, '=' );

    return result;
}

/*
================
Host_Shutdown
================
*/
void Host_Shutdown( state_t &pHostState ) {
    LOG_INFO( log::channel_t::HOST, "shutdown begin: engine='%s'.", common::COM_ENGINE_INFO.name );

	pHostState.running = false;
	pHostState.stage = stage_t::SHUTDOWN;

    (void)sys::Sys_DestroyWindow( pHostState.window );
    cfg::Cfg_Shutdown();
    cvar::Cvar_Shutdown();
    cmd::Cmd_Shutdown();
    fs::FS_Shutdown();
    mem::Mem_Shutdown();
    LOG_INFO( log::channel_t::HOST, "shutdown complete: engine='%s'.", common::COM_ENGINE_INFO.name );
    log::Log_Shutdown();
    sys::Sys_Shutdown();
}

/*
================
Host_BeginFrame

Updates frame timing and opens frame-local memory.
================
*/
void Host_BeginFrame( state_t &pHostState ) {
	if ( pHostState.stage == stage_t::SHUTDOWN ) {
		return;
	}

    mem::Mem_BeginFrame();

	frame_t &frame = pHostState.frame;

	frame.nPreviousTimeSeconds = frame.nCurrentTimeSeconds;
	frame.nCurrentTimeSeconds = sys::Sys_TimeNowSeconds();

    const common::f32 nRawDeltaTimeSeconds = static_cast<common::f32>( frame.nCurrentTimeSeconds - frame.nPreviousTimeSeconds );

    const common::f32 nMaxDeltaTimeSeconds = cvar::Cvar_GetFloat( "host_max_delta_time" );
    const common::f32 timescale = cvar::Cvar_GetFloat( "host_timescale" );

    const common::f32 flSafeMaxDeltaTimeSeconds = ( nMaxDeltaTimeSeconds > 0.0f ) ? nMaxDeltaTimeSeconds : 0.25f;
    const common::f32 flSafeTimescale = ( timescale >= 0.0f ) ? timescale : 1.0f;

    frame.nDeltaTimeSeconds = ( nRawDeltaTimeSeconds > flSafeMaxDeltaTimeSeconds )   ? flSafeMaxDeltaTimeSeconds : nRawDeltaTimeSeconds;

    frame.nRealTimeSeconds += nRawDeltaTimeSeconds;

	if ( pHostState.stage == stage_t::RUNNING ) {
		frame.nSimulationTimeSeconds += frame.nDeltaTimeSeconds * flSafeTimescale;
	}

	frame.index++;
}

/*
================
Host_Update

Polls platform events and advances runtime systems.
================
*/
void Host_Update( state_t &pHostState ) {
	if ( pHostState.stage != stage_t::RUNNING ) {
		return;
	}

	if ( !pHostState.running ) {
		return;
	}

    sys::Sys_PollWindowEvents( pHostState.window );

    if ( sys::Sys_WindowShouldClose( pHostState.window ) ) {
        Host_RequestShutdown( pHostState );
        return ;
    }

    /*
    Future order:
    CypherHost_UpdateInput -> CypherHost_UpdateConsole -> CypherHost_UpdateGame -> CypherHost_UpdateAudio.
     */

}

/*
================
Host_Render
================
*/
void Host_Render( state_t &pHostState ) {
	if ( pHostState.stage != stage_t::RUNNING || !pHostState.running ) {
		return;
	}

    // The runtime renderer is intentionally absent while its backend contract is rebuilt.
}

/*
================
Host_EndFrame
================
*/
void Host_EndFrame( state_t &pHostState ) {
	if ( pHostState.stage == stage_t::SHUTDOWN ) {
		return;
	}

	if ( pHostState.stage == stage_t::SHUTTINGDOWN ) {
		pHostState.running = false;
		pHostState.stage = stage_t::SHUTDOWN;
	}

    mem::Mem_EndFrame();
}

/*
================
Host_IsRunning
================
*/
bool Host_IsRunning( const state_t &pHostState ) {
	return pHostState.running && ( pHostState.stage != stage_t::SHUTTINGDOWN && pHostState.stage != stage_t::SHUTDOWN );
}

}       // namespace cypher::engine::host
