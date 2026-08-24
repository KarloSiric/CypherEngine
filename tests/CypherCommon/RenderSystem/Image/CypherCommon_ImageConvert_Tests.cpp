//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/Image/CypherCommon_ImageConvert_Tests.cpp
//  Purpose: Tests image numeric, color-space, alpha, and swizzle conversion.
//  Details: Coverage includes every declared format, binary16 boundaries, pitched
//           regions, depth slices, in-place policy, and failure atomicity.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageConvert.h"
#include "CypherCommon_Color.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

using namespace cypher::common;

namespace
{

constexpr byte kImageConvertPadding = 0xCDu;

struct test_image_t {
    image_desc_t desc{};
    usize cbRowPitch{ 0u };
    usize cbSlicePitch{ 0u };
    std::vector<byte> pixels{};
};

image_desc_t MakeDesc(
    image_pixel_format_t pixelFormat,
    image_color_space_t colorSpace,
    image_alpha_mode_t alphaMode,
    u32 nWidth = 1u,
    u32 nHeight = 1u,
    u32 nDepth = 1u ) noexcept
{
    return {
        { nWidth, nHeight, nDepth },
        pixelFormat,
        colorSpace,
        alphaMode
    };
}

test_image_t MakeImage(
    const image_desc_t &desc,
    usize cbRowPadding = 0u,
    usize cbSlicePadding = 0u,
    byte initialValue = kImageConvertPadding )
{
    const image_format_info_t *pInfo =
        ImageFormat_GetInfo( desc.pixelFormat );
    REQUIRE( pInfo != nullptr );

    test_image_t image{};
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

image_view_t WritableView( test_image_t &image ) noexcept
{
    return {
        image.desc,
        { image.pixels.data(), image.pixels.size() },
        image.cbRowPitch,
        image.cbSlicePitch
    };
}

const_image_view_t ReadOnlyView( const test_image_t &image ) noexcept
{
    return {
        image.desc,
        { image.pixels.data(), image.pixels.size() },
        image.cbRowPitch,
        image.cbSlicePitch
    };
}

byte *PixelAddress(
    test_image_t &image,
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

const byte *PixelAddress(
    const test_image_t &image,
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

void WriteRgba32(
    test_image_t &image,
    u32 iColumn,
    const std::array<f32, 4u> &value ) noexcept
{
    std::memcpy( PixelAddress( image, iColumn ), value.data(), sizeof( value ) );
}

std::array<f32, 4u> ReadRgba32(
    const test_image_t &image,
    u32 iColumn = 0u ) noexcept
{
    std::array<f32, 4u> value{};
    std::memcpy( value.data(), PixelAddress( image, iColumn ), sizeof( value ) );
    return value;
}

f32 FormatTolerance( image_pixel_format_t format ) noexcept
{
    switch ( format ) {
        case image_pixel_format_t::R8_UNORM:
        case image_pixel_format_t::RG8_UNORM:
        case image_pixel_format_t::RGBA8_UNORM:
            return 1.0f / 255.0f + 0.00001f;
        case image_pixel_format_t::R16_UNORM:
        case image_pixel_format_t::RG16_UNORM:
        case image_pixel_format_t::RGBA16_UNORM:
            return 1.0f / 65535.0f + 0.000001f;
        case image_pixel_format_t::R16_FLOAT:
        case image_pixel_format_t::RG16_FLOAT:
        case image_pixel_format_t::RGBA16_FLOAT:
            return 0.001f;
        default:
            return 0.000001f;
    }
}

u8 FormatChannelCount( image_pixel_format_t format ) noexcept
{
    const image_format_info_t *pInfo = ImageFormat_GetInfo( format );
    return pInfo != nullptr ? pInfo->cChannels : 0u;
}

image_alpha_mode_t FormatAlphaMode(
    image_pixel_format_t format ) noexcept
{
    const image_format_info_t *pInfo = ImageFormat_GetInfo( format );
    return pInfo != nullptr && pInfo->bHasAlpha
        ? image_alpha_mode_t::STRAIGHT
        : image_alpha_mode_t::NONE;
}

} // namespace

TEST_CASE( "Image conversion validates identity and custom swizzles",
           "[CypherCommon][Image][Convert][Swizzle]" )
{
    const image_swizzle_t identity{};
    REQUIRE( ImageConvert_IsSwizzleValid( identity ) );
    REQUIRE( ImageConvert_IsIdentitySwizzle( identity ) );

    const image_swizzle_t bgra{
        image_channel_t::BLUE,
        image_channel_t::GREEN,
        image_channel_t::RED,
        image_channel_t::ONE
    };
    REQUIRE( ImageConvert_IsSwizzleValid( bgra ) );
    REQUIRE_FALSE( ImageConvert_IsIdentitySwizzle( bgra ) );

    image_swizzle_t invalid{};
    invalid.alpha = static_cast<image_channel_t>( 255u );
    REQUIRE_FALSE( ImageConvert_IsSwizzleValid( invalid ) );
}

TEST_CASE( "Image conversion covers every declared numeric pixel format",
           "[CypherCommon][Image][Convert][Formats]" )
{
    constexpr image_pixel_format_t formats[] = {
        image_pixel_format_t::R8_UNORM,
        image_pixel_format_t::RG8_UNORM,
        image_pixel_format_t::RGBA8_UNORM,
        image_pixel_format_t::R16_UNORM,
        image_pixel_format_t::RG16_UNORM,
        image_pixel_format_t::RGBA16_UNORM,
        image_pixel_format_t::R16_FLOAT,
        image_pixel_format_t::RG16_FLOAT,
        image_pixel_format_t::RGBA16_FLOAT,
        image_pixel_format_t::R32_FLOAT,
        image_pixel_format_t::RG32_FLOAT,
        image_pixel_format_t::RGBA32_FLOAT
    };
    constexpr std::array<f32, 4u> sourceValue{
        0.125f, 0.375f, 0.625f, 0.875f
    };

    test_image_t source = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT ) );
    WriteRgba32( source, 0u, sourceValue );

    for ( image_pixel_format_t format : formats ) {
        CAPTURE( ImageFormat_Name( format ) );
        test_image_t encoded = MakeImage( MakeDesc(
            format,
            image_color_space_t::LINEAR,
            FormatAlphaMode( format ) ) );
        REQUIRE( ImageConvert(
                     WritableView( encoded ),
                     ReadOnlyView( source ) ) ==
                 image_convert_status_t::OK );

        test_image_t decoded = MakeImage( MakeDesc(
            image_pixel_format_t::RGBA32_FLOAT,
            image_color_space_t::LINEAR,
            image_alpha_mode_t::STRAIGHT ) );
        REQUIRE( ImageConvert(
                     WritableView( decoded ),
                     ReadOnlyView( encoded ) ) ==
                 image_convert_status_t::OK );

        const std::array<f32, 4u> actual = ReadRgba32( decoded );
        const u8 cChannels = FormatChannelCount( format );
        const f32 tolerance = FormatTolerance( format );
        REQUIRE( actual[0] == Catch::Approx( sourceValue[0] ).margin( tolerance ) );
        REQUIRE( actual[1] == Catch::Approx(
                     cChannels >= 2u ? sourceValue[1] : 0.0f ).margin( tolerance ) );
        REQUIRE( actual[2] == Catch::Approx(
                     cChannels >= 4u ? sourceValue[2] : 0.0f ).margin( tolerance ) );
        REQUIRE( actual[3] == Catch::Approx(
                     cChannels >= 4u ? sourceValue[3] : 1.0f ).margin( tolerance ) );
    }
}

TEST_CASE( "Image half-float conversion handles finite boundaries and specials",
           "[CypherCommon][Image][Convert][Float16]" )
{
    constexpr std::array<f32, 8u> values{
        0.0f,
        -0.0f,
        1.0f,
        -2.0f,
        65504.0f,
        0x1.0p-24f,
        std::numeric_limits<f32>::infinity(),
        std::numeric_limits<f32>::quiet_NaN()
    };
    test_image_t source = MakeImage( MakeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        static_cast<u32>( values.size() ) ) );
    for ( u32 iValue = 0u; iValue < values.size(); ++iValue ) {
        std::memcpy(
            PixelAddress( source, iValue ),
            &values[iValue],
            sizeof( f32 ) );
    }

