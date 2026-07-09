//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CPUMonitoring.cpp
//  Purpose: Implements CypherCommon Tier0 CPU monitoring samples.
//  Details: This sampler reports topology plus delta-based system and process
//           CPU percentages where the host platform exposes suitable counters.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CPUMonitoring.h"

#include "CypherCommon_Platform.h"
#include "CypherCommon_Thread.h"

#include <chrono>
#include <mutex>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif CYPHER_PLATFORM_LINUX
    #include <cstdio>
    #include <sys/resource.h>
#elif CYPHER_PLATFORM_MACOS
    #include <mach/mach.h>
    #include <sys/resource.h>
#endif

namespace cypher::common
{

namespace
{

struct cpu_time_sample_t {
    u64 nIdle;
    u64 nTotal;
    bool_t isValid;
};

struct process_time_sample_t {
    u64 nProcessTime;
    std::chrono::steady_clock::time_point wallTime;
    bool_t isValid;
};

std::mutex g_cpuMonitorMutex;
cpu_time_sample_t g_previousCpuTime{};
process_time_sample_t g_previousProcessTime{};
bool_t g_hasPreviousCpuTime = CY_FALSE;
bool_t g_hasPreviousProcessTime = CY_FALSE;

f32 ClampPercent( f64 value )
{
    if ( value < 0.0 ) {
        return 0.0f;
    }
    if ( value > 100.0 ) {
        return 100.0f;
    }
    return static_cast<f32>( value );
}

#if CYPHER_PLATFORM_WINDOWS
u64 FileTimeToU64( const FILETIME &time )
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

cpu_time_sample_t QueryCpuTimeSample()
{
    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if ( !GetSystemTimes( &idleTime, &kernelTime, &userTime ) ) {
        return {};
    }

    const u64 nIdle = FileTimeToU64( idleTime );
    const u64 nKernel = FileTimeToU64( kernelTime );
    const u64 nUser = FileTimeToU64( userTime );

    cpu_time_sample_t sample{};
    sample.nIdle = nIdle;
    sample.nTotal = nKernel + nUser;
    sample.isValid = CY_TRUE;
    return sample;
}

process_time_sample_t QueryProcessTimeSample()
{
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if ( !GetProcessTimes( GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime ) ) {
        return {};
    }

    process_time_sample_t sample{};
    sample.nProcessTime = FileTimeToU64( kernelTime ) + FileTimeToU64( userTime );
    sample.wallTime = std::chrono::steady_clock::now();
    sample.isValid = CY_TRUE;
    return sample;
}
#elif CYPHER_PLATFORM_LINUX
cpu_time_sample_t QueryCpuTimeSample()
{
    FILE *pFile = std::fopen( "/proc/stat", "r" );
    if ( pFile == nullptr ) {
        return {};
    }

    unsigned long long nUser = 0u;
    unsigned long long nNice = 0u;
    unsigned long long nSystem = 0u;
    unsigned long long nIdle = 0u;
    unsigned long long nIoWait = 0u;
    unsigned long long nIrq = 0u;
    unsigned long long nSoftIrq = 0u;
    unsigned long long nSteal = 0u;

    const int cParsed = std::fscanf(
        pFile,
        "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &nUser,
        &nNice,
        &nSystem,
        &nIdle,
        &nIoWait,
        &nIrq,
        &nSoftIrq,
        &nSteal );
    std::fclose( pFile );

    if ( cParsed < 4 ) {
        return {};
    }

    cpu_time_sample_t sample{};
    sample.nIdle = static_cast<u64>( nIdle + nIoWait );
    sample.nTotal = static_cast<u64>( nUser + nNice + nSystem + nIdle + nIoWait + nIrq + nSoftIrq + nSteal );
    sample.isValid = CY_TRUE;
    return sample;
}

process_time_sample_t QueryProcessTimeSample()
{
    struct rusage usage{};
    if ( getrusage( RUSAGE_SELF, &usage ) != 0 ) {
        return {};
    }

    const u64 nUserMicros = static_cast<u64>( usage.ru_utime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_utime.tv_usec );
    const u64 nSystemMicros = static_cast<u64>( usage.ru_stime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_stime.tv_usec );

    process_time_sample_t sample{};
    sample.nProcessTime = nUserMicros + nSystemMicros;
    sample.wallTime = std::chrono::steady_clock::now();
    sample.isValid = CY_TRUE;
    return sample;
}
#elif CYPHER_PLATFORM_MACOS
cpu_time_sample_t QueryCpuTimeSample()
{
    host_cpu_load_info_data_t cpuInfo{};
    mach_msg_type_number_t cInfo = HOST_CPU_LOAD_INFO_COUNT;
    const kern_return_t result = host_statistics(
        mach_host_self(),
        HOST_CPU_LOAD_INFO,
        reinterpret_cast<host_info_t>( &cpuInfo ),
        &cInfo );
    if ( result != KERN_SUCCESS ) {
        return {};
    }

    const u64 nUser = static_cast<u64>( cpuInfo.cpu_ticks[CPU_STATE_USER] );
    const u64 nSystem = static_cast<u64>( cpuInfo.cpu_ticks[CPU_STATE_SYSTEM] );
    const u64 nIdle = static_cast<u64>( cpuInfo.cpu_ticks[CPU_STATE_IDLE] );
    const u64 nNice = static_cast<u64>( cpuInfo.cpu_ticks[CPU_STATE_NICE] );

    cpu_time_sample_t sample{};
    sample.nIdle = nIdle;
    sample.nTotal = nUser + nSystem + nIdle + nNice;
    sample.isValid = CY_TRUE;
    return sample;
}

process_time_sample_t QueryProcessTimeSample()
{
    struct rusage usage{};
    if ( getrusage( RUSAGE_SELF, &usage ) != 0 ) {
        return {};
    }

    const u64 nUserMicros = static_cast<u64>( usage.ru_utime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_utime.tv_usec );
    const u64 nSystemMicros = static_cast<u64>( usage.ru_stime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_stime.tv_usec );

    process_time_sample_t sample{};
    sample.nProcessTime = nUserMicros + nSystemMicros;
    sample.wallTime = std::chrono::steady_clock::now();
    sample.isValid = CY_TRUE;
    return sample;
}
#else
cpu_time_sample_t QueryCpuTimeSample()
{
    return {};
}

process_time_sample_t QueryProcessTimeSample()
{
    return {};
}
#endif

f32 CalculateCpuUsage( const cpu_time_sample_t &previous, const cpu_time_sample_t &current )
{
    if ( !previous.isValid || !current.isValid || current.nTotal <= previous.nTotal ) {
        return 0.0f;
    }

    const u64 nTotalDelta = current.nTotal - previous.nTotal;
    const u64 nIdleDelta = current.nIdle > previous.nIdle ? current.nIdle - previous.nIdle : 0u;
    const u64 nBusyDelta = nTotalDelta > nIdleDelta ? nTotalDelta - nIdleDelta : 0u;

    return ClampPercent( ( static_cast<f64>( nBusyDelta ) * 100.0 ) / static_cast<f64>( nTotalDelta ) );
}

f32 CalculateProcessUsage( const process_time_sample_t &previous, const process_time_sample_t &current, u32 cLogicalThreads )
{
    if ( !previous.isValid || !current.isValid || current.nProcessTime <= previous.nProcessTime || cLogicalThreads == 0u ) {
        return 0.0f;
    }

    const std::chrono::duration<f64> wallDelta = current.wallTime - previous.wallTime;
    if ( wallDelta.count() <= 0.0 ) {
        return 0.0f;
    }

#if CYPHER_PLATFORM_WINDOWS
    constexpr f64 PROCESS_TIME_UNITS_PER_SECOND = 10000000.0;
#else
    constexpr f64 PROCESS_TIME_UNITS_PER_SECOND = 1000000.0;
#endif

    const f64 processDeltaSeconds = static_cast<f64>( current.nProcessTime - previous.nProcessTime ) / PROCESS_TIME_UNITS_PER_SECOND;
    const f64 machineSeconds = wallDelta.count() * static_cast<f64>( cLogicalThreads );
    if ( machineSeconds <= 0.0 ) {
        return 0.0f;
    }

    return ClampPercent( ( processDeltaSeconds * 100.0 ) / machineSeconds );
}

} // namespace

bool_t CPUMonitoring_Sample( cpu_monitor_sample_t *pOutSample )
{
    if ( pOutSample == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( g_cpuMonitorMutex );

    const u32 cLogicalThreads = Cy_ThreadGetLogicalCount();
    const cpu_time_sample_t currentCpuTime = QueryCpuTimeSample();
    const process_time_sample_t currentProcessTime = QueryProcessTimeSample();

    pOutSample->logical_thread_count = cLogicalThreads;
    pOutSample->total_usage = g_hasPreviousCpuTime ? CalculateCpuUsage( g_previousCpuTime, currentCpuTime ) : 0.0f;
    pOutSample->process_usage = g_hasPreviousProcessTime ? CalculateProcessUsage( g_previousProcessTime, currentProcessTime, cLogicalThreads ) : 0.0f;

    if ( currentCpuTime.isValid ) {
        g_previousCpuTime = currentCpuTime;
        g_hasPreviousCpuTime = CY_TRUE;
    }
    if ( currentProcessTime.isValid ) {
        g_previousProcessTime = currentProcessTime;
        g_hasPreviousProcessTime = CY_TRUE;
    }

    return CY_TRUE;
}

} // namespace cypher::common
