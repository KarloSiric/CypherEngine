//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Formats/CypherCommon_CookedTexture_Tests.cpp
//  Purpose: Tests the backend-neutral cooked texture resource contract.
//  Details: Covers canonical mip layout, deterministic serialization, format
//           policy, malformed chains, content damage, and transactional reads.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedTexture.h"

#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cypher::common;

namespace
{

struct texture_fixture_t {
    byte mip0[32]{};
    byte mip1[8]{};
    byte mip2[4]{};
    cooked_texture_desc_t texture{};
    cooked_texture_mip_source_t mips[3]{};

    texture_fixture_t() noexcept
    {
        for ( usize iByte = 0u; iByte < sizeof( mip0 ); ++iByte ) {
            mip0[iByte] = static_cast<byte>( iByte );
        }
        for ( usize iByte = 0u; iByte < sizeof( mip1 ); ++iByte ) {
            mip1[iByte] = static_cast<byte>( 64u + iByte );
        }
        for ( usize iByte = 0u; iByte < sizeof( mip2 ); ++iByte ) {
            mip2[iByte] = static_cast<byte>( 128u + iByte );
        }

        texture.flags = COOKED_TEXTURE_FLAG_GENERATED_MIPS;
        texture.nWidth = 4u;
        texture.nHeight = 2u;
        texture.nMipLevels = 3u;
        mips[0] = { 4u, 2u, 1u, 16u, { mip0, sizeof( mip0 ) } };
        mips[1] = { 2u, 1u, 1u, 8u, { mip1, sizeof( mip1 ) } };
        mips[2] = { 1u, 1u, 1u, 4u, { mip2, sizeof( mip2 ) } };
    }
};

std::vector<byte> BuildVersionOneTexture()
{
    constexpr byte pixels[]{ 0x10u, 0x20u, 0x30u, 0x40u };
    constexpr u32 nChunks = 2u;
    constexpr usize cbMetadata = CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1 +
                                 CY_COOKED_TEXTURE_MIP_RECORD_SIZE_V1;
    cooked_chunk_desc_t chunks[nChunks]{};
    usize iOffset = CookedResource_PrefixSize( nChunks );
    if ( !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_TEXTURE_METADATA_ALIGNMENT,
             iOffset ) ) {
        return {};
    }
    chunks[0].chunkType = CY_COOKED_TEXTURE_METADATA_CHUNK;
    chunks[0].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    chunks[0].nAlignment = CY_COOKED_TEXTURE_METADATA_ALIGNMENT;
    chunks[0].iOffset = iOffset;
    chunks[0].cbStored = cbMetadata;
    chunks[0].cbDecoded = cbMetadata;
    iOffset += cbMetadata;
    if ( !Cy_AlignUpChecked(
             iOffset,
             CY_COOKED_TEXTURE_DATA_ALIGNMENT,
             iOffset ) ) {
        return {};
    }
    chunks[1].chunkType = CY_COOKED_TEXTURE_DATA_CHUNK;
    chunks[1].flags = COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    chunks[1].nAlignment = CY_COOKED_TEXTURE_DATA_ALIGNMENT;
    chunks[1].iOffset = iOffset;
    chunks[1].cbStored = sizeof( pixels );
    chunks[1].cbDecoded = sizeof( pixels );
    chunks[1].contentHash = ContentHash_Data( { pixels, sizeof( pixels ) } );

