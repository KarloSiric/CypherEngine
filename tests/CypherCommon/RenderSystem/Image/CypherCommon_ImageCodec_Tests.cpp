//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageCodec_Tests.cpp
//  Purpose: Tests shared image-file import and PNG export behavior.
//  Details: Coverage exercises real PNG, JPEG, TGA, and EXR payloads, origin
//           normalization, resource limits, malformed data, and transactional
//           output ownership used by Picasso and texture compilation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageCodec.h"

#include "CypherCommon_Allocator.h"
#include "CypherCommon_ImageView.h"
#include "CypherCommon_MemoryOps.h"

#include <png.h>
#include <tinyexr.h>
#include <turbojpeg.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string_view>
#include <vector>

using namespace cypher::common;

namespace
{

template <usize nExtent>
constexpr string_view_t TestText( const char ( &text )[nExtent] ) noexcept
{
    return { text, nExtent - 1u };
}

binary_block_t Block( const std::vector<byte> &bytes ) noexcept
{
    return { bytes.data(), bytes.size() };
}

const_image_view_t ConstView( const image_surface_t &surface ) noexcept
{
    return ImageSurface_GetView( &surface );
}

std::vector<byte> MakePng(
    u32 nWidth,
    u32 nHeight,
    const std::vector<byte> &rgba )
{
    REQUIRE( rgba.size() ==
             static_cast<usize>( nWidth ) * nHeight * 4u );
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = nWidth;
    image.height = nHeight;
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t cbPng = 0u;
    REQUIRE( png_image_write_to_memory(
                 &image,
                 nullptr,
                 &cbPng,
                 0,
                 rgba.data(),
                 0,
                 nullptr ) != 0 );
    std::vector<byte> encoded( static_cast<usize>( cbPng ) );
    REQUIRE( png_image_write_to_memory(
                 &image,
                 encoded.data(),
                 &cbPng,
                 0,
                 rgba.data(),
                 0,
                 nullptr ) != 0 );
    encoded.resize( static_cast<usize>( cbPng ) );
    return encoded;
}

std::vector<byte> MakeJpeg(
    u32 nWidth,
    u32 nHeight,
    const std::vector<byte> &rgba )
{
    tjhandle encoder = tj3Init( TJINIT_COMPRESS );
    REQUIRE( encoder != nullptr );
    REQUIRE( tj3Set( encoder, TJPARAM_QUALITY, 100 ) == 0 );
    REQUIRE( tj3Set( encoder, TJPARAM_SUBSAMP, TJSAMP_444 ) == 0 );
    unsigned char *pEncoded = nullptr;
    size_t cbEncoded = 0u;
    REQUIRE( tj3Compress8(
                 encoder,
                 rgba.data(),
                 static_cast<int>( nWidth ),
                 0,
                 static_cast<int>( nHeight ),
                 TJPF_RGBA,
                 &pEncoded,
                 &cbEncoded ) == 0 );
    std::vector<byte> result( pEncoded, pEncoded + cbEncoded );
    tj3Free( pEncoded );
    tj3Destroy( encoder );
    return result;
}

std::vector<byte> MakeExr(
    u32 nWidth,
    u32 nHeight,
    const std::vector<f32> &rgba )
{
    unsigned char *pEncoded = nullptr;
    const char *pError = nullptr;
    const int cbEncoded = SaveEXRToMemory(
        rgba.data(),
        static_cast<int>( nWidth ),
        static_cast<int>( nHeight ),
        4,
        0,
        &pEncoded,
        &pError );
    INFO( ( pError != nullptr ? pError : "" ) );
    REQUIRE( cbEncoded > 0 );
    std::vector<byte> result( pEncoded, pEncoded + cbEncoded );
    std::free( pEncoded );
    if ( pError != nullptr ) {
        FreeEXRErrorMessage( pError );
    }
    return result;
}

std::vector<byte> MakeTgaTopLeft24()
{
    // Two top-origin BGR pixels: opaque red followed by opaque green.
    std::vector<byte> bytes( 18u, 0u );
    bytes[2] = 2u;
    bytes[12] = 2u;
    bytes[14] = 1u;
    bytes[16] = 24u;
    bytes[17] = 0x20u;
    bytes.insert( bytes.end(), { 0u, 0u, 255u, 0u, 255u, 0u } );
    return bytes;
}

std::vector<byte> MakeTgaBottomLeftRle32()
{
    // A 2x2 bottom-origin image. One RLE packet fills all pixels with blue.
    std::vector<byte> bytes( 18u, 0u );
    bytes[2] = 10u;
    bytes[12] = 2u;
    bytes[14] = 2u;
    bytes[16] = 32u;
    bytes[17] = 8u;
    bytes.insert( bytes.end(), { 0x83u, 255u, 0u, 0u, 128u } );
    return bytes;
}

} // namespace

