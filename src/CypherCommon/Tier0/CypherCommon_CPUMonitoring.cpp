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
#include "CypherCommon_Timer.h"

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
    bool_t isValid;
};

f32 ClampPercent( f64 value ) noexcept
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
u64 FileTimeToU64( const FILETIME &time ) noexcept
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

cpu_time_sample_t QueryCpuTimeSample() noexcept
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

process_time_sample_t QueryProcessTimeSample() noexcept
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
    sample.isValid = CY_TRUE;
    return sample;
}
#elif CYPHER_PLATFORM_LINUX
cpu_time_sample_t QueryCpuTimeSample() noexcept
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

process_time_sample_t QueryProcessTimeSample() noexcept
{
    struct rusage usage{};
    if ( getrusage( RUSAGE_SELF, &usage ) != 0 ) {
        return {};
    }

    const u64 nUserMicros = static_cast<u64>( usage.ru_utime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_utime.tv_usec );
    const u64 nSystemMicros = static_cast<u64>( usage.ru_stime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_stime.tv_usec );

    process_time_sample_t sample{};
    sample.nProcessTime = nUserMicros + nSystemMicros;
    sample.isValid = CY_TRUE;
    return sample;
}
#elif CYPHER_PLATFORM_MACOS
cpu_time_sample_t QueryCpuTimeSample() noexcept
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

process_time_sample_t QueryProcessTimeSample() noexcept
{
    struct rusage usage{};
    if ( getrusage( RUSAGE_SELF, &usage ) != 0 ) {
        return {};
    }

    const u64 nUserMicros = static_cast<u64>( usage.ru_utime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_utime.tv_usec );
    const u64 nSystemMicros = static_cast<u64>( usage.ru_stime.tv_sec ) * 1000000ull + static_cast<u64>( usage.ru_stime.tv_usec );

    process_time_sample_t sample{};
    sample.nProcessTime = nUserMicros + nSystemMicros;
    sample.isValid = CY_TRUE;
    return sample;
}
#else
cpu_time_sample_t QueryCpuTimeSample() noexcept
{
    return {};
}

process_time_sample_t QueryProcessTimeSample() noexcept
{
    return {};
}
#endif

f32 CalculateCpuUsage(
    u64 nPreviousIdle,
    u64 nPreviousTotal,
    const cpu_time_sample_t &current ) noexcept
{
    if ( !current.isValid || current.nTotal <= nPreviousTotal ) {
        return 0.0f;
    }

    const u64 nTotalDelta = current.nTotal - nPreviousTotal;
    const u64 nIdleDelta =
        current.nIdle > nPreviousIdle ? current.nIdle - nPreviousIdle : 0u;
    const u64 nBusyDelta = nTotalDelta > nIdleDelta ? nTotalDelta - nIdleDelta : 0u;

    return ClampPercent( ( static_cast<f64>( nBusyDelta ) * 100.0 ) / static_cast<f64>( nTotalDelta ) );
}

f32 CalculateProcessUsage(
    u64 nPreviousProcessTime,
    const process_time_sample_t &current,
    f64 intervalSeconds,
    u32 nLogicalThreadCount ) noexcept
{
    if ( !current.isValid ||
         current.nProcessTime <= nPreviousProcessTime ||
         intervalSeconds <= 0.0 ||
         nLogicalThreadCount == 0u ) {
        return 0.0f;
    }

#if CYPHER_PLATFORM_WINDOWS
    constexpr f64 PROCESS_TIME_UNITS_PER_SECOND = 10000000.0;
#else
    constexpr f64 PROCESS_TIME_UNITS_PER_SECOND = 1000000.0;
#endif

    const f64 processDeltaSeconds =
        static_cast<f64>( current.nProcessTime - nPreviousProcessTime ) /
        PROCESS_TIME_UNITS_PER_SECOND;
    const f64 machineSeconds =
        intervalSeconds * static_cast<f64>( nLogicalThreadCount );
    if ( machineSeconds <= 0.0 ) {
        return 0.0f;
    }

    return ClampPercent( ( processDeltaSeconds * 100.0 ) / machineSeconds );
}

} // namespace