    std::vector<byte> file( iOffset + sizeof( pixels ), 0u );
    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             { file.data() + chunks[0].iOffset, cbMetadata },
             data_byte_order_t::LITTLE ) ||
         !ByteWriter_WriteU32( &writer, CY_COOKED_TEXTURE_METADATA_MAGIC ) ||
         !ByteWriter_WriteU32(
             &writer,
             CY_COOKED_TEXTURE_METADATA_VERSION_V1 ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1 ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( render_texture_dimension_t::TEXTURE_2D ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>(
                 render_texture_pixel_format_t::RGBA8_SRGB ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( render_texture_usage_t::COLOR ) ) ||
         !ByteWriter_WriteU32(
             &writer,
             static_cast<u32>( render_texture_color_space_t::SRGB ) ) ||
         !ByteWriter_WriteU32( &writer, COOKED_TEXTURE_FLAG_NONE ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ||
         !ByteWriter_WriteU32( &writer, 0u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU32( &writer, 4u ) ||
         !ByteWriter_WriteU32( &writer, 1u ) ||
         !ByteWriter_WriteU64( &writer, sizeof( pixels ) ) ) {
        return {};
    }
    chunks[0].contentHash = ContentHash_Data( {
        file.data() + chunks[0].iOffset,
        cbMetadata
    } );
    Cy_MemCopy(
        file.data() + chunks[1].iOffset,
        pixels,
        sizeof( pixels ) );

    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_TEXTURE_RESOURCE_TYPE;
    header.nResourceVersion = CY_COOKED_TEXTURE_RESOURCE_VERSION_V1;
    header.nChunks = nChunks;
    header.cbFile = file.size();
    if ( !CookedResource_Succeeded( CookedResource_WriteLayout(
             header,
             { chunks, nChunks },
             { file.data(), file.size() } ) ) ) {
        return {};
    }
    header.flags = COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    header.contentHash = CookedResource_ComputeContentHash( {
        file.data(), file.size()
    } );
    if ( !CookedResource_Succeeded( CookedResource_WriteLayout(
             header,
             { chunks, nChunks },
             { file.data(), file.size() } ) ) ) {
        return {};
    }
    return file;
}

} // namespace

TEST_CASE( "Cooked textures round trip through canonical CYRS files",
           "[CypherCommon][Formats][CookedTexture]" )
{
    texture_fixture_t fixture{};
    const usize cbRequired = CookedTexture_RequiredSize(
        fixture.texture,
        { fixture.mips, 3u } );
    REQUIRE( cbRequired > CY_COOKED_RESOURCE_HEADER_SIZE );

    std::vector<byte> file( cbRequired );
    const content_hash_t sourceHash = ContentHash_String(
        { "textures/wall.cytex", 19u } );
    const cooked_texture_result_t written = CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        sourceHash,
        { file.data(), file.size() } );
    REQUIRE( CookedTexture_Succeeded( written ) );
    REQUIRE( written.cbWritten == cbRequired );

    cooked_resource_header_t header{};
    cooked_chunk_desc_t chunks[4]{};
    REQUIRE( CookedResource_Succeeded( CookedResource_ReadLayout(
        { file.data(), file.size() },
        &header,
        { chunks, 4u } ) ) );
    REQUIRE( header.nResourceVersion ==
             CY_COOKED_TEXTURE_RESOURCE_VERSION_V2 );
    REQUIRE( header.nChunks == 4u );

    cooked_texture_view_t view{};
    const cooked_texture_result_t read = CookedTexture_Read(
        { file.data(), file.size() },
        &view );
    REQUIRE( CookedTexture_Succeeded( read ) );
    REQUIRE( view.desc.dimension == render_texture_dimension_t::TEXTURE_2D );
    REQUIRE( view.desc.pixelFormat ==
             render_texture_pixel_format_t::RGBA8_SRGB );
    REQUIRE( view.desc.usage == render_texture_usage_t::COLOR );
    REQUIRE( view.desc.colorSpace == render_texture_color_space_t::SRGB );
    REQUIRE( view.desc.storageFormat == render_format_t::RGBA8_SRGB );
    REQUIRE( view.desc.alphaMode == cooked_texture_alpha_mode_t::STRAIGHT );
    REQUIRE( view.desc.alphaCutoff == 0.5f );
    REQUIRE( view.desc.target == cooked_texture_target_t::PORTABLE );
    REQUIRE( view.desc.residency ==
             cooked_texture_residency_t::FULLY_RESIDENT );
    REQUIRE( view.desc.nSubresources == 3u );
    REQUIRE( view.desc.nResidentMipLevels == 3u );
    REQUIRE( view.desc.iResidentFirstSubresource == 0u );
    REQUIRE( view.desc.nResidentSubresources == 3u );
    REQUIRE( view.desc.cbResidentData == 44u );
    REQUIRE( view.desc.cbData == 44u );
    REQUIRE( view.nMipLevels == 3u );
    REQUIRE( view.nSubresources == 3u );
    REQUIRE( view.nResourceVersion == CY_COOKED_TEXTURE_RESOURCE_VERSION_V2 );
    REQUIRE( ContentHash_Equals( view.sourceHash, sourceHash ) );

    const cooked_texture_mip_view_t *pMip0 = CookedTexture_FindMip( view, 0u );
    const cooked_texture_mip_view_t *pMip1 = CookedTexture_FindMip( view, 1u );
    const cooked_texture_mip_view_t *pMip2 = CookedTexture_FindMip( view, 2u );
    REQUIRE( pMip0 != nullptr );
    REQUIRE( pMip1 != nullptr );
    REQUIRE( pMip2 != nullptr );
    REQUIRE( pMip0->nWidth == 4u );
    REQUIRE( pMip0->nHeight == 2u );
    REQUIRE( pMip0->cbRowPitch == 16u );
    REQUIRE( pMip2->nWidth == 1u );
    REQUIRE( pMip2->nHeight == 1u );
    REQUIRE( Cy_MemEqual(
        pMip0->pixels.pData,
        fixture.mip0,
        sizeof( fixture.mip0 ) ) );
    REQUIRE( Cy_MemEqual(
        pMip1->pixels.pData,
        fixture.mip1,
        sizeof( fixture.mip1 ) ) );
    REQUIRE( Cy_MemEqual(
        pMip2->pixels.pData,
        fixture.mip2,
        sizeof( fixture.mip2 ) ) );
    REQUIRE( CookedTexture_FindMip( view, 3u ) == nullptr );
}

