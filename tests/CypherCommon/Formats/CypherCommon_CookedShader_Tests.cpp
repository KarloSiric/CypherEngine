//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Formats/CypherCommon_CookedShader_Tests.cpp
//  Purpose: Tests the backend-neutral cooked shader resource contract.
//  Details: Covers deterministic OpenGL GLSL packaging, metadata serialization,
//           stage lookup, invalid stage sets, malformed payloads, hash failures,
//           and transactional read behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedShader.h"
#include "CypherCommon_ByteWriter.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

constexpr char g_vertexSource[] =
    "#version 410 core\n"
    "void main() { gl_Position = vec4(0.0); }\n";
constexpr char g_fragmentSource[] =
    "#version 410 core\n"
    "out vec4 color;\n"
    "void main() { color = vec4(1.0); }\n";

binary_block_t TextBlock( const char *pText, usize cbText ) noexcept
{
    return {
        reinterpret_cast<const byte *>( pText ),
        cbText
    };
}

void MakeGraphicsStages(
    cooked_shader_stage_source_t ( &stages )[2] ) noexcept
{
    stages[0].stage = render_shader_stage_t::VERTEX;
    stages[0].code = TextBlock( g_vertexSource, sizeof( g_vertexSource ) );
    stages[1].stage = render_shader_stage_t::FRAGMENT;
    stages[1].code = TextBlock(
        g_fragmentSource,
        sizeof( g_fragmentSource ) );
}

u64 BindingId( string_view_t name ) noexcept
{
    u64 bindingId = 0u;
    REQUIRE( CookedShader_MakeLogicalBindingId( name, &bindingId ) );
    REQUIRE( bindingId != 0u );
    return bindingId;
}

cooked_shader_binding_source_t MakeTextureBinding(
    const char *pName ) noexcept
{
    cooked_shader_binding_source_t binding{};
    binding.name = StringView_FromCString( pName );
    binding.kind = render_shader_binding_kind_t::SAMPLED_TEXTURE;
    binding.resourceType = render_shader_resource_type_t::TEXTURE_2D;
    binding.stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    binding.flags = COOKED_SHADER_BINDING_FLAG_REQUIRED |
                    COOKED_SHADER_BINDING_FLAG_MATERIAL;
    binding.nLogicalBinding = BindingId( binding.name );
    return binding;
}

void SealCookedFile(
    byte *pFile,
    usize cbFile,
    cooked_resource_header_t &header,
    cooked_chunk_desc_t *pChunks,
    usize nChunks ) noexcept
{
    header.flags &= ~COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = {};
    REQUIRE( CookedResource_Succeeded( CookedResource_WriteLayout(
        header,
        { pChunks, nChunks },
        { pFile, cbFile } ) ) );
    header.flags |= COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = CookedResource_ComputeContentHash(
        { pFile, cbFile } );
    REQUIRE( CookedResource_Succeeded( CookedResource_WriteLayout(
        header,
        { pChunks, nChunks },
        { pFile, cbFile } ) ) );
}

} // namespace

TEST_CASE( "Cooked OpenGL shaders round trip through canonical CYRS files",
           "[CypherCommon][Formats][CookedShader]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    const cooked_shader_desc_t shader{};
    const usize cbRequired = CookedShader_RequiredSize(
        shader,
        { stages, 2u } );
    REQUIRE( cbRequired > CY_COOKED_RESOURCE_HEADER_SIZE );

    byte file[1024]{};
    REQUIRE( cbRequired <= sizeof( file ) );
    const content_hash_t sourceHash = ContentHash_String(
        { "shaders/world.cyshader", 22u } );
    const cooked_shader_result_t written = CookedShader_Write(
        shader,
        { stages, 2u },
        sourceHash,
        { file, cbRequired } );
    REQUIRE( CookedShader_Succeeded( written ) );
    REQUIRE( written.cbRequired == cbRequired );
    REQUIRE( written.cbWritten == cbRequired );

    cooked_shader_view_t view{};
    const cooked_shader_result_t read = CookedShader_Read(
        { file, cbRequired },
        &view );
    REQUIRE( CookedShader_Succeeded( read ) );
    REQUIRE( read.cbRead == cbRequired );
    REQUIRE( view.nResourceVersion ==
             CY_COOKED_SHADER_RESOURCE_VERSION_V2 );
    REQUIRE( view.backend == render_shader_backend_t::OPENGL );
    REQUIRE( view.kind == render_shader_program_kind_t::GRAPHICS );
    REQUIRE( view.languageProfile ==
             render_shader_language_profile_t::GLSL_CORE );
    REQUIRE( view.nLanguageVersion == 410u );
    REQUIRE( view.nStages == 2u );
    REQUIRE( view.nBindings == 0u );
    REQUIRE_FALSE( ContentHash_IsValid( view.interfaceHash ) );
    REQUIRE( ContentHash_Equals( view.sourceHash, sourceHash ) );

    const cooked_shader_stage_view_t *pVertex = CookedShader_FindStage(
        view,
        render_shader_stage_t::VERTEX );
    const cooked_shader_stage_view_t *pFragment = CookedShader_FindStage(
        view,
        render_shader_stage_t::FRAGMENT );
    REQUIRE( pVertex != nullptr );
    REQUIRE( pFragment != nullptr );
    REQUIRE( pVertex->codeFormat == render_shader_code_format_t::GLSL_UTF8 );
    REQUIRE( pVertex->code.cbSize == sizeof( g_vertexSource ) );
    REQUIRE( Cy_MemEqual(
        pVertex->code.pData,
        g_vertexSource,
        sizeof( g_vertexSource ) ) );
    REQUIRE( Cy_MemEqual(
        pFragment->code.pData,
        g_fragmentSource,
        sizeof( g_fragmentSource ) ) );
    REQUIRE( CookedShader_FindStage(
                 view,
                 static_cast<render_shader_stage_t>( 99u ) ) == nullptr );
}

