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
#include "CypherCommon_RenderAsset.h"
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
        RenderMaterialSchema_V1()
    };
    const schema_descriptor_t *storage[3]{};
    schema_registry_t registry{};
    REQUIRE( SchemaRegistry_Init( &registry, storage, 3u ) );

    for ( const schema_descriptor_t *pSchema : schemas ) {
        REQUIRE( Schema_CheckDescriptor( pSchema ) ==
                 schema_descriptor_status_t::OK );
        REQUIRE( SchemaRegistry_Register( &registry, pSchema ) ==
                 schema_registry_status_t::OK );
    }
    REQUIRE( registry.nCount == 3u );
    REQUIRE( SchemaRegistry_Find(
                 &registry,
                 StringView_FromCString( "cypher.material" ),
                 1u ) == RenderMaterialSchema_V1() );
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