TEST_CASE( "Cooked textures preserve canonical version one compatibility",
           "[CypherCommon][Formats][CookedTexture][Compatibility]" )
{
    const std::vector<byte> file = BuildVersionOneTexture();
    REQUIRE_FALSE( file.empty() );

    cooked_texture_view_t view{};
    const cooked_texture_result_t read = CookedTexture_Read(
        { file.data(), file.size() },
        &view );
    INFO( CookedTexture_StatusName( read.status ) );
    INFO( CookedResource_StatusName( read.resourceStatus ) );
    INFO( read.iMip );
    INFO( read.iSubresource );
    INFO( read.iChunk );
    REQUIRE( CookedTexture_Succeeded( read ) );
    REQUIRE( view.nResourceVersion == CY_COOKED_TEXTURE_RESOURCE_VERSION_V1 );
    REQUIRE( view.desc.storageFormat == render_format_t::RGBA8_SRGB );
    REQUIRE( view.desc.alphaMode == cooked_texture_alpha_mode_t::STRAIGHT );
    REQUIRE( view.desc.alphaCutoff == 0.5f );
    REQUIRE( view.desc.nFrames == 1u );
    REQUIRE( view.desc.nSubresources == 1u );
    REQUIRE( view.desc.cbData == 4u );
    REQUIRE( view.desc.cbResidentData == 4u );

    cooked_texture_subresource_view_t subresource{};
    REQUIRE( CookedTexture_GetSubresource(
        view, 0u, 0u, 0u, 0u, &subresource ) );
    REQUIRE( subresource.cbRowPitch == 4u );
    REQUIRE( subresource.cbSlicePitch == 4u );
    REQUIRE( subresource.pixels.cbSize == 4u );
    REQUIRE( subresource.pixels.pData[0] == 0x10u );
}