TEST_CASE( "Image codec recognizes supported source formats",
           "[CypherCommon][Image][Codec][Format]" )
{
    REQUIRE( ImageCodec_FormatFromPath( TestText( "art/wall.PNG" ) ) ==
             image_file_format_t::PNG );
    REQUIRE( ImageCodec_FormatFromPath( TestText( "art/wall.jpeg" ) ) ==
             image_file_format_t::JPEG );
    REQUIRE( ImageCodec_FormatFromPath( TestText( "art/wall.TGA" ) ) ==
             image_file_format_t::TGA );
    REQUIRE( ImageCodec_FormatFromPath( TestText( "art/wall.exr" ) ) ==
             image_file_format_t::EXR );
    REQUIRE( ImageCodec_FormatFromPath( TestText( "art/wall.bmp" ) ) ==
             image_file_format_t::UNKNOWN );

    const std::vector<byte> png = MakePng(
        1u, 1u, { 1u, 2u, 3u, 4u } );
    REQUIRE( ImageCodec_DetectFormat( Block( png ) ) ==
             image_file_format_t::PNG );
    REQUIRE( ImageCodec_DetectFormat(
                 Block( MakeTgaTopLeft24() ),
                 image_file_format_t::TGA ) == image_file_format_t::TGA );
}

TEST_CASE( "Image codec decodes aligned PNG and exports a round trip",
           "[CypherCommon][Image][Codec][PNG]" )
{
    const std::vector<byte> expected{
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 128u,
        0u, 0u, 255u, 64u, 255u, 255u, 255u, 0u
    };
    const std::vector<byte> encoded = MakePng( 2u, 2u, expected );
    image_decode_options_t options{};
    options.cbRowAlignment = 16u;
    image_surface_t decoded{};
    const image_decode_result_t result = ImageCodec_Decode(
        Block( encoded ),
        Allocator_GetSystem(),
        options,
        &decoded );

    REQUIRE( result.status == image_codec_status_t::OK );
    REQUIRE( result.sourceFormat == image_file_format_t::PNG );
    REQUIRE( result.desc.extent.nWidth == 2u );
    REQUIRE( result.desc.extent.nHeight == 2u );
    REQUIRE( decoded.layout.cbRowPitch == 16u );
    for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
        const binary_block_t row = ImageView_GetRow(
            ConstView( decoded ), iRow, 0u );
        REQUIRE( Cy_MemEqual(
            row.pData,
            expected.data() + static_cast<usize>( iRow ) * 8u,
            8u ) );
    }

    blob_t roundTrip{};
    REQUIRE( ImageCodec_EncodePng(
                 ConstView( decoded ),
                 Allocator_GetSystem(),
                 &roundTrip ) == image_codec_status_t::OK );
    image_surface_t decodedAgain{};
    REQUIRE( ImageCodec_Decode(
                 Blob_Block( &roundTrip ),
                 Allocator_GetSystem(),
                 {},
                 &decodedAgain ).status == image_codec_status_t::OK );
    REQUIRE( decodedAgain.desc.extent.nWidth == 2u );
    REQUIRE( decodedAgain.desc.extent.nHeight == 2u );
}