TEST_CASE( "Cooked shader writers are deterministic",
           "[CypherCommon][Formats][CookedShader][Determinism]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    const usize cbRequired = CookedShader_RequiredSize(
        {},
        { stages, 2u } );
    REQUIRE( cbRequired <= 1024u );

    byte first[1024];
    byte second[1024];
    Cy_MemSet( first, 0xA5u, sizeof( first ) );
    Cy_MemSet( second, 0x5Au, sizeof( second ) );
    REQUIRE( CookedShader_Succeeded( CookedShader_Write(
        {}, { stages, 2u }, {}, { first, cbRequired } ) ) );
    REQUIRE( CookedShader_Succeeded( CookedShader_Write(
        {}, { stages, 2u }, {}, { second, cbRequired } ) ) );
    REQUIRE( Cy_MemEqual( first, second, cbRequired ) );
}

TEST_CASE( "Cooked shader V3 interfaces round trip canonically",
           "[CypherCommon][Formats][CookedShader][Reflection]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );

    cooked_shader_binding_source_t bindings[3]{};
    bindings[0].name = StringView_FromCString( "tint" );
    bindings[0].kind = render_shader_binding_kind_t::VALUE;
    bindings[0].valueType = render_shader_value_type_t::F32X4;
    bindings[0].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    bindings[0].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    bindings[0].nLogicalBinding = BindingId( bindings[0].name );
    bindings[0].iByteOffset = 0u;
    bindings[0].cbByteSize = 16u;

    bindings[1] = MakeTextureBinding( "base_color" );

    bindings[2].name = StringView_FromCString( "surface_sampler" );
    bindings[2].kind = render_shader_binding_kind_t::SAMPLER;
    bindings[2].resourceType = render_shader_resource_type_t::SAMPLER;
    bindings[2].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    bindings[2].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    bindings[2].nLogicalBinding = BindingId( bindings[2].name );

    const cooked_shader_interface_source_t shaderInterface{
        { bindings, 3u }
    };
    content_hash_t expectedInterfaceHash{};
    REQUIRE( CookedShader_ComputeInterfaceHash(
        shaderInterface,
        &expectedInterfaceHash ) );
    REQUIRE( ContentHash_IsValid( expectedInterfaceHash ) );
    const usize cbRequired = CookedShader_RequiredSizeV3(
        {},
        { stages, 2u },
        shaderInterface );
    REQUIRE( cbRequired > CookedShader_RequiredSize(
        {},
        { stages, 2u } ) );
    REQUIRE( cbRequired <= 2048u );

    byte file[2048];
    Cy_MemSet( file, 0xA5u, sizeof( file ) );
    const content_hash_t sourceHash = ContentHash_String(
        StringView_FromCString( "shaders/world_v3.cyshader" ) );
    const cooked_shader_result_t written = CookedShader_WriteV3(
        {},
        { stages, 2u },
        shaderInterface,
        sourceHash,
        { file, cbRequired } );
    REQUIRE( CookedShader_Succeeded( written ) );
    REQUIRE( written.cbWritten == cbRequired );

    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[5]{};
    REQUIRE( CookedResource_Succeeded( CookedResource_ReadLayout(
        { file, cbRequired },
        &header,
        Span_FromArray( chunks ) ) ) );
    REQUIRE( header.nResourceVersion ==
             CY_COOKED_SHADER_RESOURCE_VERSION_V3 );
    REQUIRE( header.nChunks == 5u );
    REQUIRE( chunks[0].chunkType == CY_COOKED_SHADER_METADATA_CHUNK );
    REQUIRE( chunks[1].chunkType == CY_COOKED_SHADER_REFLECTION_CHUNK );
    REQUIRE( chunks[2].chunkType == CY_COOKED_SHADER_STRING_CHUNK );
    REQUIRE( chunks[3].chunkType == CY_COOKED_SHADER_CODE_CHUNK );
    REQUIRE( chunks[4].chunkType == CY_COOKED_SHADER_CODE_CHUNK );
    REQUIRE( chunks[0].cbStored == CookedShader_MetadataSizeV3( 2u ) );
    REQUIRE( chunks[1].cbStored == CookedShader_ReflectionSize( 3u ) );

    cooked_shader_view_t view{};
    const cooked_shader_result_t read = CookedShader_Read(
        { file, cbRequired },
        &view );
    REQUIRE( CookedShader_Succeeded( read ) );
    REQUIRE( view.nResourceVersion ==
             CY_COOKED_SHADER_RESOURCE_VERSION_V3 );
    REQUIRE( view.nStages == 2u );
    REQUIRE( view.nBindings == 3u );
    REQUIRE( ContentHash_Equals(
        view.interfaceHash,
        expectedInterfaceHash ) );
    REQUIRE( ContentHash_Equals( view.sourceHash, sourceHash ) );
    REQUIRE( StringView_Equals(
        view.bindings[0].name,
        StringView_FromCString( "base_color" ) ) );
    REQUIRE( StringView_Equals(
        view.bindings[1].name,
        StringView_FromCString( "surface_sampler" ) ) );
    REQUIRE( StringView_Equals(
        view.bindings[2].name,
        StringView_FromCString( "tint" ) ) );

    const cooked_shader_binding_view_t *pTexture = CookedShader_FindBinding(
        view,
        StringView_FromCString( "base_color" ) );
    REQUIRE( pTexture != nullptr );
    REQUIRE( pTexture->kind ==
             render_shader_binding_kind_t::SAMPLED_TEXTURE );
    REQUIRE( pTexture->resourceType ==
             render_shader_resource_type_t::TEXTURE_2D );
    REQUIRE( CookedShader_FindBindingById(
                 view,
                 pTexture->nLogicalBinding ) == pTexture );
    const cooked_shader_binding_view_t *pTint = CookedShader_FindBinding(
        view,
        StringView_FromCString( "tint" ) );
    REQUIRE( pTint != nullptr );
    REQUIRE( pTint->valueType == render_shader_value_type_t::F32X4 );
    REQUIRE( pTint->iByteOffset == 0u );
    REQUIRE( pTint->cbByteSize == 16u );
    REQUIRE( CookedShader_FindBinding(
                 view,
                 StringView_FromCString( "missing" ) ) == nullptr );
}