TEST_CASE( "Cooked texture version two stores arrays and resident mip tails",
           "[CypherCommon][Formats][CookedTexture][Subresources][Streaming]" )
{
    byte level0Layer0[16]{};
    byte level0Layer1[16]{};
    byte level1Layer0[4]{};
    byte level1Layer1[4]{ 0xA1u, 0xB2u, 0xC3u, 0xD4u };

    cooked_texture_desc_t texture{};
    texture.pixelFormat = render_texture_pixel_format_t::RGBA8_UNORM;
    texture.storageFormat = render_format_t::RGBA8_UNORM;
    texture.usage = render_texture_usage_t::DATA;
    texture.colorSpace = render_texture_color_space_t::LINEAR;
    texture.alphaMode = cooked_texture_alpha_mode_t::DATA;
    texture.target = cooked_texture_target_t::DESKTOP;
    texture.residency = cooked_texture_residency_t::MIP_STREAMED;
    texture.nWidth = 2u;
    texture.nHeight = 2u;
    texture.nLayers = 2u;
    texture.nMipLevels = 2u;
    texture.nResidentMipLevels = 1u;
    texture.nStreamingPriority = 17u;

    const cooked_texture_subresource_source_t sources[]{
        { 0u, 0u, 0u, 0u, 2u, 2u, 1u, 8u, 16u,
          { level0Layer0, sizeof( level0Layer0 ) } },
        { 0u, 0u, 1u, 0u, 2u, 2u, 1u, 8u, 16u,
          { level0Layer1, sizeof( level0Layer1 ) } },
        { 1u, 0u, 0u, 0u, 1u, 1u, 1u, 4u, 4u,
          { level1Layer0, sizeof( level1Layer0 ) } },
        { 1u, 0u, 1u, 0u, 1u, 1u, 1u, 4u, 4u,
          { level1Layer1, sizeof( level1Layer1 ) } }
    };
    const usize cbRequired = CookedTexture_RequiredSizeSubresources(
        texture,
        { sources, 4u } );
    REQUIRE( cbRequired > 0u );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_WriteSubresources(
        texture,
        { sources, 4u },
        {},
        { file.data(), file.size() } ) ) );

    cooked_texture_view_t view{};
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Read(
        { file.data(), file.size() },
        &view ) ) );
    REQUIRE( view.desc.nSubresources == 4u );
    REQUIRE( view.desc.iResidentFirstSubresource == 2u );
    REQUIRE( view.desc.nResidentSubresources == 2u );
    REQUIRE( view.desc.cbData == 40u );
    REQUIRE( view.desc.cbResidentData == 8u );
    REQUIRE( view.desc.nStreamingPriority == 17u );

    cooked_texture_subresource_view_t subresource{};
    REQUIRE( CookedTexture_GetSubresource(
        view, 1u, 0u, 1u, 0u, &subresource ) );
    REQUIRE( subresource.nMipLevel == 1u );
    REQUIRE( subresource.nLayer == 1u );
    REQUIRE( subresource.cbRowPitch == 4u );
    REQUIRE( subresource.cbSlicePitch == 4u );
    REQUIRE( subresource.pixels.cbSize == 4u );
    REQUIRE( subresource.pixels.pData[0] == 0xA1u );

    subresource.nWidth = 77u;
    REQUIRE_FALSE( CookedTexture_GetSubresource(
        view, 2u, 0u, 0u, 0u, &subresource ) );
    REQUIRE( subresource.nWidth == 77u );

    cooked_texture_subresource_source_t nonCanonical[4]{
        sources[0], sources[1], sources[2], sources[3]
    };
    nonCanonical[0].nLayer = 1u;
    nonCanonical[1].nLayer = 0u;
    REQUIRE( CookedTexture_RequiredSizeSubresources(
                 texture,
                 { nonCanonical, 4u } ) == 0u );
}

TEST_CASE( "Cooked texture writers reject invalid nested payloads transactionally",
           "[CypherCommon][Formats][CookedTexture][Subresources][Invalid]" )
{
    cooked_texture_desc_t texture{};
    texture.pixelFormat = render_texture_pixel_format_t::RGBA8_UNORM;
    texture.storageFormat = render_format_t::RGBA8_UNORM;
    texture.usage = render_texture_usage_t::DATA;
    texture.colorSpace = render_texture_color_space_t::LINEAR;
    texture.alphaMode = cooked_texture_alpha_mode_t::DATA;
    texture.nWidth = 1u;
    texture.nHeight = 1u;
    texture.nMipLevels = 1u;

    const cooked_texture_subresource_source_t source{
        0u, 0u, 0u, 0u, 1u, 1u, 1u, 4u, 4u,
        { nullptr, 4u }
    };
    REQUIRE( CookedTexture_RequiredSizeSubresources(
                 texture,
                 { &source, 1u } ) == 0u );

    std::vector<byte> output( 512u, 0xA5u );
    const std::vector<byte> original = output;
    const cooked_texture_result_t result =
        CookedTexture_WriteSubresources(
            texture,
            { &source, 1u },
            {},
            { output.data(), output.size() } );
    REQUIRE( result.status == cooked_texture_status_t::INVALID_DATA );
    REQUIRE( result.iSubresource == 0u );
    REQUIRE( result.iMip == 0u );
    REQUIRE( Cy_MemEqual(
        output.data(),
        original.data(),
        output.size() ) );
}

