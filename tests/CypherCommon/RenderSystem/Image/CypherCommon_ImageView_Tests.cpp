//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageView_Tests.cpp
//  Purpose: Tests validation and bounded access for borrowed image pixels.
//  Details: Coverage protects padded row and slice addressing, immutable view
//           conversion, writable access, malformed storage, and range failures.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageView.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace cypher::common;

namespace
{

image_desc_t TestImageDesc() noexcept
{
    return {
        { 3u, 2u, 2u },
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT
    };
}

} // namespace

TEST_CASE( "Image views validate complete padded storage",
           "[CypherCommon][Image][View][Validation]" )
{
    std::array<byte, 64u> pixels{};
    const image_view_t view{
        TestImageDesc(),
        { pixels.data(), pixels.size() },
        16u,
        32u
    };

    REQUIRE( ImageView_Validate( view ) == image_view_status_t::OK );
    REQUIRE( ImageView_IsValid( view ) );
    REQUIRE( ImageView_IsValid( ImageView_AsConst( view ) ) );
}

TEST_CASE( "Image views report malformed descriptors and storage",
           "[CypherCommon][Image][View][Failure]" )
{
    std::array<byte, 64u> pixels{};
    image_view_t view{
        TestImageDesc(),
        { pixels.data(), pixels.size() },
        16u,
        32u
    };

    view.desc.extent.nWidth = 0u;
    REQUIRE( ImageView_Validate( view ) ==
             image_view_status_t::INVALID_DESCRIPTOR );

    view.desc = TestImageDesc();
    view.pixels.pData = nullptr;
    REQUIRE( ImageView_Validate( view ) ==
             image_view_status_t::NULL_PIXEL_DATA );

    view.pixels = { pixels.data(), pixels.size() };
    view.cbRowPitch = 11u;
    REQUIRE( ImageView_Validate( view ) ==
             image_view_status_t::ROW_PITCH_TOO_SMALL );

    view.cbRowPitch = 16u;
    view.cbSlicePitch = 31u;
    REQUIRE( ImageView_Validate( view ) ==
             image_view_status_t::SLICE_PITCH_TOO_SMALL );

    view.cbSlicePitch = 32u;
    view.pixels.nCount = 63u;
    REQUIRE( ImageView_Validate( view ) ==
             image_view_status_t::BUFFER_TOO_SMALL );

    view.desc = {
        { 1u, 2u, 1u },
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE
    };
    view.pixels = { pixels.data(), pixels.size() };
    view.cbRowPitch = CY_USIZE_MAX;
    view.cbSlicePitch = CY_USIZE_MAX;
    REQUIRE( ImageView_Validate( view ) ==
             image_view_status_t::ARITHMETIC_OVERFLOW );
}

TEST_CASE( "Image row access respects row and slice padding",
           "[CypherCommon][Image][View][Rows]" )
{
    std::array<byte, 64u> pixels{};
    const image_view_t view{
        TestImageDesc(),
        { pixels.data(), pixels.size() },
        16u,
        32u
    };

    const byte_span_t first = ImageView_GetRow( view, 0u, 0u );
    const byte_span_t second = ImageView_GetRow( view, 1u, 0u );
    const byte_span_t nextSlice = ImageView_GetRow( view, 0u, 1u );
    REQUIRE( first.pData == pixels.data() );
    REQUIRE( first.nCount == 12u );
    REQUIRE( second.pData == pixels.data() + 16u );
    REQUIRE( second.nCount == 12u );
    REQUIRE( nextSlice.pData == pixels.data() + 32u );

    REQUIRE( ImageView_GetRow( view, 2u, 0u ).pData == nullptr );
    REQUIRE( ImageView_GetRow( view, 0u, 2u ).pData == nullptr );
}

TEST_CASE( "Image pixel access returns one writable pixel",
           "[CypherCommon][Image][View][Pixels]" )
{
    std::array<byte, 64u> pixels{};
    const image_view_t view{
        TestImageDesc(),
        { pixels.data(), pixels.size() },
        16u,
        32u
    };

    byte_span_t pixel = ImageView_GetPixel( view, 2u, 1u, 1u );
    REQUIRE( pixel.pData == pixels.data() + 56u );
    REQUIRE( pixel.nCount == 4u );
    pixel.pData[0] = 0xA5u;

    const const_image_view_t immutable = ImageView_AsConst( view );
    const binary_block_t observed =
        ImageView_GetPixel( immutable, 2u, 1u, 1u );
    REQUIRE( observed.pData == pixel.pData );
    REQUIRE( observed.cbSize == 4u );
    REQUIRE( observed.pData[0] == 0xA5u );

    REQUIRE( ImageView_GetPixel( view, 3u, 0u, 0u ).pData == nullptr );
}

TEST_CASE( "Image view status names remain stable",
           "[CypherCommon][Image][View][Diagnostics]" )
{
    REQUIRE( std::string_view( ImageView_StatusName(
                 image_view_status_t::BUFFER_TOO_SMALL ) ) ==
             "BUFFER_TOO_SMALL" );
    REQUIRE( std::string_view( ImageView_StatusName(
                 static_cast<image_view_status_t>( 0xFFu ) ) ) ==
             "UNKNOWN_IMAGE_VIEW_STATUS" );
}
