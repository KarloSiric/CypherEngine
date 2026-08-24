//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageResize_Tests.cpp
//  Purpose: Tests nearest, linear, and area-box image resizing.
//  Details: Coverage includes pitched 2D/3D storage, center sampling, alpha-safe
//           interpolation, odd area ratios, aliasing, and failure atomicity.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageResize.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <string_view>
#include <vector>

using namespace cypher::common;

namespace
{

constexpr byte kResizePadding = 0xCDu;

struct resize_test_image_t {
    image_desc_t desc{};
    usize cbRowPitch{ 0u };
    usize cbSlicePitch{ 0u };
    std::vector<byte> pixels{};
};

image_desc_t ResizeDesc(
    image_pixel_format_t format,
    image_color_space_t colorSpace,
    image_alpha_mode_t alphaMode,
    u32 nWidth,
    u32 nHeight = 1u,
    u32 nDepth = 1u ) noexcept
{
    return {
        { nWidth, nHeight, nDepth },
        format,
        colorSpace,
        alphaMode
    };
}

resize_test_image_t MakeResizeImage(
    const image_desc_t &desc,
    usize cbRowPadding = 0u,
    usize cbSlicePadding = 0u,
    byte initialValue = kResizePadding )
{
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( desc.pixelFormat );
    REQUIRE( pInfo != nullptr );

    resize_test_image_t image{};
    image.desc = desc;
    image.cbRowPitch =
        static_cast<usize>( desc.extent.nWidth ) * pInfo->cbPixel +
        cbRowPadding;
    image.cbSlicePitch =
        image.cbRowPitch * static_cast<usize>( desc.extent.nHeight ) +
        cbSlicePadding;
    image.pixels.assign(
        image.cbSlicePitch * static_cast<usize>( desc.extent.nDepth ),
        initialValue );
    return image;
}

image_view_t ResizeWriteView( resize_test_image_t &image ) noexcept
{
    return {
        image.desc,
        { image.pixels.data(), image.pixels.size() },
        image.cbRowPitch,
        image.cbSlicePitch
    };
}

const_image_view_t ResizeReadView(
    const resize_test_image_t &image ) noexcept
{
    return {
        image.desc,
        { image.pixels.data(), image.pixels.size() },
        image.cbRowPitch,
        image.cbSlicePitch
    };
}

byte *ResizePixel(
    resize_test_image_t &image,
    u32 iColumn,
    u32 iRow = 0u,
    u32 iSlice = 0u ) noexcept
{
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( image.desc.pixelFormat );
    return image.pixels.data() +
           static_cast<usize>( iSlice ) * image.cbSlicePitch +
           static_cast<usize>( iRow ) * image.cbRowPitch +
           static_cast<usize>( iColumn ) * pInfo->cbPixel;
}

const byte *ResizePixel(
    const resize_test_image_t &image,
    u32 iColumn,
    u32 iRow = 0u,
    u32 iSlice = 0u ) noexcept
{
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( image.desc.pixelFormat );
    return image.pixels.data() +
           static_cast<usize>( iSlice ) * image.cbSlicePitch +
           static_cast<usize>( iRow ) * image.cbRowPitch +
           static_cast<usize>( iColumn ) * pInfo->cbPixel;
}

void WriteR32(
    resize_test_image_t &image,
    u32 iColumn,
    u32 iRow,
    u32 iSlice,
    f32 value ) noexcept
{
    std::memcpy(
        ResizePixel( image, iColumn, iRow, iSlice ),
        &value,
        sizeof( value ) );
}

f32 ReadR32(
    const resize_test_image_t &image,
    u32 iColumn,
    u32 iRow = 0u,
    u32 iSlice = 0u ) noexcept
{
    f32 value = 0.0f;
    std::memcpy(
        &value,
        ResizePixel( image, iColumn, iRow, iSlice ),
        sizeof( value ) );
    return value;
}

void WriteRgba32(
    resize_test_image_t &image,
    u32 iColumn,
    const std::array<f32, 4u> &value ) noexcept
{
    std::memcpy(
        ResizePixel( image, iColumn ),
        value.data(),
        sizeof( value ) );
}

std::array<f32, 4u> ReadRgba32(
    const resize_test_image_t &image,
    u32 iColumn = 0u,
    u32 iRow = 0u,
    u32 iSlice = 0u ) noexcept
{
    std::array<f32, 4u> value{};
    std::memcpy(
        value.data(),
        ResizePixel( image, iColumn, iRow, iSlice ),
        sizeof( value ) );
    return value;
}

} // namespace