TEST_CASE( "Cooked textures validate target-ready block-compressed storage",
           "[CypherCommon][Formats][CookedTexture][Storage]" )
{
    cooked_texture_format_layout_t layout{};
    REQUIRE( CookedTexture_FormatLayout( render_format_t::BC7_SRGB, &layout ) );
    REQUIRE( layout.nBlockWidth == 4u );
    REQUIRE( layout.nBlockHeight == 4u );
    REQUIRE( layout.cbBlock == 16u );
    REQUIRE( layout.bBlockCompressed );
    REQUIRE( layout.bSrgb );
    REQUIRE( layout.bHasAlpha );

    byte blocks[64]{};
    cooked_texture_desc_t texture{};
    texture.storageFormat = render_format_t::BC7_SRGB;
    texture.alphaMode = cooked_texture_alpha_mode_t::MASK;
    texture.alphaCutoff = 0.375f;
    texture.target = cooked_texture_target_t::DESKTOP;
    texture.nWidth = 5u;
    texture.nHeight = 5u;
    texture.nMipLevels = 1u;
    const cooked_texture_subresource_source_t source{
        0u, 0u, 0u, 0u, 5u, 5u, 1u, 32u, 64u,
        { blocks, sizeof( blocks ) }
    };
    const usize cbRequired = CookedTexture_RequiredSizeSubresources(
        texture,
        { &source, 1u } );
    REQUIRE( cbRequired > 0u );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_WriteSubresources(
        texture,
        { &source, 1u },
        {},
        { file.data(), file.size() } ) ) );

    cooked_texture_view_t view{};
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Read(
        { file.data(), file.size() },
        &view ) ) );
    REQUIRE( view.desc.storageFormat == render_format_t::BC7_SRGB );
    REQUIRE( view.desc.pixelFormat == render_texture_pixel_format_t::UNKNOWN );
    REQUIRE( view.desc.alphaMode == cooked_texture_alpha_mode_t::MASK );
    REQUIRE( view.desc.alphaCutoff == 0.375f );

    texture.storageFormat = render_format_t::BC1_RGB_SRGB;
    texture.alphaMode = cooked_texture_alpha_mode_t::STRAIGHT;
    REQUIRE( CookedTexture_RequiredSizeSubresources(
                 texture,
                 { &source, 1u } ) == 0u );
}

