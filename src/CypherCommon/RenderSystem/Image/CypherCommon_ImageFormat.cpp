//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageFormat.cpp
//  Purpose: Implements image-format metadata and safe allocation layouts.
//  Details: Format lookup is constant time, and every size operation rejects
//           overflow before values can reach an allocator or pixel operation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageFormat.h"

#include "CypherCommon_Align.h"

namespace cypher::common
{

namespace
{

// The format enum is also the table index. UNKNOWN deliberately occupies index
// zero so valid enum values map directly to their immutable metadata entries.
static constexpr image_format_info_t g_imageFormatInfos[] = {
    { image_pixel_format_t::UNKNOWN,      image_numeric_type_t::UNKNOWN, "UNKNOWN",      0u, 0u,  0u, CY_FALSE },
    { image_pixel_format_t::R8_UNORM,     image_numeric_type_t::UNORM,   "R8_UNORM",     1u, 1u,  1u, CY_FALSE },
    { image_pixel_format_t::RG8_UNORM,    image_numeric_type_t::UNORM,   "RG8_UNORM",    2u, 1u,  2u, CY_FALSE },
    { image_pixel_format_t::RGBA8_UNORM,  image_numeric_type_t::UNORM,   "RGBA8_UNORM",  4u, 1u,  4u, CY_TRUE  },
    { image_pixel_format_t::R16_UNORM,    image_numeric_type_t::UNORM,   "R16_UNORM",    1u, 2u,  2u, CY_FALSE },
    { image_pixel_format_t::RG16_UNORM,   image_numeric_type_t::UNORM,   "RG16_UNORM",   2u, 2u,  4u, CY_FALSE },
    { image_pixel_format_t::RGBA16_UNORM, image_numeric_type_t::UNORM,   "RGBA16_UNORM", 4u, 2u,  8u, CY_TRUE  },
    { image_pixel_format_t::R16_FLOAT,    image_numeric_type_t::FLOAT,   "R16_FLOAT",    1u, 2u,  2u, CY_FALSE },
    { image_pixel_format_t::RG16_FLOAT,   image_numeric_type_t::FLOAT,   "RG16_FLOAT",   2u, 2u,  4u, CY_FALSE },
    { image_pixel_format_t::RGBA16_FLOAT, image_numeric_type_t::FLOAT,   "RGBA16_FLOAT", 4u, 2u,  8u, CY_TRUE  },
    { image_pixel_format_t::R32_FLOAT,    image_numeric_type_t::FLOAT,   "R32_FLOAT",    1u, 4u,  4u, CY_FALSE },
    { image_pixel_format_t::RG32_FLOAT,   image_numeric_type_t::FLOAT,   "RG32_FLOAT",   2u, 4u,  8u, CY_FALSE },
    { image_pixel_format_t::RGBA32_FLOAT, image_numeric_type_t::FLOAT,   "RGBA32_FLOAT", 4u, 4u, 16u, CY_TRUE  }
};

static_assert(
    CYPHER_ARRAY_COUNT( g_imageFormatInfos ) ==
        static_cast<usize>( image_pixel_format_t::COUNT ),
    "Every image pixel format must have a metadata entry." );

// Multiplication is checked before it can wrap. Image dimensions may originate
// in untrusted files, so wrapped allocation sizes must never reach callers.
bool_t ImageFormat_TryMultiply(
    usize nLeft,
    usize nRight,
    usize &nResultOut ) noexcept
{
    if ( nLeft != 0u && nRight > CY_USIZE_MAX / nLeft ) {
        nResultOut = 0u;
        return CY_FALSE;
    }

    nResultOut = nLeft * nRight;
    return CY_TRUE;
}

} // namespace

const image_format_info_t *ImageFormat_GetInfo(
    image_pixel_format_t pixelFormat ) noexcept
{
    const usize iFormat = static_cast<usize>( pixelFormat );

    // Check both UNKNOWN and untrusted enum casts before indexing the table.
    if ( iFormat == static_cast<usize>( image_pixel_format_t::UNKNOWN ) ||
         iFormat >= CYPHER_ARRAY_COUNT( g_imageFormatInfos ) ) {
        return nullptr;
    }

    return &g_imageFormatInfos[iFormat];
}

bool_t ImageFormat_IsKnown( image_pixel_format_t pixelFormat ) noexcept
{
    return ImageFormat_GetInfo( pixelFormat ) != nullptr;
}

image_format_status_t ImageFormat_ValidateDesc(
    const image_desc_t &desc ) noexcept
{
    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( desc.pixelFormat );
    if ( pFormatInfo == nullptr ) {
        return image_format_status_t::INVALID_PIXEL_FORMAT;
    }

    // Empty dimensions cannot describe addressable image storage. A normal 2D
    // image uses depth one rather than depth zero.
    if ( desc.extent.nWidth == 0u ||
         desc.extent.nHeight == 0u ||
         desc.extent.nDepth == 0u ) {
        return image_format_status_t::INVALID_EXTENT;
    }

    switch ( desc.colorSpace ) {
        case image_color_space_t::LINEAR:
        case image_color_space_t::SRGB:
            break;
        default:
            return image_format_status_t::INVALID_COLOR_SPACE;
    }

    switch ( desc.alphaMode ) {
        case image_alpha_mode_t::NONE:
        case image_alpha_mode_t::STRAIGHT:
        case image_alpha_mode_t::PREMULTIPLIED:
            break;
        default:
            return image_format_status_t::INVALID_ALPHA_MODE;
    }

    // Formats without an alpha channel cannot claim alpha interpretation.
    // RGBA formats may still use NONE when their alpha channel is unused.
    if ( !pFormatInfo->bHasAlpha &&
         desc.alphaMode != image_alpha_mode_t::NONE ) {
        return image_format_status_t::INVALID_ALPHA_MODE;
    }

    return image_format_status_t::OK;
}

image_layout_result_t ImageFormat_CalculateLayout(
    const image_desc_t &desc,
    usize cbRowAlignment ) noexcept
{
    image_layout_result_t result{};

    // Validate metadata before using it in allocation-size arithmetic.
    result.status = ImageFormat_ValidateDesc( desc );
    if ( result.status != image_format_status_t::OK ) {
        return result;
    }

    // Cy_AlignUpChecked uses a mask and therefore requires power-of-two input.
    if ( !Cy_AlignIsPowerOfTwo( cbRowAlignment ) ) {
        result.status = image_format_status_t::INVALID_ALIGNMENT;
        return result;
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( desc.pixelFormat );
    usize cbMinimumRowPitch = 0u;

    // First calculate logical pixel bytes, then append requested row padding.
    if ( !ImageFormat_TryMultiply(
             static_cast<usize>( desc.extent.nWidth ),
             static_cast<usize>( pFormatInfo->cbPixel ),
             cbMinimumRowPitch ) ||
         !Cy_AlignUpChecked(
             cbMinimumRowPitch,
             cbRowAlignment,
             result.layout.cbRowPitch ) ) {
        result.status = image_format_status_t::ARITHMETIC_OVERFLOW;
        return result;
    }

    // One slice contains all aligned rows, and the full allocation contains all
    // depth slices. Clear partial output if either multiplication fails.
    if ( !ImageFormat_TryMultiply(
             result.layout.cbRowPitch,
             static_cast<usize>( desc.extent.nHeight ),
             result.layout.cbSlicePitch ) ||
         !ImageFormat_TryMultiply(
             result.layout.cbSlicePitch,
             static_cast<usize>( desc.extent.nDepth ),
             result.layout.cbTotalSize ) ) {
        result.layout = {};
        result.status = image_format_status_t::ARITHMETIC_OVERFLOW;
        return result;
    }

    result.status = image_format_status_t::OK;
    return result;
}

const char *ImageFormat_Name( image_pixel_format_t pixelFormat ) noexcept
{
    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( pixelFormat );
    return pFormatInfo != nullptr ? pFormatInfo->pszName : "UNKNOWN";
}

const char *ImageFormat_StatusName( image_format_status_t status ) noexcept
{
    // Keep names stable because logs, tests, and tool diagnostics expose them.
    switch ( status ) {
        case image_format_status_t::OK:                   return "OK";
        case image_format_status_t::INVALID_PIXEL_FORMAT: return "INVALID_PIXEL_FORMAT";
        case image_format_status_t::INVALID_EXTENT:       return "INVALID_EXTENT";
        case image_format_status_t::INVALID_COLOR_SPACE:  return "INVALID_COLOR_SPACE";
        case image_format_status_t::INVALID_ALPHA_MODE:   return "INVALID_ALPHA_MODE";
        case image_format_status_t::INVALID_ALIGNMENT:    return "INVALID_ALIGNMENT";
        case image_format_status_t::ARITHMETIC_OVERFLOW:  return "ARITHMETIC_OVERFLOW";
        default:                                           return "UNKNOWN_IMAGE_FORMAT_STATUS";
    }
}

} // namespace cypher::common
