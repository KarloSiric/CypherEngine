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
        benchmark::DoNotOptimize( Cy_ErrorMake( domain_t::COM_DOMAIN_TOOLS, 42u ) );
    }
}

void BM_Handle32MakeUnpack( benchmark::State &state )
{
    for ( auto _ : state ) {
        const handle32_t handle = Cy_Handle32_Make( 123u, 456u );
        benchmark::DoNotOptimize( Cy_Handle32_Unpack( handle ) );
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
    CommandLineBase_Set( &commandLine, 6, args );

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CommandLineBase_Find( &commandLine, "threads" ) );
    }
}

void BM_StatsSetGetU64( benchmark::State &state )
{
    stat_desc_t desc{};
    desc.pName = "bench.stat";
    desc.pCategory = "Bench";
    desc.pDescription = "Benchmark stat";
    desc.type = stat_value_type_t::U64;
    Cy_StatsRegister( desc );

    stat_value_t value{};
    u64 nCounter = 0u;
    for ( auto _ : state ) {
        Cy_StatsSetU64( "bench.stat", ++nCounter );
        benchmark::DoNotOptimize( Cy_StatsGet( "bench.stat", &value ) );
    }
}

void BM_MemoryTrackerRecordAllocFree( benchmark::State &state )
{
    i32 value = 0;
    memory_allocation_record_t record{};
    record.pMemory = &value;
    record.cbSize = sizeof( value );
    record.alignment = alignof( i32 );
    record.pTag = "bench";

    for ( auto _ : state ) {
        MemoryTracker_RecordAlloc( record );
        MemoryTracker_RecordFree( &value );
    }
}

void BM_CachePrefetchRead( benchmark::State &state )
{
    i32 value = 0;
    for ( auto _ : state ) {
        Cache_PrefetchRead( &value );
    }
}

void BM_CPUMonitoringSample( benchmark::State &state )
{
    cpu_monitor_sample_t sample{};
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( CPUMonitoring_Sample( &sample ) );
        benchmark::DoNotOptimize( sample.total_usage );
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
