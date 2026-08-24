//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageView.cpp
//  Purpose: Implements validated access to borrowed uncompressed image pixels.
//  Details: All external metadata and pitch arithmetic is checked before an
//           address is formed. Accessors return empty ranges on invalid input.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageView.h"

namespace cypher::common
{

namespace
{

// Performs general size multiplication without permitting unsigned wraparound.
bool_t ImageView_TryMultiply(
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

// Shared validation keeps immutable and writable views under one memory-layout
// contract. A valid buffer stores complete padded slices, including the last one.
image_view_status_t ImageView_ValidateStorage(
    const image_desc_t &desc,
    const byte *pPixels,
    usize cbPixels,
    usize cbRowPitch,
    usize cbSlicePitch ) noexcept
{
    if ( ImageFormat_ValidateDesc( desc ) != image_format_status_t::OK ) {
        return image_view_status_t::INVALID_DESCRIPTOR;
    }

    if ( pPixels == nullptr ) {
        return image_view_status_t::NULL_PIXEL_DATA;
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( desc.pixelFormat );
    // Validate the hierarchy from pixel row to slice to complete volume. Each
    // later calculation may rely on the previously checked pitch.
    usize cbMinimumRowPitch = 0u;
    if ( !ImageView_TryMultiply(
             static_cast<usize>( desc.extent.nWidth ),
             static_cast<usize>( pFormatInfo->cbPixel ),
             cbMinimumRowPitch ) ) {
        return image_view_status_t::ARITHMETIC_OVERFLOW;
    }

    if ( cbRowPitch < cbMinimumRowPitch ) {
        return image_view_status_t::ROW_PITCH_TOO_SMALL;
    }

    usize cbMinimumSlicePitch = 0u;
    if ( !ImageView_TryMultiply(
             cbRowPitch,
             static_cast<usize>( desc.extent.nHeight ),
             cbMinimumSlicePitch ) ) {
        return image_view_status_t::ARITHMETIC_OVERFLOW;
    }

    if ( cbSlicePitch < cbMinimumSlicePitch ) {
        return image_view_status_t::SLICE_PITCH_TOO_SMALL;
    }

    usize cbRequired = 0u;
    if ( !ImageView_TryMultiply(
             cbSlicePitch,
             static_cast<usize>( desc.extent.nDepth ),
             cbRequired ) ) {
        return image_view_status_t::ARITHMETIC_OVERFLOW;
    }

    return cbPixels >= cbRequired ?
        image_view_status_t::OK :
        image_view_status_t::BUFFER_TOO_SMALL;
}

} // namespace

image_view_status_t ImageView_Validate(
    const const_image_view_t &view ) noexcept
{
    return ImageView_ValidateStorage(
        view.desc,
        view.pixels.pData,
        view.pixels.cbSize,
        view.cbRowPitch,
        view.cbSlicePitch );
}

image_view_status_t ImageView_Validate( const image_view_t &view ) noexcept
{
    return ImageView_ValidateStorage(
        view.desc,
        view.pixels.pData,
        view.pixels.nCount,
        view.cbRowPitch,
        view.cbSlicePitch );
}

bool_t ImageView_IsValid( const const_image_view_t &view ) noexcept
{
    return ImageView_Validate( view ) == image_view_status_t::OK;
}

bool_t ImageView_IsValid( const image_view_t &view ) noexcept
{
    return ImageView_Validate( view ) == image_view_status_t::OK;
}

const_image_view_t ImageView_AsConst( const image_view_t &view ) noexcept
{
    // The conversion preserves the same storage and pitches while removing write
    // access. No validation or pixel copy is required for this view conversion.
    return {
        view.desc,
        { view.pixels.pData, view.pixels.nCount },
        view.cbRowPitch,
        view.cbSlicePitch
    };
}

binary_block_t ImageView_GetRow(
    const const_image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    if ( ImageView_Validate( view ) != image_view_status_t::OK ||
         iRow >= view.desc.extent.nHeight ||
         iSlice >= view.desc.extent.nDepth ) {
        return {};
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( view.desc.pixelFormat );
    const usize cbLogicalRow =
        static_cast<usize>( view.desc.extent.nWidth ) * pFormatInfo->cbPixel;

    // Validation proves both products and their sum fit within the supplied
    // buffer: one row lies inside one complete padded slice.
    const usize iOffset =
        ( static_cast<usize>( iSlice ) * view.cbSlicePitch ) +
        ( static_cast<usize>( iRow ) * view.cbRowPitch );
    return { view.pixels.pData + iOffset, cbLogicalRow };
}

byte_span_t ImageView_GetRow(
    const image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    if ( ImageView_Validate( view ) != image_view_status_t::OK ||
         iRow >= view.desc.extent.nHeight ||
         iSlice >= view.desc.extent.nDepth ) {
        return {};
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( view.desc.pixelFormat );
    const usize cbLogicalRow =
        static_cast<usize>( view.desc.extent.nWidth ) * pFormatInfo->cbPixel;
    // Validation above proves this address and logical row length remain inside
    // the writable borrowed span.
    const usize iOffset =
        ( static_cast<usize>( iSlice ) * view.cbSlicePitch ) +
        ( static_cast<usize>( iRow ) * view.cbRowPitch );
    return { view.pixels.pData + iOffset, cbLogicalRow };
}

binary_block_t ImageView_GetPixel(
    const const_image_view_t &view,
    u32 iColumn,
    u32 iRow,
    u32 iSlice ) noexcept
{
    if ( iColumn >= view.desc.extent.nWidth ) {
        return {};
    }

    // Derive pixels from a validated logical row so pitch padding is never
    // exposed as addressable texels.
    const binary_block_t row = ImageView_GetRow( view, iRow, iSlice );
    if ( row.pData == nullptr ) {
        return {};
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( view.desc.pixelFormat );
    const usize iOffset =
        static_cast<usize>( iColumn ) * pFormatInfo->cbPixel;
    return { row.pData + iOffset, pFormatInfo->cbPixel };
}

byte_span_t ImageView_GetPixel(
    const image_view_t &view,
    u32 iColumn,
    u32 iRow,
    u32 iSlice ) noexcept
{
    if ( iColumn >= view.desc.extent.nWidth ) {
        return {};
    }

    // Derive pixels from a validated logical row so pitch padding is never
    // exposed as addressable texels.
    const byte_span_t row = ImageView_GetRow( view, iRow, iSlice );
    if ( row.pData == nullptr ) {
        return {};
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( view.desc.pixelFormat );
    const usize iOffset =
        static_cast<usize>( iColumn ) * pFormatInfo->cbPixel;
    return { row.pData + iOffset, pFormatInfo->cbPixel };
}

const char *ImageView_StatusName( image_view_status_t status ) noexcept
{
    // Keep names stable because logs, tests, and tool diagnostics expose them.
    switch ( status ) {
        case image_view_status_t::OK:                    return "OK";
        case image_view_status_t::INVALID_DESCRIPTOR:    return "INVALID_DESCRIPTOR";
        case image_view_status_t::NULL_PIXEL_DATA:       return "NULL_PIXEL_DATA";
        case image_view_status_t::ROW_PITCH_TOO_SMALL:   return "ROW_PITCH_TOO_SMALL";
        case image_view_status_t::SLICE_PITCH_TOO_SMALL: return "SLICE_PITCH_TOO_SMALL";
        case image_view_status_t::BUFFER_TOO_SMALL:      return "BUFFER_TOO_SMALL";
        case image_view_status_t::ARITHMETIC_OVERFLOW:   return "ARITHMETIC_OVERFLOW";
        default:                                          return "UNKNOWN_IMAGE_VIEW_STATUS";
    }
}

} // namespace cypher::common
