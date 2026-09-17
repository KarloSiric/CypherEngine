//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Formats/CypherCommon_CookedMaterial_Bench.cpp
//  Purpose: Benchmarks cooked material serialization, validation, and lookup.
//  Details: Measures CYMT V2 state/interface packaging, strict borrowed-view
//           reads, and name/ID parameter lookup over representative materials.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedMaterial.h"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace cypher::common;

namespace
{

string_view_t View( const std::string &value ) noexcept
{
    return { value.data(), value.size() };
}

struct material_fixture_t {
    std::vector<std::string> textureNames{};
    std::vector<std::string> texturePaths{};
    std::vector<std::string> parameterNames{};
    std::vector<cooked_material_texture_source_v2_t> textures{};
    std::vector<cooked_material_parameter_source_v2_t> parameters{};
    cooked_material_feature_source_v2_t features[2]{};
    cooked_material_source_v2_t material{};
    std::vector<byte> file{};
};

u64 BindingId( string_view_t name ) noexcept
{
    u64 value = 0u;
    return CookedShader_MakeLogicalBindingId( name, &value )
        ? value
        : 0u;
}

material_fixture_t MakeMaterialFixture( usize nParameters )
{
    material_fixture_t fixture{};
    const usize nTextures = std::min<usize>(
        CY_RENDER_MATERIAL_MAX_TEXTURES,
        std::max<usize>( 1u, nParameters / 2u ) );

    fixture.textureNames.resize( nTextures );
    fixture.texturePaths.resize( nTextures );
    fixture.textures.resize( nTextures );
    for ( usize iTexture = 0u; iTexture < nTextures; ++iTexture ) {
        char name[32]{};
        char path[64]{};
        std::snprintf( name, sizeof( name ), "Texture%02zu", iTexture );
        std::snprintf(
            path,
            sizeof( path ),
            "textures/texture_%02zu.cytex",
            iTexture );
        fixture.textureNames[iTexture] = name;
        fixture.texturePaths[iTexture] = path;
        cooked_material_texture_source_v2_t &texture =
            fixture.textures[iTexture];
        texture.binding = View( fixture.textureNames[iTexture] );
        texture.texture = View( fixture.texturePaths[iTexture] );
        texture.sampler = { "linear_wrap", 11u };
        texture.nLogicalBinding = BindingId( texture.binding );
        texture.nUvSet = static_cast<u32>( iTexture & 1u );
        texture.uvScale[0] = 1.0 + static_cast<f64>( iTexture ) * 0.125;
        texture.uvScale[1] = texture.uvScale[0];
        texture.bHasSampler = CY_TRUE;
    }

    fixture.parameterNames.resize( nParameters );
    fixture.parameters.resize( nParameters );
    for ( usize iParameter = 0u; iParameter < nParameters; ++iParameter ) {
        char name[32]{};
        std::snprintf( name, sizeof( name ), "Parameter%02zu", iParameter );
        fixture.parameterNames[iParameter] = name;

        cooked_material_parameter_source_v2_t &parameter =
            fixture.parameters[iParameter];
        parameter.name = View( fixture.parameterNames[iParameter] );
        parameter.nLogicalBinding = BindingId( parameter.name );
        parameter.type = render_shader_value_type_t::F32X4;
        parameter.iByteOffset = static_cast<u32>( iParameter * 16u );
        parameter.cbByteSize = 16u;
        parameter.floatingValues[0] =
            static_cast<f64>( iParameter ) * 0.125;
        parameter.floatingValues[1] = 0.25;
        parameter.floatingValues[2] = 0.5;
        parameter.floatingValues[3] = 1.0;
    }

    fixture.features[0].name = { "alpha_test", 10u };
    fixture.features[0].type =
        cooked_material_feature_value_type_t::BOOL;
    fixture.features[0].bValue = CY_TRUE;
    fixture.features[1].name = { "quality", 7u };
    fixture.features[1].type =
        cooked_material_feature_value_type_t::ENUM;
    fixture.features[1].enumValue = { "high", 4u };

    fixture.material.shader = {
        "shaders/benchmark.cyshader",
        sizeof( "shaders/benchmark.cyshader" ) - 1u
    };
    fixture.material.shaderInterfaceHash = ContentHash_String(
        { "benchmark.shader.interface.v1",
          sizeof( "benchmark.shader.interface.v1" ) - 1u } );
    fixture.material.features = { fixture.features, 2u };
    fixture.material.textures = {
        fixture.textures.data(),
        fixture.textures.size()
    };
    fixture.material.parameters = {
        fixture.parameters.data(),
        fixture.parameters.size()
    };

    const usize cbFile = CookedMaterial_RequiredSizeV2( fixture.material );
    fixture.file.resize( cbFile );
    const cooked_material_result_t written = CookedMaterial_WriteV2(
        fixture.material,
        {},
        { fixture.file.data(), fixture.file.size() } );
    if ( !CookedMaterial_Succeeded( written ) ) {
        fixture.file.clear();
    }
    return fixture;
}

void BM_CookedMaterialWrite( benchmark::State &state )
{
    material_fixture_t fixture = MakeMaterialFixture(
        static_cast<usize>( state.range( 0 ) ) );
    if ( fixture.file.empty() ) {
        state.SkipWithError( "failed to create cooked material fixture" );
        return;
    }

    for ( auto _ : state ) {
        const cooked_material_result_t result = CookedMaterial_WriteV2(
            fixture.material,
            {},
            { fixture.file.data(), fixture.file.size() } );
        benchmark::DoNotOptimize( static_cast<u8>( result.status ) );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<i64>( fixture.file.size() ) );
}

void BM_CookedMaterialRead( benchmark::State &state )
{
    material_fixture_t fixture = MakeMaterialFixture(
        static_cast<usize>( state.range( 0 ) ) );
    if ( fixture.file.empty() ) {
        state.SkipWithError( "failed to create cooked material fixture" );
        return;
    }

    for ( auto _ : state ) {
        cooked_material_view_t view{};
        const cooked_material_result_t result = CookedMaterial_Read(
            { fixture.file.data(), fixture.file.size() },
            &view );
        benchmark::DoNotOptimize( static_cast<u8>( result.status ) );
        benchmark::DoNotOptimize( view.nParameters );
    }
    state.SetBytesProcessed(
        state.iterations() * static_cast<i64>( fixture.file.size() ) );
}

void BM_CookedMaterialFindParameter( benchmark::State &state )
{
    material_fixture_t fixture = MakeMaterialFixture(
        static_cast<usize>( state.range( 0 ) ) );
    cooked_material_view_t view{};
    if ( fixture.file.empty() ||
         !CookedMaterial_Succeeded( CookedMaterial_Read(
             { fixture.file.data(), fixture.file.size() },
             &view ) ) ) {
        state.SkipWithError( "failed to create cooked material view" );
        return;
    }

    const string_view_t name = view.parameters[view.nParameters - 1u].name;
    for ( auto _ : state ) {
        const cooked_material_parameter_view_t *pParameter =
            CookedMaterial_FindParameter( view, name );
        benchmark::DoNotOptimize( pParameter );
    }
    state.SetItemsProcessed( state.iterations() );
}

void BM_CookedMaterialFindParameterById( benchmark::State &state )
{
    material_fixture_t fixture = MakeMaterialFixture(
        static_cast<usize>( state.range( 0 ) ) );
    cooked_material_view_t view{};
    if ( fixture.file.empty() ||
         !CookedMaterial_Succeeded( CookedMaterial_Read(
             { fixture.file.data(), fixture.file.size() },
             &view ) ) ) {
        state.SkipWithError( "failed to create cooked material view" );
        return;
    }

    const u64 nLogicalBinding =
        view.parameters[view.nParameters - 1u].nLogicalBinding;
    for ( auto _ : state ) {
        const cooked_material_parameter_view_t *pParameter =
            CookedMaterial_FindParameterById( view, nLogicalBinding );
        benchmark::DoNotOptimize( pParameter );
    }
    state.SetItemsProcessed( state.iterations() );
}

} // namespace

BENCHMARK( BM_CookedMaterialWrite )->Arg( 4 )->Arg( 16 )->Arg( 64 );
BENCHMARK( BM_CookedMaterialRead )->Arg( 4 )->Arg( 16 )->Arg( 64 );
BENCHMARK( BM_CookedMaterialFindParameter )->Arg( 4 )->Arg( 16 )->Arg( 64 );
BENCHMARK( BM_CookedMaterialFindParameterById )->Arg( 4 )->Arg( 16 )->Arg( 64 );