TEST_CASE( "Image resize advertises filter support without hidden conversion",
           "[CypherCommon][Image][Resize][Support]" )
{
    const image_desc_t rgba8 = ResizeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT,
        4u,
        4u );
    REQUIRE( ImageResize_IsFilterSupported(
        rgba8, image_resize_filter_t::NEAREST ) );
    REQUIRE_FALSE( ImageResize_IsFilterSupported(
        rgba8, image_resize_filter_t::LINEAR ) );
    REQUIRE_FALSE( ImageResize_IsFilterSupported(
        rgba8, image_resize_filter_t::BOX ) );

    const image_desc_t working = ResizeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        4u,
        4u );
    REQUIRE( ImageResize_IsFilterSupported(
        working, image_resize_filter_t::NEAREST ) );
    REQUIRE( ImageResize_IsFilterSupported(
        working, image_resize_filter_t::LINEAR ) );
    REQUIRE( ImageResize_IsFilterSupported(
        working, image_resize_filter_t::BOX ) );
    REQUIRE_FALSE( ImageResize_IsFilterSupported(
        working, static_cast<image_resize_filter_t>( 255u ) ) );
}

TEST_CASE( "Image nearest resize maps raw pixels and preserves padding",
           "[CypherCommon][Image][Resize][Nearest][Pitch]" )
{
    const image_desc_t sourceDesc = ResizeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT,
        2u,
        2u );
    resize_test_image_t source = MakeResizeImage( sourceDesc, 3u, 5u );
    for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 2u; ++iColumn ) {
            byte *pPixel = ResizePixel( source, iColumn, iRow );
            const byte value = static_cast<byte>( iRow * 2u + iColumn + 1u );
            pPixel[0] = value;
            pPixel[1] = static_cast<byte>( value + 10u );
            pPixel[2] = static_cast<byte>( value + 20u );
            pPixel[3] = 255u;
        }
    }
    const std::vector<byte> sourceBefore = source.pixels;

    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT,
        4u,
        4u ), 7u, 9u );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::NEAREST ) == image_resize_status_t::OK );

    for ( u32 iRow = 0u; iRow < 4u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 4u; ++iColumn ) {
            const byte *pActual = ResizePixel( destination, iColumn, iRow );
            const byte *pExpected = ResizePixel(
                source,
                iColumn / 2u,
                iRow / 2u );
            REQUIRE( std::memcmp( pActual, pExpected, 4u ) == 0 );
        }
        const usize iPadding =
            static_cast<usize>( iRow ) * destination.cbRowPitch + 16u;
        for ( usize iByte = iPadding;
              iByte < static_cast<usize>( iRow + 1u ) *
                          destination.cbRowPitch;
              ++iByte ) {
            REQUIRE( destination.pixels[iByte] == kResizePadding );
        }
    }
    REQUIRE( source.pixels == sourceBefore );
}

TEST_CASE( "Image nearest resize samples the center of a 3D destination voxel",
           "[CypherCommon][Image][Resize][Nearest][3D]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        2u,
        2u,
        2u ) );
    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            for ( u32 iColumn = 0u; iColumn < 2u; ++iColumn ) {
                ResizePixel( source, iColumn, iRow, iSlice )[0] =
                    static_cast<byte>(
                        iSlice * 4u + iRow * 2u + iColumn );
            }
        }
    }
    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        1u,
        1u,
        1u ) );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::NEAREST ) == image_resize_status_t::OK );
    REQUIRE( destination.pixels[0] == 7u );
}

