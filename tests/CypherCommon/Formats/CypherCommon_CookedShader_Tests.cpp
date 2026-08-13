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
    REQUIRE( view.backend == render_shader_backend_t::OPENGL );
    REQUIRE( view.kind == render_shader_program_kind_t::GRAPHICS );
    REQUIRE( view.nStages == 2u );
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
    REQUIRE( CookedShader_MetadataSize( 2u ) == 80u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( CookedShader_StatusName(
            cooked_shader_status_t::INVALID_STAGE_SET ) ),
        StringView_FromCString( "INVALID_STAGE_SET" ) ) );
}