TEST_CASE( "Cooked shader V3 interface bytes ignore source binding order",
           "[CypherCommon][Formats][CookedShader][Reflection][Determinism]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    cooked_shader_binding_source_t firstBindings[2]{
        MakeTextureBinding( "normal_map" ),
        MakeTextureBinding( "base_color" )
    };
    cooked_shader_binding_source_t secondBindings[2]{
        firstBindings[1],
        firstBindings[0]
    };
    const cooked_shader_interface_source_t firstInterface{
        { firstBindings, 2u }
    };
    const cooked_shader_interface_source_t secondInterface{
        { secondBindings, 2u }
    };
    const usize cbRequired = CookedShader_RequiredSizeV3(
        {}, { stages, 2u }, firstInterface );
    REQUIRE( cbRequired == CookedShader_RequiredSizeV3(
        {}, { stages, 2u }, secondInterface ) );
    REQUIRE( cbRequired <= 2048u );

    content_hash_t firstHash{};
    content_hash_t secondHash{};
    REQUIRE( CookedShader_ComputeInterfaceHash(
        firstInterface, &firstHash ) );
    REQUIRE( CookedShader_ComputeInterfaceHash(
        secondInterface, &secondHash ) );
    REQUIRE( ContentHash_Equals( firstHash, secondHash ) );

    byte first[2048];
    byte second[2048];
    Cy_MemSet( first, 0xA5u, sizeof( first ) );
    Cy_MemSet( second, 0x5Au, sizeof( second ) );
    REQUIRE( CookedShader_Succeeded( CookedShader_WriteV3(
        {}, { stages, 2u }, firstInterface, {}, { first, cbRequired } ) ) );
    REQUIRE( CookedShader_Succeeded( CookedShader_WriteV3(
        {}, { stages, 2u }, secondInterface, {}, { second, cbRequired } ) ) );
    REQUIRE( Cy_MemEqual( first, second, cbRequired ) );
}

