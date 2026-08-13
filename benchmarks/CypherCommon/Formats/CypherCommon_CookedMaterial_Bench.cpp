//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Formats/CypherCommon_CookedMaterial_Bench.cpp
//  Purpose: Benchmarks cooked material serialization, validation, and lookup.
//  Details: Measures canonical CYMT writes, strict borrowed-view reads, and
//           binary parameter lookup over representative material sizes.
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
    std::vector<cooked_material_texture_source_t> textures{};
    std::vector<cooked_material_parameter_source_t> parameters{};
    cooked_material_source_t material{};
    std::vector<byte> file{};
};

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
        fixture.textures[iTexture] = {
            View( fixture.textureNames[iTexture] ),
            View( fixture.texturePaths[iTexture] )
        };
    }

    fixture.parameterNames.resize( nParameters );
    fixture.parameters.resize( nParameters );
    for ( usize iParameter = 0u; iParameter < nParameters; ++iParameter ) {
        char name[32]{};
        std::snprintf( name, sizeof( name ), "Parameter%02zu", iParameter );
        fixture.parameterNames[iParameter] = name;

        cooked_material_parameter_source_t &parameter =
            fixture.parameters[iParameter];
        parameter.name = View( fixture.parameterNames[iParameter] );
        parameter.type = render_material_parameter_type_t::VECTOR;
        parameter.nComponents = 4u;
        parameter.values[0] = static_cast<f64>( iParameter ) * 0.125;
        parameter.values[1] = 0.25;
        parameter.values[2] = 0.5;
        parameter.values[3] = 1.0;
    }

    fixture.material.shader = {
        "shaders/benchmark.cyshader",
        sizeof( "shaders/benchmark.cyshader" ) - 1u
    };
    fixture.material.textures = {
        fixture.textures.data(),
        fixture.textures.size()
    };
    fixture.material.parameters = {
        fixture.parameters.data(),
        fixture.parameters.size()
    };

    const usize cbFile = CookedMaterial_RequiredSize( fixture.material );
    fixture.file.resize( cbFile );
    const cooked_material_result_t written = CookedMaterial_Write(
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
        const cooked_material_result_t result = CookedMaterial_Write(
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

} // namespace

BENCHMARK( BM_CookedMaterialWrite )->Arg( 4 )->Arg( 16 )->Arg( 64 );
BENCHMARK( BM_CookedMaterialRead )->Arg( 4 )->Arg( 16 )->Arg( 64 );
BENCHMARK( BM_CookedMaterialFindParameter )->Arg( 4 )->Arg( 16 )->Arg( 64 );