TEST_CASE( "Image linear resize uses pixel-center interpolation",
           "[CypherCommon][Image][Resize][Linear]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        2u ) );
    WriteR32( source, 0u, 0u, 0u, 0.0f );
    WriteR32( source, 1u, 0u, 0u, 1.0f );

    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        3u ) );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::LINEAR ) == image_resize_status_t::OK );
    REQUIRE( ReadR32( destination, 0u ) == Catch::Approx( 0.0f ) );
    REQUIRE( ReadR32( destination, 1u ) == Catch::Approx( 0.5f ) );
    REQUIRE( ReadR32( destination, 2u ) == Catch::Approx( 1.0f ) );
}

TEST_CASE( "Image linear resize doubles a 2D grid with clamped pixel centers",
           "[CypherCommon][Image][Resize][Linear][FastPath]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        2u,
        2u ) );
    WriteR32( source, 0u, 0u, 0u, 0.0f );
    WriteR32( source, 1u, 0u, 0u, 1.0f );
    WriteR32( source, 0u, 1u, 0u, 2.0f );
    WriteR32( source, 1u, 1u, 0u, 3.0f );

    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        4u,
        4u ), 5u );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::LINEAR ) == image_resize_status_t::OK );

    constexpr f32 expected[4u][4u]{
        { 0.0f, 0.25f, 0.75f, 1.0f },
        { 0.5f, 0.75f, 1.25f, 1.5f },
        { 1.5f, 1.75f, 2.25f, 2.5f },
        { 2.0f, 2.25f, 2.75f, 3.0f }
    };
    for ( u32 iRow = 0u; iRow < 4u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 4u; ++iColumn ) {
            REQUIRE( ReadR32( destination, iColumn, iRow ) ==
                     Catch::Approx( expected[iRow][iColumn] ) );
        }
    }
}

TEST_CASE( "Image RGBA32 exact-double resize preserves straight-alpha color",
           "[CypherCommon][Image][Resize][Linear][FastPath][Alpha]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        2u,
        2u ) );
    const std::array<f32, 4u> opaqueRed{ 1.0f, 0.0f, 0.0f, 1.0f };
    const std::array<f32, 4u> transparentBlue{ 0.0f, 0.0f, 1.0f, 0.0f };
    std::memcpy( ResizePixel( source, 0u, 0u ), opaqueRed.data(), 16u );
    std::memcpy( ResizePixel( source, 1u, 0u ), transparentBlue.data(), 16u );
    std::memcpy( ResizePixel( source, 0u, 1u ), transparentBlue.data(), 16u );
    std::memcpy( ResizePixel( source, 1u, 1u ), transparentBlue.data(), 16u );

    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        4u,
        4u ), 7u );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::LINEAR ) == image_resize_status_t::OK );

    const std::array<f32, 4u> corner = ReadRgba32( destination, 0u, 0u );
    REQUIRE( corner[0u] == Catch::Approx( 1.0f ) );
    REQUIRE( corner[2u] == Catch::Approx( 0.0f ) );
    REQUIRE( corner[3u] == Catch::Approx( 1.0f ) );

    const std::array<f32, 4u> blended = ReadRgba32( destination, 1u, 1u );
    REQUIRE( blended[0u] == Catch::Approx( 1.0f ) );
    REQUIRE( blended[1u] == Catch::Approx( 0.0f ) );
    REQUIRE( blended[2u] == Catch::Approx( 0.0f ) );
    REQUIRE( blended[3u] == Catch::Approx( 0.5625f ) );

    const std::array<f32, 4u> transparent =
        ReadRgba32( destination, 3u, 3u );
    REQUIRE( transparent[0u] == Catch::Approx( 0.0f ) );
    REQUIRE( transparent[1u] == Catch::Approx( 0.0f ) );
    REQUIRE( transparent[2u] == Catch::Approx( 0.0f ) );
    REQUIRE( transparent[3u] == Catch::Approx( 0.0f ) );
}

