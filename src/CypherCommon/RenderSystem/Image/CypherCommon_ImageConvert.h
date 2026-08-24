//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageConvert.h
//  Purpose: Declares allocation-free conversion between CPU image formats.
//  Details: Conversion normalizes pixels through straight-alpha linear RGBA,
//           applies an optional channel swizzle, and writes caller-owned storage.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGECONVERT_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGECONVERT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ImageProcess.h"

namespace cypher::common
{

// Identifies one component of the canonical source pixel. ZERO and ONE make
// common operations such as dropping alpha or creating opaque alpha explicit.
enum class image_channel_t : u8 {
    ZERO = 0u, // Constant zero component.
    ONE,       // Constant one component.
    RED,       // Canonical source red.
    GREEN,     // Canonical source green.
    BLUE,      // Canonical source blue.
    ALPHA,     // Canonical source alpha.
    COUNT      // Sentinel for selector validation.
};

// Maps canonical source channels into destination RGBA order. Channels not
// physically present in the destination format are calculated but discarded.
struct image_swizzle_t {
    image_channel_t red{ image_channel_t::RED };     // Source of destination R.
    image_channel_t green{ image_channel_t::GREEN }; // Source of destination G.
    image_channel_t blue{ image_channel_t::BLUE };   // Source of destination B.
    image_channel_t alpha{ image_channel_t::ALPHA }; // Source of destination A.
};

// Keeps conversion policy extensible without adding positional arguments to
// every call. Default construction requests an identity channel mapping.
struct image_convert_options_t {
    image_swizzle_t swizzle{}; // Channel mapping applied in canonical linear space.
};

// Reports validation and aliasing failures before any destination byte is
// modified. Imported or authored image metadata must never require assertions.
enum class image_convert_status_t : u8 {
    OK = 0u,                       // Destination region was completely initialized.
    INVALID_SOURCE_VIEW,           // Source descriptor, pitches, or storage are invalid.
    INVALID_DESTINATION_VIEW,      // Destination descriptor, pitches, or storage are invalid.
    INVALID_SWIZZLE,               // At least one channel selector is out of range.
    INVALID_REGION,                // Source region is empty or malformed.
    SOURCE_REGION_OUT_OF_BOUNDS,   // Source selection exceeds its descriptor.
    DESTINATION_REGION_OUT_OF_BOUNDS, // Destination placement exceeds its descriptor.
    EXTENT_MISMATCH,               // Whole-image conversion requires equal dimensions.
    OVERLAPPING_MEMORY              // Aliasing would overwrite unread source pixels.
};

// Returns true only when all four selectors contain recognized channel values.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageConvert_IsSwizzleValid(
    const image_swizzle_t &swizzle ) noexcept;

// Returns true for the default RED/GREEN/BLUE/ALPHA mapping.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageConvert_IsIdentitySwizzle(
    const image_swizzle_t &swizzle ) noexcept;

// Converts every source pixel into a destination with the same extent. Pixel
// format, color space, alpha mode, row pitch, and slice pitch may differ.
CYPHER_NODISCARD CYPHER_COMMON_API
image_convert_status_t ImageConvert(
    const image_view_t &destination,
    const const_image_view_t &source,
    const image_convert_options_t &options = {} ) noexcept;

// Converts a source region into the destination at destinationOrigin. Exact
// in-place operation is supported only when origins, pitches, extents, and pixel
// byte sizes match; all other overlapping storage is rejected.
CYPHER_NODISCARD CYPHER_COMMON_API
image_convert_status_t ImageConvert_Region(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion,
    const image_convert_options_t &options = {} ) noexcept;

// Returns a stable diagnostic name for tests, logs, and Picasso tool output.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageConvert_StatusName(
    image_convert_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGECONVERT_H
