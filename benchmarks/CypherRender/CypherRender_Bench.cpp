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

render::render_vertex_layout_t MakeBenchmarkVertexLayout()
{
    render::render_vertex_layout_t layout{};
    layout.bindingCount = 1u;
    layout.bindings[0].binding = 0u;
    layout.bindings[0].stride = 32u;
    layout.attributeCount = 3u;
    layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    layout.attributes[1] = { render::render_format_t::RGB32_FLOAT, 12u, 1u, 0u };
    layout.attributes[2] = { render::render_format_t::RG32_FLOAT, 24u, 2u, 0u };
    return layout;
}

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

void BM_R_GetVertexFormatInfo( benchmark::State &state )
{
    for ( auto _ : state ) {
        render::render_vertex_format_info_t info{};
        benchmark::DoNotOptimize( render::R_GetVertexFormatInfo(
            render::render_format_t::RGB32_FLOAT,
            &info ) );
        benchmark::DoNotOptimize( info );
    }
    state.SetItemsProcessed( state.iterations() );
}

void BM_R_ValidateVertexLayout( benchmark::State &state )
{
    const render::render_vertex_layout_t layout = MakeBenchmarkVertexLayout();
    for ( auto _ : state ) {
        benchmark::DoNotOptimize( render::R_ValidateVertexLayout( layout ) );
    }
    state.SetItemsProcessed( state.iterations() );
}

} // namespace

BENCHMARK( BM_R_DefaultConfig );
BENCHMARK( BM_R_ValidateConfig );
BENCHMARK( BM_R_SelectBackend );
BENCHMARK( BM_R_IsBackendValid );
BENCHMARK( BM_R_ErrorName );
BENCHMARK( BM_R_GetVertexFormatInfo );
BENCHMARK( BM_R_ValidateVertexLayout );
