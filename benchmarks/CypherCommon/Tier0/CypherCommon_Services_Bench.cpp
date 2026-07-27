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
#include "CypherCommon/Tier0/CypherCommon_CommandLineBase.h"
#include "CypherCommon/Tier0/CypherCommon_CPUMonitoring.h"
#include "CypherCommon/Tier0/CypherCommon_Error.h"
#include "CypherCommon/Tier0/CypherCommon_Handle.h"
#include "CypherCommon/Tier0/CypherCommon_MemoryTracker.h"
#include "CypherCommon/Tier0/CypherCommon_Stats.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_ErrorMakeDomainCode( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_ErrorMake( error_domain_t::COM_DOMAIN_TOOLS, 42u ) );
    }
}

void BM_Handle32MakeUnpack( benchmark::State &state )
{
    for ( auto _ : state ) {
        const handle32_t handle = Cy_Handle32Make( 123u, 456u );
        benchmark::DoNotOptimize( Cy_Handle32Unpack( handle ) );
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

} // namespace

BENCHMARK( BM_ErrorMakeDomainCode );
BENCHMARK( BM_Handle32MakeUnpack );
BENCHMARK( BM_CommandLineFind );
BENCHMARK( BM_StatsSetGetU64 );
BENCHMARK( BM_MemoryTrackerRecordAllocFree );
BENCHMARK( BM_CachePrefetchRead );
BENCHMARK( BM_CPUMonitoringSample );
