//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Services_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 service helper costs.
//  Details: These measurements cover small metadata, diagnostics, command-line,
//           stats, and memory tracking helpers used by tools and runtime code.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/Tier0/CypherCommon_CacheHints.h"
#include "CypherCommon/Tier0/CypherCommon_BuildId.h"
#include "CypherCommon/Tier0/CypherCommon_CommandLineBase.h"
#include "CypherCommon/Tier0/CypherCommon_CPUMonitoring.h"
#include "CypherCommon/Tier0/CypherCommon_DynamicLibrary.h"
#include "CypherCommon/Tier0/CypherCommon_Environment.h"
#include "CypherCommon/Tier0/CypherCommon_Error.h"
#include "CypherCommon/Tier0/CypherCommon_Handle.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier0/CypherCommon_LogToggle.h"
#include "CypherCommon/Tier0/CypherCommon_MemoryDebug.h"
#include "CypherCommon/Tier0/CypherCommon_MemoryTracker.h"
#include "CypherCommon/Tier0/CypherCommon_Module.h"
#include "CypherCommon/Tier0/CypherCommon_PageAllocator.h"
#include "CypherCommon/Tier0/CypherCommon_PerformanceCounter.h"
#include "CypherCommon/Tier0/CypherCommon_PlatformMemory.h"
#include "CypherCommon/Tier0/CypherCommon_Process.h"
#include "CypherCommon/Tier0/CypherCommon_Profile.h"
#include "CypherCommon/Tier0/CypherCommon_Stats.h"
#include "CypherCommon/Tier0/CypherCommon_SourceLocation.h"
#include "CypherCommon/Tier0/CypherCommon_TsList.h"
#include "CypherCommon/Tier0/CypherCommon_Validator.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void DiscardLog( const log_record_t &, void * ) noexcept
{
}

void DiscardMemoryDebug( const memory_debug_record_t &, void * ) noexcept
{
}

void DiscardProfileEvent( const profile_event_t &, void * ) noexcept
{
}

void DiscardValidation( const validation_record_t &, void * ) noexcept
{
}

void BM_ErrorMakeDomainCode( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_ErrorMake( error_domain_t::COM_DOMAIN_TOOLS, 42u ) );
    }
}

void BM_ErrorTableLookup( benchmark::State &state )
{
    const error_table_t *pTable = Cy_CommonErrorTable();
    const error_code_t errorCode = Cy_ErrorMake( common_error_t::ERR_VERSION_MISMATCH );

    for ( auto _ : state ) {
        const error_description_t *pDescription = Cy_ErrorFindDesc( *pTable, errorCode );
        benchmark::DoNotOptimize( pDescription );
    }
}

void BM_Handle32MakeUnpack( benchmark::State &state )
{
    for ( auto _ : state ) {
        const handle32_t handle = Cy_Handle32Make( 123u, 456u );
        benchmark::DoNotOptimize( Cy_Handle32Unpack( handle ) );
    }
}

void BM_BuildIdFormat( benchmark::State &state )
{
    const build_id_t *pBuildId = Cy_BuildIdGetEngine();
    char szText[512]{};
    for ( auto _ : state ) {
        usize cchWritten = Cy_BuildIdFormat(
            pBuildId,
            szText,
            sizeof( szText ) );
        benchmark::DoNotOptimize( cchWritten );
        benchmark::DoNotOptimize( szText );
        benchmark::ClobberMemory();
    }
}

void BM_SourceLocationFormat( benchmark::State &state )
{
    const source_location_t location = CY_SOURCE_LOCATION;
    char szText[1024]{};
    for ( auto _ : state ) {
        usize cchWritten = Cy_SourceLocation_Format(
            location,
            szText,
            sizeof( szText ) );
        benchmark::DoNotOptimize( cchWritten );
        benchmark::DoNotOptimize( szText );
        benchmark::ClobberMemory();
    }
}

void BM_CommandLineFind( benchmark::State &state )
{
    const char *args[] = {
        "tool",
        "-game",
        "reap",
        "--map=arena01",
        "-threads",
        "8"
    };

    command_line_base_t commandLine{};
    if ( !Cy_CommandLineBaseSet( &commandLine, 6, args ) ) {
        state.SkipWithError( "command line setup failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            Cy_CommandLineBaseFindValue( &commandLine, "threads" ) );
    }
}

void BM_StatsSetGetU64( benchmark::State &state )
{
    stat_desc_t desc{};
    desc.pszName = "bench.stat";
    desc.pszCategory = "Bench";
    desc.pszDescription = "Benchmark stat";
    desc.type = stat_value_type_t::U64;
    stat_id_t statId = CY_STAT_ID_INVALID;
    if ( !Cy_StatsRegister( desc, &statId ) ) {
        state.SkipWithError( "stat registration failed" );
        return;
    }

    stat_value_t value{};
    u64 nCounter = 0u;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_StatsSetU64( statId, ++nCounter ) );
        benchmark::DoNotOptimize( Cy_StatsGet( statId, &value ) );
    }
}