    test_image_t half = MakeImage( MakeDesc(
        image_pixel_format_t::R16_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        static_cast<u32>( values.size() ) ) );
    test_image_t decoded = MakeImage( source.desc );
    REQUIRE( ImageConvert( WritableView( half ), ReadOnlyView( source ) ) ==
             image_convert_status_t::OK );
    REQUIRE( ImageConvert( WritableView( decoded ), ReadOnlyView( half ) ) ==
             image_convert_status_t::OK );

    for ( u32 iValue = 0u; iValue < values.size(); ++iValue ) {
        f32 actual = 0.0f;
        std::memcpy( &actual, PixelAddress( decoded, iValue ), sizeof( actual ) );
        if ( std::isnan( values[iValue] ) ) {
            REQUIRE( std::isnan( actual ) );
        } else if ( std::isinf( values[iValue] ) ) {
            REQUIRE( std::isinf( actual ) );
            REQUIRE( std::signbit( actual ) == std::signbit( values[iValue] ) );
        } else {
            REQUIRE( actual == Catch::Approx( values[iValue] ).margin( 0.000001f ) );
            REQUIRE( std::signbit( actual ) == std::signbit( values[iValue] ) );
        }
    }
}

TEST_CASE( "Image half-float conversion round trips every binary16 pattern",
           "[CypherCommon][Image][Convert][Float16][Exhaustive]" )
{
    constexpr u32 cHalfPatterns = 65536u;
    test_image_t halfSource = MakeImage( MakeDesc(
        image_pixel_format_t::R16_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        cHalfPatterns ) );
    for ( u32 iPattern = 0u; iPattern < cHalfPatterns; ++iPattern ) {
        const u16 pattern = static_cast<u16>( iPattern );
        std::memcpy(
            PixelAddress( halfSource, iPattern ),
            &pattern,
            sizeof( pattern ) );
    }

    test_image_t binary32 = MakeImage( MakeDesc(
        image_pixel_format_t::R32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        cHalfPatterns ) );
    test_image_t halfResult = MakeImage( halfSource.desc );
    REQUIRE( ImageConvert(
                 WritableView( binary32 ),
                 ReadOnlyView( halfSource ) ) == image_convert_status_t::OK );
    REQUIRE( ImageConvert(
                 WritableView( halfResult ),
                 ReadOnlyView( binary32 ) ) == image_convert_status_t::OK );

    for ( u32 iPattern = 0u; iPattern < cHalfPatterns; ++iPattern ) {
        const u16 sourcePattern = static_cast<u16>( iPattern );
        u16 actualPattern = 0u;
        std::memcpy(
            &actualPattern,
            PixelAddress( halfResult, iPattern ),
            sizeof( actualPattern ) );

        // Conversion quiets signaling NaNs; all finite values and infinities are
        // otherwise required to recover their exact original binary16 bits.
        const bool_t bNaN =
            ( sourcePattern & 0x7C00u ) == 0x7C00u &&
            ( sourcePattern & 0x03FFu ) != 0u;
        const u16 expectedPattern = bNaN
            ? static_cast<u16>( sourcePattern | 0x0200u )
            : sourcePattern;
        if ( actualPattern != expectedPattern ) {
            CAPTURE( iPattern, sourcePattern, expectedPattern, actualPattern );
            FAIL( "binary16 round trip changed the encoded value" );
        }
    }
}

