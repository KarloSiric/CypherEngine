//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Formats/CypherCommon_RenderAsset_Tests.cpp
//  Purpose: Tests renderer source schemas and typed decoders.
//  Details: Covers shader, texture, and material CYKV documents, semantic path and
//           identifier checks, dynamic property maps, defaults, and transactional
//           failure behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_RenderFormats.h"
#include "CypherCommon_SchemaRegistry.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

key_value_document_t *ParseAsset( const char *pSource )
{
    key_value_document_t *pDocument = KeyValue_CreateDocument( {} );
    REQUIRE( pDocument != nullptr );
    const key_value_parse_result_t result = KeyValue_ParseText(
        StringView_FromCString( pSource ),
        {},
        pDocument );
    REQUIRE( result.status == key_value_parse_status_t::OK );
    return pDocument;
}

bool_t ViewEquals( string_view_t value, const char *pText )
{
    return StringView_Equals( value, StringView_FromCString( pText ) );
}

} // namespace

TEST_CASE( "Renderer asset schemas register as exact CYKV contracts",
           "[CypherCommon][Formats][RenderAsset][Schema]" )
{
    const schema_descriptor_t *schemas[]{
        RenderShaderSchema_V1(),
        RenderTextureSchema_V1(),
        RenderMaterialSchema_V1(),
        RenderShaderSchema_V2(),
        RenderTextureSchema_V2(),
        RenderMaterialSchema_V2()
    };
    const schema_descriptor_t *storage[6]{};
    schema_registry_t registry{};
    REQUIRE( SchemaRegistry_Init( &registry, storage, 6u ) );

    for ( const schema_descriptor_t *pSchema : schemas ) {
        REQUIRE( Schema_CheckDescriptor( pSchema ) ==
                 schema_descriptor_status_t::OK );
        REQUIRE( SchemaRegistry_Register( &registry, pSchema ) ==
                 schema_registry_status_t::OK );
    }
    REQUIRE( registry.nCount == 6u );
    REQUIRE( SchemaRegistry_Find(
                 &registry,
                 StringView_FromCString( "cypher.material" ),
                 1u ) == RenderMaterialSchema_V1() );
    REQUIRE( SchemaRegistry_Find(
                 &registry,
                 StringView_FromCString( "cypher.shader" ),
                 2u ) == RenderShaderSchema_V2() );
    REQUIRE( SchemaRegistry_Find(
                 &registry,
                 StringView_FromCString( "cypher.texture" ),
                 2u ) == RenderTextureSchema_V2() );
    REQUIRE( SchemaRegistry_Find(
                 &registry,
                 StringView_FromCString( "cypher.material" ),
                 2u ) == RenderMaterialSchema_V2() );
}