TEST_CASE( "Image linear resize performs trilinear volume sampling",
           "[CypherCommon][Image][Resize][Linear][3D]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        2u,
        2u,
        2u ) );
    f32 value = 0.0f;
    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            for ( u32 iColumn = 0u; iColumn < 2u; ++iColumn ) {
                WriteR32( source, iColumn, iRow, iSlice, value++ );
            }
        }
    }
    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        1u,
        1u,
        1u ) );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::LINEAR ) == image_resize_status_t::OK );
    REQUIRE( ReadR32( destination, 0u ) == Catch::Approx( 3.5f ) );
}

TEST_CASE( "Image linear resize filters straight alpha without color halos",
           "[CypherCommon][Image][Resize][Linear][Alpha]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        2u ) );
    WriteRgba32( source, 0u, { 1.0f, 0.0f, 0.0f, 1.0f } );
    WriteRgba32( source, 1u, { 0.0f, 0.0f, 1.0f, 0.0f } );
    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        1u ) );

    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::LINEAR ) == image_resize_status_t::OK );
    const std::array<f32, 4u> actual = ReadRgba32( destination );
    REQUIRE( actual[0] == Catch::Approx( 1.0f ) );
    REQUIRE( actual[1] == Catch::Approx( 0.0f ) );
    REQUIRE( actual[2] == Catch::Approx( 0.0f ) );
    REQUIRE( actual[3] == Catch::Approx( 0.5f ) );
}

TEST_CASE( "Image exact-half filters share alpha-correct RGBA32 results",
           "[CypherCommon][Image][Resize][FastPath][Alpha]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        2u,
        2u ) );
    const std::array<f32, 4u> opaqueRed{ 1.0f, 0.0f, 0.0f, 1.0f };
    const std::array<f32, 4u> transparentBlue{ 0.0f, 0.0f, 1.0f, 0.0f };
    std::memcpy( ResizePixel( source, 0u, 0u ), opaqueRed.data(), 16u );
    std::memcpy( ResizePixel( source, 1u, 0u ), transparentBlue.data(), 16u );
    std::memcpy( ResizePixel( source, 0u, 1u ), transparentBlue.data(), 16u );
    std::memcpy( ResizePixel( source, 1u, 1u ), transparentBlue.data(), 16u );

    for ( const image_resize_filter_t filter : {
              image_resize_filter_t::LINEAR,
              image_resize_filter_t::BOX } ) {
        resize_test_image_t destination = MakeResizeImage( ResizeDesc(
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT,
            1u,
            1u ) );
        REQUIRE( ImageResize(
                     ResizeWriteView( destination ),
                     ResizeReadView( source ),
                     filter ) == image_resize_status_t::OK );

        const std::array<f32, 4u> actual = ReadRgba32( destination );
        REQUIRE( actual[0u] == Catch::Approx( 1.0f ) );
        REQUIRE( actual[1u] == Catch::Approx( 0.0f ) );
        REQUIRE( actual[2u] == Catch::Approx( 0.0f ) );
        REQUIRE( actual[3u] == Catch::Approx( 0.25f ) );
    }
}

TEST_CASE( "Image box resize integrates odd source footprints",
           "[CypherCommon][Image][Resize][Box]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        3u,
        3u ) );
    f32 value = 0.0f;
    for ( u32 iRow = 0u; iRow < 3u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 3u; ++iColumn ) {
            WriteR32( source, iColumn, iRow, 0u, value++ );
        }
    }
    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        1u,
        1u ) );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::BOX ) == image_resize_status_t::OK );
    REQUIRE( ReadR32( destination, 0u ) == Catch::Approx( 4.0f ) );
}