TEST_CASE( "Image conversion applies the IEC sRGB transfer and preserves alpha",
           "[CypherCommon][Image][Convert][ColorSpace]" )
{
    test_image_t srgb = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT ) );
    const byte encoded[4u]{ 0u, 128u, 255u, 64u };
    std::memcpy( PixelAddress( srgb, 0u ), encoded, sizeof( encoded ) );

    test_image_t linear = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT ) );
    REQUIRE( ImageConvert( WritableView( linear ), ReadOnlyView( srgb ) ) ==
             image_convert_status_t::OK );

    const std::array<f32, 4u> value = ReadRgba32( linear );
    REQUIRE( value[0] == Catch::Approx( 0.0f ) );
    REQUIRE( value[1] == Catch::Approx( 0.2158605f ).margin( 0.000001f ) );
    REQUIRE( value[2] == Catch::Approx( 1.0f ) );
    REQUIRE( value[3] == Catch::Approx( 64.0f / 255.0f ).margin( 0.000001f ) );

    test_image_t roundTrip = MakeImage( srgb.desc );
    REQUIRE( ImageConvert( WritableView( roundTrip ), ReadOnlyView( linear ) ) ==
             image_convert_status_t::OK );
    REQUIRE( std::memcmp(
                 PixelAddress( roundTrip, 0u ),
                 encoded,
                 sizeof( encoded ) ) == 0 );
}

