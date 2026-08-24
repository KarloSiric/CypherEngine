//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageMip.h
//  Purpose: Declares mip-chain extent planning and next-level generation.
//  Details: The API plans complete 1D/2D/3D chains and generates one preallocated
//           level at a time through the color-correct image resize contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGEMIP_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGEMIP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ImageResize.h"

namespace cypher::common
{

enum class image_mip_status_t : u8 {
    OK = 0u,                 // Immediate child level was generated.
    INVALID_SOURCE_VIEW,     // Source descriptor, pitches, or storage are invalid.
    INVALID_DESTINATION_VIEW,// Destination descriptor, pitches, or storage are invalid.
    INVALID_LEVEL_EXTENT,    // Destination is not the immediate half-size child.
    RESIZE_FAILED            // ImageResize rejected or could not process the views.
};

// Preserves the detailed resize failure instead of collapsing unsupported format,
// metadata, or aliasing errors into a single mip-generation status.
struct image_mip_result_t {
    image_mip_status_t status{ image_mip_status_t::INVALID_LEVEL_EXTENT }; // Mip-level result.
    image_resize_status_t resizeStatus{ image_resize_status_t::OK };       // Detailed filter result.
};

// Returns the number of levels including the base image and final 1x1x1 level.
// Any zero dimension is invalid and produces zero levels.
CYPHER_NODISCARD CYPHER_COMMON_API
u32 ImageMip_CalculateLevelCount(
    const image_extent_t &baseExtent ) noexcept;

// Returns one level's extent using floor division by two with a minimum of one.
// Invalid base extents produce an all-zero result.
CYPHER_NODISCARD CYPHER_COMMON_API
image_extent_t ImageMip_CalculateLevelExtent(
    const image_extent_t &baseExtent,
    u32 iLevel ) noexcept;

// Checks whether candidate is the immediate level after source. A final 1x1x1
// source has no next level and therefore never accepts a candidate.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageMip_IsNextLevelExtent(
    const image_extent_t &source,
    const image_extent_t &candidate ) noexcept;

// Generates one immediate child level with an area-box filter. As with
// ImageResize BOX, callers provide LINEAR FLOAT32 working surfaces and convert
// packed/sRGB data before and after mip generation.
CYPHER_NODISCARD CYPHER_COMMON_API
image_mip_result_t ImageMip_GenerateNextLevel(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageMip_StatusName(
    image_mip_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGEMIP_H
