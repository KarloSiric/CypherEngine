//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Security/CypherSecurity_Random_Bench.cpp
//  Purpose: Benchmarks cryptographically secure random-data services.
//  Details: Byte throughput and scalar sampling costs are measured separately,
//           including the high-rejection case for bounded u64 generation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"

#include <benchmark/benchmark.h>

#include <array>
#include <limits>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

std::array<byte, 1u << 20u> g_randomOutput{};

bool EnsureSecurityReady( benchmark::State &state )
{
    if ( Security_Init() == security_status_t::OK ) {
        return true;
    }
    state.SkipWithError( "Unable to initialize CypherSecurity." );
    return false;
}

} // namespace

static void BM_SecurityRandom_Fill( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    const usize cbOutput = static_cast<usize>( state.range( 0 ) );
    for ( auto _ : state ) {
        security_status_t result = SecurityRandom_Fill(
            g_randomOutput.data(),
            cbOutput );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbOutput ) );
}

static void BM_SecurityRandom_U32( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    for ( auto _ : state ) {
        u32 nValue = 0u;
        security_status_t result = SecurityRandom_U32( &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }
}

static void BM_SecurityRandom_U64( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    for ( auto _ : state ) {
        u64 nValue = 0u;
        security_status_t result = SecurityRandom_U64( &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }
}

static void BM_SecurityRandom_UniformU32( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    const u32 nBound = static_cast<u32>( state.range( 0 ) );
    for ( auto _ : state ) {
        u32 nValue = 0u;
        security_status_t result =
            SecurityRandom_UniformU32( nBound, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }
}

static void BM_SecurityRandom_UniformU64Common( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    constexpr u64 nBound = 10u;
    for ( auto _ : state ) {
        u64 nValue = 0u;
        security_status_t result =
            SecurityRandom_UniformU64( nBound, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }
}

static void BM_SecurityRandom_UniformU64HighRejection(
    benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    constexpr u64 nBound = 0x8000000000000001ull;
    for ( auto _ : state ) {
        u64 nValue = 0u;
        security_status_t result =
            SecurityRandom_UniformU64( nBound, &nValue );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( nValue );
    }
}

BENCHMARK( BM_SecurityRandom_Fill )
    ->Arg( 16 )
    ->Arg( 32 )
    ->Arg( 256 )
    ->Arg( 4096 )
    ->Arg( 1 << 20 );
BENCHMARK( BM_SecurityRandom_U32 );
BENCHMARK( BM_SecurityRandom_U64 );
BENCHMARK( BM_SecurityRandom_UniformU32 )
    ->Arg( 10 )
    ->Arg( 1000 )
    ->Arg( std::numeric_limits<u32>::max() );
BENCHMARK( BM_SecurityRandom_UniformU64Common );
BENCHMARK( BM_SecurityRandom_UniformU64HighRejection );
