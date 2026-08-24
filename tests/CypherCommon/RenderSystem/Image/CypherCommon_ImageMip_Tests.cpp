//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageMip_Tests.cpp
//  Purpose: Tests mip extent planning and next-level image generation.
//  Details: Coverage includes 1D/2D/3D chains, odd dimensions, box-filtered
//           values, invalid relationships, and nested resize diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageMip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string_view>
#include <vector>

using namespace cypher::common;

namespace
{

struct mip_test_image_t {
    image_desc_t desc{};
    usize cbRowPitch{ 0u };
    usize cbSlicePitch{ 0u };
    std::vector<byte> pixels{};
};

mip_test_image_t MakeMipImage(
    image_pixel_format_t format,
    const image_extent_t &extent,
    image_color_space_t colorSpace = image_color_space_t::LINEAR,
    image_alpha_mode_t alphaMode = image_alpha_mode_t::NONE )
{
    mip_test_image_t image{};
    image.desc = { extent, format, colorSpace, alphaMode };
    const image_format_info_t *pInfo = ImageFormat_GetInfo( format );
    REQUIRE( pInfo != nullptr );
    image.cbRowPitch = static_cast<usize>( extent.nWidth ) * pInfo->cbPixel;
    image.cbSlicePitch = image.cbRowPitch * extent.nHeight;
    image.pixels.resize( image.cbSlicePitch * extent.nDepth, 0u );
    return image;
}

image_view_t MipWriteView( mip_test_image_t &image ) noexcept
{
    return {
        image.desc,
        { image.pixels.data(), image.pixels.size() },
        image.cbRowPitch,
        image.cbSlicePitch
    };
}

const_image_view_t MipReadView( const mip_test_image_t &image ) noexcept
{
    return {
        image.desc,
        { image.pixels.data(), image.pixels.size() },
        image.cbRowPitch,
        image.cbSlicePitch
    };
}

byte *MipPixel(
    mip_test_image_t &image,
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

void WriteMipR32(
    mip_test_image_t &image,
    u32 iColumn,
    u32 iRow,
    f32 value ) noexcept
{
    std::memcpy( MipPixel( image, iColumn, iRow ), &value, sizeof( value ) );
}

f32 ReadMipR32(
    const mip_test_image_t &image,
    u32 iColumn,
    u32 iRow ) noexcept
{
    f32 value = 0.0f;
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( image.desc.pixelFormat );
    const byte *pPixel = image.pixels.data() +
        static_cast<usize>( iRow ) * image.cbRowPitch +
        static_cast<usize>( iColumn ) * pInfo->cbPixel;
    std::memcpy( &value, pPixel, sizeof( value ) );
    return value;
}

} // namespace

TEST_CASE( "Image mip planning reaches one on every dimension",
           "[CypherCommon][Image][Mip][Extent]" )
{
    REQUIRE( ImageMip_CalculateLevelCount( { 1u, 1u, 1u } ) == 1u );
    REQUIRE( ImageMip_CalculateLevelCount( { 4u, 2u, 1u } ) == 3u );
    REQUIRE( ImageMip_CalculateLevelCount( { 5u, 3u, 2u } ) == 3u );
    REQUIRE( ImageMip_CalculateLevelCount( { 0u, 4u, 1u } ) == 0u );

    const image_extent_t level0 = ImageMip_CalculateLevelExtent(
        { 9u, 5u, 3u }, 0u );
    const image_extent_t level1 = ImageMip_CalculateLevelExtent(
        { 9u, 5u, 3u }, 1u );
    const image_extent_t level2 = ImageMip_CalculateLevelExtent(
        { 9u, 5u, 3u }, 2u );
    const image_extent_t terminal = ImageMip_CalculateLevelExtent(
        { 9u, 5u, 3u }, CY_U32_MAX );
    REQUIRE( level0.nWidth == 9u );
    REQUIRE( level0.nHeight == 5u );
    REQUIRE( level0.nDepth == 3u );
    REQUIRE( level1.nWidth == 4u );
    REQUIRE( level1.nHeight == 2u );
    REQUIRE( level1.nDepth == 1u );
    REQUIRE( level2.nWidth == 2u );
    REQUIRE( level2.nHeight == 1u );
    REQUIRE( level2.nDepth == 1u );
    REQUIRE( terminal.nWidth == 1u );
    REQUIRE( terminal.nHeight == 1u );
    REQUIRE( terminal.nDepth == 1u );

    const image_extent_t invalid = ImageMip_CalculateLevelExtent(
        { 4u, 0u, 1u }, 2u );
    REQUIRE( invalid.nWidth == 0u );
    REQUIRE( invalid.nHeight == 0u );
    REQUIRE( invalid.nDepth == 0u );
}

TEST_CASE( "Image mip next-level relation uses floor halving",
           "[CypherCommon][Image][Mip][Extent]" )
{
    REQUIRE( ImageMip_IsNextLevelExtent(
        { 5u, 3u, 2u }, { 2u, 1u, 1u } ) );
    REQUIRE_FALSE( ImageMip_IsNextLevelExtent(
        { 5u, 3u, 2u }, { 3u, 2u, 1u } ) );
    REQUIRE_FALSE( ImageMip_IsNextLevelExtent(
        { 1u, 1u, 1u }, { 1u, 1u, 1u } ) );
    REQUIRE_FALSE( ImageMip_IsNextLevelExtent(
        { 0u, 2u, 1u }, { 1u, 1u, 1u } ) );
}

TEST_CASE( "Image mip generation averages each even source footprint",
           "[CypherCommon][Image][Mip][Generate]" )
{
    mip_test_image_t source = MakeMipImage(
        image_pixel_format_t::R32_FLOAT,
        { 4u, 4u, 1u } );
    f32 value = 0.0f;
    for ( u32 iRow = 0u; iRow < 4u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 4u; ++iColumn ) {
            WriteMipR32( source, iColumn, iRow, value++ );
        }
    }
    mip_test_image_t destination = MakeMipImage(
        image_pixel_format_t::R32_FLOAT,
        { 2u, 2u, 1u } );

