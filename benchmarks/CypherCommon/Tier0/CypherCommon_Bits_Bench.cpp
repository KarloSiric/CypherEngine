//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Bits_Bench.cpp
//  Purpose: Benchmarks Bits Bench performance.
//  Details: This benchmark measures runtime cost for the corresponding low-level
//           path. Results should be treated as signals and compared across build
//           modes and platforms.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Bits.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

constexpr usize kValueCount = 1024u;

struct bits_bench_values_t {
    std::array<u32, kValueCount> nValues32{};
    std::array<u64, kValueCount> nValues64{};
};

bits_bench_values_t BuildValues()
{
    bits_bench_values_t values{};
    for ( usize i = 0u; i < kValueCount; ++i ) {
        values.nValues32[i] = static_cast<u32>( ( i * 2654435761u ) ^ ( i << 7u ) ^ 0xA5A5A5A5u );
        values.nValues64[i] = ( static_cast<u64>( values.nValues32[i] ) << 32u ) |
                               static_cast<u64>( values.nValues32[kValueCount - 1u - i] );
    }
    return values;
}

bits_bench_values_t &GetValues()
{
    static bits_bench_values_t values = BuildValues();
    return values;
}

void BM_PopCount32_1024Values( benchmark::State &state )
{
    bits_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        i32 nAccum = 0;
        for ( u32 nValue : values.nValues32 ) {
            nAccum += PopCount32( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_PopCount64_1024Values( benchmark::State &state )
{
    bits_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        i32 nAccum = 0;
        for ( u64 nValue : values.nValues64 ) {
            nAccum += PopCount64( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_FindLowestSetBit32_1024Values( benchmark::State &state )
{
    bits_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        i32 nAccum = 0;
        for ( u32 nValue : values.nValues32 ) {
            nAccum += FindLowestSetBit32( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_FindHighestSetBit32_1024Values( benchmark::State &state )
{
    bits_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        i32 nAccum = 0;
        for ( u32 nValue : values.nValues32 ) {
            nAccum += FindHighestSetBit32( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_NextPowerOfTwo32_1024Values( benchmark::State &state )
{
    bits_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( u32 nValue : values.nValues32 ) {
            nAccum += NextPowerOfTwo32( nValue & 0x00FFFFFFu );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_FlagHelpers32_1024Values( benchmark::State &state )
{
    bits_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( u32 nValue : values.nValues32 ) {
            nAccum ^= SetFlags( nValue, 0x0000000Fu );
            nAccum ^= ClearFlags( nValue, 0x000000F0u );
            nAccum ^= ToggleFlags( nValue, 0x00000F00u );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_RotateLeft32_1024Values( benchmark::State &state )
{
    bits_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( u32 nValue : values.nValues32 ) {
            nAccum ^= RotateLeft32( nValue, 13 );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

} // namespace

BENCHMARK( BM_PopCount32_1024Values );
BENCHMARK( BM_PopCount64_1024Values );
BENCHMARK( BM_FindLowestSetBit32_1024Values );
BENCHMARK( BM_FindHighestSetBit32_1024Values );
BENCHMARK( BM_NextPowerOfTwo32_1024Values );
BENCHMARK( BM_FlagHelpers32_1024Values );
BENCHMARK( BM_RotateLeft32_1024Values );
