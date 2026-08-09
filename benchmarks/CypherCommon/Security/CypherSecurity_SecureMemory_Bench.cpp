//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Security/CypherSecurity_SecureMemory_Bench.cpp
//  Purpose: Benchmarks guarded secret-memory lifecycle operations.
//  Details: Measurements cover allocation, page-protection transitions, and
//           secure clearing. These operations are not intended for hot paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"

#include <benchmark/benchmark.h>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

bool EnsureSecurityReady( benchmark::State &state )
{
    if ( Security_Init() == security_status_t::OK ) {
        return true;
    }
    state.SkipWithError( "Unable to initialize CypherSecurity." );
    return false;
}

} // namespace

static void BM_SecureMemory_CreateDestroy( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    const usize cbMemory = static_cast<usize>( state.range( 0 ) );
    for ( auto _ : state ) {
        secure_memory_t memory{};
        security_status_t result = SecureMemory_Create(
            cbMemory,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &memory );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( &memory );
        SecureMemory_Destroy( &memory );
    }
}

static void BM_SecureMemory_ProtectionCycle( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    secure_memory_t memory{};
    if ( SecureMemory_Create(
             32u,
             secure_memory_lock_policy_t::BEST_EFFORT,
             &memory ) != security_status_t::OK ) {
        state.SkipWithError( "Unable to create guarded memory." );
        return;
    }

    for ( auto _ : state ) {
        security_status_t readOnlyResult =
            SecureMemory_SetReadOnly( &memory );
        security_status_t noAccessResult =
            SecureMemory_SetNoAccess( &memory );
        security_status_t readWriteResult =
            SecureMemory_SetReadWrite( &memory );
        benchmark::DoNotOptimize( readOnlyResult );
        benchmark::DoNotOptimize( noAccessResult );
        benchmark::DoNotOptimize( readWriteResult );
    }
}

static void BM_SecureMemory_Zero( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    const usize cbMemory = static_cast<usize>( state.range( 0 ) );
    secure_memory_t memory{};
    if ( SecureMemory_Create(
             cbMemory,
             secure_memory_lock_policy_t::BEST_EFFORT,
             &memory ) != security_status_t::OK ) {
        state.SkipWithError( "Unable to create guarded memory." );
        return;
    }

    for ( auto _ : state ) {
        security_status_t result = SecureMemory_Zero( &memory );
        benchmark::DoNotOptimize( result );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbMemory ) );
}

BENCHMARK( BM_SecureMemory_CreateDestroy )
    ->Arg( 32 )
    ->Arg( 64 )
    ->Arg( 4096 );
BENCHMARK( BM_SecureMemory_ProtectionCycle );
BENCHMARK( BM_SecureMemory_Zero )
    ->Arg( 32 )
    ->Arg( 4096 );