    const image_mip_result_t result = ImageMip_GenerateNextLevel(
        MipWriteView( destination ),
        MipReadView( source ) );
    REQUIRE( result.status == image_mip_status_t::OK );
    REQUIRE( result.resizeStatus == image_resize_status_t::OK );
    REQUIRE( ReadMipR32( destination, 0u, 0u ) == Catch::Approx( 2.5f ) );
    REQUIRE( ReadMipR32( destination, 1u, 0u ) == Catch::Approx( 4.5f ) );
    REQUIRE( ReadMipR32( destination, 0u, 1u ) == Catch::Approx( 10.5f ) );
    REQUIRE( ReadMipR32( destination, 1u, 1u ) == Catch::Approx( 12.5f ) );
}

TEST_CASE( "Image mip generation integrates odd dimensions",
           "[CypherCommon][Image][Mip][Generate][Odd]" )
{
    mip_test_image_t source = MakeMipImage(
        image_pixel_format_t::R32_FLOAT,
        { 3u, 3u, 1u } );
    f32 value = 0.0f;
    for ( u32 iRow = 0u; iRow < 3u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 3u; ++iColumn ) {
            WriteMipR32( source, iColumn, iRow, value++ );
        }
    }
    mip_test_image_t destination = MakeMipImage(
        image_pixel_format_t::R32_FLOAT,
        { 1u, 1u, 1u } );
    const image_mip_result_t result = ImageMip_GenerateNextLevel(
        MipWriteView( destination ),
        MipReadView( source ) );
    REQUIRE( result.status == image_mip_status_t::OK );
    REQUIRE( ReadMipR32( destination, 0u, 0u ) == Catch::Approx( 4.0f ) );
}

TEST_CASE( "Image mip generation preserves resize failure detail",
           "[CypherCommon][Image][Mip][Failure]" )
{
    mip_test_image_t packedSource = MakeMipImage(
        image_pixel_format_t::RGBA8_UNORM,
        { 4u, 4u, 1u },
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT );
    mip_test_image_t packedDestination = MakeMipImage(
        image_pixel_format_t::RGBA8_UNORM,
        { 2u, 2u, 1u },
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT );
    image_mip_result_t result = ImageMip_GenerateNextLevel(
        MipWriteView( packedDestination ),
        MipReadView( packedSource ) );
    REQUIRE( result.status == image_mip_status_t::RESIZE_FAILED );
    REQUIRE( result.resizeStatus ==
             image_resize_status_t::FILTER_FORMAT_NOT_SUPPORTED );

    mip_test_image_t wrongExtent = MakeMipImage(
        image_pixel_format_t::RGBA8_UNORM,
        { 3u, 3u, 1u },
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT );
    result = ImageMip_GenerateNextLevel(
        MipWriteView( wrongExtent ),
        MipReadView( packedSource ) );
    REQUIRE( result.status == image_mip_status_t::INVALID_LEVEL_EXTENT );

    const_image_view_t invalidSource = MipReadView( packedSource );
    invalidSource.pixels.pData = nullptr;
    result = ImageMip_GenerateNextLevel(
        MipWriteView( packedDestination ),
        invalidSource );
    REQUIRE( result.status == image_mip_status_t::INVALID_SOURCE_VIEW );
    REQUIRE( result.resizeStatus == image_resize_status_t::INVALID_SOURCE_VIEW );
}

TEST_CASE( "Image mip status names are stable",
           "[CypherCommon][Image][Mip][Status]" )
{
    REQUIRE( std::string_view( ImageMip_StatusName(
                 image_mip_status_t::OK ) ) == "OK" );
    REQUIRE( std::string_view( ImageMip_StatusName(
                 image_mip_status_t::RESIZE_FAILED ) ) == "RESIZE_FAILED" );
    REQUIRE( std::string_view( ImageMip_StatusName(
                 static_cast<image_mip_status_t>( 255u ) ) ) ==
             "UNKNOWN_IMAGE_MIP_STATUS" );
}