TEST_CASE( "Cooked shader interface hashing rejects hostile binding counts",
           "[CypherCommon][Formats][CookedShader][Reflection][Failure]" )
{
    const cooked_shader_binding_source_t binding =
        MakeTextureBinding( "base_color" );
    content_hash_t output{
        0x1122334455667788ull,
        0x8877665544332211ull
    };
    const content_hash_t sentinel = output;

    const cooked_shader_interface_source_t aboveLimit{
        { &binding, CY_COOKED_SHADER_MAX_BINDINGS + 1u }
    };
    REQUIRE_FALSE( CookedShader_ComputeInterfaceHash(
        aboveLimit,
        &output ) );
    REQUIRE( ContentHash_Equals( output, sentinel ) );

    const cooked_shader_interface_source_t wrappedByteCount{
        { &binding, CY_USIZE_MAX }
    };
    REQUIRE_FALSE( CookedShader_ComputeInterfaceHash(
        wrappedByteCount,
        &output ) );
    REQUIRE( ContentHash_Equals( output, sentinel ) );
}

TEST_CASE( "Cooked shader metadata enforces canonical stage sets",
           "[CypherCommon][Formats][CookedShader][Validation]" )
{
    const cooked_shader_desc_t graphics{};
    cooked_shader_stage_desc_t stages[2]{
        {
            render_shader_stage_t::VERTEX,
            render_shader_code_format_t::GLSL_UTF8,
            COOKED_SHADER_STAGE_FLAG_NONE,
            1u,
            16u
        },
        {
            render_shader_stage_t::FRAGMENT,
            render_shader_code_format_t::GLSL_UTF8,
            COOKED_SHADER_STAGE_FLAG_NONE,
            2u,
            16u
        }
    };
    byte metadata[128]{};

    REQUIRE( CookedShader_Succeeded( CookedShader_WriteMetadata(
        graphics,
        { stages, 2u },
        Span_FromArray( metadata ) ) ) );

    stages[1].stage = render_shader_stage_t::VERTEX;
    REQUIRE( CookedShader_WriteMetadata(
                 graphics,
                 { stages, 2u },
                 Span_FromArray( metadata ) ).status ==
             cooked_shader_status_t::DUPLICATE_STAGE );

    stages[1].stage = render_shader_stage_t::FRAGMENT;
    cooked_shader_desc_t unknownKind{};
    unknownKind.kind = static_cast<render_shader_program_kind_t>( 2u );
    REQUIRE( CookedShader_WriteMetadata(
                 unknownKind,
                 { stages, 2u },
                 Span_FromArray( metadata ) ).status ==
             cooked_shader_status_t::INVALID_PROGRAM_KIND );

    stages[0].stage = render_shader_stage_t::VERTEX;
    REQUIRE( CookedShader_WriteMetadata(
        graphics,
        { stages, 1u },
        Span_FromArray( metadata ) ).status ==
             cooked_shader_status_t::INVALID_STAGE_SET );

    cooked_shader_desc_t unknownBackend{};
    unknownBackend.backend = static_cast<render_shader_backend_t>( 2u );
    REQUIRE( CookedShader_WriteMetadata(
                 unknownBackend,
                 { stages, 2u },
                 Span_FromArray( metadata ) ).status ==
             cooked_shader_status_t::INVALID_BACKEND );

    cooked_shader_desc_t invalidProfile{};
    invalidProfile.languageProfile =
        static_cast<render_shader_language_profile_t>( 99u );
    REQUIRE( CookedShader_WriteMetadata(
                 invalidProfile,
                 { stages, 2u },
                 Span_FromArray( metadata ) ).status ==
             cooked_shader_status_t::INVALID_LANGUAGE_PROFILE );

    cooked_shader_desc_t invalidVersion{};
    invalidVersion.nLanguageVersion = 120u;
    REQUIRE( CookedShader_WriteMetadata(
                 invalidVersion,
                 { stages, 2u },
                 Span_FromArray( metadata ) ).status ==
             cooked_shader_status_t::INVALID_LANGUAGE_VERSION );
}

TEST_CASE( "Cooked shader container round trips GLSL 4.60",
           "[CypherCommon][Formats][CookedShader][Version]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    cooked_shader_desc_t shader{};
    shader.nLanguageVersion = 460u;
    const usize cbRequired = CookedShader_RequiredSizeV3(
        shader,
        { stages, 2u },
        {} );
    REQUIRE( cbRequired > 0u );
    REQUIRE( cbRequired <= 2048u );

    byte file[2048]{};
    REQUIRE( CookedShader_Succeeded( CookedShader_WriteV3(
        shader,
        { stages, 2u },
        {},
        {},
        { file, cbRequired } ) ) );
    cooked_shader_view_t view{};
    REQUIRE( CookedShader_Succeeded( CookedShader_Read(
        { file, cbRequired },
        &view ) ) );
    REQUIRE( view.nLanguageVersion == 460u );
    REQUIRE( view.nResourceVersion == CY_COOKED_SHADER_RESOURCE_VERSION_V3 );
}