bool_t Cy_CPUMonitorInit( cy_cpu_monitor_t *pMonitor ) noexcept
{
    if ( pMonitor == nullptr ) {
        return CY_FALSE;
    }

    const cpu_time_sample_t currentCpuTime = QueryCpuTimeSample();
    const process_time_sample_t currentProcessTime = QueryProcessTimeSample();
    const timer_tick_t nWallTicks = Cy_TimerNowTicks();

    *pMonitor = {};
    pMonitor->nPreviousIdleTicks = currentCpuTime.nIdle;
    pMonitor->nPreviousTotalTicks = currentCpuTime.nTotal;
    pMonitor->nPreviousProcessTicks = currentProcessTime.nProcessTime;
    pMonitor->nPreviousWallTicks = nWallTicks;
    pMonitor->hasSystemBaseline = currentCpuTime.isValid;
    pMonitor->hasProcessBaseline = currentProcessTime.isValid;
    pMonitor->isInitialized =
        currentCpuTime.isValid || currentProcessTime.isValid;
    return pMonitor->isInitialized;
}

bool_t Cy_CPUMonitorReset( cy_cpu_monitor_t *pMonitor ) noexcept
{
    return Cy_CPUMonitorInit( pMonitor );
}

bool_t Cy_CPUMonitorSample(
    cy_cpu_monitor_t *pMonitor,
    cy_cpu_monitor_sample_t *pOutSample ) noexcept
{
    if ( pOutSample == nullptr ) {
        return CY_FALSE;
    }
    *pOutSample = {};
    if ( pMonitor == nullptr || !pMonitor->isInitialized ) {
        return CY_FALSE;
    }

    const cpu_time_sample_t currentCpuTime = QueryCpuTimeSample();
    const process_time_sample_t currentProcessTime = QueryProcessTimeSample();
    const timer_tick_t nCurrentWallTicks = Cy_TimerNowTicks();
    const u32 nLogicalThreadCount = Cy_ThreadGetLogicalCount();
    const f64 intervalSeconds = Cy_TimerElapsedSeconds(
        pMonitor->nPreviousWallTicks,
        nCurrentWallTicks );

    pOutSample->nLogicalThreadCount = nLogicalThreadCount;
    pOutSample->intervalSeconds = intervalSeconds;
    pOutSample->hasSystemUsage =
        pMonitor->hasSystemBaseline &&
        currentCpuTime.isValid &&
        currentCpuTime.nTotal > pMonitor->nPreviousTotalTicks;
    pOutSample->hasProcessUsage =
        pMonitor->hasProcessBaseline &&
        currentProcessTime.isValid &&
        currentProcessTime.nProcessTime >= pMonitor->nPreviousProcessTicks &&
        intervalSeconds > 0.0;

    if ( pOutSample->hasSystemUsage ) {
        pOutSample->totalUsagePercent = CalculateCpuUsage(
            pMonitor->nPreviousIdleTicks,
            pMonitor->nPreviousTotalTicks,
            currentCpuTime );
    }
    if ( pOutSample->hasProcessUsage ) {
        pOutSample->processUsagePercent = CalculateProcessUsage(
            pMonitor->nPreviousProcessTicks,
            currentProcessTime,
            intervalSeconds,
            nLogicalThreadCount );
    }

    if ( currentCpuTime.isValid ) {
        pMonitor->nPreviousIdleTicks = currentCpuTime.nIdle;
        pMonitor->nPreviousTotalTicks = currentCpuTime.nTotal;
        pMonitor->hasSystemBaseline = CY_TRUE;
    }
    if ( currentProcessTime.isValid ) {
        pMonitor->nPreviousProcessTicks = currentProcessTime.nProcessTime;
        pMonitor->hasProcessBaseline = CY_TRUE;
    }
    pMonitor->nPreviousWallTicks = nCurrentWallTicks;

    return currentCpuTime.isValid || currentProcessTime.isValid;
}

} // namespace cypher::common
