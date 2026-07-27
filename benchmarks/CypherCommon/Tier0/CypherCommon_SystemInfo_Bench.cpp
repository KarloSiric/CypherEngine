//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_SystemInfo_Bench.cpp
//  Purpose: Benchmarks CypherCommon Tier0 system information paths.
//  Details: This benchmark separates cached snapshot access from live OS queries
//           so diagnostics can stay useful without accidentally becoming expensive.
//
//  History:
//  - Created by Karlo Siric on 2026-07-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SystemInfo.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;

namespace
{

void BM_SystemInfoGetCached( benchmark::State &state )
{
    if ( !Cy_SystemInfoInit() ) {
        state.SkipWithError( "system information initialization failed" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SystemInfoGet() );
    }
}

void BM_SystemInfoQueryMemoryStatus( benchmark::State &state )
{
    for ( auto _ : state ) {
        const cy_system_memory_status_t status = Cy_SystemInfoQueryMemoryStatus();
        u64 nTotal = status.totalPhysicalBytes;
        u64 nAvailable = status.availablePhysicalBytes;
        u64 nResident = status.processResidentBytes;
        u64 nVirtual = status.processVirtualBytes;
        benchmark::DoNotOptimize( nTotal );
        benchmark::DoNotOptimize( nAvailable );
        benchmark::DoNotOptimize( nResident );
        benchmark::DoNotOptimize( nVirtual );
    }
}

void BM_SystemInfoQueryDiskStatus( benchmark::State &state )
{
    for ( auto _ : state ) {
        const cy_system_disk_status_t status = Cy_SystemInfoQueryDiskStatus( "." );
        u64 nTotal = status.totalBytes;
        u64 nFree = status.freeBytes;
        u64 nAvailable = status.availableBytes;
        benchmark::DoNotOptimize( nTotal );
        benchmark::DoNotOptimize( nFree );
        benchmark::DoNotOptimize( nAvailable );
    }
}

void BM_SystemInfoFormatReport( benchmark::State &state )
{
    char szReport[CY_SYSTEMINFO_REPORT_MAX] = {};

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( Cy_SystemInfoFormatReport( szReport, sizeof( szReport ) ) );
        benchmark::ClobberMemory();
    }
}

} // namespace

BENCHMARK( BM_SystemInfoGetCached );
BENCHMARK( BM_SystemInfoQueryMemoryStatus );
BENCHMARK( BM_SystemInfoQueryDiskStatus );
BENCHMARK( BM_SystemInfoFormatReport );
