//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Color.h
//  Purpose: Declares packed and linear floating-point color primitives.
//  Details: color32_t stores straight-alpha sRGB bytes; colorf_t stores straight-alpha
//           linear floats unless a function explicitly states otherwise.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COLOR_H
#define CYPHER_COMMON_TIER1_COLOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct color32_t {
    u8 r{ 0u };
    u8 g{ 0u };
    u8 b{ 0u };
    u8 a{ 255u };
};

struct colorf_t {
    f32 r{ 0.0f };
    f32 g{ 0.0f };
    f32 b{ 0.0f };
    f32 a{ 1.0f };
};

constexpr color32_t CY_COLOR32_BLACK{ 0u, 0u, 0u, 255u };
constexpr color32_t CY_COLOR32_WHITE{ 255u, 255u, 255u, 255u };
constexpr color32_t CY_COLOR32_TRANSPARENT{ 0u, 0u, 0u, 0u };

CYPHER_NODISCARD CYPHER_COMMON_API
color32_t Color32_RGBA( u8 r, u8 g, u8 b, u8 a = 255u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
colorf_t ColorF_RGBA( f32 r, f32 g, f32 b, f32 a = 1.0f ) noexcept;

// Converts sRGB color channels to linear light; alpha is only normalized.
CYPHER_NODISCARD CYPHER_COMMON_API colorf_t Color_SrgbToLinear( color32_t color ) noexcept;

// Converts linear light to sRGB bytes using nearest-integer quantization.
CYPHER_NODISCARD CYPHER_COMMON_API color32_t Color_LinearToSrgb( colorf_t color ) noexcept;

// Clamps finite channels to [0, 1]; NaN becomes zero.
CYPHER_NODISCARD CYPHER_COMMON_API colorf_t Color_Clamp( colorf_t color ) noexcept;

// Performs unclamped component-wise interpolation; t may extrapolate.
CYPHER_NODISCARD CYPHER_COMMON_API colorf_t Color_Lerp( colorf_t left, colorf_t right, f32 t ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API colorf_t Color_PremultiplyAlpha( colorf_t color ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API colorf_t Color_UnpremultiplyAlpha( colorf_t color ) noexcept;

// Packed integer order is R in bits 0..7 and A in bits 24..31.
CYPHER_NODISCARD CYPHER_COMMON_API u32 Color32_PackRGBA8( color32_t color ) noexcept;

// Packed integer order is B in bits 0..7 and A in bits 24..31.
CYPHER_NODISCARD CYPHER_COMMON_API u32 Color32_PackBGRA8( color32_t color ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API color32_t Color32_UnpackRGBA8( u32 packed ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API color32_t Color32_UnpackBGRA8( u32 packed ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COLOR_H