TEST_CASE( "Cooked shader V3 writers reject invalid interfaces transactionally",
           "[CypherCommon][Formats][CookedShader][Reflection][Validation]" )
{
    REQUIRE( CookedShader_ValueTypeValueSize(
                 render_shader_value_type_t::F32X3 ) == 12u );
    REQUIRE( CookedShader_ValueTypeStorageAlignment(
                 render_shader_value_type_t::F32X3 ) == 16u );
    REQUIRE( CookedShader_ValueTypeStorageSize(
                 render_shader_value_type_t::F32X3 ) == 16u );
    REQUIRE( CookedShader_ValueTypeValueSize(
                 render_shader_value_type_t::F32X3X3 ) == 36u );
    REQUIRE( CookedShader_ValueTypeStorageSize(
                 render_shader_value_type_t::F32X3X3 ) == 48u );
    REQUIRE( CookedShader_ValueTypeStorageAlignment(
                 render_shader_value_type_t::NONE ) == 0u );

    u64 stableBindingId = 0u;
    REQUIRE( CookedShader_MakeLogicalBindingId(
        StringView_FromCString( "base_color" ),
        &stableBindingId ) );
    REQUIRE( stableBindingId == 0x8AB73C3E18151204ull );
    u64 unchangedBindingId = 0x1234u;
    REQUIRE_FALSE( CookedShader_MakeLogicalBindingId(
        StringView_FromCString( "9invalid" ),
        &unchangedBindingId ) );
    REQUIRE( unchangedBindingId == 0x1234u );

    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    cooked_shader_binding_source_t bindings[2]{
        MakeTextureBinding( "base_color" ),
        MakeTextureBinding( "normal_map" )
    };
    byte output[2048];
    Cy_MemSet( output, 0xA5u, sizeof( output ) );

    bindings[0].nLogicalBinding ^= 1u;
    cooked_shader_result_t result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 2u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( result.iBinding == 0u );
    REQUIRE( output[0] == static_cast<byte>( 0xA5u ) );

    bindings[0] = MakeTextureBinding( "base_color" );
    bindings[1] = bindings[0];
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 2u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status ==
             cooked_shader_status_t::DUPLICATE_BINDING_NAME );
    REQUIRE( output[0] == static_cast<byte>( 0xA5u ) );

    bindings[1] = MakeTextureBinding( "normal_map" );
    bindings[1].kind = render_shader_binding_kind_t::SAMPLER;
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 2u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( result.iBinding == 1u );
    REQUIRE( output[0] == static_cast<byte>( 0xA5u ) );

    bindings[0] = {};
    bindings[0].name = StringView_FromCString( "tint" );
    bindings[0].kind = render_shader_binding_kind_t::VALUE;
    bindings[0].valueType = render_shader_value_type_t::F32X3;
    bindings[0].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    bindings[0].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    bindings[0].nLogicalBinding = BindingId( bindings[0].name );
    bindings[0].iByteOffset = 4u;
    bindings[0].cbByteSize = 12u;
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 1u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( result.iBinding == 0u );
    REQUIRE( output[0] == static_cast<byte>( 0xA5u ) );

    bindings[0].iByteOffset = 0u;
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 1u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( result.iBinding == 0u );

    bindings[0].cbByteSize = 16u;
    bindings[0].iByteOffset = 16u;
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 1u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( result.iBinding == 0u );

    bindings[0].iByteOffset = 0u;
    bindings[0].nArrayElements = 2u;
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 1u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( result.iBinding == 0u );
    bindings[0].nArrayElements = 1u;

    bindings[0].iByteOffset = 0u;
    bindings[0].cbByteSize = 16u;
    bindings[1] = {};
    bindings[1].name = StringView_FromCString( "wind" );
    bindings[1].kind = render_shader_binding_kind_t::VALUE;
    bindings[1].valueType = render_shader_value_type_t::F32X4;
    bindings[1].stageMask = RENDER_SHADER_STAGE_MASK_VERTEX;
    bindings[1].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    bindings[1].nLogicalBinding = BindingId( bindings[1].name );
    bindings[1].iByteOffset = 0u;
    bindings[1].cbByteSize = 16u;
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { bindings, 2u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( output[0] == static_cast<byte>( 0xA5u ) );

    cooked_shader_binding_source_t tooMany[
        CY_COOKED_SHADER_MAX_BINDINGS + 1u]{};
    result = CookedShader_WriteV3(
        {},
        { stages, 2u },
        { { tooMany, CY_COOKED_SHADER_MAX_BINDINGS + 1u } },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status ==
             cooked_shader_status_t::BINDING_LIMIT_EXCEEDED );
    REQUIRE( output[0] == static_cast<byte>( 0xA5u ) );
    REQUIRE( CookedShader_MetadataSizeV3( 0u ) == 0u );
    REQUIRE( CookedShader_MetadataSizeV3( 2u ) == 120u );
    REQUIRE( CookedShader_ReflectionSize( 0u ) == 32u );
    REQUIRE( CookedShader_ReflectionSize(
                 CY_COOKED_SHADER_MAX_BINDINGS + 1u ) == 0u );
}