TEST_CASE( "Cooked texture subresources cover cube frames and volume depth",
           "[CypherCommon][Formats][CookedTexture][Dimensions]" )
{
    byte facePixels[12][4]{};
    cooked_texture_subresource_source_t faces[12]{};
    for ( u32 iFrame = 0u; iFrame < 2u; ++iFrame ) {
        for ( u32 iFace = 0u;
              iFace < CY_COOKED_TEXTURE_CUBE_FACE_COUNT;
              ++iFace ) {
            const u32 i = iFrame * CY_COOKED_TEXTURE_CUBE_FACE_COUNT + iFace;
            facePixels[i][0] = static_cast<byte>( i + 1u );
            faces[i] = {
                0u, iFrame, 0u, iFace, 1u, 1u, 1u, 4u, 4u,
                { facePixels[i], sizeof( facePixels[i] ) }
            };
        }
    }

    cooked_texture_desc_t cube{};
    cube.dimension = render_texture_dimension_t::TEXTURE_CUBE;
    cube.pixelFormat = render_texture_pixel_format_t::RGBA8_UNORM;
    cube.usage = render_texture_usage_t::DATA;
    cube.colorSpace = render_texture_color_space_t::LINEAR;
    cube.alphaMode = cooked_texture_alpha_mode_t::DATA;
    cube.nWidth = 1u;
    cube.nHeight = 1u;
    cube.nFaces = CY_COOKED_TEXTURE_CUBE_FACE_COUNT;
    cube.nFrames = 2u;
    cube.nMipLevels = 1u;
    REQUIRE( CookedTexture_SubresourceCount( cube ) == 12u );
    const usize cbCube = CookedTexture_RequiredSizeSubresources(
        cube,
        { faces, 12u } );
    REQUIRE( cbCube > 0u );
    std::vector<byte> file( cbCube );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_WriteSubresources(
        cube,
        { faces, 12u },
        {},
        { file.data(), file.size() } ) ) );

    cooked_texture_view_t cubeView{};
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Read(
        { file.data(), file.size() },
        &cubeView ) ) );
    cooked_texture_subresource_view_t face{};
    REQUIRE( CookedTexture_GetSubresource(
        cubeView, 0u, 1u, 0u, 5u, &face ) );
    REQUIRE( face.nFrame == 1u );
    REQUIRE( face.nFace == 5u );
    REQUIRE( face.pixels.pData[0] == 12u );

    cube.nFaces = 5u;
    REQUIRE( CookedTexture_RequiredSizeSubresources(
                 cube,
                 { faces, 12u } ) == 0u );

    byte volumePixels[32]{};
    cooked_texture_desc_t volume{};
    volume.dimension = render_texture_dimension_t::TEXTURE_3D;
    volume.pixelFormat = render_texture_pixel_format_t::RGBA8_UNORM;
    volume.usage = render_texture_usage_t::DATA;
    volume.colorSpace = render_texture_color_space_t::LINEAR;
    volume.alphaMode = cooked_texture_alpha_mode_t::DATA;
    volume.nWidth = 2u;
    volume.nHeight = 2u;
    volume.nDepth = 2u;
    volume.nMipLevels = 1u;
    const cooked_texture_subresource_source_t volumeSource{
        0u, 0u, 0u, 0u, 2u, 2u, 2u, 8u, 16u,
        { volumePixels, sizeof( volumePixels ) }
    };
    REQUIRE( CookedTexture_RequiredSizeSubresources(
                 volume,
                 { &volumeSource, 1u } ) > 0u );
}

