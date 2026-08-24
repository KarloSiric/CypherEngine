//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageFormat.h
//  Purpose: Declares image-format metadata and overflow-safe layout helpers.
//  Details: The API describes uncompressed CPU pixel formats and calculates
//           allocation layouts without depending on codecs or renderer backends.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGEFORMAT_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGEFORMAT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ImageTypes.h"

namespace cypher::common
{

// Identifies how one component is encoded in memory. UNORM maps an unsigned
// integer range onto [0, 1], while FLOAT stores an IEEE floating-point value.
enum class image_numeric_type_t : u8 {
    UNKNOWN = 0u, // Component encoding has not been selected.
    UNORM,        // Unsigned integer normalized to [0, 1].
    FLOAT         // IEEE floating-point component.
};

// Immutable metadata for one pixel format. The format table is indexed directly
// by image_pixel_format_t, so each entry must remain synchronized with that enum.
struct image_format_info_t {
    image_pixel_format_t pixelFormat{ image_pixel_format_t::UNKNOWN }; // Table key.
    image_numeric_type_t numericType{ image_numeric_type_t::UNKNOWN }; // Component encoding.
    const char *pszName{ "UNKNOWN" }; // Stable diagnostic spelling.

    u8 cChannels{ 0u };   // Number of physically stored components.
    u8 cbComponent{ 0u }; // Bytes in one stored component.
    u8 cbPixel{ 0u };     // Bytes in one complete pixel.

    bool_t bHasAlpha{ CY_FALSE }; // True when channel four carries alpha.
};

// Reports structural descriptor and allocation-layout failures without asserting
// on data that may have originated in an authored or imported file.
enum class image_format_status_t : u8 {
    OK = 0u,             // Descriptor or layout is valid.
    INVALID_PIXEL_FORMAT,// UNKNOWN or out-of-range format value.
    INVALID_EXTENT,      // At least one logical dimension is zero.
    INVALID_COLOR_SPACE, // Transfer-function enum is not recognized.
    INVALID_ALPHA_MODE,  // Alpha metadata conflicts with the pixel format.
    INVALID_ALIGNMENT,   // Row alignment is not a non-zero power of two.
    ARITHMETIC_OVERFLOW  // Row, slice, or allocation size cannot be represented.
};

// Describes one complete allocation layout. A row may contain trailing padding,
// and a slice contains every padded row in one depth layer.
struct image_layout_t {
    usize cbRowPitch{ 0u };   // Aligned bytes between adjacent row starts.
    usize cbSlicePitch{ 0u }; // Bytes between adjacent depth-slice starts.
    usize cbTotalSize{ 0u };  // Complete bytes required by all slices.
};

// Couples the calculated layout with an explicit status. Layout remains zeroed
// whenever calculation fails, preventing callers from using partial results.
struct image_layout_result_t {
    image_format_status_t status{
        image_format_status_t::INVALID_PIXEL_FORMAT
    };

    image_layout_t layout{}; // Zero unless status is OK.
};

// Returns immutable metadata for a valid format, or nullptr for UNKNOWN and
// out-of-range values.
CYPHER_NODISCARD CYPHER_COMMON_API
const image_format_info_t *ImageFormat_GetInfo(
    image_pixel_format_t pixelFormat ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageFormat_IsKnown(
    image_pixel_format_t pixelFormat ) noexcept;

// Validates dimensions, color-space metadata, and alpha semantics.
CYPHER_NODISCARD CYPHER_COMMON_API
image_format_status_t ImageFormat_ValidateDesc(
    const image_desc_t &desc ) noexcept;

// Calculates row, slice, and total byte sizes. Alignment must be a non-zero
// power of two; use one for tightly packed rows.
CYPHER_NODISCARD CYPHER_COMMON_API
image_layout_result_t ImageFormat_CalculateLayout(
    const image_desc_t &desc,
    usize cbRowAlignment ) noexcept;

// Stable names are intended for diagnostics, tests, and editor output.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageFormat_Name(
    image_pixel_format_t pixelFormat ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageFormat_StatusName(
    image_format_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGEFORMAT_H
