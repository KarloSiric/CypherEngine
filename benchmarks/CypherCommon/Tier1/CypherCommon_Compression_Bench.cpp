//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Tier1/CypherCommon_Compression_Bench.cpp
//  Purpose: Benchmarks the production and internal compression codecs.
//  Details: Measures warm one-shot compression and decompression throughput over
//           representative mixed repetitive/binary data without allocation in-loop.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Compression.h"

#include <benchmark/benchmark.h>

#include <vector>

using namespace cypher::common;

namespace
{

std::vector<byte> MakeInput( usize cbSize )
{
    std::vector<byte> input( cbSize );
    for ( usize iByte = 0u; iByte < cbSize; ++iByte ) {
        input[iByte] = static_cast<byte>(
            ( iByte % 193u ) < 160u
                ? ( iByte / 23u ) & 0x1Fu
                : ( iByte * 157u ) & 0xFFu );
    }
    return input;
}

template <compression_codec_t codec>
void BM_Compress64KiB( benchmark::State &state )
{
    const std::vector<byte> input = MakeInput( 64u * CY_KIB );
    const compression_options_t options{
        codec == compression_codec_t::ZSTD ? 3 : 0,
        {},
        CY_FALSE
    };
    std::vector<byte> output(
        Compression_CompressBound( codec, input.size(), options ) );
    for ( auto _ : state ) {
        compression_result_t result = Compression_Compress(
            codec,
            { input.data(), input.size() },
            { output.data(), output.size() },
            options );
        benchmark::DoNotOptimize( result.cbWritten );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( input.size() ) );
}

template <compression_codec_t codec>
void BM_Decompress64KiB( benchmark::State &state )
{
    const std::vector<byte> input = MakeInput( 64u * CY_KIB );
    const compression_options_t options{
        codec == compression_codec_t::ZSTD ? 3 : 0,
        {},
        CY_FALSE
    };
    std::vector<byte> compressed(
        Compression_CompressBound( codec, input.size(), options ) );
    const compression_result_t encoded = Compression_Compress(
        codec,
        { input.data(), input.size() },
        { compressed.data(), compressed.size() },
        options );
    compressed.resize( encoded.cbWritten );
    std::vector<byte> output( input.size() );

    for ( auto _ : state ) {
        compression_result_t result = Compression_Decompress(
            codec,
            { compressed.data(), compressed.size() },
            { output.data(), output.size() },
            options );
        benchmark::DoNotOptimize( result.cbWritten );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( input.size() ) );
}

} // namespace

BENCHMARK_TEMPLATE( BM_Compress64KiB, compression_codec_t::CYPHER_LZ );
BENCHMARK_TEMPLATE( BM_Compress64KiB, compression_codec_t::LZ4 );
BENCHMARK_TEMPLATE( BM_Compress64KiB, compression_codec_t::ZSTD );
BENCHMARK_TEMPLATE( BM_Decompress64KiB, compression_codec_t::CYPHER_LZ );
BENCHMARK_TEMPLATE( BM_Decompress64KiB, compression_codec_t::LZ4 );
BENCHMARK_TEMPLATE( BM_Decompress64KiB, compression_codec_t::ZSTD );
