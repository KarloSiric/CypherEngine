//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageMip.cpp
//  Purpose: Implements mip-chain extent planning and next-level generation.
//  Details: Extent arithmetic is bounded and generation delegates pixel policy to
//           ImageResize so filtering behavior remains centralized and testable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Image Mip Implementation Notes

Mip generation operates on explicit image views and row pitches. Each destination level is
validated before filtering, and color or normal-map semantics are selected by the caller.
================
*/

#include "CypherCommon_ImageMip.h"

namespace cypher::common
{

namespace
{

bool_t ImageMip_IsExtentValid( const image_extent_t &extent ) noexcept
{
    return extent.nWidth > 0u &&
           extent.nHeight > 0u &&
           extent.nDepth > 0u;
}

bool_t ImageMip_IsFinalExtent( const image_extent_t &extent ) noexcept
{
    return extent.nWidth == 1u &&
           extent.nHeight == 1u &&
           extent.nDepth == 1u;
}

u32 ImageMip_ReduceDimension( u32 nDimension ) noexcept
{
    // A mip chain never reaches zero; dimensions already at one remain fixed
    // while the other dimensions continue to shrink.
    return nDimension > 1u ? nDimension / 2u : 1u;
}

image_extent_t ImageMip_NextExtent(
    const image_extent_t &extent ) noexcept
{
    return {
        ImageMip_ReduceDimension( extent.nWidth ),
        ImageMip_ReduceDimension( extent.nHeight ),
        ImageMip_ReduceDimension( extent.nDepth )
    };
}

bool_t ImageMip_ExtentsEqual(
    const image_extent_t &left,
    const image_extent_t &right ) noexcept
{
    return left.nWidth == right.nWidth &&
           left.nHeight == right.nHeight &&
           left.nDepth == right.nDepth;
}

} // namespace

u32 ImageMip_CalculateLevelCount(
    const image_extent_t &baseExtent ) noexcept
{
    if ( !ImageMip_IsExtentValid( baseExtent ) ) {
        return 0u;
    }

    image_extent_t extent = baseExtent;
    u32 cLevels = 1u;
    // Count the base image and every immediate child through 1x1x1.
    while ( !ImageMip_IsFinalExtent( extent ) ) {
        extent = ImageMip_NextExtent( extent );
        ++cLevels;
    }
    return cLevels;
}

image_extent_t ImageMip_CalculateLevelExtent(
    const image_extent_t &baseExtent,
    u32 iLevel ) noexcept
{
    if ( !ImageMip_IsExtentValid( baseExtent ) ) {
        return { 0u, 0u, 0u };
    }

    image_extent_t extent = baseExtent;
    // Requests beyond the final level clamp to 1x1x1. This keeps extent
    // planning total while level-count validation remains a caller concern.
    while ( iLevel > 0u && !ImageMip_IsFinalExtent( extent ) ) {
        extent = ImageMip_NextExtent( extent );
        --iLevel;
    }
    return extent;
}

bool_t ImageMip_IsNextLevelExtent(
    const image_extent_t &source,
    const image_extent_t &candidate ) noexcept
{
    if ( !ImageMip_IsExtentValid( source ) ||
         !ImageMip_IsExtentValid( candidate ) ||
         ImageMip_IsFinalExtent( source ) ) {
        return CY_FALSE;
    }
    return ImageMip_ExtentsEqual(
        ImageMip_NextExtent( source ),
        candidate );
}

image_mip_result_t ImageMip_GenerateNextLevel(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    image_mip_result_t result{};
    if ( ImageView_Validate( source ) != image_view_status_t::OK ) {
        result.status = image_mip_status_t::INVALID_SOURCE_VIEW;
        result.resizeStatus = image_resize_status_t::INVALID_SOURCE_VIEW;
        return result;
    }
    if ( ImageView_Validate( destination ) != image_view_status_t::OK ) {
        result.status = image_mip_status_t::INVALID_DESTINATION_VIEW;
        result.resizeStatus = image_resize_status_t::INVALID_DESTINATION_VIEW;
        return result;
    }
    if ( !ImageMip_IsNextLevelExtent(
             source.desc.extent,
             destination.desc.extent ) ) {
        result.status = image_mip_status_t::INVALID_LEVEL_EXTENT;
        return result;
    }

    // BOX is the shared area filter used for deterministic downsampling. Color
    // transfer and normal-map preparation must happen outside this low-level API.
    result.resizeStatus = ImageResize(
        destination,
        source,
        image_resize_filter_t::BOX );
    result.status = result.resizeStatus == image_resize_status_t::OK
        ? image_mip_status_t::OK
        : image_mip_status_t::RESIZE_FAILED;
    return result;
}

const char *ImageMip_StatusName( image_mip_status_t status ) noexcept
{
    switch ( status ) {
        case image_mip_status_t::OK:
            return "OK";
        case image_mip_status_t::INVALID_SOURCE_VIEW:
            return "INVALID_SOURCE_VIEW";
        case image_mip_status_t::INVALID_DESTINATION_VIEW:
            return "INVALID_DESTINATION_VIEW";
        case image_mip_status_t::INVALID_LEVEL_EXTENT:
            return "INVALID_LEVEL_EXTENT";
        case image_mip_status_t::RESIZE_FAILED:
            return "RESIZE_FAILED";
        default:
            return "UNKNOWN_IMAGE_MIP_STATUS";
    }
}

} // namespace cypher::common