TEST_CASE( "Image optimized sRGB paths preserve every byte code and quantizer result",
           "[CypherCommon][Image][Convert][ColorSpace][Exhaustive]" )
{
    constexpr u32 cSrgbCodes = 256u;
    test_image_t encoded = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT,
        cSrgbCodes ) );
    for ( u32 iCode = 0u; iCode < cSrgbCodes; ++iCode ) {
        byte *pPixel = PixelAddress( encoded, iCode );
        pPixel[0] = static_cast<byte>( iCode );
        pPixel[1] = static_cast<byte>( iCode );
        pPixel[2] = static_cast<byte>( iCode );
        pPixel[3] = static_cast<byte>( 255u - iCode );
    }

    test_image_t linear = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        cSrgbCodes ) );
    test_image_t roundTrip = MakeImage( encoded.desc );
    REQUIRE( ImageConvert( WritableView( linear ), ReadOnlyView( encoded ) ) ==
             image_convert_status_t::OK );
    REQUIRE( ImageConvert( WritableView( roundTrip ), ReadOnlyView( linear ) ) ==
             image_convert_status_t::OK );
    REQUIRE( roundTrip.pixels == encoded.pixels );

    constexpr u32 cLinearSamples = 4097u;
    test_image_t linearSamples = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        cLinearSamples ) );
    for ( u32 iSample = 0u; iSample < cLinearSamples; ++iSample ) {
        const f32 value =
            static_cast<f32>( iSample ) /
            static_cast<f32>( cLinearSamples - 1u );
        WriteRgba32(
            linearSamples,
            iSample,
            { value, value, value, value } );
    }

    test_image_t quantized = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT,
        cLinearSamples ) );
    REQUIRE( ImageConvert(
                 WritableView( quantized ),
                 ReadOnlyView( linearSamples ) ) ==
             image_convert_status_t::OK );
    for ( u32 iSample = 0u; iSample < cLinearSamples; ++iSample ) {
        const f32 value =
            static_cast<f32>( iSample ) /
            static_cast<f32>( cLinearSamples - 1u );
        const color32_t expected = Color_LinearToSrgb(
            { value, value, value, value } );
        const byte *pActual = PixelAddress( quantized, iSample );
        REQUIRE( pActual[0] == expected.r );
        REQUIRE( pActual[1] == expected.g );
        REQUIRE( pActual[2] == expected.b );
        REQUIRE( pActual[3] == expected.a );
    }

    // Probe both representable neighbors of every quantization transition. The
    // bucket accelerator must agree with the reference transfer at its hardest
    // cases, not only at evenly spaced values.
    constexpr u32 cBoundarySamples = 255u * 3u;
    test_image_t boundarySamples = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        cBoundarySamples ) );
    u32 iBoundarySample = 0u;
    for ( u32 iCode = 0u; iCode < 255u; ++iCode ) {
        const f32 srgbBoundary =
            ( static_cast<f32>( iCode ) + 0.5f ) / 255.0f;
        const f32 linearBoundary = srgbBoundary <= 0.04045f
            ? srgbBoundary / 12.92f
            : std::pow(
                ( srgbBoundary + 0.055f ) / 1.055f,
                2.4f );
        const f32 samples[3u]{
            std::nextafter( linearBoundary, 0.0f ),
            linearBoundary,
            std::nextafter( linearBoundary, 1.0f )
        };
        for ( f32 sample : samples ) {
            WriteRgba32(
                boundarySamples,
                iBoundarySample++,
                { sample, sample, sample, 1.0f } );
        }
    }

    test_image_t boundaryResults = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT,
        cBoundarySamples ) );
    REQUIRE( ImageConvert(
                 WritableView( boundaryResults ),
                 ReadOnlyView( boundarySamples ) ) ==
             image_convert_status_t::OK );
    for ( u32 iSample = 0u; iSample < cBoundarySamples; ++iSample ) {
        const std::array<f32, 4u> input = ReadRgba32(
            boundarySamples,
            iSample );
        const color32_t expected = Color_LinearToSrgb(
            { input[0], input[1], input[2], input[3] } );
        const byte *pActual = PixelAddress( boundaryResults, iSample );
        CAPTURE( iSample, input[0], expected.r, pActual[0] );
        REQUIRE( pActual[0] == expected.r );
        REQUIRE( pActual[1] == expected.g );
        REQUIRE( pActual[2] == expected.b );
    }
}