void BM_MemoryTrackerRecordAllocFree( benchmark::State &state )
{
    i32 value = 0;
    memory_allocation_record_t record{};
    record.pMemory = &value;
    record.nByteCount = sizeof( value );
    record.nAlignment = alignof( i32 );
    record.pszTag = "bench";

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_MemoryTrackerRecordAlloc( record ) );
        benchmark::DoNotOptimize( Cy_MemoryTrackerRecordFree( &value ) );
    }
}

void BM_CachePrefetchRead( benchmark::State &state )
{
    i32 value = 0;
    for ( auto _ : state ) {
        Cy_CachePrefetchRead( &value );
    }
}

void BM_CPUMonitoringSample( benchmark::State &state )
{
    cy_cpu_monitor_t monitor{};
    if ( !Cy_CPUMonitorInit( &monitor ) ) {
        state.SkipWithError( "CPU monitoring is unavailable" );
        return;
    }

    cy_cpu_monitor_sample_t sample{};
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_CPUMonitorSample( &monitor, &sample ) );
        benchmark::DoNotOptimize( sample.totalUsagePercent );
    }
}

void BM_DynamicLibraryStateQuery( benchmark::State &state )
{
    dynamic_library_t library{};
    if ( !Cy_DynamicLibraryInit( &library ) ) {
        state.SkipWithError( "dynamic library initialization failed" );
        return;
    }
    for ( auto _ : state ) {
        bool_t isLoaded = Cy_DynamicLibraryIsLoaded( &library );
        benchmark::DoNotOptimize( isLoaded );
    }
}

void BM_EnvironmentGetPath( benchmark::State &state )
{
    char szValue[4096]{};
    const cy_environment_get_result_t probe =
        Cy_EnvironmentGet( "PATH", nullptr, 0u );
    if ( !probe.exists || probe.cchRequired >= sizeof( szValue ) ) {
        state.SkipWithError( "PATH is unavailable or too large" );
        return;
    }
    for ( auto _ : state ) {
        cy_environment_get_result_t result =
            Cy_EnvironmentGet( "PATH", szValue, sizeof( szValue ) );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( szValue );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        state.iterations() *
        static_cast<benchmark::IterationCount>( probe.cchRequired ) );
}

void BM_LogCallbackDispatch( benchmark::State &state )
{
    Cy_LogSetCallback( DiscardLog, nullptr );
    Cy_LogToggleReset();
    for ( auto _ : state ) {
        Cy_LogWrite(
            log_level_t::Debug, log_channel_t::Common,
            "benchmark log record" );
    }
    Cy_LogSetCallback( nullptr, nullptr );
}

void BM_LogToggleMaskQuery( benchmark::State &state )
{
    const log_category_mask_t mask =
        Cy_LogChannelMask( log_channel_t::Render ) |
        Cy_LogChannelMask( log_channel_t::FileSystem );
    Cy_LogToggleSetMask( mask );
    for ( auto _ : state ) {
        bool_t enabled = Cy_LogToggleAllEnabled( mask );
        benchmark::DoNotOptimize( enabled );
    }
    Cy_LogToggleReset();
}

void BM_MemoryDebugCallbackDispatch( benchmark::State &state )
{
    i32 value = 0;
    const memory_debug_record_t record{
        memory_debug_event_t::Alloc,
        &value,
        sizeof( value ),
        alignof( i32 ),
        "benchmark",
        __FILE__,
        static_cast<u32>( __LINE__ )
    };
    Cy_MemoryDebugSetCallback( DiscardMemoryDebug, nullptr );
    for ( auto _ : state ) {
        Cy_MemoryDebugReportEvent( record );
    }
    Cy_MemoryDebugSetCallback( nullptr );
}

void BM_ModuleDescriptorValidation( benchmark::State &state )
{
    const module_desc_t descriptor{
        "Renderer",
        "CypherRenderer",
        "Benchmark module",
        { 1u, 2u, 3u, 4u },
        7u
    };
    for ( auto _ : state ) {
        bool_t valid = Cy_ModuleDescriptorIsValid( &descriptor );
        benchmark::DoNotOptimize( valid );
    }
}

void BM_PageAllocatorCommitReset( benchmark::State &state )
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    page_allocator_t allocator{};
    if ( info.nPageSize == 0u ||
         !Cy_PageAllocatorInit( &allocator, info.nPageSize ) ) {
        state.SkipWithError( "page allocator initialization failed" );
        return;
    }
    for ( auto _ : state ) {
        void *pMemory = Cy_PageAllocatorCommit( &allocator, 1u );
        bool_t reset = Cy_PageAllocatorReset( &allocator );
        benchmark::DoNotOptimize( pMemory );
        benchmark::DoNotOptimize( reset );
    }
    if ( !Cy_PageAllocatorShutdown( &allocator ) ) {
        state.SkipWithError( "page allocator shutdown failed" );
    }
}