TEST_CASE( "Shader source decoding validates stages and defines",
           "[CypherCommon][Formats][RenderAsset][Shader]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.shader" 1
{
    language = "glsl"
    vertex = "shaders/world.vert"
    fragment = "shaders/world.frag"
    defines = ["CY_WORLD_PASS", "CY_FOG"]
}
)cykv" );

    render_shader_source_view_t shader{};
    const render_asset_decode_result_t result = RenderShaderSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &shader );
    REQUIRE( RenderAsset_DecodeSucceeded( result ) );
    REQUIRE( shader.language == render_shader_language_t::GLSL );
    REQUIRE( ViewEquals( shader.vertexSource, "shaders/world.vert" ) );
    REQUIRE( ViewEquals( shader.fragmentSource, "shaders/world.frag" ) );
    REQUIRE( shader.nDefines == 2u );
    REQUIRE( ViewEquals( shader.defines[1], "CY_FOG" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset(
        "@cykv 1\n@schema \"cypher.shader\" 1\n"
        "{ language = \"glsl\" vertex = \"Shaders/world.vert\" "
        "fragment = \"shaders/world.frag\" }" );
    shader.vertexSource = StringView_FromCString( "unchanged" );
    const render_asset_decode_result_t badPath = RenderShaderSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &shader );
    REQUIRE( badPath.status ==
             render_asset_decode_status_t::INVALID_RESOURCE_PATH );
    REQUIRE( ViewEquals( badPath.field, "vertex" ) );
    REQUIRE( ViewEquals( shader.vertexSource, "unchanged" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset(
        "@cykv 1\n@schema \"cypher.shader\" 1\n"
        "{ language = \"glsl\" vertex = \"shaders/a.vert\" "
        "fragment = \"shaders/a.frag\" defines = [\"FOG\", \"FOG\"] }" );
    const render_asset_decode_result_t duplicate = RenderShaderSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &shader );
    REQUIRE( duplicate.status ==
             render_asset_decode_status_t::DUPLICATE_VALUE );
    REQUIRE( duplicate.iElement == 1u );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Texture source decoding applies explicit semantic defaults",
           "[CypherCommon][Formats][RenderAsset][Texture]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.texture" 1
{
    source = "textures/source/panel_n.png"
    usage = "normal"
}
)cykv" );

    render_texture_source_view_t texture{};
    const render_asset_decode_result_t result = RenderTextureSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( RenderAsset_DecodeSucceeded( result ) );
    REQUIRE( texture.usage == render_texture_usage_t::NORMAL );
    REQUIRE( texture.colorSpace == render_texture_color_space_t::LINEAR );
    REQUIRE( texture.bGenerateMips );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset(
        "@cykv 1\n@schema \"cypher.texture\" 1\n"
        "{ source = \"textures/source/panel_n.png\" "
        "usage = \"normal\" color_space = \"srgb\" }" );
    const render_asset_decode_result_t invalid = RenderTextureSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( invalid.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals( invalid.field, "color_space" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset(
        "@cykv 1\n@schema \"cypher.texture\" 1\n"
        "{ source = \"textures/source/sky.exr\" }" );
    const render_asset_decode_result_t exrDefault = RenderTextureSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( RenderAsset_DecodeSucceeded( exrDefault ) );
    REQUIRE( texture.colorSpace == render_texture_color_space_t::LINEAR );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset(
        "@cykv 1\n@schema \"cypher.texture\" 1\n"
        "{ source = \"textures/source/sky.exr\" color_space = \"srgb\" }" );
    const render_asset_decode_result_t exrSrgb = RenderTextureSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( exrSrgb.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals( exrSrgb.field, "color_space" ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Material source decoding supports typed dynamic bindings",
           "[CypherCommon][Formats][RenderAsset][Material]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.material" 1
{
    shader = "shaders/world.cyshader"
    textures = {
        base_color = "textures/panel.cytex"
        normal_map = "textures/panel_n.cytex"
    }
    parameters = {
        roughness = 1
        emissive = true
        tint = [1, 0.5, 0, 1.0]
    }
}
)cykv" );

    render_material_source_view_t material{};
    const render_asset_decode_result_t result = RenderMaterialSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &material );
    REQUIRE( RenderAsset_DecodeSucceeded( result ) );
    REQUIRE( ViewEquals( material.shader, "shaders/world.cyshader" ) );
    REQUIRE( material.nTextures == 2u );
    REQUIRE( ViewEquals( material.textures[0].binding, "base_color" ) );
    REQUIRE( ViewEquals( material.textures[1].texture,
                         "textures/panel_n.cytex" ) );
    REQUIRE( material.nParameters == 3u );
    REQUIRE( material.parameters[0].type ==
             render_material_parameter_type_t::SCALAR );
    REQUIRE( material.parameters[0].values[0] == 1.0 );
    REQUIRE( material.parameters[1].type ==
             render_material_parameter_type_t::BOOL );
    REQUIRE( material.parameters[1].bValue );
    REQUIRE( material.parameters[2].type ==
             render_material_parameter_type_t::VECTOR );
    REQUIRE( material.parameters[2].nComponents == 4u );
    REQUIRE( material.parameters[2].values[0] == 1.0 );
    REQUIRE( material.parameters[2].values[2] == 0.0 );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Material schemas reject empty maps and invalid binding paths",
           "[CypherCommon][Formats][RenderAsset][Material][Failure]" )
{
    key_value_document_t *pDocument = ParseAsset(
        "@cykv 1\n@schema \"cypher.material\" 1\n"
        "{ shader = \"shaders/world.cyshader\" textures = {} }" );
    schema_diagnostic_t diagnostic{};
    render_material_source_view_t material{};
    const render_asset_decode_result_t empty = RenderMaterialSource_Decode(
        pDocument,
        {},
        &diagnostic,
        1u,
        &material );
    REQUIRE( empty.status ==
             render_asset_decode_status_t::INVALID_DOCUMENT );
    REQUIRE( diagnostic.code == schema_diagnostic_code_t::OBJECT_LENGTH );
    REQUIRE( ViewEquals(
        StringView_FromCString( diagnostic.path ),
        "/textures" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset(
        "@cykv 1\n@schema \"cypher.material\" 1\n"
        "{ shader = \"shaders/world.cyshader\" "
        "textures = { base_color = \"textures/panel.png\" } }" );
    const render_asset_decode_result_t badTexture = RenderMaterialSource_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &material );
    REQUIRE( badTexture.status ==
             render_asset_decode_status_t::INVALID_RESOURCE_PATH );
    REQUIRE( badTexture.iElement == 0u );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Shader V2 decodes typed interface and bounded features",
           "[CypherCommon][Formats][RenderAsset][Shader][V2]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/world.vert" entry = "main" }
        fragment = { source = "shaders/world.frag" }
    }
    defines = ["CY_WORLD_PASS"]
    interface = {
        textures = {
            base_color = {
                type = "texture2d"
                usage = "color"
                color_space = "srgb"
                required = true
            }
            normal_map = {
                type = "texture2d"
                usage = "normal"
                required = false
            }
        }
        samplers = {
            surface_sampler = {
                type = "filtering"
                default = "linear_wrap"
            }
        }
        parameters = {
            roughness = {
                type = "f32"
                default = 0.6
                minimum = 0
                maximum = 1
            }
            tint = {
                type = "color4"
                default = [1, 0.5, 0.25, 1]
            }
        }
    }
    features = {
        normal_map = { type = "bool" default = false }
        alpha_mode = {
            type = "enum"
            values = ["opaque", "mask", "blend"]
            default = "opaque"
        }
        fog = { type = "bool" mode = "dynamic" default = true }
    }
    variant_budget = 8u
}
)cykv" );

    render_shader_source_v2_view_t shader{};
    const render_asset_decode_result_t result = RenderShaderSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &shader );
    REQUIRE( RenderAsset_DecodeSucceeded( result ) );
    REQUIRE( ViewEquals( shader.vertex.source, "shaders/world.vert" ) );
    REQUIRE( ViewEquals( shader.fragment.entry, "main" ) );
    REQUIRE( shader.nTextures == 2u );
    REQUIRE( shader.textures[1].usage == render_texture_usage_t::NORMAL );
    REQUIRE( shader.textures[1].colorSpace ==
             render_texture_color_space_t::LINEAR );
    REQUIRE_FALSE( shader.textures[1].bRequired );
    REQUIRE( shader.nSamplers == 1u );
    REQUIRE( ViewEquals(
        shader.samplers[0].defaultPreset,
        "linear_wrap" ) );
    REQUIRE( shader.nParameters == 2u );
    REQUIRE( shader.parameters[0].type ==
             render_shader_parameter_type_t::F32 );
    REQUIRE( shader.parameters[0].defaultValue.values[0] == 0.6 );
    REQUIRE( shader.parameters[1].defaultValue.nComponents == 4u );
    REQUIRE( shader.nFeatures == 3u );
    REQUIRE( shader.features[1].nEnumValues == 3u );
    REQUIRE( shader.features[2].mode ==
             render_shader_feature_mode_t::DYNAMIC );
    REQUIRE( shader.nVariantBudget == 8u );
    REQUIRE( shader.nStaticVariantCount == 6u );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Shader V2 rejects incompatible defaults and variant explosion",
           "[CypherCommon][Formats][RenderAsset][Shader][V2][Failure]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/a.vert" }
        fragment = { source = "shaders/a.frag" }
    }
    features = {
        detail = { type = "bool" }
        quality = {
            type = "enum"
            values = ["low", "medium", "high"]
            default = "high"
        }
    }
    variant_budget = 4u
}
)cykv" );
    render_shader_source_v2_view_t shader{};
    shader.nVariantBudget = 777u;
    const render_asset_decode_result_t variants = RenderShaderSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &shader );
    REQUIRE( variants.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals( variants.field, "variant_budget" ) );
    REQUIRE( shader.nVariantBudget == 777u );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/a.vert" }
        fragment = { source = "shaders/a.frag" }
    }
    interface = {
        parameters = {
            tint = { type = "color4" default = [1, 1, 1] }
        }
    }
}
)cykv" );
    const render_asset_decode_result_t badDefault =
        RenderShaderSourceV2_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &shader );
    REQUIRE( badDefault.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals(
        badDefault.field,
        "interface.parameters.default" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.shader" 2
{
    language = "glsl"
    stages = {
        vertex = { source = "shaders/a.vert" }
        fragment = { source = "shaders/a.frag" }
    }
    features = {
        quality = {
            type = "enum"
            values = ["low", "low"]
            default = "low"
        }
    }
}
)cykv" );
    const render_asset_decode_result_t duplicateEnum =
        RenderShaderSourceV2_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &shader );
    REQUIRE( duplicateEnum.status ==
             render_asset_decode_status_t::DUPLICATE_VALUE );
    REQUIRE( ViewEquals( duplicateEnum.field, "features.values" ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Texture V2 decodes alpha mip output and streaming policies",
           "[CypherCommon][Formats][RenderAsset][Texture][V2]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.texture" 2
{
    source = "textures/source/fence.png"
    type = "2d"
    usage = "color"
    color_space = "srgb"
    alpha = {
        mode = "mask"
        cutoff = 0.45
        dilate_rgb = true
    }
    mips = {
        mode = "generate"
        filter = "kaiser"
        edge = "repeat"
        preserve_alpha_coverage = true
    }
    output = { format = "auto" quality = "production" }
    streaming = { class = "world" resident_mips = 3u }
}
)cykv" );

    render_texture_source_v2_view_t texture{};
    const render_asset_decode_result_t result = RenderTextureSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( RenderAsset_DecodeSucceeded( result ) );
    REQUIRE( texture.type == render_texture_type_t::TEXTURE_2D );
    REQUIRE( texture.alpha.mode == render_texture_alpha_mode_t::MASK );
    REQUIRE( texture.alpha.cutoff == 0.45 );
    REQUIRE( texture.alpha.bDilateRgb );
    REQUIRE( texture.mips.filter == render_texture_mip_filter_t::KAISER );
    REQUIRE( texture.mips.edge == render_texture_edge_mode_t::REPEAT );
    REQUIRE( texture.mips.bPreserveAlphaCoverage );
    REQUIRE( texture.output.quality ==
             render_texture_output_quality_t::PRODUCTION );
    REQUIRE( texture.streaming.bEnabled );
    REQUIRE( ViewEquals( texture.streaming.resourceClass, "world" ) );
    REQUIRE( texture.streaming.nResidentMipCount == 3u );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.texture" 2
{
    source = "textures/source/masks.png"
    type = "2d"
    usage = "data"
    color_space = "linear"
}
)cykv" );
    const render_asset_decode_result_t defaults = RenderTextureSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( RenderAsset_DecodeSucceeded( defaults ) );
    REQUIRE( texture.alpha.mode == render_texture_alpha_mode_t::NONE );
    REQUIRE( texture.mips.mode == render_texture_mip_mode_t::GENERATE );
    REQUIRE( texture.mips.filter == render_texture_mip_filter_t::BOX );
    REQUIRE( texture.output.format == render_texture_output_format_t::AUTO );
    REQUIRE( texture.output.quality ==
             render_texture_output_quality_t::BALANCED );
    REQUIRE_FALSE( texture.streaming.bEnabled );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Texture V2 rejects policy combinations that cannot be cooked",
           "[CypherCommon][Formats][RenderAsset][Texture][V2][Failure]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.texture" 2
{
    source = "textures/source/panel.png"
    type = "2d"
    usage = "color"
    color_space = "srgb"
    alpha = { mode = "straight" }
    mips = { mode = "generate" preserve_alpha_coverage = true }
}
)cykv" );
    render_texture_source_v2_view_t texture{};
    const render_asset_decode_result_t coverage = RenderTextureSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( coverage.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals(
        coverage.field,
        "mips.preserve_alpha_coverage" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.texture" 2
{
    source = "textures/source/panel.png"
    type = "2d"
    usage = "color"
    color_space = "srgb"
    mips = { mode = "preserve" }
}
)cykv" );
    const render_asset_decode_result_t preserve = RenderTextureSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &texture );
    REQUIRE( preserve.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals( preserve.field, "mips.mode" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.texture" 2
{
    source = "textures/source/panel.png"
    type = "2d"
    usage = "color"
    color_space = "srgb"
    streaming = { class = "world" resident_mips = 16u }
}
)cykv" );
    schema_diagnostic_t diagnostic{};
    const render_asset_decode_result_t residentBound =
        RenderTextureSourceV2_Decode(
            pDocument,
            {},
            &diagnostic,
            1u,
            &texture );
    REQUIRE( residentBound.status ==
             render_asset_decode_status_t::INVALID_DOCUMENT );
    REQUIRE( diagnostic.code == schema_diagnostic_code_t::U64_RANGE );
    REQUIRE( ViewEquals(
        StringView_FromCString( diagnostic.path ),
        "/streaming/resident_mips" ) );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Material V2 decodes inheritance semantic state and overrides",
           "[CypherCommon][Formats][RenderAsset][Material][V2]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.material" 2
{
    base = "materials/templates/world_surface.cymat"
    shader = "shaders/world.cyshader"
    domain = "surface"
    surface = "surfaces/concrete.cysurface"
    features = {
        normal_map = true
        alpha_mode = "mask"
        legacy_detail = null
    }
    state = {
        alpha_mode = "mask"
        alpha_cutoff = 0.5
        two_sided = false
        casts_shadows = true
        receives_shadows = true
    }
    textures = {
        base_color = {
            resource = "textures/wall/base_color.cytex"
            sampler = "linear_wrap"
            uv = {
                set = 1u
                scale = [2, 2]
                offset = [0.25, 0]
                rotation = 0.5
            }
        }
        normal_map = { sampler = "anisotropic_wrap" }
        old_mask = null
    }
    parameters = {
        enabled = true
        signed_value = -7
        unsigned_value = 7u
        roughness = 0.6
        transform = [1, 0, 0, 1, 0, 0, 0, 1, 0]
        obsolete = null
    }
}
)cykv" );

    render_material_source_v2_view_t material{};
    const render_asset_decode_result_t result = RenderMaterialSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &material );
    REQUIRE( RenderAsset_DecodeSucceeded( result ) );
    REQUIRE( material.bHasBase );
    REQUIRE( material.bHasShader );
    REQUIRE( material.bHasSurface );
    REQUIRE( material.domain == render_material_domain_t::SURFACE );
    REQUIRE( material.nFeatures == 3u );
    REQUIRE( material.features[0].bValue );
    REQUIRE( ViewEquals( material.features[1].enumValue, "mask" ) );
    REQUIRE( material.features[2].bRemove );
    REQUIRE( material.state.alphaMode == render_material_alpha_mode_t::MASK );
    REQUIRE( material.state.bHasAlphaCutoff );
    REQUIRE( material.nTextures == 3u );
    REQUIRE( material.textures[0].bHasResource );
    REQUIRE( material.textures[0].uv.nSet == 1u );
    REQUIRE( material.textures[0].uv.scale[0] == 2.0 );
    REQUIRE_FALSE( material.textures[1].bHasResource );
    REQUIRE( material.textures[2].bRemove );
    REQUIRE( material.nParameters == 6u );
    REQUIRE( material.parameters[1].value.type ==
             render_asset_value_type_t::I64 );
    REQUIRE( material.parameters[2].value.type ==
             render_asset_value_type_t::U64 );
    REQUIRE( material.parameters[4].value.nComponents == 9u );
    REQUIRE( material.parameters[5].bRemove );
    KeyValue_DestroyDocument( pDocument );
}

