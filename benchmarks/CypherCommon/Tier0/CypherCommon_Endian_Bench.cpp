//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier0/CypherCommon_Endian_Bench.cpp
//  Purpose: Benchmarks Endian Bench performance.
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

#include "CypherCommon_Endian.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

constexpr usize kValueCount = 1024u;

struct endian_bench_values_t {
    std::array<u16, kValueCount> nValues16{};
    std::array<u32, kValueCount> nValues32{};
    std::array<u64, kValueCount> nValues64{};
    std::array<char, kValueCount> chValues{};
};

endian_bench_values_t BuildValues()
{
    endian_bench_values_t values{};
    for ( usize i = 0u; i < kValueCount; ++i ) {
        values.nValues16[i] = static_cast<u16>( ( i * 251u ) ^ 0xA55Au );
        values.nValues32[i] = static_cast<u32>( ( i * 2654435761u ) ^ ( i << 11u ) ^ 0xC3C3C3C3u );
        values.nValues64[i] = ( static_cast<u64>( values.nValues32[i] ) << 32u ) |
                               static_cast<u64>( values.nValues32[kValueCount - 1u - i] );
        values.chValues[i] = static_cast<char>( 'A' + ( i % 26u ) );
    }
    return values;
}

endian_bench_values_t &GetValues()
{
    static endian_bench_values_t values = BuildValues();
    return values;
}

void BM_ByteSwap16_1024Values( benchmark::State &state )
{
    endian_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u16 nAccum = 0u;
        for ( u16 nValue : values.nValues16 ) {
            nAccum ^= Cy_ByteSwap16( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_ByteSwap32_1024Values( benchmark::State &state )
{
    endian_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( u32 nValue : values.nValues32 ) {
            nAccum ^= Cy_ByteSwap32( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_ByteSwap64_1024Values( benchmark::State &state )
{
    endian_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u64 nAccum = 0ull;
        for ( u64 nValue : values.nValues64 ) {
            nAccum ^= Cy_ByteSwap64( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_HostToLittle32_1024Values( benchmark::State &state )
{
    endian_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( u32 nValue : values.nValues32 ) {
            nAccum ^= Cy_HostToLittle32( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_HostToBig32_1024Values( benchmark::State &state )
{
    endian_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( u32 nValue : values.nValues32 ) {
            nAccum ^= Cy_HostToBig32( nValue );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_MakeFourCC_1024Values( benchmark::State &state )
{
    endian_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( usize i = 0u; i < kValueCount; i += 4u ) {
            nAccum ^= Cy_MakeFourCC( values.chValues[i],
                                  values.chValues[i + 1u],
                                  values.chValues[i + 2u],
                                  values.chValues[i + 3u] );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_FourCCChar_1024Values( benchmark::State &state )
{
    endian_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        u32 nAccum = 0u;
        for ( usize i = 0u; i < kValueCount; i += 4u ) {
            const u32 nFourCC = Cy_MakeFourCC( values.chValues[i],
                                            values.chValues[i + 1u],
                                            values.chValues[i + 2u],
                                            values.chValues[i + 3u] );
            nAccum ^= static_cast<u32>( static_cast<u8>( Cy_FourCCChar0( nFourCC ) ) );
            nAccum ^= static_cast<u32>( static_cast<u8>( Cy_FourCCChar1( nFourCC ) ) ) << 8u;
            nAccum ^= static_cast<u32>( static_cast<u8>( Cy_FourCCChar2( nFourCC ) ) ) << 16u;
            nAccum ^= static_cast<u32>( static_cast<u8>( Cy_FourCCChar3( nFourCC ) ) ) << 24u;
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

} // namespace

BENCHMARK( BM_ByteSwap16_1024Values );
BENCHMARK( BM_ByteSwap32_1024Values );
BENCHMARK( BM_ByteSwap64_1024Values );
BENCHMARK( BM_HostToLittle32_1024Values );
BENCHMARK( BM_HostToBig32_1024Values );
BENCHMARK( BM_MakeFourCC_1024Values );
BENCHMARK( BM_FourCCChar_1024Values );
