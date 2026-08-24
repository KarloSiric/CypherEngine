//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageProcess_Tests.cpp
//  Purpose: Tests allocation-free image copy, fill, flip, and rotation kernels.
//  Details: Coverage includes pitched storage, depth slices, region bounds,
//           metadata mismatches, aliasing policy, and exact in-place transforms.
//
//  History:
//  - Created by Karlo Siric on 2026-08-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageProcess.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

using namespace cypher::common;

namespace
{

constexpr byte kPaddingByte = 0xCDu;

image_desc_t R8Desc(
    u32 nWidth,
    u32 nHeight,
    u32 nDepth = 1u ) noexcept
{
    return {
        { nWidth, nHeight, nDepth },
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE
    };
}

image_view_t WritableView(
    std::vector<byte> &pixels,
    const image_desc_t &desc,
    usize cbRowPitch,
    usize cbSlicePitch ) noexcept
{
    return {
        desc,
        { pixels.data(), pixels.size() },
        cbRowPitch,
        cbSlicePitch
    };
}

byte PixelValue(
    const const_image_view_t &view,
    u32 iColumn,
    u32 iRow,
    u32 iSlice = 0u ) noexcept
{
    const binary_block_t pixel = ImageView_GetPixel(
        view,
        iColumn,
        iRow,
        iSlice );
    return pixel.pData == nullptr ? 0u : pixel.pData[0];
}

void SetR8Pixels(
    const image_view_t &view,
    std::initializer_list<byte> values )
{
    REQUIRE( values.size() ==
             static_cast<usize>( view.desc.extent.nWidth ) *
             static_cast<usize>( view.desc.extent.nHeight ) *
             static_cast<usize>( view.desc.extent.nDepth ) );

    auto value = values.begin();
    for ( u32 iSlice = 0u;
          iSlice < view.desc.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < view.desc.extent.nHeight;
              ++iRow ) {
            for ( u32 iColumn = 0u;
                  iColumn < view.desc.extent.nWidth;
                  ++iColumn ) {
                const byte_span_t pixel = ImageView_GetPixel(
                    view,
                    iColumn,
                    iRow,
                    iSlice );
                REQUIRE( pixel.pData != nullptr );
                pixel.pData[0] = *value;
                ++value;
            }
        }
    }
}

void RequireR8Pixels(
    const const_image_view_t &view,
    std::initializer_list<byte> expected )
{
    REQUIRE( expected.size() ==
             static_cast<usize>( view.desc.extent.nWidth ) *
             static_cast<usize>( view.desc.extent.nHeight ) *
             static_cast<usize>( view.desc.extent.nDepth ) );

    auto value = expected.begin();
    for ( u32 iSlice = 0u;
          iSlice < view.desc.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < view.desc.extent.nHeight;
              ++iRow ) {
            for ( u32 iColumn = 0u;
                  iColumn < view.desc.extent.nWidth;
                  ++iColumn ) {
                REQUIRE( PixelValue(
                             view,
                             iColumn,
                             iRow,
                             iSlice ) == *value );
                ++value;
            }
        }
    }
}

void RequireRowPadding(
    const image_view_t &view,
    byte expected )
{
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( view.desc.pixelFormat );
    REQUIRE( pInfo != nullptr );
    const usize cbLogicalRow =
        static_cast<usize>( view.desc.extent.nWidth ) * pInfo->cbPixel;

    for ( u32 iSlice = 0u;
          iSlice < view.desc.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < view.desc.extent.nHeight;
              ++iRow ) {
            const usize iOffset =
                static_cast<usize>( iSlice ) * view.cbSlicePitch +
                static_cast<usize>( iRow ) * view.cbRowPitch;
            for ( usize iByte = cbLogicalRow;
                  iByte < view.cbRowPitch;
                  ++iByte ) {
                REQUIRE( view.pixels.pData[iOffset + iByte] == expected );
            }
        }
    }
}

} // namespace

