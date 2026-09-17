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

u64 BindingId( string_view_t name ) noexcept
{
    u64 bindingId = 0u;
    const bool_t bMade = CookedShader_MakeLogicalBindingId(
        name,
        &bindingId );
    return bMade ? bindingId : 0u;
}

struct material_v2_fixture_t {
    cooked_material_feature_source_v2_t features[2]{};
    cooked_material_texture_source_v2_t textures[2]{};
    cooked_material_parameter_source_v2_t parameters[4]{};
    cooked_material_source_v2_t material{};

    material_v2_fixture_t() noexcept
    {
        features[0].name = Text( "UseClearcoat" );
        features[0].type = cooked_material_feature_value_type_t::BOOL;
        features[0].bValue = CY_TRUE;
        features[1].name = Text( "Quality" );
        features[1].type = cooked_material_feature_value_type_t::ENUM;
        features[1].enumValue = Text( "High" );

        textures[0].binding = Text( "NormalMap" );
        textures[0].texture = Text( "textures/wall_normal.cytex" );
        textures[0].nLogicalBinding = BindingId( textures[0].binding );
        textures[0].nUvSet = 1u;
        textures[0].uvScale[0] = 2.0;
        textures[0].uvScale[1] = 2.0;
        textures[1].binding = Text( "AlbedoMap" );
        textures[1].texture = Text( "textures/wall_albedo.cytex" );
        textures[1].sampler = Text( "linear_wrap" );
        textures[1].bHasSampler = CY_TRUE;
        textures[1].nLogicalBinding = BindingId( textures[1].binding );
        textures[1].uvOffset[0] = 0.25;
        textures[1].uvRotation = 0.125;

        parameters[0].name = Text( "Tint" );
        parameters[0].nLogicalBinding = BindingId( parameters[0].name );
        parameters[0].type = render_shader_value_type_t::F32X3;
        parameters[0].iByteOffset = 64u;
        parameters[0].cbByteSize = 16u;
        parameters[0].floatingValues[0] = 0.25;
        parameters[0].floatingValues[1] = 0.5;
        parameters[0].floatingValues[2] = 0.75;

        parameters[1].name = Text( "Roughness" );
        parameters[1].nLogicalBinding = BindingId( parameters[1].name );
        parameters[1].type = render_shader_value_type_t::F32;
        parameters[1].iByteOffset = 52u;
        parameters[1].cbByteSize = 4u;
        parameters[1].floatingValues[0] = 0.625;

        parameters[2].name = Text( "Basis" );
        parameters[2].nLogicalBinding = BindingId( parameters[2].name );
        parameters[2].type = render_shader_value_type_t::F32X3X3;
        parameters[2].iByteOffset = 0u;
        parameters[2].cbByteSize = 48u;
        for ( usize iValue = 0u; iValue < 9u; ++iValue ) {
            parameters[2].floatingValues[iValue] =
                static_cast<f64>( iValue + 1u );
        }

        parameters[3].name = Text( "Layer" );
        parameters[3].nLogicalBinding = BindingId( parameters[3].name );
        parameters[3].type = render_shader_value_type_t::U32;
        parameters[3].iByteOffset = 48u;
        parameters[3].cbByteSize = 4u;
        parameters[3].unsignedValues[0] = 7u;

        material.shader = Text( "shaders/world_lit.cyshader" );
        material.surface = Text( "surfaces/stone.cysurface" );
        material.bHasSurface = CY_TRUE;
        material.shaderInterfaceHash = ContentHash_String(
            Text( "world-lit-interface-v3" ) );
        material.domain = render_material_domain_t::SURFACE;
        material.alphaMode = render_material_alpha_mode_t::MASK;
        material.alphaCutoff = 0.375;
        material.features = { features, 2u };
        material.textures = { textures, 2u };
        material.parameters = { parameters, 4u };
        material.flags = COOKED_MATERIAL_FLAG_TWO_SIDED |
            COOKED_MATERIAL_FLAG_CASTS_SHADOWS |
            COOKED_MATERIAL_FLAG_RECEIVES_SHADOWS;
        const bool_t bVariantHash = CookedMaterial_ComputeVariantHash(
            material.features,
            &material.variantHash );
        (void)bVariantHash;
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

std::vector<byte> WriteFixtureV2(
    const cooked_material_source_v2_t &source,
    content_hash_t sourceHash = {} )
{
    const usize cbRequired = CookedMaterial_RequiredSizeV2( source );
    REQUIRE( cbRequired > CY_COOKED_RESOURCE_HEADER_SIZE );
    std::vector<byte> file( cbRequired, 0xA5u );
    const cooked_material_result_t written = CookedMaterial_WriteV2(
        source,
        sourceHash,
        { file.data(), file.size() } );
    REQUIRE( CookedMaterial_Succeeded( written ) );
    REQUIRE( written.cbWritten == cbRequired );
    return file;
}

u32 LoadLittleU32(
    const std::vector<byte> &file,
    usize iOffset )
{
    REQUIRE( iOffset <= file.size() );
    REQUIRE( sizeof( u32 ) <= file.size() - iOffset );
    u32 value = 0u;
    for ( usize iByte = 0u; iByte < sizeof( value ); ++iByte ) {
        value |= static_cast<u32>( file[iOffset + iByte] ) <<
            ( iByte * 8u );
    }
    return value;
}

u64 LoadLittleU64(
    const std::vector<byte> &file,
    usize iOffset )
{
    REQUIRE( iOffset <= file.size() );
    REQUIRE( sizeof( u64 ) <= file.size() - iOffset );
    u64 value = 0u;
    for ( usize iByte = 0u; iByte < sizeof( value ); ++iByte ) {
        value |= static_cast<u64>( file[iOffset + iByte] ) <<
            ( iByte * 8u );
    }
    return value;
}

void StoreLittleU32(
    std::vector<byte> &file,
    usize iOffset,
    u32 value )
{
    REQUIRE( iOffset <= file.size() );
    REQUIRE( sizeof( value ) <= file.size() - iOffset );
    for ( usize iByte = 0u; iByte < sizeof( value ); ++iByte ) {
        file[iOffset + iByte] = static_cast<byte>(
            ( value >> ( iByte * 8u ) ) & 0xFFu );
    }
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

void ResealResource( std::vector<byte> &file )
{
    const content_hash_t resourceHash = CookedResource_ComputeContentHash( {
        file.data(),
        file.size()
    } );
    StoreLittleU64( file, 64u, resourceHash.low );
    StoreLittleU64( file, 72u, resourceHash.high );
}

void ResealChunk(
    std::vector<byte> &file,
    usize iChunk )
{
    const usize iDescriptor = CY_COOKED_RESOURCE_HEADER_SIZE +
        iChunk * CY_COOKED_RESOURCE_CHUNK_SIZE;
    const usize iPayload = static_cast<usize>(
        LoadLittleU64( file, iDescriptor + 16u ) );
    const usize cbPayload = static_cast<usize>(
        LoadLittleU64( file, iDescriptor + 24u ) );
    REQUIRE( iPayload <= file.size() );
    REQUIRE( cbPayload <= file.size() - iPayload );
    const content_hash_t hash = ContentHash_Data( {
        file.data() + iPayload,
        cbPayload
    } );
    StoreLittleU64( file, iDescriptor + 48u, hash.low );
    StoreLittleU64( file, iDescriptor + 56u, hash.high );
    ResealResource( file );
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

TEST_CASE( "Cooked material V2 round trips resolved runtime state",
           "[CypherCommon][Formats][CookedMaterial][V2]" )
{
    material_v2_fixture_t fixture{};
    REQUIRE( ContentHash_IsValid( fixture.material.variantHash ) );
    const content_hash_t sourceHash = ContentHash_String(
        Text( "materials/wall-v2.cymat" ) );
    const std::vector<byte> file = WriteFixtureV2(
        fixture.material,
        sourceHash );

    cooked_material_view_t view{};
    const cooked_material_result_t read = CookedMaterial_Read(
        { file.data(), file.size() },
        &view );
    REQUIRE( CookedMaterial_Succeeded( read ) );
    REQUIRE( view.nResourceVersion ==
             CY_COOKED_MATERIAL_RESOURCE_VERSION_V2 );
    REQUIRE( StringView_Equals(
        view.shader,
        Text( "shaders/world_lit.cyshader" ) ) );
    REQUIRE( view.bHasSurface );
    REQUIRE( StringView_Equals(
        view.surface,
        Text( "surfaces/stone.cysurface" ) ) );
    REQUIRE( view.domain == render_material_domain_t::SURFACE );
    REQUIRE( view.alphaMode == render_material_alpha_mode_t::MASK );
    REQUIRE( view.alphaCutoff == 0.375F );
    REQUIRE( view.flags == fixture.material.flags );
    REQUIRE( ContentHash_Equals(
        view.shaderInterfaceHash,
        fixture.material.shaderInterfaceHash ) );
    REQUIRE( ContentHash_Equals(
        view.variantHash,
        fixture.material.variantHash ) );
    REQUIRE( ContentHash_Equals( view.sourceHash, sourceHash ) );
    REQUIRE( view.nFeatures == 2u );
    REQUIRE( view.nTextures == 2u );
    REQUIRE( view.nParameters == 4u );
    REQUIRE( view.constantData.cbSize == 80u );

    REQUIRE( StringView_Equals( view.features[0].name, Text( "Quality" ) ) );
    REQUIRE( view.features[0].type ==
             cooked_material_feature_value_type_t::ENUM );
    REQUIRE( StringView_Equals(
        view.features[0].enumValue,
        Text( "High" ) ) );
    REQUIRE( StringView_Equals(
        view.features[1].name,
        Text( "UseClearcoat" ) ) );
    REQUIRE( view.features[1].bValue );

    const cooked_material_texture_view_t *pAlbedo =
        CookedMaterial_FindTexture( view, Text( "AlbedoMap" ) );
    REQUIRE( pAlbedo != nullptr );
    REQUIRE( pAlbedo->bHasSampler );
    REQUIRE( StringView_Equals(
        pAlbedo->sampler,
        Text( "linear_wrap" ) ) );
    REQUIRE( pAlbedo->uvOffset[0] == 0.25F );
    REQUIRE( CookedMaterial_FindTextureById(
                 view,
                 pAlbedo->nLogicalBinding ) == pAlbedo );
    REQUIRE( CookedMaterial_FindTextureById( view, 0u ) == nullptr );

    const cooked_material_parameter_view_t *pBasis =
        CookedMaterial_FindParameter( view, Text( "Basis" ) );
    const cooked_material_parameter_view_t *pLayer =
        CookedMaterial_FindParameter( view, Text( "Layer" ) );
    const cooked_material_parameter_view_t *pRoughness =
        CookedMaterial_FindParameter( view, Text( "Roughness" ) );
    const cooked_material_parameter_view_t *pTint =
        CookedMaterial_FindParameter( view, Text( "Tint" ) );
    REQUIRE( pBasis != nullptr );
    REQUIRE( pBasis->shaderType == render_shader_value_type_t::F32X3X3 );
    REQUIRE( pBasis->iByteOffset == 0u );
    REQUIRE( pBasis->cbValue == 36u );
    REQUIRE( pBasis->cbByteSize == 48u );
    REQUIRE( pBasis->data.cbSize == 48u );
    REQUIRE( pBasis->data.pData == view.constantData.pData );
    for ( usize iValue = 0u; iValue < 9u; ++iValue ) {
        REQUIRE( pBasis->floatingValues[iValue] ==
                 static_cast<f64>( iValue + 1u ) );
    }
    REQUIRE( pLayer != nullptr );
    REQUIRE( pLayer->unsignedValues[0] == 7u );
    REQUIRE( pRoughness != nullptr );
    REQUIRE( pRoughness->floatingValues[0] == 0.625 );
    REQUIRE( pTint != nullptr );
    REQUIRE( pTint->cbValue == 12u );
    REQUIRE( pTint->cbByteSize == 16u );
    REQUIRE( pTint->floatingValues[2] == 0.75 );
    REQUIRE( CookedMaterial_FindParameterById(
                 view,
                 pTint->nLogicalBinding ) == pTint );
    REQUIRE( CookedMaterial_FindParameterById( view, 0u ) == nullptr );

    // Reflected gaps, vector tails, and every mat3 column pad are canonical.
    REQUIRE( Cy_MemIsZero( view.constantData.pData + 12u, 4u ) );
    REQUIRE( Cy_MemIsZero( view.constantData.pData + 28u, 4u ) );
    REQUIRE( Cy_MemIsZero( view.constantData.pData + 44u, 4u ) );
    REQUIRE( Cy_MemIsZero( view.constantData.pData + 56u, 8u ) );
    REQUIRE( Cy_MemIsZero( view.constantData.pData + 76u, 4u ) );
    const usize basisValueOffsets[9]{
        0u, 4u, 8u,
        16u, 20u, 24u,
        32u, 36u, 40u
    };
    const u32 expectedBasisBits[9]{
        0x3F800000u, 0x40000000u, 0x40400000u,
        0x40800000u, 0x40A00000u, 0x40C00000u,
        0x40E00000u, 0x41000000u, 0x41100000u
    };
    for ( usize iValue = 0u; iValue < 9u; ++iValue ) {
        const usize iOffset = basisValueOffsets[iValue];
        const u32 bits =
            static_cast<u32>( view.constantData.pData[iOffset] ) |
            ( static_cast<u32>( view.constantData.pData[iOffset + 1u] ) <<
              8u ) |
            ( static_cast<u32>( view.constantData.pData[iOffset + 2u] ) <<
              16u ) |
            ( static_cast<u32>( view.constantData.pData[iOffset + 3u] ) <<
              24u );
        REQUIRE( bits == expectedBasisBits[iValue] );
    }
}

TEST_CASE( "Cooked material V2 is deterministic across source order",
           "[CypherCommon][Formats][CookedMaterial][V2][Determinism]" )
{
    material_v2_fixture_t firstFixture{};
    material_v2_fixture_t secondFixture{};
    cooked_material_feature_source_v2_t features[2]{
        secondFixture.features[1],
        secondFixture.features[0]
    };
    cooked_material_texture_source_v2_t textures[2]{
        secondFixture.textures[1],
        secondFixture.textures[0]
    };
    cooked_material_parameter_source_v2_t parameters[4]{
        secondFixture.parameters[3],
        secondFixture.parameters[2],
        secondFixture.parameters[1],
        secondFixture.parameters[0]
    };
    // Inactive and representational negative zero values do not change bytes.
    textures[0].uvOffset[1] = -0.0;
    parameters[0].floatingValues[0] = -0.0;
    secondFixture.material.features = { features, 2u };
    secondFixture.material.textures = { textures, 2u };
    secondFixture.material.parameters = { parameters, 4u };

    content_hash_t reorderedVariant{};
    REQUIRE( CookedMaterial_ComputeVariantHash(
        secondFixture.material.features,
        &reorderedVariant ) );
    REQUIRE( ContentHash_Equals(
        reorderedVariant,
        firstFixture.material.variantHash ) );
    secondFixture.material.variantHash = reorderedVariant;

    const std::vector<byte> first = WriteFixtureV2(
        firstFixture.material );
    const std::vector<byte> second = WriteFixtureV2(
        secondFixture.material );
    REQUIRE( first.size() == second.size() );
    REQUIRE( Cy_MemEqual( first.data(), second.data(), first.size() ) );
}

TEST_CASE( "Cooked material V2 omits an empty constant chunk",
           "[CypherCommon][Formats][CookedMaterial][V2][Minimal]" )
{
    cooked_material_source_v2_t source{};
    source.shader = Text( "shaders/unlit.cyshader" );
    source.shaderInterfaceHash = ContentHash_String(
        Text( "unlit-interface-v3" ) );
    REQUIRE( CookedMaterial_ComputeVariantHash(
        source.features,
        &source.variantHash ) );

    const std::vector<byte> file = WriteFixtureV2( source );
    REQUIRE( LoadLittleU32( file, 24u ) == 2u );

    cooked_material_view_t view{};
    const cooked_material_result_t read = CookedMaterial_Read(
        { file.data(), file.size() },
        &view );
    REQUIRE( CookedMaterial_Succeeded( read ) );
    REQUIRE( view.nParameters == 0u );
    REQUIRE( view.constantData.cbSize == 0u );
    REQUIRE( view.alphaCutoff == 0.5F );
    REQUIRE( ContentHash_Equals( view.variantHash, source.variantHash ) );
}

TEST_CASE( "Cooked material V2 validates source identities and layouts",
           "[CypherCommon][Formats][CookedMaterial][V2][Validation]" )
{
    material_v2_fixture_t fixture{};

    fixture.material.shaderInterfaceHash = {};
    REQUIRE( CookedMaterial_RequiredSizeV2( fixture.material ) == 0u );
    fixture.material.shaderInterfaceHash = ContentHash_String(
        Text( "world-lit-interface-v3" ) );

    fixture.material.variantHash.low ^= 1u;
    REQUIRE( CookedMaterial_RequiredSizeV2( fixture.material ) == 0u );
    REQUIRE( CookedMaterial_ComputeVariantHash(
        fixture.material.features,
        &fixture.material.variantHash ) );

    fixture.parameters[0].cbByteSize = 12u;
    REQUIRE( CookedMaterial_RequiredSizeV2( fixture.material ) == 0u );
    fixture.parameters[0].cbByteSize = 16u;

    fixture.parameters[0].iByteOffset = 68u;
    REQUIRE( CookedMaterial_RequiredSizeV2( fixture.material ) == 0u );
    fixture.parameters[0].iByteOffset = 64u;

    fixture.textures[0].nLogicalBinding ^= 1u;
    REQUIRE( CookedMaterial_RequiredSizeV2( fixture.material ) == 0u );
    fixture.textures[0].nLogicalBinding = BindingId(
        fixture.textures[0].binding );

    fixture.textures[0].uvRotation =
        std::numeric_limits<f64>::infinity();
    REQUIRE( CookedMaterial_RequiredSizeV2( fixture.material ) == 0u );
}

TEST_CASE( "Cooked material V2 readers reject forged semantic payloads",
           "[CypherCommon][Formats][CookedMaterial][V2][Failure]" )
{
    material_v2_fixture_t fixture{};
    const std::vector<byte> original = WriteFixtureV2( fixture.material );
    const usize iMetadata = static_cast<usize>( LoadLittleU64(
        original,
        CY_COOKED_RESOURCE_HEADER_SIZE + 16u ) );
    const usize iParameters = iMetadata + LoadLittleU32(
        original,
        iMetadata + 112u );
    const usize iConstants = static_cast<usize>( LoadLittleU64(
        original,
        CY_COOKED_RESOURCE_HEADER_SIZE +
            CY_COOKED_RESOURCE_CHUNK_SIZE + 16u ) );

    SECTION( "shader interface identity is required" ) {
        std::vector<byte> file = original;
        for ( usize iByte = 0u; iByte < 16u; ++iByte ) {
            file[iMetadata + 72u + iByte] = 0u;
        }
        ResealChunk( file, 0u );
        cooked_material_view_t output{};
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status ==
                 cooked_material_status_t::INVALID_INTERFACE_HASH );
    }

    SECTION( "variant identity must match resolved features" ) {
        std::vector<byte> file = original;
        file[iMetadata + 88u] ^= 1u;
        ResealChunk( file, 0u );
        cooked_material_view_t output{};
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status ==
                 cooked_material_status_t::INVALID_VARIANT_HASH );
    }

    SECTION( "logical binding IDs are derived from names" ) {
        std::vector<byte> file = original;
        file[iParameters + 8u] ^= 1u;
        ResealChunk( file, 0u );
        cooked_material_view_t output{};
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status == cooked_material_status_t::INVALID_PARAMETER );
        REQUIRE( read.iParameter == 0u );
    }

    SECTION( "reflected constant offsets cannot overlap" ) {
        std::vector<byte> file = original;
        // Basis is first in canonical name order. Offset 16 stays aligned but
        // overlaps later values and violates canonical packing.
        StoreLittleU32( file, iParameters + 20u, 16u );
        ResealChunk( file, 0u );
        cooked_material_view_t output{};
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status ==
                 cooked_material_status_t::INVALID_CONSTANT_DATA );
    }