TEST_CASE( "Image codec decodes JPEG and finite EXR",
           "[CypherCommon][Image][Codec][Interchange]" )
{
    SECTION( "JPEG" )
    {
        const std::vector<byte> jpeg = MakeJpeg(
            1u, 1u, { 200u, 100u, 50u, 255u } );
        image_surface_t image{};
        const image_decode_result_t result = ImageCodec_Decode(
            Block( jpeg ), Allocator_GetSystem(), {}, &image );
        REQUIRE( result.status == image_codec_status_t::OK );
        REQUIRE( result.sourceFormat == image_file_format_t::JPEG );
        REQUIRE( image.desc.pixelFormat == image_pixel_format_t::RGBA8_UNORM );
        REQUIRE( image.desc.alphaMode == image_alpha_mode_t::NONE );
    }

    SECTION( "EXR" )
    {
        const std::vector<f32> pixels{ 0.25f, 0.5f, 1.0f, 1.0f };
        const std::vector<byte> exr = MakeExr( 1u, 1u, pixels );
        image_surface_t image{};
        const image_decode_result_t result = ImageCodec_Decode(
            Block( exr ), Allocator_GetSystem(), {}, &image );
        REQUIRE( result.status == image_codec_status_t::OK );
        REQUIRE( result.sourceFormat == image_file_format_t::EXR );
        REQUIRE( image.desc.pixelFormat == image_pixel_format_t::RGBA32_FLOAT );
        REQUIRE( image.desc.colorSpace == image_color_space_t::LINEAR );
        const auto *pDecoded = static_cast<const f32 *>(
            image.allocation.pData );
        REQUIRE( pDecoded[0] == pixels[0] );
        REQUIRE( pDecoded[1] == pixels[1] );
        REQUIRE( pDecoded[2] == pixels[2] );
        REQUIRE( pDecoded[3] == pixels[3] );
    }
}

TEST_CASE( "Image codec normalizes TGA channel order, origin, and RLE",
           "[CypherCommon][Image][Codec][TGA]" )
{
    image_decode_options_t options{};
    options.formatHint = image_file_format_t::TGA;

    SECTION( "top-left uncompressed" )
    {
        const std::vector<byte> tga = MakeTgaTopLeft24();
        image_surface_t image{};
        REQUIRE( ImageCodec_Decode(
                     Block( tga ),
                     Allocator_GetSystem(),
                     options,
                     &image ).status == image_codec_status_t::OK );
        const binary_block_t row = ImageView_GetRow(
            ConstView( image ), 0u, 0u );
        const byte expected[]{
            255u, 0u, 0u, 255u,
            0u, 255u, 0u, 255u
        };
        REQUIRE( Cy_MemEqual( row.pData, expected, sizeof( expected ) ) );
    }

    SECTION( "bottom-left RLE" )
    {
        const std::vector<byte> tga = MakeTgaBottomLeftRle32();
        image_surface_t image{};
        REQUIRE( ImageCodec_Decode(
                     Block( tga ),
                     Allocator_GetSystem(),
                     options,
                     &image ).status == image_codec_status_t::OK );
        const const_image_view_t view = ConstView( image );
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            const binary_block_t row = ImageView_GetRow( view, iRow, 0u );
            const byte expected[]{
                0u, 0u, 255u, 128u,
                0u, 0u, 255u, 128u
            };
            REQUIRE( Cy_MemEqual( row.pData, expected, sizeof( expected ) ) );
        }
    }
}

TEST_CASE( "Image codec failures leave destinations untouched",
           "[CypherCommon][Image][Codec][Failure]" )
{
    const std::vector<byte> malformed{ 1u, 2u, 3u, 4u };
    image_surface_t output{};
    REQUIRE( ImageCodec_Decode(
                 Block( malformed ),
                 Allocator_GetSystem(),
                 {},
                 &output ).status == image_codec_status_t::UNKNOWN_FORMAT );
    REQUIRE( ImageSurface_IsEmpty( &output ) );

    const std::vector<byte> png = MakePng(
        2u, 2u, std::vector<byte>( 16u, 255u ) );
    image_decode_options_t limited{};
    limited.nMaximumDimension = 1u;
    REQUIRE( ImageCodec_Decode(
                 Block( png ),
                 Allocator_GetSystem(),
                 limited,
                 &output ).status == image_codec_status_t::INVALID_DIMENSIONS );
    REQUIRE( ImageSurface_IsEmpty( &output ) );

    blob_t encoded{};
    const byte scalar = 42u;
    const const_image_view_t unsupported{
        {
            { 1u, 1u, 1u },
            image_pixel_format_t::R8_UNORM,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::NONE
        },
        { &scalar, 1u },
        1u,
        1u
    };
    REQUIRE( ImageCodec_EncodePng(
                 unsupported,
                 Allocator_GetSystem(),
                 &encoded ) == image_codec_status_t::UNSUPPORTED_PIXEL_FORMAT );
    REQUIRE( encoded.pData == nullptr );
}
