//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Color.cpp
//  Purpose: Implements packed and linear color primitives.
//  Details: Conversions follow the IEC sRGB transfer curve, keep alpha linear,
//           and define packed integers by bit position rather than host byte order.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Color Implementation Notes

Color conversion states its numeric domain and transfer assumptions explicitly. Callers must not
confuse linear values, encoded display values, and packed byte channels.
================
*/

#include "CypherCommon_Color.h"

#include <cmath>

namespace cypher::common
{

namespace
{

constexpr f32 CY_COLOR_BYTE_SCALE = 1.0f / 255.0f;
constexpr f32 CY_COLOR_SRGB_TO_LINEAR_THRESHOLD = 0.04045f;
constexpr f32 CY_COLOR_LINEAR_TO_SRGB_THRESHOLD = 0.0031308f;

CYPHER_NODISCARD f32 Color_ClampChannel( f32 value ) noexcept
{
    if ( std::isnan( value ) || value <= 0.0f ) {
        return 0.0f;
    }
    return value >= 1.0f ? 1.0f : value;
}

CYPHER_NODISCARD f32 Color_SrgbChannelToLinear( u8 value ) noexcept
{
    const f32 normalized = static_cast<f32>( value ) * CY_COLOR_BYTE_SCALE;
    return normalized <= CY_COLOR_SRGB_TO_LINEAR_THRESHOLD
        ? normalized / 12.92f
        : std::pow( ( normalized + 0.055f ) / 1.055f, 2.4f );
}

CYPHER_NODISCARD u8 Color_LinearChannelToSrgb( f32 value ) noexcept
{
    const f32 linear = Color_ClampChannel( value );
    const f32 srgb = linear <= CY_COLOR_LINEAR_TO_SRGB_THRESHOLD
        ? linear * 12.92f
        : 1.055f * std::pow( linear, 1.0f / 2.4f ) - 0.055f;
    return static_cast<u8>( srgb * 255.0f + 0.5f );
}

CYPHER_NODISCARD u8 Color_LinearAlphaToByte( f32 value ) noexcept
{
    return static_cast<u8>( Color_ClampChannel( value ) * 255.0f + 0.5f );
}

} // namespace

color32_t Color32_RGBA( u8 r, u8 g, u8 b, u8 a ) noexcept
{
    return { r, g, b, a };
}

colorf_t ColorF_RGBA( f32 r, f32 g, f32 b, f32 a ) noexcept
{
    return { r, g, b, a };
}

colorf_t Color_SrgbToLinear( color32_t color ) noexcept
{
    // RGB uses the sRGB transfer curve; alpha is coverage and remains linear.
    return {
        Color_SrgbChannelToLinear( color.r ),
        Color_SrgbChannelToLinear( color.g ),
        Color_SrgbChannelToLinear( color.b ),
        static_cast<f32>( color.a ) * CY_COLOR_BYTE_SCALE
    };
}

color32_t Color_LinearToSrgb( colorf_t color ) noexcept
{
    return {
        Color_LinearChannelToSrgb( color.r ),
        Color_LinearChannelToSrgb( color.g ),
        Color_LinearChannelToSrgb( color.b ),
        Color_LinearAlphaToByte( color.a )
    };
}

colorf_t Color_Clamp( colorf_t color ) noexcept
{
    return {
        Color_ClampChannel( color.r ),
        Color_ClampChannel( color.g ),
        Color_ClampChannel( color.b ),
        Color_ClampChannel( color.a )
    };
}

colorf_t Color_Lerp( colorf_t left, colorf_t right, f32 t ) noexcept
{
    return {
        left.r + ( right.r - left.r ) * t,
        left.g + ( right.g - left.g ) * t,
        left.b + ( right.b - left.b ) * t,
        left.a + ( right.a - left.a ) * t
    };
}

colorf_t Color_PremultiplyAlpha( colorf_t color ) noexcept
{
    return {
        color.r * color.a,
        color.g * color.a,
        color.b * color.a,
        color.a
    };
}

colorf_t Color_UnpremultiplyAlpha( colorf_t color ) noexcept
{
    // Transparent black is the only stable result when alpha carries no color weight.
    if ( color.a == 0.0f ) {
        return { 0.0f, 0.0f, 0.0f, 0.0f };
    }

    const f32 inverseAlpha = 1.0f / color.a;
    return {
        color.r * inverseAlpha,
        color.g * inverseAlpha,
        color.b * inverseAlpha,
        color.a
    };
}

u32 Color32_PackRGBA8( color32_t color ) noexcept
{
    // Packed names describe bit significance, independent of host byte order.
    return static_cast<u32>( color.r ) |
           ( static_cast<u32>( color.g ) << 8u ) |
           ( static_cast<u32>( color.b ) << 16u ) |
           ( static_cast<u32>( color.a ) << 24u );
}

u32 Color32_PackBGRA8( color32_t color ) noexcept
{
    return static_cast<u32>( color.b ) |
           ( static_cast<u32>( color.g ) << 8u ) |
           ( static_cast<u32>( color.r ) << 16u ) |
           ( static_cast<u32>( color.a ) << 24u );
}

color32_t Color32_UnpackRGBA8( u32 packed ) noexcept
{
    return {
        static_cast<u8>( packed ),
        static_cast<u8>( packed >> 8u ),
        static_cast<u8>( packed >> 16u ),
        static_cast<u8>( packed >> 24u )
    };
}

color32_t Color32_UnpackBGRA8( u32 packed ) noexcept
{
    return {
        static_cast<u8>( packed >> 16u ),
        static_cast<u8>( packed >> 8u ),
        static_cast<u8>( packed ),
        static_cast<u8>( packed >> 24u )
    };
}

} // namespace cypher::common
