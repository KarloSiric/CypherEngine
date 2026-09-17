//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Formats/CypherCommon_CookedShader_Bench.cpp
//  Purpose: Benchmarks cooked shader packaging and validation.
//  Details: Measures CYSH V3 stage packaging, logical-interface validation, and
//           binding lookup over representative small and large GLSL resources.
//           Serialization and full validation remain offline/load-time work.
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
    cooked_shader_binding_source_t bindings[5]{};
    usize cbFile{ 0u };
};

void SetBindingId( cooked_shader_binding_source_t &binding ) noexcept
{
    if ( !CookedShader_MakeLogicalBindingId(
             binding.name,
             &binding.nLogicalBinding ) ) {
        binding.nLogicalBinding = 0u;
    }
}

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

    fixture.bindings[0].name = { "base_color", 10u };
    fixture.bindings[0].kind =
        render_shader_binding_kind_t::SAMPLED_TEXTURE;
    fixture.bindings[0].resourceType =
        render_shader_resource_type_t::TEXTURE_2D;
    fixture.bindings[0].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    fixture.bindings[0].flags =
        COOKED_SHADER_BINDING_FLAG_REQUIRED |
        COOKED_SHADER_BINDING_FLAG_MATERIAL;
    SetBindingId( fixture.bindings[0] );

    fixture.bindings[1].name = { "surface_sampler", 15u };
    fixture.bindings[1].kind = render_shader_binding_kind_t::SAMPLER;
    fixture.bindings[1].resourceType =
        render_shader_resource_type_t::SAMPLER;
    fixture.bindings[1].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    fixture.bindings[1].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    SetBindingId( fixture.bindings[1] );

    fixture.bindings[2].name = { "roughness", 9u };
    fixture.bindings[2].kind = render_shader_binding_kind_t::VALUE;
    fixture.bindings[2].valueType = render_shader_value_type_t::F32;
    fixture.bindings[2].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    fixture.bindings[2].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    fixture.bindings[2].iByteOffset = 0u;
    fixture.bindings[2].cbByteSize = 4u;
    SetBindingId( fixture.bindings[2] );

    fixture.bindings[3].name = { "tint", 4u };
    fixture.bindings[3].kind = render_shader_binding_kind_t::VALUE;
    fixture.bindings[3].valueType = render_shader_value_type_t::F32X4;
    fixture.bindings[3].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    fixture.bindings[3].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    fixture.bindings[3].iByteOffset = 16u;
    fixture.bindings[3].cbByteSize = 16u;
    SetBindingId( fixture.bindings[3] );

    fixture.bindings[4].name = { "model_view_projection", 21u };
    fixture.bindings[4].kind = render_shader_binding_kind_t::VALUE;
    fixture.bindings[4].valueType = render_shader_value_type_t::F32X4X4;
    fixture.bindings[4].stageMask = RENDER_SHADER_STAGE_MASK_VERTEX;
    fixture.bindings[4].flags = COOKED_SHADER_BINDING_FLAG_INSTANCE;
    fixture.bindings[4].iByteOffset = 32u;
    fixture.bindings[4].cbByteSize = 64u;
    SetBindingId( fixture.bindings[4] );

    const cooked_shader_interface_source_t shaderInterface{
        { fixture.bindings, 5u }
    };
    fixture.cbFile = CookedShader_RequiredSizeV3(
        {},
        { fixture.stages, 2u },
        shaderInterface );
    fixture.file = std::make_unique<byte[]>( fixture.cbFile );
    const cooked_shader_result_t written = CookedShader_WriteV3(
        {},
        { fixture.stages, 2u },
        shaderInterface,
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

    const cooked_shader_interface_source_t shaderInterface{
        { fixture.bindings, 5u }
    };
    for ( auto _ : state ) {
        const cooked_shader_result_t result = CookedShader_WriteV3(
            {},
            { fixture.stages, 2u },
            shaderInterface,
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
        benchmark::DoNotOptimize( static_cast<u32>( view.nBindings ) );
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<i64>( fixture.cbFile ) );
}

void BM_CookedShaderFindBinding( benchmark::State &state )
{
    shader_fixture_t fixture = MakeFixture(
        static_cast<usize>( state.range( 0 ) ) );
    cooked_shader_view_t view{};
    if ( fixture.cbFile == 0u ||
         !CookedShader_Succeeded( CookedShader_Read(
             { fixture.file.get(), fixture.cbFile },
             &view ) ) ) {
        state.SkipWithError( "failed to create cooked shader view" );
        return;
    }

    const string_view_t name = view.bindings[view.nBindings - 1u].name;
    for ( auto _ : state ) {
        const cooked_shader_binding_view_t *pBinding =
            CookedShader_FindBinding( view, name );
        benchmark::DoNotOptimize( pBinding );
    }
    state.SetItemsProcessed( state.iterations() );
}

void BM_CookedShaderFindBindingById( benchmark::State &state )
{
    shader_fixture_t fixture = MakeFixture(
        static_cast<usize>( state.range( 0 ) ) );
    cooked_shader_view_t view{};
    if ( fixture.cbFile == 0u ||
         !CookedShader_Succeeded( CookedShader_Read(
             { fixture.file.get(), fixture.cbFile },
             &view ) ) ) {
        state.SkipWithError( "failed to create cooked shader view" );
        return;
    }

    const u64 nLogicalBinding =
        view.bindings[view.nBindings - 1u].nLogicalBinding;
    for ( auto _ : state ) {
        const cooked_shader_binding_view_t *pBinding =
            CookedShader_FindBindingById( view, nLogicalBinding );
        benchmark::DoNotOptimize( pBinding );
    }
    state.SetItemsProcessed( state.iterations() );
}

} // namespace

BENCHMARK( BM_CookedShaderWrite )->Arg( 4 * 1024 )->Arg( 256 * 1024 );
BENCHMARK( BM_CookedShaderRead )->Arg( 4 * 1024 )->Arg( 256 * 1024 );
BENCHMARK( BM_CookedShaderFindBinding )->Arg( 4 * 1024 );
BENCHMARK( BM_CookedShaderFindBindingById )->Arg( 4 * 1024 );
