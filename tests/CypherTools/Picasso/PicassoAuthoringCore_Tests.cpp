//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherTools/Picasso/PicassoAuthoringCore_Tests.cpp
//  Purpose: Tests Picasso channel, texture-set, and paint-material contracts.
//  Details: Coverage protects semantic storage policy, lazy channel ownership,
//           transactional publication, revisions, and canonical material paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "PicassoChannel.h"
#include "PicassoMaterialImport.h"
#include "PicassoPaintMaterial.h"
#include "PicassoTextureSet.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_ImageProcess.h"
#include "CypherCommon_ImageView.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::common;
using namespace cypher::tools::picasso;

namespace
{

string_view_t Text( const char *pText, usize cchLength ) noexcept
{
    return { pText, cchLength };
}

const byte *FirstPixel( const image_surface_t &surface ) noexcept
{
    return ImageView_GetPixel(
        ImageSurface_GetView( &surface ),
        0u,
        0u,
        0u ).pData;
}

} // namespace

TEST_CASE( "Picasso channels define stable semantic storage policy",
           "[CypherTools][Picasso][Channel]" )
{
    picasso_channel_mask_t combined = 0u;
    for ( usize iChannel = 0u;
          iChannel < PICASSO_CHANNEL_COUNT;
          ++iChannel ) {
        const auto semantic =
            static_cast<picasso_channel_semantic_t>( iChannel );
        const picasso_channel_mask_t bit = PicassoChannel_Bit( semantic );
        REQUIRE( bit != 0u );
        REQUIRE( ( combined & bit ) == 0u );
        combined |= bit;
        REQUIRE( PicassoChannel_ValidateDesc(
                     PicassoChannel_DefaultDesc( semantic ) ) ==
                 picasso_channel_status_t::OK );
        REQUIRE( PicassoChannel_Name( semantic ) != nullptr );
    }

    picasso_channel_desc_t invalidBase = PicassoChannel_DefaultDesc(
        picasso_channel_semantic_t::BASE_COLOR );
    invalidBase.pixelFormat = image_pixel_format_t::R8_UNORM;
    REQUIRE( PicassoChannel_ValidateDesc( invalidBase ) ==
             picasso_channel_status_t::INVALID_PIXEL_FORMAT );

    picasso_channel_desc_t invalidRoughness = PicassoChannel_DefaultDesc(
        picasso_channel_semantic_t::ROUGHNESS );
    invalidRoughness.colorSpace = image_color_space_t::SRGB;
    REQUIRE( PicassoChannel_ValidateDesc( invalidRoughness ) ==
             picasso_channel_status_t::INVALID_COLOR_SPACE );

    picasso_channel_desc_t compactNormal = PicassoChannel_DefaultDesc(
        picasso_channel_semantic_t::NORMAL );
    compactNormal.pixelFormat = image_pixel_format_t::RG16_UNORM;
    REQUIRE( PicassoChannel_ValidateDesc( compactNormal ) ==
             picasso_channel_status_t::OK );
}

TEST_CASE( "Picasso channel encoding follows format and color-space policy",
           "[CypherTools][Picasso][Channel][Encoding]" )
{
    byte encoded[16]{};
    REQUIRE( PicassoChannel_EncodePixel(
                 PicassoChannel_DefaultDesc(
                     picasso_channel_semantic_t::BASE_COLOR ),
                 { 1.0f, 0.0f, 0.0f, 1.0f },
                 { encoded, sizeof( encoded ) } ) ==
             picasso_channel_status_t::OK );
    REQUIRE( encoded[0] == 255u );
    REQUIRE( encoded[1] == 0u );
    REQUIRE( encoded[2] == 0u );
    REQUIRE( encoded[3] == 255u );

    REQUIRE( PicassoChannel_EncodePixel(
                 PicassoChannel_DefaultDesc(
                     picasso_channel_semantic_t::HEIGHT ),
                 PicassoChannel_DefaultValue(
                     picasso_channel_semantic_t::HEIGHT ),
                 { encoded, 1u } ) ==
             picasso_channel_status_t::OUTPUT_TOO_SMALL );
}

