//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Security/CypherSecurity_Hash_Bench.cpp
//  Purpose: Benchmarks CypherSecurity hashing and password-policy costs.
//  Details: Fast digest and short-hash throughput remain separate from intentionally
//           expensive Argon2id operations so their results are interpreted correctly.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"
#include "CypherCommon_StringView.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

const std::array<byte, 1u << 20u> g_securityData = [] {
    std::array<byte, 1u << 20u> data{};
    for ( usize iByte = 0u; iByte < data.size(); ++iByte ) {
        data[iByte] = static_cast<byte>( iByte * 131u + 19u );
    }
    return data;
}();

security_short_hash_key_t MakeShortHashKey() noexcept
{
    security_short_hash_key_t key{};
    for ( usize iByte = 0u; iByte < sizeof( key.bytes ); ++iByte ) {
        key.bytes[iByte] = static_cast<byte>( iByte * 7u + 1u );
    }
    return key;
}

} // namespace

static void BM_SecurityDigest_Blake2b( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const binary_block_t data{ g_securityData.data(), cbSize };
    for ( auto _ : state ) {
        security_digest_t digest{};
        security_status_t result = SecurityDigest_Data(
            data,
            {},
            CY_SECURITY_DIGEST_DEFAULT_SIZE,
            &digest );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( digest );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cbSize ) );
}

static void BM_SecurityShortHash_SipHash24( benchmark::State &state )
{
    const usize cbSize = static_cast<usize>( state.range( 0 ) );
    const binary_block_t data{ g_securityData.data(), cbSize };
    const security_short_hash_key_t key = MakeShortHashKey();
    for ( auto _ : state ) {
        security_short_hash_t hash{};
        security_status_t result = SecurityShortHash_Data(
            data,
            key,
            &hash );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( hash );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) * static_cast<i64>( cbSize ) );
}

static void BM_PasswordHash_CreateInteractive( benchmark::State &state )
{
    const string_view_t password =
        StringView_FromCString( "correct horse battery staple" );
    for ( auto _ : state ) {
        password_hash_t hash{};
        security_status_t result = PasswordHash_Create(
            password,
            password_hash_profile_t::INTERACTIVE,
            &hash );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( hash );
    }
}

static void BM_PasswordHash_VerifyInteractive( benchmark::State &state )
{
    const string_view_t password =
        StringView_FromCString( "correct horse battery staple" );
    password_hash_t hash{};
    if ( PasswordHash_Create(
             password,
             password_hash_profile_t::INTERACTIVE,
             &hash ) != security_status_t::OK ) {
        state.SkipWithError( "Unable to create Argon2id benchmark hash." );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( PasswordHash_Verify( password, hash ) );
    }
}

BENCHMARK( BM_SecurityDigest_Blake2b )->Arg( 32 )->Arg( 1024 )->Arg( 1 << 20 );
BENCHMARK( BM_SecurityShortHash_SipHash24 )->Arg( 8 )->Arg( 32 )->Arg( 1024 );
BENCHMARK( BM_PasswordHash_CreateInteractive )->Iterations( 3 );
BENCHMARK( BM_PasswordHash_VerifyInteractive )->Iterations( 3 );