TEST_CASE( "Image optimized RGBA8 premultiplication uses nearest quantization",
           "[CypherCommon][Image][Convert][Alpha][RGBA8]" )
{
    constexpr std::array<byte, 5u> values{ 0u, 1u, 64u, 128u, 255u };
    constexpr u32 cPixels =
        static_cast<u32>( values.size() * values.size() );
    test_image_t straight = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        cPixels ) );
    u32 iPixel = 0u;
    for ( byte color : values ) {
        for ( byte alpha : values ) {
            byte *pPixel = PixelAddress( straight, iPixel++ );
            pPixel[0] = color;
            pPixel[1] = static_cast<byte>( 255u - color );
            pPixel[2] = 127u;
            pPixel[3] = alpha;
        }
    }

    test_image_t premultiplied = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::PREMULTIPLIED,
        cPixels ) );
    REQUIRE( ImageConvert(
                 WritableView( premultiplied ),
                 ReadOnlyView( straight ) ) ==
             image_convert_status_t::OK );

    for ( u32 iValue = 0u; iValue < cPixels; ++iValue ) {
        const byte *pSource = PixelAddress( straight, iValue );
        const byte *pActual = PixelAddress( premultiplied, iValue );
        for ( usize iColor = 0u; iColor < 3u; ++iColor ) {
            const byte expected = static_cast<byte>(
                ( static_cast<u32>( pSource[iColor] ) * pSource[3] + 127u ) /
                255u );
            REQUIRE( pActual[iColor] == expected );
        }
        REQUIRE( pActual[3] == pSource[3] );
    }
}