void BM_PerformanceCounterNow( benchmark::State &state )
{
    const u64 frequency = Cy_PerformanceCounterFrequency();
    if ( frequency == 0u ) {
        state.SkipWithError( "performance counter frequency is unavailable" );
        return;
    }
    for ( auto _ : state ) {
        performance_counter_t counter = Cy_PerformanceCounterNow();
        f64 seconds = Cy_PerformanceCounterToSeconds( counter % frequency );
        benchmark::DoNotOptimize( counter );
        benchmark::DoNotOptimize( seconds );
    }
}

void BM_PlatformMemoryReserveCommitRelease( benchmark::State &state )
{
    const platform_memory_info_t info = Cy_PlatformMemoryGetInfo();
    if ( info.nPageSize == 0u ) {
        state.SkipWithError( "platform page size is unavailable" );
        return;
    }
    for ( auto _ : state ) {
        void *pMemory = Cy_PlatformMemoryReserve( info.nPageSize );
        bool_t committed = pMemory != nullptr &&
            Cy_PlatformMemoryCommit( pMemory, info.nPageSize );
        bool_t decommitted = committed &&
            Cy_PlatformMemoryDecommit( pMemory, info.nPageSize );
        bool_t released = pMemory != nullptr &&
            Cy_PlatformMemoryRelease( pMemory, info.nPageSize );
        benchmark::DoNotOptimize( committed );
        benchmark::DoNotOptimize( decommitted );
        benchmark::DoNotOptimize( released );
    }
}

void BM_ProcessMetadata( benchmark::State &state )
{
    for ( auto _ : state ) {
        process_id_t processId = Cy_ProcessGetCurrentId();
        const char *pPath = Cy_ProcessGetExecutablePath();
        benchmark::DoNotOptimize( processId );
        benchmark::DoNotOptimize( pPath );
    }
}

void BM_ProfileZoneDispatch( benchmark::State &state )
{
    const profile_zone_desc_t zone{
        "BenchmarkZone",
        "Benchmark",
        CY_SOURCE_LOCATION,
        PROFILE_FLAG_CPU
    };
    Cy_ProfileResetState();
    Cy_ProfileSetSink( DiscardProfileEvent, nullptr );
    Cy_ProfileSetEnabled( CY_TRUE );
    for ( auto _ : state ) {
        profile_token_t token = Cy_ProfileBeginZone( &zone );
        bool_t ended = Cy_ProfileEndZone( token );
        benchmark::DoNotOptimize( token );
        benchmark::DoNotOptimize( ended );
    }
    Cy_ProfileSetEnabled( CY_FALSE );
    Cy_ProfileSetSink( nullptr, nullptr );
    Cy_ProfileResetState();
}

void BM_TsListPushPop( benchmark::State &state )
{
    tslist_t list{};
    tslist_node_t node{};
    if ( !Cy_TsListInit( &list ) ) {
        state.SkipWithError( "thread-safe list initialization failed" );
        return;
    }
    for ( auto _ : state ) {
        bool_t pushed = Cy_TsListPush( &list, &node );
        tslist_node_t *pPopped = Cy_TsListPop( &list );
        benchmark::DoNotOptimize( pushed );
        benchmark::DoNotOptimize( pPopped );
    }
    if ( !Cy_TsListShutdown( &list ) ) {
        state.SkipWithError( "thread-safe list shutdown failed" );
    }
}

void BM_ValidatorCallbackDispatch( benchmark::State &state )
{
    Cy_ValidatorSetCallback( DiscardValidation, nullptr );
    for ( auto _ : state ) {
        Cy_ValidatorReport(
            validator_severity_t::Info,
            "benchmark validation record" );
    }
    Cy_ValidatorSetCallback( nullptr, nullptr );
}

} // namespace

BENCHMARK( BM_ErrorMakeDomainCode );
BENCHMARK( BM_ErrorTableLookup );
BENCHMARK( BM_Handle32MakeUnpack );
BENCHMARK( BM_BuildIdFormat );
BENCHMARK( BM_SourceLocationFormat );
BENCHMARK( BM_CommandLineFind );
BENCHMARK( BM_StatsSetGetU64 );
BENCHMARK( BM_MemoryTrackerRecordAllocFree );
BENCHMARK( BM_CachePrefetchRead );
BENCHMARK( BM_CPUMonitoringSample );
BENCHMARK( BM_DynamicLibraryStateQuery );
BENCHMARK( BM_EnvironmentGetPath );
BENCHMARK( BM_LogCallbackDispatch );
BENCHMARK( BM_LogToggleMaskQuery );
BENCHMARK( BM_MemoryDebugCallbackDispatch );
BENCHMARK( BM_ModuleDescriptorValidation );
BENCHMARK( BM_PageAllocatorCommitReset );
BENCHMARK( BM_PerformanceCounterNow );
BENCHMARK( BM_PlatformMemoryReserveCommitRelease );
BENCHMARK( BM_ProcessMetadata );
BENCHMARK( BM_ProfileZoneDispatch );
BENCHMARK( BM_TsListPushPop );
BENCHMARK( BM_ValidatorCallbackDispatch );