    SECTION( "constant padding must remain zero" ) {
        std::vector<byte> file = original;
        file[iConstants + 56u] = 1u;
        ResealChunk( file, 1u );
        cooked_material_view_t output{};
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status ==
                 cooked_material_status_t::NON_CANONICAL_LAYOUT );
    }

    SECTION( "mat3 column padding must remain zero" ) {
        std::vector<byte> file = original;
        // Basis begins at byte zero; its first padded column ends at byte 15.
        file[iConstants + 12u] = 1u;
        ResealChunk( file, 1u );
        cooked_material_view_t output{};
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status ==
                 cooked_material_status_t::NON_CANONICAL_LAYOUT );
        REQUIRE( read.iParameter == 0u );
    }

    SECTION( "per-chunk hashes are independently verified" ) {
        std::vector<byte> file = original;
        file[iConstants] ^= 1u;
        // Repair only the CYRS-wide seal. The stale MTCD descriptor hash must
        // still stop the type reader before any material view is published.
        ResealResource( file );
        cooked_material_view_t output{};
        output.nParameters = 77u;
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status ==
                 cooked_material_status_t::CONTENT_HASH_MISMATCH );
        REQUIRE( read.iChunk == 1u );
        REQUIRE( output.nParameters == 77u );
    }

    SECTION( "non-finite packed floats are rejected" ) {
        std::vector<byte> file = original;
        StoreLittleU32( file, iConstants, 0x7F800000u );
        ResealChunk( file, 1u );
        cooked_material_view_t output{};
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status == cooked_material_status_t::NON_FINITE_VALUE );
    }

    SECTION( "chunk identities are exact and failures are transactional" ) {
        std::vector<byte> file = original;
        const usize iConstantDescriptor = CY_COOKED_RESOURCE_HEADER_SIZE +
            CY_COOKED_RESOURCE_CHUNK_SIZE;
        StoreLittleU32(
            file,
            iConstantDescriptor,
            Cy_MakeFourCC( 'B', 'A', 'D', '!' ) );
        ResealResource( file );
        cooked_material_view_t output{};
        output.nFeatures = 91u;
        const cooked_material_result_t read = CookedMaterial_Read(
            { file.data(), file.size() },
            &output );
        REQUIRE( read.status ==
                 cooked_material_status_t::INVALID_CONSTANT_CHUNK );
        REQUIRE( output.nFeatures == 91u );
    }
}

TEST_CASE( "Cooked material reader remains compatible with V1",
           "[CypherCommon][Formats][CookedMaterial][V1]" )
{
    material_fixture_t fixture{};
    const std::vector<byte> file = WriteFixture( fixture.material );
    cooked_material_view_t view{};
    const cooked_material_result_t read = CookedMaterial_Read(
        { file.data(), file.size() },
        &view );
    REQUIRE( CookedMaterial_Succeeded( read ) );
    REQUIRE( view.nResourceVersion ==
             CY_COOKED_MATERIAL_RESOURCE_VERSION_V1 );
    REQUIRE( view.nFeatures == 0u );
    REQUIRE( view.constantData.cbSize == 0u );
    REQUIRE( !ContentHash_IsValid( view.shaderInterfaceHash ) );
}