TEST_CASE( "Image conversion changes alpha association in linear light",
           "[CypherCommon][Image][Convert][Alpha]" )
{
    test_image_t straight = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        2u ) );
    WriteRgba32( straight, 0u, { 0.8f, 0.4f, 0.2f, 0.5f } );
    WriteRgba32( straight, 1u, { 0.7f, 0.6f, 0.5f, 0.0f } );

    test_image_t premultiplied = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::PREMULTIPLIED,
        2u ) );
    REQUIRE( ImageConvert(
                 WritableView( premultiplied ),
                 ReadOnlyView( straight ) ) ==
             image_convert_status_t::OK );

    const std::array<f32, 4u> first = ReadRgba32( premultiplied, 0u );
    REQUIRE( first[0] == Catch::Approx( 0.4f ) );
    REQUIRE( first[1] == Catch::Approx( 0.2f ) );
    REQUIRE( first[2] == Catch::Approx( 0.1f ) );
    REQUIRE( first[3] == Catch::Approx( 0.5f ) );

    test_image_t restored = MakeImage( straight.desc );
    REQUIRE( ImageConvert(
                 WritableView( restored ),
                 ReadOnlyView( premultiplied ) ) ==
             image_convert_status_t::OK );
    const std::array<f32, 4u> restoredFirst = ReadRgba32( restored, 0u );
    REQUIRE( restoredFirst[0] == Catch::Approx( 0.8f ) );
    REQUIRE( restoredFirst[1] == Catch::Approx( 0.4f ) );
    REQUIRE( restoredFirst[2] == Catch::Approx( 0.2f ) );
    REQUIRE( restoredFirst[3] == Catch::Approx( 0.5f ) );

    // A zero-alpha premultiplied color has no recoverable straight RGB value.
    const std::array<f32, 4u> restoredZero = ReadRgba32( restored, 1u );
    REQUIRE( restoredZero[0] == 0.0f );
    REQUIRE( restoredZero[1] == 0.0f );
    REQUIRE( restoredZero[2] == 0.0f );
    REQUIRE( restoredZero[3] == 0.0f );
}

TEST_CASE( "Image conversion swizzles channels and supplies constants",
           "[CypherCommon][Image][Convert][Swizzle]" )
{
    test_image_t source = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT ) );
    const byte sourcePixel[4u]{ 10u, 20u, 30u, 40u };
    std::memcpy( PixelAddress( source, 0u ), sourcePixel, sizeof( sourcePixel ) );

    test_image_t destination = MakeImage( source.desc );
    const image_convert_options_t options{
        {
            image_channel_t::BLUE,
            image_channel_t::GREEN,
            image_channel_t::RED,
            image_channel_t::ONE
        }
    };
    REQUIRE( ImageConvert(
                 WritableView( destination ),
                 ReadOnlyView( source ),
                 options ) == image_convert_status_t::OK );

    const byte expected[4u]{ 30u, 20u, 10u, 255u };
    REQUIRE( std::memcmp(
                 PixelAddress( destination, 0u ),
                 expected,
                 sizeof( expected ) ) == 0 );
}

TEST_CASE( "Image region conversion handles pitched rows and depth slices",
           "[CypherCommon][Image][Convert][Region][Pitch]" )
{
    test_image_t source = MakeImage( MakeDesc(
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        3u,
        2u,
        2u ), 2u, 3u );
    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
            for ( u32 iColumn = 0u; iColumn < 3u; ++iColumn ) {
                PixelAddress( source, iColumn, iRow, iSlice )[0] =
                    static_cast<byte>( 10u * iSlice + 3u * iRow + iColumn );
            }
        }
    }
    const std::vector<byte> sourceBefore = source.pixels;

    test_image_t destination = MakeImage( MakeDesc(
        image_pixel_format_t::R16_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        4u,
        3u,
        2u ), 4u, 5u );
    const image_region_t sourceRegion{
        { 1u, 0u, 0u },
        { 2u, 2u, 2u }
    };
    REQUIRE( ImageConvert_Region(
                 WritableView( destination ),
                 { 1u, 1u, 0u },
                 ReadOnlyView( source ),
                 sourceRegion ) == image_convert_status_t::OK );

    for ( u32 iSlice = 0u; iSlice < 2u; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < 3u; ++iRow ) {
            for ( u32 iColumn = 0u; iColumn < 4u; ++iColumn ) {
                const byte *pPixel = PixelAddress(
                    destination,
                    iColumn,
                    iRow,
                    iSlice );
                if ( iColumn >= 1u && iColumn <= 2u &&
                     iRow >= 1u && iRow <= 2u ) {
                    u16 actual = 0u;
                    std::memcpy( &actual, pPixel, sizeof( actual ) );
                    const u16 sourceValue = static_cast<u16>(
                        10u * iSlice + 3u * ( iRow - 1u ) + iColumn );
                    REQUIRE( actual == static_cast<u16>( sourceValue * 257u ) );
                } else {
                    REQUIRE( pPixel[0] == kImageConvertPadding );
                    REQUIRE( pPixel[1] == kImageConvertPadding );
                }
            }

            const usize cbLogicalRow = 4u * sizeof( u16 );
            const usize iRowOffset =
                static_cast<usize>( iSlice ) * destination.cbSlicePitch +
                static_cast<usize>( iRow ) * destination.cbRowPitch;
            for ( usize iByte = cbLogicalRow;
                  iByte < destination.cbRowPitch;
                  ++iByte ) {
                REQUIRE( destination.pixels[iRowOffset + iByte] ==
                         kImageConvertPadding );
            }
        }
    }
    REQUIRE( source.pixels == sourceBefore );
}