TEST_CASE( "Cooked textures round trip the maximum subresource count",
           "[CypherCommon][Formats][CookedTexture][Subresources][Limits]" )
{
    constexpr u32 nFrames = 4u;
    constexpr u32 nLayers = CY_COOKED_TEXTURE_MAX_SUBRESOURCES / nFrames;
    STATIC_REQUIRE( nFrames * nLayers ==
                    CY_COOKED_TEXTURE_MAX_SUBRESOURCES );

    std::vector<byte> pixels(
        static_cast<usize>( CY_COOKED_TEXTURE_MAX_SUBRESOURCES ) * 4u );
    std::vector<cooked_texture_subresource_source_t> sources(
        CY_COOKED_TEXTURE_MAX_SUBRESOURCES );
    for ( u32 i = 0u; i < CY_COOKED_TEXTURE_MAX_SUBRESOURCES; ++i ) {
        const u32 iFrame = i / nLayers;
        const u32 iLayer = i % nLayers;
        pixels[static_cast<usize>( i ) * 4u] = static_cast<byte>( i );
        sources[i] = {
            0u, iFrame, iLayer, 0u, 1u, 1u, 1u, 4u, 4u,
            { pixels.data() + static_cast<usize>( i ) * 4u, 4u }
        };
    }

    cooked_texture_desc_t texture{};
    texture.pixelFormat = render_texture_pixel_format_t::RGBA8_UNORM;
    texture.storageFormat = render_format_t::RGBA8_UNORM;
    texture.usage = render_texture_usage_t::DATA;
    texture.colorSpace = render_texture_color_space_t::LINEAR;
    texture.alphaMode = cooked_texture_alpha_mode_t::DATA;
    texture.nWidth = 1u;
    texture.nHeight = 1u;
    texture.nFrames = nFrames;
    texture.nLayers = nLayers;
    texture.nMipLevels = 1u;

    const usize cbRequired = CookedTexture_RequiredSizeSubresources(
        texture,
        { sources.data(), sources.size() } );
    REQUIRE( cbRequired > 0u );
    std::vector<byte> file( cbRequired );
    const cooked_texture_result_t written = CookedTexture_WriteSubresources(
        texture,
        { sources.data(), sources.size() },
        {},
        { file.data(), file.size() } );
    REQUIRE( CookedTexture_Succeeded( written ) );
    REQUIRE( written.cbWritten == cbRequired );

    cooked_resource_header_t header{};
    std::vector<cooked_chunk_desc_t> chunks(
        CY_COOKED_TEXTURE_MAX_SUBRESOURCES + 1u );
    REQUIRE( CookedResource_Succeeded( CookedResource_ReadLayout(
        { file.data(), file.size() },
        &header,
        { chunks.data(), chunks.size() } ) ) );
    REQUIRE( header.nChunks == CY_COOKED_TEXTURE_MAX_SUBRESOURCES + 1u );

    cooked_texture_view_t view{};
    const cooked_texture_result_t read = CookedTexture_Read(
        { file.data(), file.size() },
        &view );
    REQUIRE( CookedTexture_Succeeded( read ) );
    REQUIRE( view.nSubresources == CY_COOKED_TEXTURE_MAX_SUBRESOURCES );
    REQUIRE( view.desc.nSubresources == CY_COOKED_TEXTURE_MAX_SUBRESOURCES );
    REQUIRE( view.desc.nResidentSubresources ==
             CY_COOKED_TEXTURE_MAX_SUBRESOURCES );
    REQUIRE( view.desc.cbData ==
             static_cast<u64>( CY_COOKED_TEXTURE_MAX_SUBRESOURCES ) * 4u );

    cooked_texture_subresource_view_t last{};
    REQUIRE( CookedTexture_GetSubresource(
        view,
        0u,
        nFrames - 1u,
        nLayers - 1u,
        0u,
        &last ) );
    REQUIRE( last.nFrame == nFrames - 1u );
    REQUIRE( last.nLayer == nLayers - 1u );
    REQUIRE( last.pixels.cbSize == 4u );
    REQUIRE( last.pixels.pData[0] == static_cast<byte>(
        CY_COOKED_TEXTURE_MAX_SUBRESOURCES - 1u ) );
}

TEST_CASE( "Cooked texture subresource lookup rejects output aliases",
           "[CypherCommon][Formats][CookedTexture][Subresources][Aliasing]" )
{
    texture_fixture_t fixture{};
    const usize cbRequired = CookedTexture_RequiredSize(
        fixture.texture,
        { fixture.mips, 3u } );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        {},
        { file.data(), file.size() } ) ) );

    cooked_texture_view_t view{};
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Read(
        { file.data(), file.size() },
        &view ) ) );
    const std::vector<byte> original = file;
    auto *pFileBackedOutput =
        reinterpret_cast<cooked_texture_subresource_view_t *>( file.data() );
    REQUIRE_FALSE( CookedTexture_GetSubresource(
        view,
        0u,
        0u,
        0u,
        0u,
        pFileBackedOutput ) );
    REQUIRE( Cy_MemEqual( file.data(), original.data(), file.size() ) );

    const u32 nMipLevelsBefore = view.nMipLevels;
    auto *pViewBackedOutput =
        reinterpret_cast<cooked_texture_subresource_view_t *>( &view );
    REQUIRE_FALSE( CookedTexture_GetSubresource(
        view,
        0u,
        0u,
        0u,
        0u,
        pViewBackedOutput ) );
    REQUIRE( view.nMipLevels == nMipLevelsBefore );
}

