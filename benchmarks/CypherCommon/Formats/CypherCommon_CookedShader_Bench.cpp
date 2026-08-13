//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Formats/CypherCommon_CookedShader_Bench.cpp
//  Purpose: Benchmarks cooked shader packaging and validation.
//  Details: Measures the format layer over representative small and large GLSL
//           resources. Results establish a baseline for cooker and load-time
//           work; neither operation belongs in the per-frame renderer hot path.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedShader.h"

#include <benchmark/benchmark.h>

#include <memory>

using namespace cypher::common;

namespace
{

struct shader_fixture_t {
    std::unique_ptr<byte[]> vertex{};
    std::unique_ptr<byte[]> fragment{};
    std::unique_ptr<byte[]> file{};
    cooked_shader_stage_source_t stages[2]{};
    usize cbFile{ 0u };
};

shader_fixture_t MakeFixture( usize cbStage )
{
    shader_fixture_t fixture{};
    fixture.vertex = std::make_unique<byte[]>( cbStage );
    fixture.fragment = std::make_unique<byte[]>( cbStage );
    for ( usize iByte = 0u; iByte + 1u < cbStage; ++iByte ) {
        fixture.vertex[iByte] = static_cast<byte>( 'v' );
        fixture.fragment[iByte] = static_cast<byte>( 'f' );
    }
    fixture.vertex[cbStage - 1u] = static_cast<byte>( '\0' );
    fixture.fragment[cbStage - 1u] = static_cast<byte>( '\0' );
    fixture.stages[0] = {
        render_shader_stage_t::VERTEX,
        render_shader_code_format_t::GLSL_UTF8,
        COOKED_SHADER_STAGE_FLAG_NONE,
        { fixture.vertex.get(), cbStage }
    };
    fixture.stages[1] = {
        render_shader_stage_t::FRAGMENT,
        render_shader_code_format_t::GLSL_UTF8,
        COOKED_SHADER_STAGE_FLAG_NONE,
        { fixture.fragment.get(), cbStage }
    };
    fixture.cbFile = CookedShader_RequiredSize(
        {},
        { fixture.stages, 2u } );
    fixture.file = std::make_unique<byte[]>( fixture.cbFile );
    const cooked_shader_result_t written = CookedShader_Write(
        {},
        { fixture.stages, 2u },
        {},
        { fixture.file.get(), fixture.cbFile } );
    if ( !CookedShader_Succeeded( written ) ) {
        fixture.cbFile = 0u;
    }
    return fixture;
}

void BM_CookedShaderWrite( benchmark::State &state )
{
    const usize cbStage = static_cast<usize>( state.range( 0 ) );
    shader_fixture_t fixture = MakeFixture( cbStage );
    if ( fixture.cbFile == 0u ) {
        state.SkipWithError( "failed to create cooked shader fixture" );
        return;
    }

    for ( auto _ : state ) {
        const cooked_shader_result_t result = CookedShader_Write(
            {},
            { fixture.stages, 2u },
            {},
            { fixture.file.get(), fixture.cbFile } );
        benchmark::DoNotOptimize(
            static_cast<u8>( result.status ) );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<i64>( cbStage * 2u ) );
}

void BM_CookedShaderRead( benchmark::State &state )
{
    const usize cbStage = static_cast<usize>( state.range( 0 ) );
    shader_fixture_t fixture = MakeFixture( cbStage );
    if ( fixture.cbFile == 0u ) {
        state.SkipWithError( "failed to create cooked shader fixture" );
        return;
    }

    for ( auto _ : state ) {
        cooked_shader_view_t view{};
        const cooked_shader_result_t result = CookedShader_Read(
            { fixture.file.get(), fixture.cbFile },
            &view );
        benchmark::DoNotOptimize(
            static_cast<u8>( result.status ) );
        benchmark::DoNotOptimize( static_cast<u32>( view.nStages ) );
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<i64>( fixture.cbFile ) );
}

} // namespace

BENCHMARK( BM_CookedShaderWrite )->Arg( 4 * 1024 )->Arg( 256 * 1024 );
BENCHMARK( BM_CookedShaderRead )->Arg( 4 * 1024 )->Arg( 256 * 1024 );
