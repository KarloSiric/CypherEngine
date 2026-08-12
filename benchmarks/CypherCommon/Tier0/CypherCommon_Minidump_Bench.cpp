//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Minidump_Bench.cpp
//  Purpose: Benchmarks Tier0 portable diagnostic-dump services.
//  Details: Measures configured-path lookup and complete portable report writes.
//           Dump creation is an OS-I/O workload and therefore uses real time.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Minidump.h"

#include <benchmark/benchmark.h>

#include <chrono>
#include <filesystem>
#include <string>

using namespace cypher::common;

namespace
{

struct benchmark_dump_path_t {
    benchmark_dump_path_t()
    {
        const auto nUnique = std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
        path = std::filesystem::temp_directory_path() /
            ( "CypherCommon_Minidump_Bench_" +
              std::to_string( nUnique ) + ".txt" );
        native = path.string();
    }

    ~benchmark_dump_path_t()
    {
        std::error_code error;
        std::filesystem::remove( path, error );
    }

    std::filesystem::path path{};
    std::string native{};
};

void BM_Minidump_OutputPathQuery( benchmark::State &state )
{
    benchmark_dump_path_t output{};
    if ( Cy_MinidumpSetOutputPath( output.native.c_str() ) !=
         minidump_result_t::Ok ) {
        state.SkipWithError( "Unable to set benchmark dump path." );
        return;
    }

    char szPath[1024]{};
    for ( auto _ : state ) {
        usize cchRequired = Cy_MinidumpGetOutputPath(
            szPath,
            sizeof( szPath ) );
        benchmark::DoNotOptimize( cchRequired );
        benchmark::DoNotOptimize( szPath );
        benchmark::ClobberMemory();
    }
    static_cast<void>( Cy_MinidumpSetOutputPath( nullptr ) );
}

void BM_Minidump_WritePortableReport( benchmark::State &state )
{
    benchmark_dump_path_t output{};
    const minidump_info_t info{
        "CypherBenchmark",
        "1.0.0",
        output.native.c_str(),
        "Synthetic benchmark report",
        CY_SOURCE_LOCATION,
        8u,
        1u
    };

    for ( auto _ : state ) {
        minidump_result_t result = Cy_MinidumpWrite( info );
        benchmark::DoNotOptimize( result );
        if ( result != minidump_result_t::Ok ) {
            state.SkipWithError( Cy_MinidumpResultName( result ) );
            break;
        }
    }
    state.SetItemsProcessed( static_cast<i64>( state.iterations() ) );
}

} // namespace

BENCHMARK( BM_Minidump_OutputPathQuery );
BENCHMARK( BM_Minidump_WritePortableReport )->UseRealTime();