TEST_CASE( "Image conversion permits only safe exact in-place storage",
           "[CypherCommon][Image][Convert][Aliasing]" )
{
    test_image_t storage = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::SRGB,
        image_alpha_mode_t::STRAIGHT ) );
    const byte encoded[4u]{ 128u, 64u, 32u, 128u };
    std::memcpy( PixelAddress( storage, 0u ), encoded, sizeof( encoded ) );

    const_image_view_t source = ReadOnlyView( storage );
    image_view_t destination = WritableView( storage );
    destination.desc.colorSpace = image_color_space_t::LINEAR;
    REQUIRE( ImageConvert( destination, source ) == image_convert_status_t::OK );
    REQUIRE( storage.pixels[0] == 55u );
    REQUIRE( storage.pixels[1] == 13u );
    REQUIRE( storage.pixels[2] == 4u );
    REQUIRE( storage.pixels[3] == 128u );

    std::vector<byte> shared( 8u, 0x5Au );
    const image_desc_t sourceDesc = MakeDesc(
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        4u );
    const image_desc_t destinationDesc = MakeDesc(
        image_pixel_format_t::R16_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        4u );
    const const_image_view_t overlappingSource{
        sourceDesc,
        { shared.data(), shared.size() },
        8u,
        8u
    };
    const image_view_t overlappingDestination{
        destinationDesc,
        { shared.data(), shared.size() },
        8u,
        8u
    };
    const std::vector<byte> before = shared;
    REQUIRE( ImageConvert(
                 overlappingDestination,
                 overlappingSource ) ==
             image_convert_status_t::OVERLAPPING_MEMORY );
    REQUIRE( shared == before );
}

TEST_CASE( "Image conversion reports failures before modifying destination",
           "[CypherCommon][Image][Convert][Failure]" )
{
    test_image_t source = MakeImage( MakeDesc(
        image_pixel_format_t::R8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::NONE,
        2u,
        2u ) );
    test_image_t destination = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        2u,
        2u ) );
    const std::vector<byte> before = destination.pixels;

    image_view_t invalidDestination = WritableView( destination );
    invalidDestination.pixels.pData = nullptr;
    REQUIRE( ImageConvert(
                 invalidDestination,
                 ReadOnlyView( source ) ) ==
             image_convert_status_t::INVALID_DESTINATION_VIEW );

    const_image_view_t invalidSource = ReadOnlyView( source );
    invalidSource.pixels.pData = nullptr;
    REQUIRE( ImageConvert(
                 WritableView( destination ),
                 invalidSource ) ==
             image_convert_status_t::INVALID_SOURCE_VIEW );

    test_image_t wrongExtent = MakeImage( MakeDesc(
        image_pixel_format_t::RGBA8_UNORM,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        3u,
        2u ) );
    REQUIRE( ImageConvert(
                 WritableView( wrongExtent ),
                 ReadOnlyView( source ) ) ==
             image_convert_status_t::EXTENT_MISMATCH );

    image_convert_options_t invalidOptions{};
    invalidOptions.swizzle.red = static_cast<image_channel_t>( 99u );
    REQUIRE( ImageConvert_Region(
                 WritableView( destination ),
                 {},
                 ReadOnlyView( source ),
                 { {}, { 1u, 1u, 1u } },
                 invalidOptions ) ==
             image_convert_status_t::INVALID_SWIZZLE );
    REQUIRE( ImageConvert_Region(
                 WritableView( destination ),
                 {},
                 ReadOnlyView( source ),
                 { {}, { 0u, 1u, 1u } } ) ==
             image_convert_status_t::INVALID_REGION );
    REQUIRE( ImageConvert_Region(
                 WritableView( destination ),
                 {},
                 ReadOnlyView( source ),
                 { { 1u, 0u, 0u }, { 2u, 1u, 1u } } ) ==
             image_convert_status_t::SOURCE_REGION_OUT_OF_BOUNDS );
    REQUIRE( ImageConvert_Region(
                 WritableView( destination ),
                 { 1u, 1u, 0u },
                 ReadOnlyView( source ),
                 { {}, { 2u, 2u, 1u } } ) ==
             image_convert_status_t::DESTINATION_REGION_OUT_OF_BOUNDS );
    REQUIRE( destination.pixels == before );
}

