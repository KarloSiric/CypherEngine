//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageProcess.h
//  Purpose: Declares allocation-free operations over uncompressed image views.
//  Details: Processing functions copy, fill, flip, and rotate logical pixels while
//           preserving row padding and leaving storage ownership with the caller.
//
//  History:
//  - Created by Karlo Siric on 2026-08-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGEPROCESS_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGEPROCESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ImageView.h"

namespace cypher::common
{

// Locates one pixel inside an image. Coordinates use the image subsystem's
// top-left origin, with rows increasing downward and slices increasing by depth.
struct image_origin_t {
    u32 iColumn{ 0u }; // X coordinate of the first selected texel.
    u32 iRow{ 0u };    // Y coordinate of the first selected texel.
    u32 iSlice{ 0u };  // Z coordinate of the first selected texel.
};

// Selects a non-empty rectangular volume. Bounds are half-open: origin is
// included and origin + extent is excluded on every axis.
struct image_region_t {
    image_origin_t origin{};               // Inclusive first texel.
    image_extent_t extent{ 0u, 0u, 0u };  // Non-zero size on every selected axis.
};

// Reports policy and validation failures without asserting on imported data.
enum class image_process_status_t : u8 {
    OK = 0u,                       // Destination operation completed.
    INVALID_SOURCE_VIEW,           // Source descriptor, pitches, or storage are invalid.
    INVALID_DESTINATION_VIEW,      // Destination descriptor, pitches, or storage are invalid.
    INVALID_REGION,                // Region contains a zero extent or malformed origin.
    SOURCE_REGION_OUT_OF_BOUNDS,   // Source selection extends beyond its descriptor.
    DESTINATION_REGION_OUT_OF_BOUNDS, // Destination placement extends beyond its descriptor.
    PIXEL_FORMAT_MISMATCH,         // Raw-copy operations require identical pixel layouts.
    COLOR_SPACE_MISMATCH,          // Raw-copy operations require identical transfer metadata.
    ALPHA_MODE_MISMATCH,           // Raw-copy operations require identical alpha metadata.
    EXTENT_MISMATCH,               // Whole-image operation received different dimensions.
    INVALID_FILL_PIXEL,            // Fill value is null, aliased, or the wrong byte size.
    OVERLAPPING_MEMORY,            // Distinct source and destination ranges overlap.
    IN_PLACE_NOT_SUPPORTED         // Exact aliasing is unsafe for this transformation.
};

// Returns a full-image region for a valid descriptor. Invalid descriptors produce
// an empty region, which processing functions reject as INVALID_REGION.
CYPHER_NODISCARD CYPHER_COMMON_API
image_region_t ImageProcess_FullRegion(
    const image_desc_t &desc ) noexcept;

// Validates a non-empty region without performing origin + extent arithmetic that
// could wrap. The descriptor must itself be structurally valid.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageProcess_IsRegionValid(
    const image_desc_t &desc,
    const image_region_t &region ) noexcept;

// Copies every logical pixel. Different row and slice pitches are supported;
// destination padding is preserved. Copying an exact view onto itself is a no-op.
CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_Copy(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept;

// Copies one source region to a destination origin. Pixel format and semantic
// metadata must match. Ambiguous overlapping storage is deliberately rejected.
CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_CopyRegion(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion ) noexcept;

// Repeats one raw pixel value across a complete image or selected region. The
// supplied byte count must exactly equal the destination format's pixel size,
// and the pixel value storage must not alias the destination image.
CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_Fill(
    const image_view_t &destination,
    binary_block_t pixel ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_FillRegion(
    const image_view_t &destination,
    const image_region_t &region,
    binary_block_t pixel ) noexcept;

// Mirrors pixels without changing descriptor metadata. Horizontal and vertical
// flips support exact in-place operation; other aliasing is rejected.
CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_FlipHorizontal(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_FlipVertical(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept;

// Rotates each depth slice independently. A 180-degree rotation supports exact
// in-place operation. Ninety-degree rotations require separate storage and a
// destination whose width and height are the source height and width.
CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_Rotate180(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_Rotate90Clockwise(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
image_process_status_t ImageProcess_Rotate90CounterClockwise(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept;

// Returns a stable diagnostic name for logs, tests, and future Picasso output.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageProcess_StatusName(
    image_process_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGEPROCESS_H