TEST_CASE( "Cooked texture writers are deterministic",
           "[CypherCommon][Formats][CookedTexture][Determinism]" )
{
    texture_fixture_t fixture{};
    const usize cbRequired = CookedTexture_RequiredSize(
        fixture.texture,
        { fixture.mips, 3u } );
    std::vector<byte> first( cbRequired, 0xA5u );
    std::vector<byte> second( cbRequired, 0x5Au );

    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        {},
        { first.data(), first.size() } ) ) );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        {},
        { second.data(), second.size() } ) ) );
    REQUIRE( Cy_MemEqual( first.data(), second.data(), cbRequired ) );
}

TEST_CASE( "Cooked texture metadata rejects incompatible semantics",
           "[CypherCommon][Formats][CookedTexture][Validation]" )
{
    texture_fixture_t fixture{};
    cooked_texture_desc_t invalid = fixture.texture;
    invalid.usage = render_texture_usage_t::NORMAL;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 3u } ) == 0u );

    invalid = fixture.texture;
    invalid.pixelFormat = render_texture_pixel_format_t::RGBA8_UNORM;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 3u } ) == 0u );

    invalid = fixture.texture;
    invalid.nMipLevels = 2u;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 2u } ) == 0u );

    invalid = fixture.texture;
    invalid.alphaCutoff = 0.25f;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 3u } ) == 0u );

    invalid = fixture.texture;
    invalid.alphaMode = cooked_texture_alpha_mode_t::MASK;
    invalid.alphaCutoff = 1.25f;
    REQUIRE( CookedTexture_RequiredSize(
                 invalid,
                 { fixture.mips, 3u } ) == 0u );

    cooked_texture_mip_source_t badMips[3]{
        fixture.mips[0],
        fixture.mips[1],
        fixture.mips[2]
    };
    badMips[1].cbRowPitch = 12u;
    REQUIRE( CookedTexture_RequiredSize(
                 fixture.texture,
                 { badMips, 3u } ) == 0u );
}

TEST_CASE( "Cooked texture readers reject damaged files transactionally",
           "[CypherCommon][Formats][CookedTexture][Failure]" )
{
    texture_fixture_t fixture{};
    const usize cbRequired = CookedTexture_RequiredSize(
        fixture.texture,
        { fixture.mips, 3u } );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        fixture.texture,
        { fixture.mips, 3u },
        {},
        { file.data(), file.size() } ) ) );

    cooked_texture_view_t output{};
    output.nMipLevels = 77u;
    file.back() ^= static_cast<byte>( 1u );
    const cooked_texture_result_t damaged = CookedTexture_Read(
        { file.data(), file.size() },
        &output );
    REQUIRE( damaged.status == cooked_texture_status_t::RESOURCE_ERROR );
    REQUIRE( damaged.resourceStatus ==
             cooked_resource_status_t::CONTENT_HASH_MISMATCH );
    REQUIRE( output.nMipLevels == 77u );

    REQUIRE( CookedTexture_Read(
                 { file.data(), file.size() - 1u },
                 &output ).status ==
             cooked_texture_status_t::RESOURCE_ERROR );
    REQUIRE( output.nMipLevels == 77u );
}

TEST_CASE( "Cooked texture helpers describe canonical pixel storage",
           "[CypherCommon][Formats][CookedTexture][Helpers]" )
{
    REQUIRE( CookedTexture_BytesPerPixel(
                 render_texture_pixel_format_t::RGBA8_UNORM ) == 4u );
    REQUIRE( CookedTexture_BytesPerPixel(
                 render_texture_pixel_format_t::RGBA8_SRGB ) == 4u );
    REQUIRE( CookedTexture_BytesPerPixel(
                 render_texture_pixel_format_t::RGBA32_FLOAT ) == 16u );
    REQUIRE( CookedTexture_BytesPerPixel(
                 static_cast<render_texture_pixel_format_t>( 99u ) ) == 0u );
    REQUIRE( CookedTexture_FullMipCount( 1u, 1u ) == 1u );
    REQUIRE( CookedTexture_FullMipCount( 4u, 2u ) == 3u );
    REQUIRE( CookedTexture_FullMipCount( 0u, 2u ) == 0u );
    REQUIRE( CookedTexture_MetadataSize( 3u ) == 320u );
    REQUIRE( CookedTexture_SubresourceCount( texture_fixture_t{}.texture ) ==
             3u );
}