TEST_CASE( "Picasso texture sets allocate semantic channels lazily",
           "[CypherTools][Picasso][TextureSet][Ownership]" )
{
    picasso_texture_set_t textureSet{};
    REQUIRE( PicassoTextureSet_Init(
                 &textureSet,
                 Allocator_GetSystem() ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE( PicassoTextureSet_Create( &textureSet, 4u, 2u ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE( PicassoTextureSet_ChannelCount( &textureSet ) == 0u );

    REQUIRE( PicassoTextureSet_AddDefaultChannel(
                 &textureSet,
                 picasso_channel_semantic_t::BASE_COLOR ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE( PicassoTextureSet_AddDefaultChannel(
                 &textureSet,
                 picasso_channel_semantic_t::NORMAL ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE( PicassoTextureSet_ChannelCount( &textureSet ) == 2u );
    REQUIRE( PicassoTextureSet_IsValid( &textureSet ) );

    const image_surface_t *pBase = PicassoTextureSet_GetChannel(
        &textureSet,
        picasso_channel_semantic_t::BASE_COLOR );
    const image_surface_t *pNormal = PicassoTextureSet_GetChannel(
        &textureSet,
        picasso_channel_semantic_t::NORMAL );
    REQUIRE( pBase != nullptr );
    REQUIRE( pNormal != nullptr );
    REQUIRE( FirstPixel( *pBase )[3] == 0u );
    REQUIRE( FirstPixel( *pNormal )[0] == 128u );
    REQUIRE( FirstPixel( *pNormal )[1] == 128u );
    REQUIRE( FirstPixel( *pNormal )[2] == 255u );

    REQUIRE( PicassoTextureSet_AddDefaultChannel(
                 &textureSet,
                 picasso_channel_semantic_t::NORMAL ) ==
             picasso_texture_set_status_t::CHANNEL_EXISTS );
    REQUIRE( PicassoTextureSet_ByteSize( &textureSet ) > 0u );

    PicassoTextureSet_Shutdown( &textureSet );
    REQUIRE_FALSE( PicassoTextureSet_IsCreated( &textureSet ) );
}

TEST_CASE( "Picasso texture-set replacement is transactional",
           "[CypherTools][Picasso][TextureSet][Transaction]" )
{
    picasso_texture_set_t textureSet{};
    REQUIRE( PicassoTextureSet_Init(
                 &textureSet,
                 Allocator_GetSystem() ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE( PicassoTextureSet_Create( &textureSet, 2u, 2u ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE( PicassoTextureSet_AddDefaultChannel(
                 &textureSet,
                 picasso_channel_semantic_t::BASE_COLOR ) ==
             picasso_texture_set_status_t::OK );

    const u64 revisionBefore = textureSet.nRevision;
    byte mismatchedPixels[4u * 4u * 4u]{};
    const const_image_view_t mismatched{
        {
            { 4u, 4u, 1u },
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT
        },
        { mismatchedPixels, sizeof( mismatchedPixels ) },
        16u,
        sizeof( mismatchedPixels )
    };
    REQUIRE( PicassoTextureSet_SetChannelFromView(
                 &textureSet,
                 picasso_channel_semantic_t::BASE_COLOR,
                 mismatched ) ==
             picasso_texture_set_status_t::EXTENT_MISMATCH );
    REQUIRE( textureSet.nRevision == revisionBefore );
    REQUIRE( PicassoTextureSet_IsValid( &textureSet ) );

    image_surface_t adopted{};
    const image_desc_t adoptedDesc{
        { 2u, 2u, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
    REQUIRE( ImageSurface_Create(
                 &adopted,
                 Allocator_GetSystem(),
                 adoptedDesc,
                 image_surface_init_t::ZEROED,
                 PICASSO_TEXTURE_ROW_ALIGNMENT ) ==
             image_surface_status_t::OK );
    REQUIRE( PicassoTextureSet_AdoptChannelSurface(
                 &textureSet,
                 picasso_channel_semantic_t::BASE_COLOR,
                 &adopted ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE( ImageSurface_IsEmpty( &adopted ) );
    REQUIRE( textureSet.nRevision > revisionBefore );

    REQUIRE( PicassoTextureSet_RemoveChannel(
                 &textureSet,
                 picasso_channel_semantic_t::BASE_COLOR ) ==
             picasso_texture_set_status_t::OK );
    REQUIRE_FALSE( PicassoTextureSet_HasChannel(
        &textureSet,
        picasso_channel_semantic_t::BASE_COLOR ) );
    PicassoTextureSet_Shutdown( &textureSet );
}

TEST_CASE( "Picasso paint materials own multi-channel authoring sources",
           "[CypherTools][Picasso][PaintMaterial]" )
{
    picasso_paint_material_t material{};
    REQUIRE( PicassoPaintMaterial_Init(
                 &material,
                 Text( "Weathered Steel", 15u ) ) ==
             picasso_paint_material_status_t::OK );
    REQUIRE( PicassoPaintMaterial_SetConstant(
                 &material,
                 picasso_channel_semantic_t::BASE_COLOR,
                 { 0.22f, 0.24f, 0.26f, 1.0f } ) ==
             picasso_paint_material_status_t::OK );
    REQUIRE( PicassoPaintMaterial_SetTexture(
                 &material,
                 picasso_channel_semantic_t::NORMAL,
                 Text( "textures/metal/steel_normal.cytex", 33u ),
                 0.75f ) == picasso_paint_material_status_t::OK );

    picasso_material_mapping_t mapping{};
    mapping.scaleU = 4.0f;
    mapping.scaleV = 4.0f;
    mapping.rotationDegrees = 15.0f;
    REQUIRE( PicassoPaintMaterial_SetMapping(
                 &material,
                 picasso_channel_semantic_t::NORMAL,
                 mapping ) == picasso_paint_material_status_t::OK );

    REQUIRE( PicassoPaintMaterial_IsValid( &material ) );
    REQUIRE( PicassoPaintMaterial_ChannelCount( &material ) == 2u );
    const picasso_material_channel_t *pNormal =
        PicassoPaintMaterial_GetChannel(
            &material,
            picasso_channel_semantic_t::NORMAL );
    REQUIRE( pNormal != nullptr );
    REQUIRE( pNormal->kind ==
             picasso_material_source_kind_t::TEXTURE_RESOURCE );
    REQUIRE( pNormal->strength == 0.75f );
    REQUIRE( FixedString_Equals(
        pNormal->texture,
        Text( "textures/metal/steel_normal.cytex", 33u ) ) );

    REQUIRE( PicassoPaintMaterial_DisableChannel(
                 &material,
                 picasso_channel_semantic_t::NORMAL ) ==
             picasso_paint_material_status_t::OK );
    REQUIRE( PicassoPaintMaterial_ChannelCount( &material ) == 1u );
    REQUIRE( PicassoPaintMaterial_IsValid( &material ) );
}

TEST_CASE( "Picasso paint materials reject invalid authored state",
           "[CypherTools][Picasso][PaintMaterial][Failure]" )
{
    picasso_paint_material_t material{};
    REQUIRE( PicassoPaintMaterial_Init(
                 &material,
                 Text( "Test", 4u ) ) ==
             picasso_paint_material_status_t::OK );

    REQUIRE( PicassoPaintMaterial_SetTexture(
                 &material,
                 picasso_channel_semantic_t::ROUGHNESS,
                 Text( "Textures/Bad.cytex", 18u ) ) ==
             picasso_paint_material_status_t::INVALID_RESOURCE_PATH );
    REQUIRE( PicassoPaintMaterial_SetTexture(
                 &material,
                 picasso_channel_semantic_t::ROUGHNESS,
                 Text( "../bad.cytex", 12u ) ) ==
             picasso_paint_material_status_t::INVALID_RESOURCE_PATH );
    REQUIRE( PicassoPaintMaterial_SetConstant(
                 &material,
                 picasso_channel_semantic_t::ROUGHNESS,
                 {
                     std::numeric_limits<f32>::quiet_NaN(),
                     0.0f,
                     0.0f,
                     1.0f
                 } ) == picasso_paint_material_status_t::INVALID_VALUE );

    picasso_material_mapping_t collapsed{};
    collapsed.scaleU = 0.0f;
    REQUIRE( PicassoPaintMaterial_SetMapping(
                 &material,
                 picasso_channel_semantic_t::ROUGHNESS,
                 collapsed ) ==
             picasso_paint_material_status_t::INVALID_MAPPING );
    REQUIRE( PicassoPaintMaterial_IsValid( &material ) );
}

TEST_CASE( "Picasso imports standard material bindings transactionally",
           "[CypherTools][Picasso][MaterialImport]" )
{
    constexpr const char *SOURCE = R"cykv(@cykv 1
@schema "cypher.material" 1
{
    shader = "shaders/world.cyshader"
    textures = {
        base_color = "textures/wall/base.cytex"
        normal_map = "textures/wall/normal.cytex"
        shader_detail = "textures/wall/detail.cytex"
    }
    parameters = {
        roughness = 0.7
        tint = [1, 0.8, 0.6, 1]
    }
}
)cykv";

    picasso_paint_material_t material{};
    const picasso_material_import_result_t result =
        PicassoMaterialImport_FromText(
            Text( "Concrete Wall", 13u ),
            StringView_FromCString( SOURCE ),
            &material );
    REQUIRE( result.status == picasso_material_import_status_t::OK );
    REQUIRE( result.nTexturesImported == 2u );
    REQUIRE( result.nTexturesSkipped == 1u );
    REQUIRE( result.nParametersSkipped == 2u );
    REQUIRE( PicassoPaintMaterial_IsValid( &material ) );
    REQUIRE( FixedString_Equals(
        material.shader,
        Text( "shaders/world.cyshader", 22u ) ) );
    REQUIRE( PicassoPaintMaterial_HasChannel(
        &material,
        picasso_channel_semantic_t::BASE_COLOR ) );
    REQUIRE( PicassoPaintMaterial_HasChannel(
        &material,
        picasso_channel_semantic_t::NORMAL ) );
}

TEST_CASE( "Picasso material import preserves output on invalid CYKV",
           "[CypherTools][Picasso][MaterialImport][Failure]" )
{
    picasso_paint_material_t material{};
    REQUIRE( PicassoPaintMaterial_Init(
                 &material,
                 Text( "Existing", 8u ) ) ==
             picasso_paint_material_status_t::OK );
    const u64 revisionBefore = material.nRevision;

    const picasso_material_import_result_t result =
        PicassoMaterialImport_FromText(
            Text( "Broken", 6u ),
            StringView_FromCString(
                "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = }" ),
            &material );
    REQUIRE( result.status ==
             picasso_material_import_status_t::CYKV_PARSE_FAILED );
    REQUIRE( result.sourceLocation.nLine != 0u );
    REQUIRE( material.nRevision == revisionBefore );
    REQUIRE( FixedString_Equals( material.name, Text( "Existing", 8u ) ) );
}
