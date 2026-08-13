//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Formats/CypherCommon_CookedMaterial_Tests.cpp
//  Purpose: Tests the backend-neutral cooked material resource contract.
//  Details: Covers canonical ordering, typed values, deterministic output,
//           lookup helpers, semantic rejection, damaged data, and transactional
//           reader behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedMaterial.h"

#include "CypherCommon_ContentHash.h"
#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>
#include <vector>

using namespace cypher::common;

namespace
{

template <usize nLength>
constexpr string_view_t Text( const char ( &text )[nLength] ) noexcept
{
    return { text, nLength - 1u };
}

struct material_fixture_t {
    cooked_material_texture_source_t textures[2]{
        { Text( "NormalMap" ), Text( "textures/wall_normal.cytex" ) },
        { Text( "AlbedoMap" ), Text( "textures/wall_albedo.cytex" ) }
    };
    cooked_material_parameter_source_t parameters[3]{};
    cooked_material_source_t material{};

    material_fixture_t() noexcept
    {
        parameters[0].name = Text( "Tint" );
        parameters[0].type = render_material_parameter_type_t::VECTOR;
        parameters[0].values[0] = 0.25;
        parameters[0].values[1] = 0.5;
        parameters[0].values[2] = 0.75;
        parameters[0].nComponents = 3u;

        parameters[1].name = Text( "AlphaTest" );
        parameters[1].type = render_material_parameter_type_t::BOOL;
        parameters[1].bValue = CY_TRUE;

        parameters[2].name = Text( "Roughness" );
        parameters[2].type = render_material_parameter_type_t::SCALAR;
        parameters[2].values[0] = 0.625;
        parameters[2].nComponents = 1u;

        material.shader = Text( "shaders/world_lit.cyshader" );
        material.textures = { textures, 2u };
        material.parameters = { parameters, 3u };
    }
};

std::vector<byte> WriteFixture(
    const cooked_material_source_t &source,
    content_hash_t sourceHash = {} )
{
    const usize cbRequired = CookedMaterial_RequiredSize( source );
    REQUIRE( cbRequired > CY_COOKED_RESOURCE_HEADER_SIZE );
    std::vector<byte> file( cbRequired, 0xA5u );
    const cooked_material_result_t written = CookedMaterial_Write(
        source,
        sourceHash,
        { file.data(), file.size() } );
    REQUIRE( CookedMaterial_Succeeded( written ) );
    REQUIRE( written.cbWritten == cbRequired );
    return file;
}

void StoreLittleU64(
    std::vector<byte> &file,
    usize iOffset,
    u64 value )
{
    REQUIRE( iOffset <= file.size() );
    REQUIRE( sizeof( value ) <= file.size() - iOffset );
    for ( usize iByte = 0u; iByte < sizeof( value ); ++iByte ) {
        file[iOffset + iByte] = static_cast<byte>(
            ( value >> ( iByte * 8u ) ) & 0xFFu );
    }
}

void ResealMaterialFile(
    std::vector<byte> &file,
    usize iMetadata,
    usize cbMetadata )
{
    const content_hash_t metadataHash = ContentHash_Data( {
        file.data() + iMetadata,
        cbMetadata
    } );
    const usize iFirstChunkHash =
        CY_COOKED_RESOURCE_HEADER_SIZE + 48u;
    StoreLittleU64( file, iFirstChunkHash, metadataHash.low );
    StoreLittleU64(
        file,
        iFirstChunkHash + sizeof( u64 ),
        metadataHash.high );

    const content_hash_t resourceHash = CookedResource_ComputeContentHash( {
        file.data(),
        file.size()
    } );
    StoreLittleU64( file, 64u, resourceHash.low );
    StoreLittleU64( file, 72u, resourceHash.high );
}

} // namespace