TEST_CASE( "Material V2 enforces standalone completeness and inheritance removal",
           "[CypherCommon][Formats][RenderAsset][Material][V2][Failure]" )
{
    key_value_document_t *pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.material" 2
{
    shader = "shaders/world.cyshader"
    textures = { base_color = { sampler = "linear_wrap" } }
}
)cykv" );
    render_material_source_v2_view_t material{};
    const render_asset_decode_result_t missingTexture =
        RenderMaterialSourceV2_Decode(
            pDocument,
            {},
            nullptr,
            0u,
            &material );
    REQUIRE( missingTexture.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals( missingTexture.field, "textures.resource" ) );
    KeyValue_DestroyDocument( pDocument );

    pDocument = ParseAsset( R"cykv(@cykv 1
@schema "cypher.material" 2
{
    shader = "shaders/world.cyshader"
    parameters = { inherited_only = null }
}
)cykv" );
    const render_asset_decode_result_t removal = RenderMaterialSourceV2_Decode(
        pDocument,
        {},
        nullptr,
        0u,
        &material );
    REQUIRE( removal.status ==
             render_asset_decode_status_t::INVALID_COMBINATION );
    REQUIRE( ViewEquals( removal.field, "parameters" ) );
    KeyValue_DestroyDocument( pDocument );
}