TEST_CASE( "Image identity conversion preserves exact bits and padding",
           "[CypherCommon][Image][Convert][Identity]" )
{
    const image_desc_t desc = MakeDesc(
        image_pixel_format_t::RGBA32_FLOAT,
        image_color_space_t::LINEAR,
        image_alpha_mode_t::STRAIGHT,
        2u,
        2u );
    test_image_t source = MakeImage( desc, 7u, 5u, 0xA5u );
    test_image_t destination = MakeImage( desc, 11u, 3u, kImageConvertPadding );

    // A signaling-looking NaN payload verifies that identity conversion copies
    // storage bits instead of canonicalizing through floating-point arithmetic.
    constexpr u32 payload = 0x7FA12345u;
    std::memcpy( PixelAddress( source, 0u, 0u ), &payload, sizeof( payload ) );
    for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
        for ( u32 iColumn = 0u; iColumn < 2u; ++iColumn ) {
            byte *pPixel = PixelAddress( source, iColumn, iRow );
            for ( usize iByte = iColumn == 0u && iRow == 0u ? 4u : 0u;
                  iByte < 16u;
                  ++iByte ) {
                pPixel[iByte] = static_cast<byte>(
                    17u * iRow + 7u * iColumn + iByte );
            }
        }
    }

    REQUIRE( ImageConvert(
                 WritableView( destination ),
                 ReadOnlyView( source ) ) == image_convert_status_t::OK );
    for ( u32 iRow = 0u; iRow < 2u; ++iRow ) {
        REQUIRE( std::memcmp(
                     PixelAddress( destination, 0u, iRow ),
                     PixelAddress( source, 0u, iRow ),
                     2u * 16u ) == 0 );
        const usize iPadding =
            static_cast<usize>( iRow ) * destination.cbRowPitch + 2u * 16u;
        for ( usize iByte = iPadding;
              iByte < static_cast<usize>( iRow + 1u ) *
                          destination.cbRowPitch;
              ++iByte ) {
            REQUIRE( destination.pixels[iByte] == kImageConvertPadding );
        }
    }
}

TEST_CASE( "Image conversion status names are stable",
           "[CypherCommon][Image][Convert][Status]" )
{
    REQUIRE( std::string_view( ImageConvert_StatusName(
                 image_convert_status_t::OK ) ) == "OK" );
    REQUIRE( std::string_view( ImageConvert_StatusName(
                 image_convert_status_t::INVALID_SWIZZLE ) ) ==
             "INVALID_SWIZZLE" );
    REQUIRE( std::string_view( ImageConvert_StatusName(
                 image_convert_status_t::OVERLAPPING_MEMORY ) ) ==
             "OVERLAPPING_MEMORY" );
    REQUIRE( std::string_view( ImageConvert_StatusName(
                 static_cast<image_convert_status_t>( 255u ) ) ) ==
             "UNKNOWN_IMAGE_CONVERT_STATUS" );
}