TEST_CASE( "Cooked materials round trip with canonical named values",
           "[CypherCommon][Formats][CookedMaterial]" )
{
    material_fixture_t fixture{};
    const content_hash_t sourceHash = ContentHash_String(
        Text( "materials/wall.cymat" ) );
    std::vector<byte> file = WriteFixture( fixture.material, sourceHash );

    cooked_material_view_t view{};
    const cooked_material_result_t read = CookedMaterial_Read(
        { file.data(), file.size() },
        &view );
    REQUIRE( CookedMaterial_Succeeded( read ) );
    REQUIRE( StringView_Equals(
        view.shader,
        Text( "shaders/world_lit.cyshader" ) ) );
    REQUIRE( view.nTextures == 2u );
    REQUIRE( view.nParameters == 3u );
    REQUIRE( ContentHash_Equals( view.sourceHash, sourceHash ) );

    // The authored input was deliberately unsorted; disk views are canonical.
    REQUIRE( StringView_Equals( view.textures[0].binding, Text( "AlbedoMap" ) ) );
    REQUIRE( StringView_Equals( view.textures[1].binding, Text( "NormalMap" ) ) );
    REQUIRE( StringView_Equals( view.parameters[0].name, Text( "AlphaTest" ) ) );
    REQUIRE( StringView_Equals( view.parameters[1].name, Text( "Roughness" ) ) );
    REQUIRE( StringView_Equals( view.parameters[2].name, Text( "Tint" ) ) );

    const cooked_material_texture_view_t *pAlbedo =
        CookedMaterial_FindTexture( view, Text( "AlbedoMap" ) );
    REQUIRE( pAlbedo != nullptr );
    REQUIRE( StringView_Equals(
        pAlbedo->texture,
        Text( "textures/wall_albedo.cytex" ) ) );
    REQUIRE( CookedMaterial_FindTexture(
        view,
        Text( "MissingMap" ) ) == nullptr );

    const cooked_material_parameter_view_t *pAlpha =
        CookedMaterial_FindParameter( view, Text( "AlphaTest" ) );
    const cooked_material_parameter_view_t *pRoughness =
        CookedMaterial_FindParameter( view, Text( "Roughness" ) );
    const cooked_material_parameter_view_t *pTint =
        CookedMaterial_FindParameter( view, Text( "Tint" ) );
    REQUIRE( pAlpha != nullptr );
    REQUIRE( pAlpha->type == render_material_parameter_type_t::BOOL );
    REQUIRE( pAlpha->bValue );
    REQUIRE( pRoughness != nullptr );
    REQUIRE( pRoughness->values[0] == 0.625 );
    REQUIRE( pTint != nullptr );
    REQUIRE( pTint->nComponents == 3u );
    REQUIRE( pTint->values[2] == 0.75 );
}

TEST_CASE( "Cooked materials are deterministic across author ordering",
           "[CypherCommon][Formats][CookedMaterial][Determinism]" )
{
    material_fixture_t firstFixture{};
    material_fixture_t secondFixture{};
    cooked_material_texture_source_t textures[2]{
        secondFixture.textures[1],
        secondFixture.textures[0]
    };
    cooked_material_parameter_source_t parameters[3]{
        secondFixture.parameters[1],
        secondFixture.parameters[2],
        secondFixture.parameters[0]
    };
    secondFixture.material.textures = { textures, 2u };
    secondFixture.material.parameters = { parameters, 3u };

    const std::vector<byte> first = WriteFixture( firstFixture.material );
    const std::vector<byte> second = WriteFixture( secondFixture.material );
    REQUIRE( first.size() == second.size() );
    REQUIRE( Cy_MemEqual( first.data(), second.data(), first.size() ) );
}