TEST_CASE( "Cooked shader readers reject damaged files transactionally",
           "[CypherCommon][Formats][CookedShader][Failure]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    const usize cbRequired = CookedShader_RequiredSize(
        {},
        { stages, 2u } );
    byte file[1024]{};
    REQUIRE( CookedShader_Succeeded( CookedShader_Write(
        {}, { stages, 2u }, {}, { file, cbRequired } ) ) );

    cooked_shader_view_t output{};
    output.nStages = 99u;
    file[cbRequired - 2u] ^= static_cast<byte>( 1u );
    const cooked_shader_result_t damaged = CookedShader_Read(
        { file, cbRequired },
        &output );
    REQUIRE( damaged.status == cooked_shader_status_t::RESOURCE_ERROR );
    REQUIRE( damaged.resourceStatus ==
             cooked_resource_status_t::CONTENT_HASH_MISMATCH );
    REQUIRE( output.nStages == 99u );

    REQUIRE( CookedShader_Succeeded( CookedShader_Write(
        {}, { stages, 2u }, {}, { file, cbRequired } ) ) );
    // The first metadata byte follows the CYRS header and three descriptors.
    const usize iMetadata = CookedResource_PrefixSize( 3u );
    file[iMetadata] = static_cast<byte>( 'X' );
    REQUIRE( CookedShader_Read(
                 { file, cbRequired },
                 &output ).status ==
             cooked_shader_status_t::RESOURCE_ERROR );
    REQUIRE( output.nStages == 99u );

    REQUIRE( CookedShader_Succeeded( CookedShader_Write(
        {}, { stages, 2u }, {}, { file, cbRequired } ) ) );
    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[3]{};
    REQUIRE( CookedResource_Succeeded( CookedResource_ReadLayout(
        { file, cbRequired },
        &header,
        { chunks, 3u } ) ) );
    // Move the last chunk to a later aligned offset and seal the generic CYRS
    // hash again. CYRS accepts the gap; the shader contract rejects it.
    const usize iOldLast = static_cast<usize>( chunks[2].iOffset );
    const usize cbLast = static_cast<usize>( chunks[2].cbStored );
    const usize iNewLast = iOldLast + CY_COOKED_SHADER_CODE_ALIGNMENT;
    REQUIRE( iNewLast + cbLast <= sizeof( file ) );
    Cy_MemMove( file + iNewLast, file + iOldLast, cbLast );
    Cy_MemZero( file + iOldLast, iNewLast - iOldLast );
    chunks[2].iOffset = iNewLast;
    header.cbFile = iNewLast + cbLast;
    header.flags &= ~COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = {};
    REQUIRE( CookedResource_Succeeded( CookedResource_WriteLayout(
        header,
        { chunks, 3u },
        { file, static_cast<usize>( header.cbFile ) } ) ) );
    header.flags |= COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = CookedResource_ComputeContentHash(
        { file, static_cast<usize>( header.cbFile ) } );
    REQUIRE( CookedResource_Succeeded( CookedResource_WriteLayout(
        header,
        { chunks, 3u },
        { file, static_cast<usize>( header.cbFile ) } ) ) );
    const cooked_shader_result_t padding = CookedShader_Read(
        { file, static_cast<usize>( header.cbFile ) },
        &output );
    REQUIRE( padding.status ==
             cooked_shader_status_t::NON_CANONICAL_LAYOUT );
    REQUIRE( output.nStages == 99u );
}

