//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherResource/CypherResource_Bench.cpp
//  Purpose: Benchmarks runtime resource-manager hot paths.
//  Details: Measurements separate handle lookup, cached acquisition, and complete
//           synchronous load/unload bookkeeping using an intentionally trivial
//           backend so manager overhead remains visible.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource.h"

#include <benchmark/benchmark.h>

#include <cstdlib>

using namespace cypher::common;
using namespace cypher::engine::resource;

namespace
{

struct benchmark_backend_t {
    u64 payload{ 0xC1F3E2u };
};

bool_t BenchmarkLoad(
    void *pUserData,
    resource_id_t,
    resource_type_id_t,
    string_view_t,
    void **ppResourceOut ) noexcept
{
    *ppResourceOut = &static_cast<benchmark_backend_t *>( pUserData )->payload;
    return CY_TRUE;
}

void BenchmarkUnload( void *, void * ) noexcept
{
}

struct benchmark_manager_t {
    resource_manager_t manager{};
    benchmark_backend_t backend{};
    resource_type_id_t type{ ResourceTypeId_FromName(
        StringView_FromCString( "benchmark" ) ) };

    benchmark_manager_t()
    {
        resource_manager_config_t config = Res_DefaultConfig();
        config.cResourceCapacity = 64u;
        config.cTypeCapacity = 4u;
        if ( Res_Init( &manager, config ) != resource_error_t::OK ) {
            std::abort();
        }
        const resource_loader_t loader{
            type,
            BenchmarkLoad,
            BenchmarkUnload,
            &backend
        };
        if ( Res_RegisterType( &manager, loader ) !=
             resource_error_t::OK ) {
            std::abort();
        }
    }

    ~benchmark_manager_t()
    {
        (void)Res_Shutdown( &manager );
    }
};

} // namespace

static void BM_ResourceGet( benchmark::State &state )
{
    benchmark_manager_t context{};
    resource_handle_t handle{};
    const string_view_t path = StringView_FromCString(
        "materials/benchmark.cymat" );
    if ( Res_Acquire(
            &context.manager,
            context.type,
            path,
            &handle ) != resource_error_t::OK ) {
        state.SkipWithError( "resource setup failed" );
        return;
    }

    for ( auto _ : state ) {
        void *pResource = nullptr;
        resource_error_t result = Res_Get(
            &context.manager,
            handle,
            &pResource );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( pResource );
    }
}

BENCHMARK( BM_ResourceGet );

static void BM_ResourceCachedAcquireRelease( benchmark::State &state )
{
    benchmark_manager_t context{};
    resource_handle_t owner{};
    const string_view_t path = StringView_FromCString(
        "materials/benchmark.cymat" );
    if ( Res_Acquire(
            &context.manager,
            context.type,
            path,
            &owner ) != resource_error_t::OK ) {
        state.SkipWithError( "resource setup failed" );
        return;
    }

    for ( auto _ : state ) {
        resource_handle_t handle{};
        resource_error_t acquireResult = Res_Acquire(
            &context.manager,
            context.type,
            path,
            &handle );
        resource_error_t releaseResult = Res_Release(
            &context.manager,
            handle );
        benchmark::DoNotOptimize( acquireResult );
        benchmark::DoNotOptimize( releaseResult );
        benchmark::DoNotOptimize( handle );
    }
}

BENCHMARK( BM_ResourceCachedAcquireRelease );

static void BM_ResourceLoadUnload( benchmark::State &state )
{
    benchmark_manager_t context{};
    const string_view_t path = StringView_FromCString(
        "materials/benchmark.cymat" );

    for ( auto _ : state ) {
        resource_handle_t handle{};
        resource_error_t acquireResult = Res_Acquire(
            &context.manager,
            context.type,
            path,
            &handle );
        resource_error_t releaseResult = Res_Release(
            &context.manager,
            handle );
        benchmark::DoNotOptimize( acquireResult );
        benchmark::DoNotOptimize( releaseResult );
        benchmark::ClobberMemory();
    }
}

BENCHMARK( BM_ResourceLoadUnload );