TEST_CASE( "Cooked material writers reject invalid semantic data",
           "[CypherCommon][Formats][CookedMaterial][Validation]" )
{
    material_fixture_t fixture{};

    fixture.material.shader = Text( "Shaders/World.cyshader" );
    REQUIRE( CookedMaterial_RequiredSize( fixture.material ) == 0u );
    fixture.material.shader = Text( "shaders/world_lit.cyshader" );

    fixture.textures[1].binding = Text( "NormalMap" );
    REQUIRE( CookedMaterial_RequiredSize( fixture.material ) == 0u );
    fixture.textures[1].binding = Text( "AlbedoMap" );

    fixture.parameters[2].values[0] =
        std::numeric_limits<f64>::infinity();
    REQUIRE( CookedMaterial_RequiredSize( fixture.material ) == 0u );
    fixture.parameters[2].values[0] = 0.625;

    fixture.parameters[0].nComponents = 1u;
    REQUIRE( CookedMaterial_RequiredSize( fixture.material ) == 0u );

    REQUIRE( CookedMaterial_StatusName(
        cooked_material_status_t::NON_FINITE_VALUE ) ==
        std::string_view( "NON_FINITE_VALUE" ) );
}

TEST_CASE( "Cooked material readers reject damage transactionally",
           "[CypherCommon][Formats][CookedMaterial][Failure]" )
{
    material_fixture_t fixture{};
    std::vector<byte> file = WriteFixture( fixture.material );
    cooked_material_view_t output{};
    output.nTextures = 77u;

    file.back() ^= static_cast<byte>( 1u );
    const cooked_material_result_t damaged = CookedMaterial_Read(
        { file.data(), file.size() },
        &output );
    REQUIRE( damaged.status == cooked_material_status_t::RESOURCE_ERROR );
    REQUIRE( damaged.resourceStatus ==
             cooked_resource_status_t::CONTENT_HASH_MISMATCH );
    REQUIRE( output.nTextures == 77u );

    const cooked_material_result_t truncated = CookedMaterial_Read(
        { file.data(), file.size() - 1u },
        &output );
    REQUIRE( truncated.status == cooked_material_status_t::RESOURCE_ERROR );
    REQUIRE( output.nTextures == 77u );
}

TEST_CASE( "Cooked material readers reject non-canonical negative zero",
           "[CypherCommon][Formats][CookedMaterial][Canonical]" )
{
    material_fixture_t fixture{};
    std::vector<byte> file = WriteFixture( fixture.material );

    const usize iMetadata = CookedResource_PrefixSize( 2u );
    const usize cbMetadata = CookedMaterial_MetadataSize( 2u, 3u );
    const usize iFirstParameter =
        iMetadata + CY_COOKED_MATERIAL_METADATA_HEADER_SIZE +
        2u * CY_COOKED_MATERIAL_TEXTURE_RECORD_SIZE;
    const usize iFirstInactiveValue = iFirstParameter + 16u + sizeof( f64 );

    // The first canonical parameter is AlphaTest. Encode negative zero in one
    // of its inactive numeric fields, then repair both integrity hashes so the
    // reader reaches the canonical-value check rather than failing on damage.
    for ( usize iByte = 0u; iByte < sizeof( f64 ); ++iByte ) {
        file[iFirstInactiveValue + iByte] = 0u;
    }
    file[iFirstInactiveValue + sizeof( f64 ) - 1u] = 0x80u;
    ResealMaterialFile( file, iMetadata, cbMetadata );

    cooked_material_view_t material{};
    const cooked_material_result_t read = CookedMaterial_Read(
        { file.data(), file.size() },
        &material );
    REQUIRE( read.status ==
             cooked_material_status_t::NON_CANONICAL_LAYOUT );
    REQUIRE( read.iParameter == 0u );
}

TEST_CASE( "Cooked material helpers reject invalid capacities",
           "[CypherCommon][Formats][CookedMaterial][Helpers]" )
{
    REQUIRE( CookedMaterial_MetadataSize( 0u, 0u ) ==
             CY_COOKED_MATERIAL_METADATA_HEADER_SIZE );
    REQUIRE( CookedMaterial_MetadataSize(
                 CY_RENDER_MATERIAL_MAX_TEXTURES + 1u,
                 0u ) == 0u );
    REQUIRE( CookedMaterial_MetadataSize(
                 0u,
                 CY_RENDER_MATERIAL_MAX_PARAMETERS + 1u ) == 0u );
}