TEST_CASE( "Image process regions use checked half-open bounds",
           "[CypherCommon][Image][Process][Region]" )
{
    const image_desc_t desc = R8Desc( 8u, 6u, 2u );
    const image_region_t full = ImageProcess_FullRegion( desc );
    REQUIRE( full.origin.iColumn == 0u );
    REQUIRE( full.extent.nWidth == 8u );
    REQUIRE( full.extent.nHeight == 6u );
    REQUIRE( full.extent.nDepth == 2u );
    REQUIRE( ImageProcess_IsRegionValid( desc, full ) );

    REQUIRE( ImageProcess_IsRegionValid(
        desc,
        { { 6u, 4u, 1u }, { 2u, 2u, 1u } } ) );
    REQUIRE_FALSE( ImageProcess_IsRegionValid(
        desc,
        { { 7u, 4u, 1u }, { 2u, 2u, 1u } } ) );
    REQUIRE_FALSE( ImageProcess_IsRegionValid(
        desc,
        { { CY_U32_MAX, 0u, 0u }, { 2u, 1u, 1u } } ) );
    REQUIRE_FALSE( ImageProcess_IsRegionValid(
        desc,
        { {}, { 0u, 1u, 1u } } ) );

    image_desc_t invalid = desc;
    invalid.extent.nWidth = 0u;
    REQUIRE( ImageProcess_FullRegion( invalid ).extent.nDepth == 0u );
}

