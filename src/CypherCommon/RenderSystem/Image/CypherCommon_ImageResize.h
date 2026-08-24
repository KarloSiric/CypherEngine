//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageResize.h
//  Purpose: Declares allocation-free resizing of uncompressed CPU images.
//  Details: Nearest filtering preserves raw pixels, while linear and area filters
//           operate on linear FLOAT32 working images with correct alpha handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGERESIZE_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGERESIZE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ImageProcess.h"

namespace cypher::common
{

// Selects how destination pixel footprints sample the source image.
enum class image_resize_filter_t : u8 {
    NEAREST = 0u, // Chooses one source pixel and preserves its exact bytes.
    LINEAR,       // Uses bilinear or trilinear interpolation at pixel centers.
    BOX           // Integrates source pixel areas; intended for downsampling.
};

// Reports all validation and policy failures before destination pixels change.
enum class image_resize_status_t : u8 {
    OK = 0u,                    // Destination was completely initialized.
    INVALID_SOURCE_VIEW,        // Source descriptor, pitches, or storage are invalid.
    INVALID_DESTINATION_VIEW,   // Destination descriptor, pitches, or storage are invalid.
    INVALID_FILTER,             // Filter selector is outside the declared enum.
    PIXEL_FORMAT_MISMATCH,      // Source and destination component layouts differ.
    COLOR_SPACE_MISMATCH,       // Source and destination transfer functions differ.
    ALPHA_MODE_MISMATCH,        // Source and destination alpha policies differ.
    FILTER_FORMAT_NOT_SUPPORTED,// Filter requires another working representation.
    BOX_REQUIRES_DOWNSAMPLING,  // Area integration cannot enlarge an axis.
    OVERLAPPING_MEMORY          // Distinct views alias storage unsafely.
};

// Reports whether a descriptor can use the requested filter. NEAREST accepts all
// declared formats. LINEAR and BOX require LINEAR R/RG/RGBA32_FLOAT storage.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageResize_IsFilterSupported(
    const image_desc_t &desc,
    image_resize_filter_t filter ) noexcept;

// Resizes the complete source volume into preallocated destination storage.
// Source and destination must share format and semantic metadata. No function in
// this layer allocates scratch or output memory, and unsafe aliasing is rejected.
CYPHER_NODISCARD CYPHER_COMMON_API
image_resize_status_t ImageResize(
    const image_view_t &destination,
    const const_image_view_t &source,
    image_resize_filter_t filter ) noexcept;

// Returns a stable diagnostic name for tests, logs, and Picasso tool output.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageResize_StatusName(
    image_resize_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGERESIZE_H
