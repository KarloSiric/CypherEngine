//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherRender/CypherRender_Bench.cpp
//  Purpose: Measures deterministic renderer frontend and dispatch operations.
//  Details: Native context creation, driver calls, and presentation are omitted
//           because those timings primarily characterize the host graphics stack.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Backend.h"
#include "CypherRender/CypherRender_Public.h"

#include <benchmark/benchmark.h>

namespace render = ::cypher::engine::render;

namespace
{

void BM_R_DefaultConfig( benchmark::State &state )
{
    for ( auto _ : state ) {
        render::render_config_t config = render::R_DefaultConfig();
        benchmark::DoNotOptimize( config );
    }
    state.SetItemsProcessed( state.iterations() );
}

void BM_R_ValidateConfig( benchmark::State &state )
{
    const render::render_config_t config = render::R_DefaultConfig();
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( render::R_ValidateConfig( config ) );
    }
    state.SetItemsProcessed( state.iterations() );
}

void BM_R_SelectBackend( benchmark::State &state )
{
    const render::render_config_t config = render::R_DefaultConfig();
    const render::backend_api_t *backend = nullptr;
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( render::R_SelectBackend( config, &backend ) );
        benchmark::DoNotOptimize( backend );
    }
    state.SetItemsProcessed( state.iterations() );
}

void BM_R_IsBackendValid( benchmark::State &state )
{
    const render::render_config_t config = render::R_DefaultConfig();
    const render::backend_api_t *backend = nullptr;
    if ( render::R_SelectBackend( config, &backend ) != render::render_error_t::OK ) {
        state.SkipWithError( "OpenGL backend was not compiled" );
        return;
    }

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( render::R_IsBackendValid( backend ) );
    }
    state.SetItemsProcessed( state.iterations() );
}

void BM_R_ErrorName( benchmark::State &state )
{
    for ( auto _ : state ) {
        benchmark::DoNotOptimize(
            render::R_ErrorName( render::render_error_t::ERR_CONTEXT_CREATE_FAILED ) );
    }
    state.SetItemsProcessed( state.iterations() );
}

} // namespace

BENCHMARK( BM_R_DefaultConfig );
BENCHMARK( BM_R_ValidateConfig );
BENCHMARK( BM_R_SelectBackend );
BENCHMARK( BM_R_IsBackendValid );
BENCHMARK( BM_R_ErrorName );
