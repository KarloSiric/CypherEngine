//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageTypes.h
//  Purpose: Declares backend-neutral image memory and metadata contracts.
//  Details: These types describe uncompressed authoring pixels without owning
//           memory or depending on image codecs, Qt, or rendering backends.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGETYPES_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGETYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

// Describes the physical channel representation. Color interpretation remains
// separate so RGBA8 storage can represent either linear or sRGB data.
enum class image_pixel_format_t : u16 {
    UNKNOWN = 0u, // No physical channel representation selected.

    R8_UNORM,     // One normalized unsigned 8-bit channel.
    RG8_UNORM,    // Two normalized unsigned 8-bit channels.
    RGBA8_UNORM,  // Four normalized unsigned 8-bit channels.

    R16_UNORM,    // One normalized unsigned 16-bit channel.
    RG16_UNORM,   // Two normalized unsigned 16-bit channels.
    RGBA16_UNORM, // Four normalized unsigned 16-bit channels.

    R16_FLOAT,    // One IEEE 754 binary16 channel.
    RG16_FLOAT,   // Two IEEE 754 binary16 channels.
    RGBA16_FLOAT, // Four IEEE 754 binary16 channels.

    R32_FLOAT,    // One IEEE 754 binary32 channel.
    RG32_FLOAT,   // Two IEEE 754 binary32 channels.
    RGBA32_FLOAT, // Four IEEE 754 binary32 channels.

    COUNT         // Sentinel used to size synchronized format tables.
};

enum class image_color_space_t : u8 {
    UNKNOWN = 0u, // Transfer function has not been specified.
    LINEAR,       // Stored color components are linear-light values.
    SRGB          // Stored color components use the sRGB transfer function.
};

// Describes whether the stored alpha channel is unused, independent, or already
// multiplied into the color channels. Correct compositing depends on this policy.
enum class image_alpha_mode_t : u8 {
    NONE = 0u,    // Format has no meaningful alpha component.
    STRAIGHT,     // RGB is independent of alpha.
    PREMULTIPLIED // RGB has already been multiplied by alpha.
};

struct image_extent_t {
    u32 nWidth{ 0u };  // Texel columns in one row.
    u32 nHeight{ 0u }; // Texel rows in one slice.
    u32 nDepth{ 1u };  // Slices in the image volume.
};

// Describes one uncompressed image subresource. Texture mip chains, arrays, and
// cube faces are represented by higher-level texture contracts.
struct image_desc_t {
    image_extent_t extent{}; // Logical dimensions, excluding pitch padding.
    image_pixel_format_t pixelFormat{ image_pixel_format_t::UNKNOWN }; // Physical channel layout.
    image_color_space_t colorSpace{ image_color_space_t::UNKNOWN };    // RGB transfer function.
    image_alpha_mode_t alphaMode{ image_alpha_mode_t::NONE };         // RGB/alpha relationship.
};

// Image rows use top-left origin. Row and slice pitches may include alignment
// padding, so consumers must never assume tightly packed storage.
struct const_image_view_t {
    image_desc_t desc{};       // Meaning and logical extent of borrowed pixels.
    binary_block_t pixels{};   // Read-only backing range; never owned by the view.
    usize cbRowPitch{ 0u };    // Bytes from one row start to the next.
    usize cbSlicePitch{ 0u };  // Bytes from one depth-slice start to the next.
};

struct image_view_t {
    image_desc_t desc{};      // Meaning and logical extent of borrowed pixels.
    byte_span_t pixels{};     // Writable backing range; never owned by the view.
    usize cbRowPitch{ 0u };   // Bytes from one row start to the next.
    usize cbSlicePitch{ 0u }; // Bytes from one depth-slice start to the next.
};

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGETYPES_H