TEST_CASE( "Image resize validates operations before writing destination",
           "[CypherCommon][Image][Resize][Failure]" )
{
    resize_test_image_t source = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        2u,
        2u ) );
    resize_test_image_t destination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        4u,
        4u ) );
    const std::vector<byte> before = destination.pixels;

    image_view_t invalidDestination = ResizeWriteView( destination );
    invalidDestination.pixels.pData = nullptr;
    REQUIRE( ImageResize(
                 invalidDestination,
                 ResizeReadView( source ),
                 image_resize_filter_t::NEAREST ) ==
             image_resize_status_t::INVALID_DESTINATION_VIEW );
    const_image_view_t invalidSource = ResizeReadView( source );
    invalidSource.pixels.pData = nullptr;
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 invalidSource,
                 image_resize_filter_t::NEAREST ) ==
             image_resize_status_t::INVALID_SOURCE_VIEW );
    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 static_cast<image_resize_filter_t>( 255u ) ) ==
             image_resize_status_t::INVALID_FILTER );

    resize_test_image_t formatMismatch = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R16_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        4u,
        4u ) );
    REQUIRE( ImageResize(
                 ResizeWriteView( formatMismatch ),
                 ResizeReadView( source ),
                 image_resize_filter_t::NEAREST ) ==
             image_resize_status_t::PIXEL_FORMAT_MISMATCH );

    resize_test_image_t colorMismatch = destination;
    colorMismatch.desc.colorSpace = image_color_space_t::SRGB;
    REQUIRE( ImageResize(
                 ResizeWriteView( colorMismatch ),
                 ResizeReadView( source ),
                 image_resize_filter_t::NEAREST ) ==
             image_resize_status_t::COLOR_SPACE_MISMATCH );

    resize_test_image_t alphaSource = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT,
        2u,
        2u ) );
    resize_test_image_t alphaMismatch = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::PREMULTIPLIED,
        4u,
        4u ) );
    REQUIRE( ImageResize(
                 ResizeWriteView( alphaMismatch ),
                 ResizeReadView( alphaSource ),
                 image_resize_filter_t::NEAREST ) ==
             image_resize_status_t::ALPHA_MODE_MISMATCH );

    REQUIRE( ImageResize(
                 ResizeWriteView( destination ),
                 ResizeReadView( source ),
                 image_resize_filter_t::LINEAR ) ==
             image_resize_status_t::FILTER_FORMAT_NOT_SUPPORTED );

    resize_test_image_t boxSource = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        2u,
        2u ) );
    resize_test_image_t boxDestination = MakeResizeImage( ResizeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        4u,
        4u ) );
    REQUIRE( ImageResize(
                 ResizeWriteView( boxDestination ),
                 ResizeReadView( boxSource ),
                 image_resize_filter_t::BOX ) ==
             image_resize_status_t::BOX_REQUIRES_DOWNSAMPLING );

    std::vector<byte> shared( 16u, 0x5Au );
    const const_image_view_t overlappingSource{
        source.desc,
        { shared.data(), shared.size() },
        2u,
        4u
    };
    const image_view_t overlappingDestination{
        destination.desc,
        { shared.data(), shared.size() },
        4u,
        16u
    };
    REQUIRE( ImageResize(
                 overlappingDestination,
                 overlappingSource,
                 image_resize_filter_t::NEAREST ) ==
             image_resize_status_t::OVERLAPPING_MEMORY );
    REQUIRE( destination.pixels == before );
}

TEST_CASE( "Image resize status names are stable",
           "[CypherCommon][Image][Resize][Status]" )
{
    REQUIRE( std::string_view( ImageResize_StatusName(
                 image_resize_status_t::OK ) ) == "OK" );
    REQUIRE( std::string_view( ImageResize_StatusName(
                 image_resize_status_t::FILTER_FORMAT_NOT_SUPPORTED ) ) ==
             "FILTER_FORMAT_NOT_SUPPORTED" );
    REQUIRE( std::string_view( ImageResize_StatusName(
                 static_cast<image_resize_status_t>( 255u ) ) ) ==
             "UNKNOWN_IMAGE_RESIZE_STATUS" );
}