TEST_CASE( "Cooked shader V3 readers verify interface identity and records",
           "[CypherCommon][Formats][CookedShader][Reflection][Failure]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    cooked_shader_binding_source_t binding =
        MakeTextureBinding( "base_color" );
    const cooked_shader_interface_source_t shaderInterface{
        { &binding, 1u }
    };
    const usize cbRequired = CookedShader_RequiredSizeV3(
        {}, { stages, 2u }, shaderInterface );
    REQUIRE( cbRequired <= 2048u );

    byte file[2048]{};
    REQUIRE( CookedShader_Succeeded( CookedShader_WriteV3(
        {},
        { stages, 2u },
        shaderInterface,
        {},
        { file, cbRequired } ) ) );
    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[5]{};
    REQUIRE( CookedResource_Succeeded( CookedResource_ReadLayout(
        { file, cbRequired },
        &header,
        Span_FromArray( chunks ) ) ) );

    // Change the stored logical ID and update both generic resource hashes.
    // The unchanged SHMD interface identity must detect this semantic mutation.
    constexpr usize iBindingIdInRecord = 32u;
    const usize iBindingId = static_cast<usize>( chunks[1].iOffset ) +
        CY_COOKED_SHADER_REFLECTION_HEADER_SIZE + iBindingIdInRecord;
    file[iBindingId] ^= static_cast<byte>( 1u );
    chunks[1].contentHash = ContentHash_Data( {
        file + chunks[1].iOffset,
        static_cast<usize>( chunks[1].cbStored )
    } );
    SealCookedFile( file, cbRequired, header, chunks, 5u );

    cooked_shader_view_t output{};
    output.nBindings = 77u;
    cooked_shader_result_t read = CookedShader_Read(
        { file, cbRequired },
        &output );
    REQUIRE( read.status ==
             cooked_shader_status_t::INVALID_INTERFACE_HASH );
    REQUIRE( output.nBindings == 77u );

    // Update the interface identity too. The binding-level name-to-ID contract
    // must still reject the internally consistent but semantically invalid file.
    const content_hash_t interfaceHash = ContentHash_Combine(
        chunks[1].contentHash,
        chunks[2].contentHash );
    byte_writer_t metadataWriter{};
    REQUIRE( ByteWriter_Init(
        &metadataWriter,
        {
            file + chunks[0].iOffset + 56u,
            sizeof( content_hash_t )
        },
        data_byte_order_t::LITTLE ) );
    REQUIRE( ByteWriter_WriteU64( &metadataWriter, interfaceHash.low ) );
    REQUIRE( ByteWriter_WriteU64( &metadataWriter, interfaceHash.high ) );
    chunks[0].contentHash = ContentHash_Data( {
        file + chunks[0].iOffset,
        static_cast<usize>( chunks[0].cbStored )
    } );
    SealCookedFile( file, cbRequired, header, chunks, 5u );

    read = CookedShader_Read( { file, cbRequired }, &output );
    REQUIRE( read.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( read.iBinding == 0u );
    REQUIRE( output.nBindings == 77u );
}

TEST_CASE( "Cooked shader V3 readers reject noncanonical material packing",
           "[CypherCommon][Formats][CookedShader][Reflection][Failure]" )
{
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    cooked_shader_binding_source_t bindings[2]{};
    bindings[0].name = StringView_FromCString( "a" );
    bindings[0].kind = render_shader_binding_kind_t::VALUE;
    bindings[0].valueType = render_shader_value_type_t::F32;
    bindings[0].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    bindings[0].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    bindings[0].nLogicalBinding = BindingId( bindings[0].name );
    bindings[0].iByteOffset = 0u;
    bindings[0].cbByteSize = 4u;
    bindings[1].name = StringView_FromCString( "b" );
    bindings[1].kind = render_shader_binding_kind_t::VALUE;
    bindings[1].valueType = render_shader_value_type_t::F32X4;
    bindings[1].stageMask = RENDER_SHADER_STAGE_MASK_FRAGMENT;
    bindings[1].flags = COOKED_SHADER_BINDING_FLAG_MATERIAL;
    bindings[1].nLogicalBinding = BindingId( bindings[1].name );
    bindings[1].iByteOffset = 16u;
    bindings[1].cbByteSize = 16u;

    const cooked_shader_interface_source_t shaderInterface{
        { bindings, 2u }
    };
    const usize cbRequired = CookedShader_RequiredSizeV3(
        {}, { stages, 2u }, shaderInterface );
    REQUIRE( cbRequired <= 2048u );
    byte file[2048]{};
    REQUIRE( CookedShader_Succeeded( CookedShader_WriteV3(
        {},
        { stages, 2u },
        shaderInterface,
        {},
        { file, cbRequired } ) ) );

    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[5]{};
    REQUIRE( CookedResource_Succeeded( CookedResource_ReadLayout(
        { file, cbRequired },
        &header,
        Span_FromArray( chunks ) ) ) );

    // The second record's offset lives after the 32-byte SHRF header, one
    // 56-byte record, and 40 bytes of fixed fields. Forge it to overlap `a`,
    // then repair every integrity seal so semantic validation must catch it.
    const usize iForgedOffset = static_cast<usize>( chunks[1].iOffset ) +
        CY_COOKED_SHADER_REFLECTION_HEADER_SIZE +
        CY_COOKED_SHADER_BINDING_RECORD_SIZE + 40u;
    byte_writer_t offsetWriter{};
    REQUIRE( ByteWriter_Init(
        &offsetWriter,
        { file + iForgedOffset, sizeof( u32 ) },
        data_byte_order_t::LITTLE ) );
    REQUIRE( ByteWriter_WriteU32( &offsetWriter, 0u ) );
    chunks[1].contentHash = ContentHash_Data( {
        file + chunks[1].iOffset,
        static_cast<usize>( chunks[1].cbStored )
    } );
    const content_hash_t interfaceHash = ContentHash_Combine(
        chunks[1].contentHash,
        chunks[2].contentHash );
    byte_writer_t metadataWriter{};
    REQUIRE( ByteWriter_Init(
        &metadataWriter,
        {
            file + chunks[0].iOffset + 56u,
            sizeof( content_hash_t )
        },
        data_byte_order_t::LITTLE ) );
    REQUIRE( ByteWriter_WriteU64( &metadataWriter, interfaceHash.low ) );
    REQUIRE( ByteWriter_WriteU64( &metadataWriter, interfaceHash.high ) );
    chunks[0].contentHash = ContentHash_Data( {
        file + chunks[0].iOffset,
        static_cast<usize>( chunks[0].cbStored )
    } );
    SealCookedFile( file, cbRequired, header, chunks, 5u );

    cooked_shader_view_t output{};
    output.nBindings = 77u;
    const cooked_shader_result_t read = CookedShader_Read(
        { file, cbRequired },
        &output );
    REQUIRE( read.status == cooked_shader_status_t::INVALID_BINDING );
    REQUIRE( read.iBinding == 1u );
    REQUIRE( output.nBindings == 77u );
}

TEST_CASE( "Cooked shader writers reject malformed OpenGL source",
           "[CypherCommon][Formats][CookedShader][Code]" )
{
    char unterminated[]{ 'v', 'o', 'i', 'd' };
    cooked_shader_stage_source_t stages[2]{};
    MakeGraphicsStages( stages );
    stages[0].code = TextBlock( unterminated, sizeof( unterminated ) );

    byte output[1024]{};
    const cooked_shader_result_t result = CookedShader_Write(
        {},
        { stages, 2u },
        {},
        Span_FromArray( output ) );
    REQUIRE( result.status == cooked_shader_status_t::INVALID_CODE );
    REQUIRE( result.iStage == 0u );

    byte tiny[1]{ static_cast<byte>( 0xA5u ) };
    MakeGraphicsStages( stages );
    const cooked_shader_result_t tooSmall = CookedShader_Write(
        {},
        { stages, 2u },
        {},
        Span_FromArray( tiny ) );
    REQUIRE( tooSmall.status == cooked_shader_status_t::OUTPUT_TOO_SMALL );
    REQUIRE( tiny[0] == static_cast<byte>( 0xA5u ) );

    alignas( cooked_shader_stage_source_t ) byte aliased[1024]{};
    cooked_shader_stage_source_t *pAliasedStages =
        reinterpret_cast<cooked_shader_stage_source_t *>( aliased );
    pAliasedStages[0] = stages[0];
    pAliasedStages[1] = stages[1];
    REQUIRE( CookedShader_Write(
                 {},
                 { pAliasedStages, 2u },
                 {},
                 Span_FromArray( aliased ) ).status ==
             cooked_shader_status_t::INVALID_ARGUMENT );

    alignas( cooked_shader_desc_t ) byte metadataAlias[128]{};
    cooked_shader_desc_t *pAliasedShader =
        reinterpret_cast<cooked_shader_desc_t *>( metadataAlias );
    *pAliasedShader = {};
    cooked_shader_stage_desc_t metadataStages[2]{
        {
            render_shader_stage_t::VERTEX,
            render_shader_code_format_t::GLSL_UTF8,
            COOKED_SHADER_STAGE_FLAG_NONE,
            1u,
            8u
        },
        {
            render_shader_stage_t::FRAGMENT,
            render_shader_code_format_t::GLSL_UTF8,
            COOKED_SHADER_STAGE_FLAG_NONE,
            2u,
            8u
        }
    };
    REQUIRE( CookedShader_WriteMetadata(
                 *pAliasedShader,
                 { metadataStages, 2u },
                 Span_FromArray( metadataAlias ) ).status ==
             cooked_shader_status_t::INVALID_ARGUMENT );
    REQUIRE( CookedShader_MetadataSize( 0u ) == 0u );
    REQUIRE( CookedShader_MetadataSize( 2u ) == 88u );
    REQUIRE( CookedShader_SupportsLanguage(
        render_shader_language_profile_t::GLSL_CORE,
        330u ) );
    REQUIRE( CookedShader_SupportsLanguage(
        render_shader_language_profile_t::GLSL_CORE,
        450u ) );
    REQUIRE( CookedShader_SupportsLanguage(
        render_shader_language_profile_t::GLSL_CORE,
        460u ) );
    REQUIRE_FALSE( CookedShader_SupportsLanguage(
        render_shader_language_profile_t::GLSL_CORE,
        120u ) );
    REQUIRE( StringView_Equals(
        StringView_FromCString( CookedShader_StatusName(
            cooked_shader_status_t::INVALID_STAGE_SET ) ),
        StringView_FromCString( "INVALID_STAGE_SET" ) ) );
}