TEST_CASE( "Image process copy preserves destination padding and depth layout",
           "[CypherCommon][Image][Process][Copy]" )
{
    const image_desc_t desc = R8Desc( 3u, 2u, 2u );
    std::vector<byte> sourcePixels( 24u, kPaddingByte );
    std::vector<byte> destinationPixels( 28u, kPaddingByte );
    const image_view_t source = WritableView(
        sourcePixels,
        desc,
        5u,
        12u );
    const image_view_t destination = WritableView(
        destinationPixels,
        desc,
        6u,
        14u );
    SetR8Pixels( source, { 1u, 2u, 3u, 4u, 5u, 6u,
                           7u, 8u, 9u, 10u, 11u, 12u } );

    const std::vector<byte> sourceBefore = sourcePixels;
    REQUIRE( ImageProcess_Copy(
                 destination,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( destination ),
        { 1u, 2u, 3u, 4u, 5u, 6u,
          7u, 8u, 9u, 10u, 11u, 12u } );
    RequireRowPadding( destination, kPaddingByte );
    REQUIRE( sourcePixels == sourceBefore );

    REQUIRE( ImageProcess_Copy(
                 source,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    REQUIRE( sourcePixels == sourceBefore );
}

TEST_CASE( "Image process copy reports view and metadata mismatches",
           "[CypherCommon][Image][Process][Copy][Failure]" )
{
    std::vector<byte> destinationPixels( 32u, 0u );
    std::vector<byte> sourcePixels( 32u, 0u );
    const image_desc_t rgbaDesc{
        { 2u, 2u, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
    const image_view_t destination = WritableView(
        destinationPixels,
        rgbaDesc,
        8u,
        16u );
    image_view_t source = WritableView(
        sourcePixels,
        rgbaDesc,
        8u,
        16u );

    image_view_t invalidDestination = destination;
    invalidDestination.pixels.pData = nullptr;
    REQUIRE( ImageProcess_Copy(
                 invalidDestination,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::INVALID_DESTINATION_VIEW );

    const_image_view_t changed = ImageView_AsConst( source );
    changed.pixels.pData = nullptr;
    REQUIRE( ImageProcess_Copy( destination, changed ) ==
             image_process_status_t::INVALID_SOURCE_VIEW );

    changed = ImageView_AsConst( source );
    changed.desc.pixelFormat = image_pixel_format_t::RG8_UNORM;
    changed.desc.alphaMode = image_alpha_mode_t::NONE;
    REQUIRE( ImageProcess_Copy( destination, changed ) ==
             image_process_status_t::PIXEL_FORMAT_MISMATCH );

    changed = ImageView_AsConst( source );
    changed.desc.colorSpace = image_color_space_t::LINEAR;
    REQUIRE( ImageProcess_Copy( destination, changed ) ==
             image_process_status_t::COLOR_SPACE_MISMATCH );

    changed = ImageView_AsConst( source );
    changed.desc.alphaMode = image_alpha_mode_t::PREMULTIPLIED;
    REQUIRE( ImageProcess_Copy( destination, changed ) ==
             image_process_status_t::ALPHA_MODE_MISMATCH );

    changed = ImageView_AsConst( source );
    changed.desc.extent.nWidth = 1u;
    REQUIRE( ImageProcess_Copy( destination, changed ) ==
             image_process_status_t::EXTENT_MISMATCH );
}

TEST_CASE( "Image process region copy changes only the selected pixels",
           "[CypherCommon][Image][Process][CopyRegion]" )
{
    std::vector<byte> sourcePixels( 12u, 0u );
    std::vector<byte> destinationPixels( 32u, kPaddingByte );
    const image_view_t source = WritableView(
        sourcePixels,
        R8Desc( 4u, 3u ),
        4u,
        12u );
    const image_view_t destination = WritableView(
        destinationPixels,
        R8Desc( 5u, 4u ),
        8u,
        32u );
    SetR8Pixels(
        source,
        { 1u, 2u, 3u, 4u,
          5u, 6u, 7u, 8u,
          9u, 10u, 11u, 12u } );

    REQUIRE( ImageProcess_CopyRegion(
                 destination,
                 { 2u, 0u, 0u },
                 ImageView_AsConst( source ),
                 { { 1u, 1u, 0u }, { 2u, 2u, 1u } } ) ==
             image_process_status_t::OK );

    REQUIRE( PixelValue( ImageView_AsConst( destination ), 2u, 0u ) == 6u );
    REQUIRE( PixelValue( ImageView_AsConst( destination ), 3u, 0u ) == 7u );
    REQUIRE( PixelValue( ImageView_AsConst( destination ), 2u, 1u ) == 10u );
    REQUIRE( PixelValue( ImageView_AsConst( destination ), 3u, 1u ) == 11u );
    REQUIRE( PixelValue( ImageView_AsConst( destination ), 1u, 0u ) ==
             kPaddingByte );
    REQUIRE( PixelValue( ImageView_AsConst( destination ), 4u, 1u ) ==
             kPaddingByte );
    RequireRowPadding( destination, kPaddingByte );

    REQUIRE( ImageProcess_CopyRegion(
                 destination,
                 {},
                 ImageView_AsConst( source ),
                 { {}, { 0u, 1u, 1u } } ) ==
             image_process_status_t::INVALID_REGION );
    REQUIRE( ImageProcess_CopyRegion(
                 destination,
                 {},
                 ImageView_AsConst( source ),
                 { { 3u, 0u, 0u }, { 2u, 1u, 1u } } ) ==
             image_process_status_t::SOURCE_REGION_OUT_OF_BOUNDS );
    REQUIRE( ImageProcess_CopyRegion(
                 destination,
                 { 4u, 0u, 0u },
                 ImageView_AsConst( source ),
                 { {}, { 2u, 1u, 1u } } ) ==
             image_process_status_t::DESTINATION_REGION_OUT_OF_BOUNDS );
}

TEST_CASE( "Image process rejects ambiguous overlapping copies",
           "[CypherCommon][Image][Process][Aliasing]" )
{
    std::array<byte, 16u> pixels{};
    const image_desc_t desc = R8Desc( 2u, 2u );
    const const_image_view_t source{
        desc,
        { pixels.data(), 4u },
        2u,
        4u
    };
    const image_view_t shiftedDestination{
        desc,
        { pixels.data() + 1u, 4u },
        2u,
        4u
    };
    REQUIRE( ImageProcess_Copy( shiftedDestination, source ) ==
             image_process_status_t::OVERLAPPING_MEMORY );

    const image_view_t exactDestination{
        desc,
        { pixels.data(), 4u },
        2u,
        4u
    };
    REQUIRE( ImageProcess_CopyRegion(
                 exactDestination,
                 { 1u, 0u, 0u },
                 source,
                 { {}, { 1u, 1u, 1u } } ) ==
             image_process_status_t::OVERLAPPING_MEMORY );
}

TEST_CASE( "Image process fill supports every current pixel size",
           "[CypherCommon][Image][Process][Fill]" )
{
    for ( usize iFormat = 1u;
          iFormat < static_cast<usize>( image_pixel_format_t::COUNT );
          ++iFormat ) {
        const image_pixel_format_t format =
            static_cast<image_pixel_format_t>( iFormat );
        const image_format_info_t *pInfo = ImageFormat_GetInfo( format );
        INFO( "Format: " << ImageFormat_Name( format ) );
        REQUIRE( pInfo != nullptr );

        image_desc_t desc{
            { 3u, 2u, 1u },
            format,
            image_color_space_t::LINEAR,
            pInfo->bHasAlpha
                ? image_alpha_mode_t::STRAIGHT
                : image_alpha_mode_t::NONE
        };
        const usize cbLogicalRow = 3u * pInfo->cbPixel;
        const usize cbRowPitch = cbLogicalRow + 5u;
        const usize cbSlicePitch = cbRowPitch * 2u;
        std::vector<byte> pixels( cbSlicePitch, kPaddingByte );
        std::vector<byte> pattern( pInfo->cbPixel );
        for ( usize iByte = 0u; iByte < pattern.size(); ++iByte ) {
            pattern[iByte] = static_cast<byte>( 0x20u + iByte );
        }
        const image_view_t view = WritableView(
            pixels,
            desc,
            cbRowPitch,
            cbSlicePitch );

        REQUIRE( ImageProcess_Fill(
                     view,
                     { pattern.data(), pattern.size() } ) ==
                 image_process_status_t::OK );
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            for ( u32 iColumn = 0u; iColumn < 3u; ++iColumn ) {
                const byte_span_t pixel = ImageView_GetPixel(
                    view,
                    iColumn,
                    iRow,
                    0u );
                REQUIRE( std::equal(
                    pattern.begin(),
                    pattern.end(),
                    pixel.pData ) );
            }
        }
        RequireRowPadding( view, kPaddingByte );
    }
}

TEST_CASE( "Image process region fill preserves outside pixels",
           "[CypherCommon][Image][Process][FillRegion]" )
{
    std::vector<byte> pixels( 32u, kPaddingByte );
    const image_view_t view = WritableView(
        pixels,
        R8Desc( 5u, 4u ),
        8u,
        32u );
    const byte value = 0x5Au;

    REQUIRE( ImageProcess_FillRegion(
                 view,
                 { { 1u, 1u, 0u }, { 3u, 2u, 1u } },
                 { &value, 1u } ) == image_process_status_t::OK );
    for ( u32 iRow = 0u; iRow < 4u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 5u; ++iColumn ) {
            const bool bInside = iRow >= 1u && iRow < 3u &&
                                 iColumn >= 1u && iColumn < 4u;
            REQUIRE( PixelValue(
                         ImageView_AsConst( view ),
                         iColumn,
                         iRow ) ==
                     ( bInside ? value : kPaddingByte ) );
        }
    }
    RequireRowPadding( view, kPaddingByte );

    REQUIRE( ImageProcess_Fill( view, {} ) ==
             image_process_status_t::INVALID_FILL_PIXEL );
    REQUIRE( ImageProcess_Fill( view, { pixels.data(), 1u } ) ==
             image_process_status_t::OVERLAPPING_MEMORY );
}

TEST_CASE( "Image process horizontal flip works out of place and in place",
           "[CypherCommon][Image][Process][FlipHorizontal]" )
{
    std::vector<byte> sourcePixels( 8u, kPaddingByte );
    std::vector<byte> destinationPixels( 8u, kPaddingByte );
    const image_view_t source = WritableView(
        sourcePixels,
        R8Desc( 3u, 2u ),
        4u,
        8u );
    const image_view_t destination = WritableView(
        destinationPixels,
        R8Desc( 3u, 2u ),
        4u,
        8u );
    SetR8Pixels( source, { 1u, 2u, 3u, 4u, 5u, 6u } );

    REQUIRE( ImageProcess_FlipHorizontal(
                 destination,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( destination ),
        { 3u, 2u, 1u, 6u, 5u, 4u } );
    RequireRowPadding( destination, kPaddingByte );

    REQUIRE( ImageProcess_FlipHorizontal(
                 source,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( source ),
        { 3u, 2u, 1u, 6u, 5u, 4u } );
    RequireRowPadding( source, kPaddingByte );
}

TEST_CASE( "Image process vertical flip works out of place and in place",
           "[CypherCommon][Image][Process][FlipVertical]" )
{
    std::vector<byte> sourcePixels( 12u, kPaddingByte );
    std::vector<byte> destinationPixels( 12u, kPaddingByte );
    const image_view_t source = WritableView(
        sourcePixels,
        R8Desc( 3u, 3u ),
        4u,
        12u );
    const image_view_t destination = WritableView(
        destinationPixels,
        R8Desc( 3u, 3u ),
        4u,
        12u );
    SetR8Pixels(
        source,
        { 1u, 2u, 3u,
          4u, 5u, 6u,
          7u, 8u, 9u } );

    REQUIRE( ImageProcess_FlipVertical(
                 destination,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( destination ),
        { 7u, 8u, 9u,
          4u, 5u, 6u,
          1u, 2u, 3u } );

    REQUIRE( ImageProcess_FlipVertical(
                 source,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( source ),
        { 7u, 8u, 9u,
          4u, 5u, 6u,
          1u, 2u, 3u } );
    RequireRowPadding( source, kPaddingByte );
}

TEST_CASE( "Image process rotate 180 handles odd dimensions in place",
           "[CypherCommon][Image][Process][Rotate180]" )
{
    std::vector<byte> pixels( 12u, kPaddingByte );
    const image_view_t view = WritableView(
        pixels,
        R8Desc( 3u, 3u ),
        4u,
        12u );
    SetR8Pixels(
        view,
        { 1u, 2u, 3u,
          4u, 5u, 6u,
          7u, 8u, 9u } );

    REQUIRE( ImageProcess_Rotate180(
                 view,
                 ImageView_AsConst( view ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( view ),
        { 9u, 8u, 7u,
          6u, 5u, 4u,
          3u, 2u, 1u } );
    RequireRowPadding( view, kPaddingByte );
}

TEST_CASE( "Image process rotates rectangular images by 90 degrees",
           "[CypherCommon][Image][Process][Rotate90]" )
{
    std::vector<byte> sourcePixels( 8u, kPaddingByte );
    std::vector<byte> clockwisePixels( 12u, kPaddingByte );
    std::vector<byte> counterClockwisePixels( 12u, kPaddingByte );
    const image_view_t source = WritableView(
        sourcePixels,
        R8Desc( 3u, 2u ),
        4u,
        8u );
    const image_view_t clockwise = WritableView(
        clockwisePixels,
        R8Desc( 2u, 3u ),
        4u,
        12u );
    const image_view_t counterClockwise = WritableView(
        counterClockwisePixels,
        R8Desc( 2u, 3u ),
        4u,
        12u );
    SetR8Pixels( source, { 1u, 2u, 3u, 4u, 5u, 6u } );

    REQUIRE( ImageProcess_Rotate90Clockwise(
                 clockwise,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( clockwise ),
        { 4u, 1u,
          5u, 2u,
          6u, 3u } );

    REQUIRE( ImageProcess_Rotate90CounterClockwise(
                 counterClockwise,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    RequireR8Pixels(
        ImageView_AsConst( counterClockwise ),
        { 3u, 6u,
          2u, 5u,
          1u, 4u } );
    RequireRowPadding( clockwise, kPaddingByte );
    RequireRowPadding( counterClockwise, kPaddingByte );
}

TEST_CASE( "Image process 90 degree rotations reject in-place storage",
           "[CypherCommon][Image][Process][Rotate90][Aliasing]" )
{
    std::array<byte, 4u> pixels{ 1u, 2u, 3u, 4u };
    const image_desc_t desc = R8Desc( 2u, 2u );
    const image_view_t view{
        desc,
        { pixels.data(), pixels.size() },
        2u,
        4u
    };
    REQUIRE( ImageProcess_Rotate90Clockwise(
                 view,
                 ImageView_AsConst( view ) ) ==
             image_process_status_t::IN_PLACE_NOT_SUPPORTED );

    std::array<byte, 4u> otherPixels{};
    image_view_t wrongExtent{
        R8Desc( 1u, 4u ),
        { otherPixels.data(), otherPixels.size() },
        1u,
        4u
    };
    REQUIRE( ImageProcess_Rotate90Clockwise(
                 wrongExtent,
                 ImageView_AsConst( view ) ) ==
             image_process_status_t::EXTENT_MISMATCH );
}

TEST_CASE( "Image process tiled rotations preserve depth slices",
           "[CypherCommon][Image][Process][Rotate90][Tiles]" )
{
    const image_desc_t sourceDesc = R8Desc( 35u, 33u, 2u );
    const image_desc_t rotatedDesc = R8Desc( 33u, 35u, 2u );
    constexpr usize cbSourceRowPitch = 40u;
    constexpr usize cbSourceSlicePitch = cbSourceRowPitch * 33u;
    constexpr usize cbRotatedRowPitch = 40u;
    constexpr usize cbRotatedSlicePitch = cbRotatedRowPitch * 35u;
    std::vector<byte> sourcePixels(
        cbSourceSlicePitch * 2u,
        kPaddingByte );
    std::vector<byte> rotatedPixels(
        cbRotatedSlicePitch * 2u,
        kPaddingByte );
    std::vector<byte> restoredPixels(
        cbSourceSlicePitch * 2u,
        kPaddingByte );
    const image_view_t source = WritableView(
        sourcePixels,
        sourceDesc,
        cbSourceRowPitch,
        cbSourceSlicePitch );
    const image_view_t rotated = WritableView(
        rotatedPixels,
        rotatedDesc,
        cbRotatedRowPitch,
        cbRotatedSlicePitch );
    const image_view_t restored = WritableView(
        restoredPixels,
        sourceDesc,
        cbSourceRowPitch,
        cbSourceSlicePitch );

    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 33u; ++iRow ) {
            for ( u32 iColumn = 0u; iColumn < 35u; ++iColumn ) {
                const byte_span_t pixel = ImageView_GetPixel(
                    source,
                    iColumn,
                    iRow,
                    iSlice );
                REQUIRE( pixel.pData != nullptr );
                pixel.pData[0] = static_cast<byte>(
                    ( iColumn * 3u + iRow * 5u + iSlice * 71u ) & 0xFFu );
            }
        }
    }

    REQUIRE( ImageProcess_Rotate90Clockwise(
                 rotated,
                 ImageView_AsConst( source ) ) ==
             image_process_status_t::OK );
    REQUIRE( ImageProcess_Rotate90CounterClockwise(
                 restored,
                 ImageView_AsConst( rotated ) ) ==
             image_process_status_t::OK );

    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 33u; ++iRow ) {
            for ( u32 iColumn = 0u; iColumn < 35u; ++iColumn ) {
                REQUIRE( PixelValue(
                             ImageView_AsConst( restored ),
                             iColumn,
                             iRow,
                             iSlice ) ==
                         PixelValue(
                             ImageView_AsConst( source ),
                             iColumn,
                             iRow,
                             iSlice ) );
            }
        }
    }
    RequireRowPadding( rotated, kPaddingByte );
    RequireRowPadding( restored, kPaddingByte );
}

TEST_CASE( "Image process status names remain stable",
           "[CypherCommon][Image][Process][Diagnostics]" )
{
    REQUIRE( std::string_view( ImageProcess_StatusName(
                 image_process_status_t::OVERLAPPING_MEMORY ) ) ==
             "OVERLAPPING_MEMORY" );
    REQUIRE( std::string_view( ImageProcess_StatusName(
                 static_cast<image_process_status_t>( 0xFFu ) ) ) ==
             "UNKNOWN_IMAGE_PROCESS_STATUS" );
}
