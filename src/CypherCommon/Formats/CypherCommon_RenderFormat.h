//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_RenderFormat.h
//  Purpose: Declares stable identities for renderer-facing resource formats.
//  Details: Source schemas, cookers, resource loaders, and renderer clients share
//           these values without depending on a graphics backend or native GPU API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Render Format Contract

This header is a serialized resource contract. Persisted fields use fixed-width values and
explicit offsets; readers validate magic, version, counts, and byte ranges before interpreting
payload data.
================
*/

#ifndef CYPHER_COMMON_FORMATS_RENDERFORMAT_H
#define CYPHER_COMMON_FORMATS_RENDERFORMAT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Endian.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_RENDER_SHADER_RESOURCE_TYPE =
    Cy_MakeFourCC( 'C', 'Y', 'S', 'H' ); // Cooked shader program.
inline constexpr fourcc_t CY_RENDER_TEXTURE_RESOURCE_TYPE =
    Cy_MakeFourCC( 'C', 'Y', 'T', 'X' ); // Cooked texture and mip chain.
inline constexpr fourcc_t CY_RENDER_MATERIAL_RESOURCE_TYPE =
    Cy_MakeFourCC( 'C', 'Y', 'M', 'T' ); // Cooked material bindings and values.

inline constexpr format_version_t CY_RENDER_SHADER_RESOURCE_VERSION = 2u;
inline constexpr format_version_t CY_RENDER_TEXTURE_RESOURCE_VERSION = 1u;
inline constexpr format_version_t CY_RENDER_MATERIAL_RESOURCE_VERSION = 1u;

/*
================
Canonical Render Formats

Values are stable across tools, cooked resources, and renderer backends.
Ranges leave room for adding formats without renumbering existing entries.
================
*/
enum class render_format_t : u16 {
    UNKNOWN = 0x0000u,

    // Eight-bit component formats.
    R8_UNORM = 0x0100u, R8_SNORM, R8_UINT, R8_SINT,
    RG8_UNORM = 0x0110u, RG8_SNORM, RG8_UINT, RG8_SINT,
    RGB8_UNORM = 0x0120u, RGB8_SNORM, RGB8_UINT, RGB8_SINT, RGB8_SRGB,
    RGBA8_UNORM = 0x0130u, RGBA8_SNORM, RGBA8_UINT, RGBA8_SINT, RGBA8_SRGB,
    BGRA8_UNORM = 0x0140u, BGRA8_SRGB,
    RGB565_UNORM = 0x0150u, RGBA4_UNORM, RGB5A1_UNORM,

    // Sixteen-bit component formats.
    R16_UNORM = 0x0200u, R16_SNORM, R16_UINT, R16_SINT, R16_FLOAT,
    RG16_UNORM = 0x0210u, RG16_SNORM, RG16_UINT, RG16_SINT, RG16_FLOAT,
    RGB16_UNORM = 0x0220u, RGB16_SNORM, RGB16_UINT, RGB16_SINT, RGB16_FLOAT,
    RGBA16_UNORM = 0x0230u, RGBA16_SNORM, RGBA16_UINT, RGBA16_SINT, RGBA16_FLOAT,

    // Thirty-two-bit and packed component formats.
    R32_UINT = 0x0300u, R32_SINT, R32_FLOAT,
    RG32_UINT = 0x0310u, RG32_SINT, RG32_FLOAT,
    RGB32_UINT = 0x0320u, RGB32_SINT, RGB32_FLOAT,
    RGBA32_UINT = 0x0330u, RGBA32_SINT, RGBA32_FLOAT,
    RGB10A2_UNORM = 0x0380u, RGB10A2_UINT,
    R11G11B10_UFLOAT, RGB9E5_UFLOAT,

    // Depth and stencil formats.
    D16_UNORM = 0x0400u,
    D24_UNORM_S8_UINT,
    D32_FLOAT,
    D32_FLOAT_S8_UINT,
    S8_UINT,

    // Desktop block compression.
    BC1_RGB_UNORM = 0x0500u, BC1_RGB_SRGB,
    BC1_RGBA_UNORM, BC1_RGBA_SRGB,
    BC2_UNORM, BC2_SRGB,
    BC3_UNORM, BC3_SRGB,
    BC4_UNORM, BC4_SNORM,
    BC5_UNORM, BC5_SNORM,
    BC6H_UFLOAT, BC6H_SFLOAT,
    BC7_UNORM, BC7_SRGB,

    // ETC2 and EAC compression.
    ETC2_RGB8_UNORM = 0x0600u, ETC2_RGB8_SRGB,
    ETC2_RGB8A1_UNORM, ETC2_RGB8A1_SRGB,
    ETC2_RGBA8_UNORM, ETC2_RGBA8_SRGB,
    EAC_R11_UNORM, EAC_R11_SNORM,
    EAC_RG11_UNORM, EAC_RG11_SNORM,

    // ASTC compression. Each format name contains its texel-block dimensions.
    ASTC_4X4_UNORM = 0x0700u, ASTC_4X4_SRGB,
    ASTC_5X4_UNORM, ASTC_5X4_SRGB,
    ASTC_5X5_UNORM, ASTC_5X5_SRGB,
    ASTC_6X5_UNORM, ASTC_6X5_SRGB,
    ASTC_6X6_UNORM, ASTC_6X6_SRGB,
    ASTC_8X5_UNORM, ASTC_8X5_SRGB,
    ASTC_8X6_UNORM, ASTC_8X6_SRGB,
    ASTC_8X8_UNORM, ASTC_8X8_SRGB,
    ASTC_10X5_UNORM, ASTC_10X5_SRGB,
    ASTC_10X6_UNORM, ASTC_10X6_SRGB,
    ASTC_10X8_UNORM, ASTC_10X8_SRGB,
    ASTC_10X10_UNORM, ASTC_10X10_SRGB,
    ASTC_12X10_UNORM, ASTC_12X10_SRGB,
    ASTC_12X12_UNORM, ASTC_12X12_SRGB,

    INVALID = 0xFFFFu
};

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_RENDERFORMAT_H
