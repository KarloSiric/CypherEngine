//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageFormat_Tests.cpp
//  Purpose: Tests image-format metadata, validation, and allocation layouts.
//  Details: Coverage protects enum-table synchronization, descriptor policy,
//           row alignment, 3D slice sizing, and arithmetic-overflow behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageFormat.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>

using namespace cypher::common;

namespace
{

image_desc_t TestImageDesc() noexcept
{
    return {
        { 3u, 2u, 1u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
}

} // namespace

TEST_CASE( "Image format metadata covers every declared format",
           "[CypherCommon][Image][Format]" )
{
    REQUIRE_FALSE( ImageFormat_IsKnown( image_pixel_format_t::UNKNOWN ) );
    REQUIRE( ImageFormat_GetInfo( image_pixel_format_t::UNKNOWN ) == nullptr );

    for ( usize iFormat = 1u;
          iFormat < static_cast<usize>( image_pixel_format_t::COUNT );
          ++iFormat ) {
        const auto format = static_cast<image_pixel_format_t>( iFormat );
        const image_format_info_t *pInfo = ImageFormat_GetInfo( format );
        REQUIRE( pInfo != nullptr );
        REQUIRE( pInfo->pixelFormat == format );
        REQUIRE( pInfo->cChannels > 0u );
        REQUIRE( pInfo->cbComponent > 0u );
        REQUIRE( pInfo->cbPixel == pInfo->cChannels * pInfo->cbComponent );
        REQUIRE( std::string_view( pInfo->pszName ) != "UNKNOWN" );
    }

    const auto invalid = static_cast<image_pixel_format_t>( 0xFFFFu );
    REQUIRE_FALSE( ImageFormat_IsKnown( invalid ) );
    REQUIRE( ImageFormat_GetInfo( invalid ) == nullptr );
    REQUIRE( std::string_view( ImageFormat_Name( invalid ) ) == "UNKNOWN" );
}

TEST_CASE( "Image descriptor validation rejects malformed metadata",
           "[CypherCommon][Image][Format][Validation]" )
{
    image_desc_t desc = TestImageDesc();
    REQUIRE( ImageFormat_ValidateDesc( desc ) == image_format_status_t::OK );

    desc.pixelFormat = image_pixel_format_t::UNKNOWN;
    REQUIRE( ImageFormat_ValidateDesc( desc ) ==
             image_format_status_t::INVALID_PIXEL_FORMAT );

    desc = TestImageDesc();
    desc.extent.nWidth = 0u;
    REQUIRE( ImageFormat_ValidateDesc( desc ) ==
             image_format_status_t::INVALID_EXTENT );

    desc = TestImageDesc();
    desc.colorSpace = image_color_space_t::UNKNOWN;
    REQUIRE( ImageFormat_ValidateDesc( desc ) ==
             image_format_status_t::INVALID_COLOR_SPACE );

    desc = TestImageDesc();
    desc.alphaMode = static_cast<image_alpha_mode_t>( 0xFFu );
    REQUIRE( ImageFormat_ValidateDesc( desc ) ==
             image_format_status_t::INVALID_ALPHA_MODE );

    desc = TestImageDesc();
    desc.pixelFormat = image_pixel_format_t::R8_UNORM;
    REQUIRE( ImageFormat_ValidateDesc( desc ) ==
             image_format_status_t::INVALID_ALPHA_MODE );

    desc = TestImageDesc();
    desc.alphaMode = image_alpha_mode_t::NONE;
    REQUIRE( ImageFormat_ValidateDesc( desc ) == image_format_status_t::OK );
}

TEST_CASE( "Image layout calculation supports padding and depth slices",
           "[CypherCommon][Image][Format][Layout]" )
{
    image_desc_t desc = TestImageDesc();

    image_layout_result_t result = ImageFormat_CalculateLayout( desc, 1u );
    REQUIRE( result.status == image_format_status_t::OK );
    REQUIRE( result.layout.cbRowPitch == 12u );
    REQUIRE( result.layout.cbSlicePitch == 24u );
    REQUIRE( result.layout.cbTotalSize == 24u );

    desc.extent.nDepth = 3u;
    result = ImageFormat_CalculateLayout( desc, 16u );
    REQUIRE( result.status == image_format_status_t::OK );
    REQUIRE( result.layout.cbRowPitch == 16u );
    REQUIRE( result.layout.cbSlicePitch == 32u );
    REQUIRE( result.layout.cbTotalSize == 96u );
}

TEST_CASE( "Image layouts scale through 4096-pixel texture dimensions",
           "[CypherCommon][Image][Format][Scale]" )
{
    constexpr u32 dimensions[] = { 256u, 1024u, 2048u, 4096u };

    for ( const u32 nDimension : dimensions ) {
        CAPTURE( nDimension );
        const image_desc_t desc{
            { nDimension, nDimension, 1u },
            image_pixel_format_t::RGBA8_UNORM,
            image_color_space_t::SRGB,
            image_alpha_mode_t::STRAIGHT
        };
        const image_layout_result_t result =
            ImageFormat_CalculateLayout( desc, 256u );
        const usize cbExpectedRow =
            static_cast<usize>( nDimension ) * 4u;
        const usize cbExpectedTotal =
            cbExpectedRow * static_cast<usize>( nDimension );

        REQUIRE( result.status == image_format_status_t::OK );
        REQUIRE( result.layout.cbRowPitch == cbExpectedRow );
        REQUIRE( result.layout.cbSlicePitch == cbExpectedTotal );
        REQUIRE( result.layout.cbTotalSize == cbExpectedTotal );
    }
}

TEST_CASE( "Image layout calculation rejects alignment and size overflow",
           "[CypherCommon][Image][Format][Overflow]" )
{
    image_desc_t desc = TestImageDesc();
    image_layout_result_t result = ImageFormat_CalculateLayout( desc, 3u );
    REQUIRE( result.status == image_format_status_t::INVALID_ALIGNMENT );
    REQUIRE( result.layout.cbTotalSize == 0u );

    desc.extent = {
        std::numeric_limits<u32>::max(),
        std::numeric_limits<u32>::max(),
        std::numeric_limits<u32>::max()
    };
    desc.pixelFormat = image_pixel_format_t::RGBA32_FLOAT;
    result = ImageFormat_CalculateLayout( desc, 1u );
    REQUIRE( result.status == image_format_status_t::ARITHMETIC_OVERFLOW );
    REQUIRE( result.layout.cbRowPitch == 0u );
    REQUIRE( result.layout.cbSlicePitch == 0u );
    REQUIRE( result.layout.cbTotalSize == 0u );
}

TEST_CASE( "Image format status names remain stable",
           "[CypherCommon][Image][Format][Diagnostics]" )
{
    REQUIRE( std::string_view( ImageFormat_StatusName(
                 image_format_status_t::ARITHMETIC_OVERFLOW ) ) ==
             "ARITHMETIC_OVERFLOW" );
    REQUIRE( std::string_view( ImageFormat_StatusName(
                 static_cast<image_format_status_t>( 0xFFu ) ) ) ==
             "UNKNOWN_IMAGE_FORMAT_STATUS" );
}
