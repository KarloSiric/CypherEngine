//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageProcess.cpp
//  Purpose: Implements allocation-free operations over uncompressed image views.
//  Details: Kernels validate their complete operation before writing, preserve
//           physical padding, and make supported aliasing behavior explicit.
//
//  History:
//  - Created by Karlo Siric on 2026-08-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageProcess.h"

#include "CypherCommon_MemoryOps.h"

#include <cstring>

namespace cypher::common
{

namespace
{

constexpr usize kImageProcessRowScratchBytes = 256u; // Bounded stack storage for row swaps.
constexpr u32 kImageProcessRotationTileSize = 32u;   // Cache tile used by 90-degree rotations.

bool_t ImageProcess_RegionHasVolume(
    const image_region_t &region ) noexcept
{
    return region.extent.nWidth > 0u &&
           region.extent.nHeight > 0u &&
           region.extent.nDepth > 0u;
}

// Subtraction-based bounds checks avoid ever evaluating origin + extent until
// both values have been proven to fit inside the descriptor.
bool_t ImageProcess_RegionFits(
    const image_desc_t &desc,
    const image_region_t &region ) noexcept
{
    if ( !ImageProcess_RegionHasVolume( region ) ||
         region.origin.iColumn >= desc.extent.nWidth ||
         region.origin.iRow >= desc.extent.nHeight ||
         region.origin.iSlice >= desc.extent.nDepth ) {
        return CY_FALSE;
    }

    return region.extent.nWidth <=
               desc.extent.nWidth - region.origin.iColumn &&
           region.extent.nHeight <=
               desc.extent.nHeight - region.origin.iRow &&
           region.extent.nDepth <=
               desc.extent.nDepth - region.origin.iSlice;
}

bool_t ImageProcess_ExtentsEqual(
    const image_extent_t &left,
    const image_extent_t &right ) noexcept
{
    return left.nWidth == right.nWidth &&
           left.nHeight == right.nHeight &&
           left.nDepth == right.nDepth;
}

image_process_status_t ImageProcess_ValidateCompatibleViews(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    if ( ImageView_Validate( destination ) != image_view_status_t::OK ) {
        return image_process_status_t::INVALID_DESTINATION_VIEW;
    }
    if ( ImageView_Validate( source ) != image_view_status_t::OK ) {
        return image_process_status_t::INVALID_SOURCE_VIEW;
    }
    if ( destination.desc.pixelFormat != source.desc.pixelFormat ) {
        return image_process_status_t::PIXEL_FORMAT_MISMATCH;
    }
    if ( destination.desc.colorSpace != source.desc.colorSpace ) {
        return image_process_status_t::COLOR_SPACE_MISMATCH;
    }
    if ( destination.desc.alphaMode != source.desc.alphaMode ) {
        return image_process_status_t::ALPHA_MODE_MISMATCH;
    }

    return image_process_status_t::OK;
}

bool_t ImageProcess_IsExactView(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    return destination.pixels.pData == source.pixels.pData &&
           destination.cbRowPitch == source.cbRowPitch &&
           destination.cbSlicePitch == source.cbSlicePitch &&
           ImageProcess_ExtentsEqual(
               destination.desc.extent,
               source.desc.extent );
}

bool_t ImageProcess_StorageOverlaps(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    return Cy_MemRangesOverlap(
        destination.pixels.pData,
        destination.pixels.nCount,
        source.pixels.pData,
        source.pixels.cbSize );
}

byte *ImageProcess_GetRowUnchecked(
    const image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    const usize iOffset =
        static_cast<usize>( iSlice ) * view.cbSlicePitch +
        static_cast<usize>( iRow ) * view.cbRowPitch;
    return view.pixels.pData + iOffset;
}

const byte *ImageProcess_GetRowUnchecked(
    const const_image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept
{
    const usize iOffset =
        static_cast<usize>( iSlice ) * view.cbSlicePitch +
        static_cast<usize>( iRow ) * view.cbRowPitch;
    return view.pixels.pData + iOffset;
}

inline void ImageProcess_SwapPixels(
    byte *pLeft,
    byte *pRight,
    usize cbPixel ) noexcept
{
    // Known pixel widths become fixed-size loads and stores. The byte loop keeps
    // the helper correct if a wider format is introduced later.
    byte scratch[16u];
    switch ( cbPixel ) {
        case 1u: {
            const byte value = pLeft[0];
            pLeft[0] = pRight[0];
            pRight[0] = value;
            return;
        }
        case 2u:
            std::memcpy( scratch, pLeft, 2u );
            std::memcpy( pLeft, pRight, 2u );
            std::memcpy( pRight, scratch, 2u );
            return;
        case 4u:
            std::memcpy( scratch, pLeft, 4u );
            std::memcpy( pLeft, pRight, 4u );
            std::memcpy( pRight, scratch, 4u );
            return;
        case 8u:
            std::memcpy( scratch, pLeft, 8u );
            std::memcpy( pLeft, pRight, 8u );
            std::memcpy( pRight, scratch, 8u );
            return;
        case 16u:
            std::memcpy( scratch, pLeft, 16u );
            std::memcpy( pLeft, pRight, 16u );
            std::memcpy( pRight, scratch, 16u );
            return;
        default:
            for ( usize iByte = 0u; iByte < cbPixel; ++iByte ) {
                const byte value = pLeft[iByte];
                pLeft[iByte] = pRight[iByte];
                pRight[iByte] = value;
            }
            return;
    }
}

// Every Phase 1 format uses a 1, 2, 4, 8, or 16-byte pixel. Constant-sized
// memcpy calls compile to unaligned-safe scalar/vector loads instead of paying
// for one out-of-line memory wrapper call per pixel.
inline void ImageProcess_CopyPixel(
    byte *pDestination,
    const byte *pSource,
    usize cbPixel ) noexcept
{
    switch ( cbPixel ) {
        case 1u:
            pDestination[0] = pSource[0];
            return;
        case 2u:
            std::memcpy( pDestination, pSource, 2u );
            return;
        case 4u:
            std::memcpy( pDestination, pSource, 4u );
            return;
        case 8u:
            std::memcpy( pDestination, pSource, 8u );
            return;
        case 16u:
            std::memcpy( pDestination, pSource, 16u );
            return;
        default:
            Cy_MemCopy( pDestination, pSource, cbPixel );
            return;
    }
}

void ImageProcess_SwapRows(
    byte *pTop,
    byte *pBottom,
    usize cbRow ) noexcept
{
    // Swap arbitrarily wide rows in bounded chunks instead of allocating a row-
    // sized temporary buffer for large authoring images.
    byte scratch[kImageProcessRowScratchBytes];
    for ( usize iByte = 0u; iByte < cbRow; ) {
        const usize cbRemaining = cbRow - iByte;
        const usize cbChunk = cbRemaining < kImageProcessRowScratchBytes
            ? cbRemaining
            : kImageProcessRowScratchBytes;
        Cy_MemCopy( scratch, pTop + iByte, cbChunk );
        Cy_MemCopy( pTop + iByte, pBottom + iByte, cbChunk );
        Cy_MemCopy( pBottom + iByte, scratch, cbChunk );
        iByte += cbChunk;
    }
}

void ImageProcess_CopyRegionUnchecked(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion ) noexcept
{
    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const usize cbRegionRow =
        static_cast<usize>( sourceRegion.extent.nWidth ) *
        static_cast<usize>( pFormatInfo->cbPixel );
    const usize iSourceColumnOffset =
        static_cast<usize>( sourceRegion.origin.iColumn ) *
        static_cast<usize>( pFormatInfo->cbPixel );
    const usize iDestinationColumnOffset =
        static_cast<usize>( destinationOrigin.iColumn ) *
        static_cast<usize>( pFormatInfo->cbPixel );

    // A region spanning complete tight rows can move as one block per slice.
    // Pitched and partial-width regions fall through to explicit row copies.
    if ( iSourceColumnOffset == 0u &&
         iDestinationColumnOffset == 0u &&
         source.cbRowPitch == cbRegionRow &&
         destination.cbRowPitch == cbRegionRow ) {
        const usize cbRegionSlice =
            cbRegionRow * static_cast<usize>( sourceRegion.extent.nHeight );
        for ( u32 iSlice = 0u;
              iSlice < sourceRegion.extent.nDepth;
              ++iSlice ) {
            const byte *pSource = ImageProcess_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow,
                sourceRegion.origin.iSlice + iSlice );
            byte *pDestination = ImageProcess_GetRowUnchecked(
                destination,
                destinationOrigin.iRow,
                destinationOrigin.iSlice + iSlice );
            Cy_MemCopy( pDestination, pSource, cbRegionSlice );
        }
        return;
    }

    for ( u32 iSlice = 0u;
          iSlice < sourceRegion.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < sourceRegion.extent.nHeight;
              ++iRow ) {
            const byte *pSource = ImageProcess_GetRowUnchecked(
                source,
                sourceRegion.origin.iRow + iRow,
                sourceRegion.origin.iSlice + iSlice ) +
                iSourceColumnOffset;
            byte *pDestination = ImageProcess_GetRowUnchecked(
                destination,
                destinationOrigin.iRow + iRow,
                destinationOrigin.iSlice + iSlice ) +
                iDestinationColumnOffset;
            Cy_MemCopy( pDestination, pSource, cbRegionRow );
        }
    }
}

image_process_status_t ImageProcess_ValidateSameExtentOperation(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateCompatibleViews( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }
    if ( !ImageProcess_ExtentsEqual(
             destination.desc.extent,
             source.desc.extent ) ) {
        return image_process_status_t::EXTENT_MISMATCH;
    }

    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_ValidateRotatedExtentOperation(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateCompatibleViews( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }
    if ( destination.desc.extent.nWidth != source.desc.extent.nHeight ||
         destination.desc.extent.nHeight != source.desc.extent.nWidth ||
         destination.desc.extent.nDepth != source.desc.extent.nDepth ) {
        return image_process_status_t::EXTENT_MISMATCH;
    }

    return image_process_status_t::OK;
}

} // namespace

image_region_t ImageProcess_FullRegion(
    const image_desc_t &desc ) noexcept
{
    if ( ImageFormat_ValidateDesc( desc ) != image_format_status_t::OK ) {
        return {};
    }

    return { {}, desc.extent };
}

bool_t ImageProcess_IsRegionValid(
    const image_desc_t &desc,
    const image_region_t &region ) noexcept
{
    return ImageFormat_ValidateDesc( desc ) == image_format_status_t::OK &&
           ImageProcess_RegionFits( desc, region );
}

image_process_status_t ImageProcess_Copy(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateSameExtentOperation( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }
    if ( ImageProcess_IsExactView( destination, source ) ) {
        return image_process_status_t::OK;
    }
    if ( ImageProcess_StorageOverlaps( destination, source ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    ImageProcess_CopyRegionUnchecked(
        destination,
        {},
        source,
        ImageProcess_FullRegion( source.desc ) );
    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_CopyRegion(
    const image_view_t &destination,
    const image_origin_t &destinationOrigin,
    const const_image_view_t &source,
    const image_region_t &sourceRegion ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateCompatibleViews( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }
    if ( !ImageProcess_RegionHasVolume( sourceRegion ) ) {
        return image_process_status_t::INVALID_REGION;
    }
    if ( !ImageProcess_RegionFits( source.desc, sourceRegion ) ) {
        return image_process_status_t::SOURCE_REGION_OUT_OF_BOUNDS;
    }

    // Represent destination placement as the same half-open region shape so the
    // subtraction-based bound check is shared with source validation.
    const image_region_t destinationRegion{
        destinationOrigin,
        sourceRegion.extent
    };
    if ( !ImageProcess_RegionFits(
             destination.desc,
             destinationRegion ) ) {
        return image_process_status_t::DESTINATION_REGION_OUT_OF_BOUNDS;
    }

    const bool_t bExactView =
        ImageProcess_IsExactView( destination, source );
    if ( bExactView &&
         destinationOrigin.iColumn == sourceRegion.origin.iColumn &&
         destinationOrigin.iRow == sourceRegion.origin.iRow &&
         destinationOrigin.iSlice == sourceRegion.origin.iSlice ) {
        return image_process_status_t::OK;
    }
    if ( ImageProcess_StorageOverlaps( destination, source ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    ImageProcess_CopyRegionUnchecked(
        destination,
        destinationOrigin,
        source,
        sourceRegion );
    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_Fill(
    const image_view_t &destination,
    binary_block_t pixel ) noexcept
{
    return ImageProcess_FillRegion(
        destination,
        ImageProcess_FullRegion( destination.desc ),
        pixel );
}

image_process_status_t ImageProcess_FillRegion(
    const image_view_t &destination,
    const image_region_t &region,
    binary_block_t pixel ) noexcept
{
    if ( ImageView_Validate( destination ) != image_view_status_t::OK ) {
        return image_process_status_t::INVALID_DESTINATION_VIEW;
    }
    if ( !ImageProcess_RegionHasVolume( region ) ) {
        return image_process_status_t::INVALID_REGION;
    }
    if ( !ImageProcess_RegionFits( destination.desc, region ) ) {
        return image_process_status_t::DESTINATION_REGION_OUT_OF_BOUNDS;
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( destination.desc.pixelFormat );
    const usize cbPixel = static_cast<usize>( pFormatInfo->cbPixel );
    if ( pixel.pData == nullptr || pixel.cbSize != cbPixel ) {
        return image_process_status_t::INVALID_FILL_PIXEL;
    }
    if ( Cy_MemRangesOverlap(
             destination.pixels.pData,
             destination.pixels.nCount,
             pixel.pData,
             pixel.cbSize ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    const usize cbRegionRow =
        static_cast<usize>( region.extent.nWidth ) * cbPixel;
    const usize iColumnOffset =
        static_cast<usize>( region.origin.iColumn ) * cbPixel;
    byte *pFirstRow = ImageProcess_GetRowUnchecked(
        destination,
        region.origin.iRow,
        region.origin.iSlice ) + iColumnOffset;

    // Seed one pixel, then repeatedly double the initialized prefix. This keeps
    // arbitrary pixel formats efficient without format-specific scalar loops.
    Cy_MemCopy( pFirstRow, pixel.pData, cbPixel );
    for ( usize cbFilled = cbPixel; cbFilled < cbRegionRow; ) {
        const usize cbRemaining = cbRegionRow - cbFilled;
        const usize cbCopy = cbRemaining < cbFilled
            ? cbRemaining
            : cbFilled;
        Cy_MemCopy( pFirstRow + cbFilled, pFirstRow, cbCopy );
        cbFilled += cbCopy;
    }

    // Every remaining row receives the already expanded first row. Only the
    // requested logical region is touched; row and slice padding stay unchanged.
    for ( u32 iSlice = 0u; iSlice < region.extent.nDepth; ++iSlice ) {
        for ( u32 iRow = 0u; iRow < region.extent.nHeight; ++iRow ) {
            byte *pDestination = ImageProcess_GetRowUnchecked(
                destination,
                region.origin.iRow + iRow,
                region.origin.iSlice + iSlice ) + iColumnOffset;
            if ( pDestination != pFirstRow ) {
                Cy_MemCopy( pDestination, pFirstRow, cbRegionRow );
            }
        }
    }

    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_FlipHorizontal(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateSameExtentOperation( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const usize cbPixel = static_cast<usize>( pFormatInfo->cbPixel );
    const u32 nWidth = source.desc.extent.nWidth;
    if ( ImageProcess_IsExactView( destination, source ) ) {
        // In-place horizontal flip exchanges only mirrored pairs; the center
        // texel of an odd-width row remains untouched.
        for ( u32 iSlice = 0u;
              iSlice < destination.desc.extent.nDepth;
              ++iSlice ) {
            for ( u32 iRow = 0u;
                  iRow < destination.desc.extent.nHeight;
                  ++iRow ) {
                byte *pRow = ImageProcess_GetRowUnchecked(
                    destination,
                    iRow,
                    iSlice );
                for ( u32 iColumn = 0u;
                      iColumn < nWidth / 2u;
                      ++iColumn ) {
                    ImageProcess_SwapPixels(
                        pRow + static_cast<usize>( iColumn ) * cbPixel,
                        pRow + static_cast<usize>(
                            nWidth - 1u - iColumn ) * cbPixel,
                        cbPixel );
                }
            }
        }
        return image_process_status_t::OK;
    }
    if ( ImageProcess_StorageOverlaps( destination, source ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    for ( u32 iSlice = 0u;
          iSlice < source.desc.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < source.desc.extent.nHeight;
              ++iRow ) {
            const byte *pSourceRow = ImageProcess_GetRowUnchecked(
                source,
                iRow,
                iSlice );
            byte *pDestinationRow = ImageProcess_GetRowUnchecked(
                destination,
                iRow,
                iSlice );
            for ( u32 iColumn = 0u; iColumn < nWidth; ++iColumn ) {
                ImageProcess_CopyPixel(
                    pDestinationRow + static_cast<usize>( iColumn ) * cbPixel,
                    pSourceRow + static_cast<usize>(
                        nWidth - 1u - iColumn ) * cbPixel,
                    cbPixel );
            }
        }
    }

    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_FlipVertical(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateSameExtentOperation( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const usize cbLogicalRow =
        static_cast<usize>( source.desc.extent.nWidth ) *
        static_cast<usize>( pFormatInfo->cbPixel );
    const u32 nHeight = source.desc.extent.nHeight;
    if ( ImageProcess_IsExactView( destination, source ) ) {
        // In-place vertical flip swaps logical row bytes in bounded chunks.
        // Pitch padding remains attached to its physical row and is preserved.
        for ( u32 iSlice = 0u;
              iSlice < destination.desc.extent.nDepth;
              ++iSlice ) {
            for ( u32 iRow = 0u; iRow < nHeight / 2u; ++iRow ) {
                ImageProcess_SwapRows(
                    ImageProcess_GetRowUnchecked(
                        destination,
                        iRow,
                        iSlice ),
                    ImageProcess_GetRowUnchecked(
                        destination,
                        nHeight - 1u - iRow,
                        iSlice ),
                    cbLogicalRow );
            }
        }
        return image_process_status_t::OK;
    }
    if ( ImageProcess_StorageOverlaps( destination, source ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    for ( u32 iSlice = 0u;
          iSlice < source.desc.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u; iRow < nHeight; ++iRow ) {
            Cy_MemCopy(
                ImageProcess_GetRowUnchecked(
                    destination,
                    iRow,
                    iSlice ),
                ImageProcess_GetRowUnchecked(
                    source,
                    nHeight - 1u - iRow,
                    iSlice ),
                cbLogicalRow );
        }
    }

    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_Rotate180(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateSameExtentOperation( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const usize cbPixel = static_cast<usize>( pFormatInfo->cbPixel );
    const u32 nWidth = source.desc.extent.nWidth;
    const u32 nHeight = source.desc.extent.nHeight;
    if ( ImageProcess_IsExactView( destination, source ) ) {
        // Exchange opposite rows while reversing their column order. An odd
        // middle row needs a final horizontal reversal of its own.
        for ( u32 iSlice = 0u;
              iSlice < destination.desc.extent.nDepth;
              ++iSlice ) {
            for ( u32 iRow = 0u; iRow < nHeight / 2u; ++iRow ) {
                byte *pTop = ImageProcess_GetRowUnchecked(
                    destination,
                    iRow,
                    iSlice );
                byte *pBottom = ImageProcess_GetRowUnchecked(
                    destination,
                    nHeight - 1u - iRow,
                    iSlice );
                for ( u32 iColumn = 0u; iColumn < nWidth; ++iColumn ) {
                    ImageProcess_SwapPixels(
                        pTop + static_cast<usize>( iColumn ) * cbPixel,
                        pBottom + static_cast<usize>(
                            nWidth - 1u - iColumn ) * cbPixel,
                        cbPixel );
                }
            }

            if ( ( nHeight & 1u ) != 0u ) {
                byte *pMiddle = ImageProcess_GetRowUnchecked(
                    destination,
                    nHeight / 2u,
                    iSlice );
                for ( u32 iColumn = 0u;
                      iColumn < nWidth / 2u;
                      ++iColumn ) {
                    ImageProcess_SwapPixels(
                        pMiddle + static_cast<usize>( iColumn ) * cbPixel,
                        pMiddle + static_cast<usize>(
                            nWidth - 1u - iColumn ) * cbPixel,
                        cbPixel );
                }
            }
        }
        return image_process_status_t::OK;
    }
    if ( ImageProcess_StorageOverlaps( destination, source ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    for ( u32 iSlice = 0u;
          iSlice < source.desc.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u; iRow < nHeight; ++iRow ) {
            const byte *pSourceRow = ImageProcess_GetRowUnchecked(
                source,
                nHeight - 1u - iRow,
                iSlice );
            byte *pDestinationRow = ImageProcess_GetRowUnchecked(
                destination,
                iRow,
                iSlice );
            for ( u32 iColumn = 0u; iColumn < nWidth; ++iColumn ) {
                ImageProcess_CopyPixel(
                    pDestinationRow + static_cast<usize>( iColumn ) * cbPixel,
                    pSourceRow + static_cast<usize>(
                        nWidth - 1u - iColumn ) * cbPixel,
                    cbPixel );
            }
        }
    }

    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_Rotate90Clockwise(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateRotatedExtentOperation( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }
    if ( destination.pixels.pData == source.pixels.pData ) {
        return image_process_status_t::IN_PLACE_NOT_SUPPORTED;
    }
    if ( ImageProcess_StorageOverlaps( destination, source ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const usize cbPixel = static_cast<usize>( pFormatInfo->cbPixel );
    const u32 nSourceWidth = source.desc.extent.nWidth;
    const u32 nSourceHeight = source.desc.extent.nHeight;
    for ( u32 iSlice = 0u;
          iSlice < source.desc.extent.nDepth;
          ++iSlice ) {
        // Tiles keep both the column-oriented source reads and row-oriented
        // destination writes resident in cache for large 2K and 4K images.
        for ( u32 iTileColumn = 0u;
              iTileColumn < nSourceWidth;
              iTileColumn += kImageProcessRotationTileSize ) {
            const u32 iColumnEnd =
                iTileColumn + kImageProcessRotationTileSize < nSourceWidth
                    ? iTileColumn + kImageProcessRotationTileSize
                    : nSourceWidth;
            for ( u32 iTileRow = 0u;
                  iTileRow < nSourceHeight;
                  iTileRow += kImageProcessRotationTileSize ) {
                const u32 iRowEnd =
                    iTileRow + kImageProcessRotationTileSize < nSourceHeight
                        ? iTileRow + kImageProcessRotationTileSize
                        : nSourceHeight;
                for ( u32 iSourceColumn = iTileColumn;
                      iSourceColumn < iColumnEnd;
                      ++iSourceColumn ) {
                    byte *pDestinationRow = ImageProcess_GetRowUnchecked(
                        destination,
                        iSourceColumn,
                        iSlice );
                    for ( u32 iSourceRow = iTileRow;
                          iSourceRow < iRowEnd;
                          ++iSourceRow ) {
                        const byte *pSourceRow =
                            ImageProcess_GetRowUnchecked(
                                source,
                                iSourceRow,
                                iSlice );
                        const u32 iDestinationColumn =
                            nSourceHeight - 1u - iSourceRow;
                        ImageProcess_CopyPixel(
                            pDestinationRow +
                                static_cast<usize>( iDestinationColumn ) *
                                    cbPixel,
                            pSourceRow +
                                static_cast<usize>( iSourceColumn ) * cbPixel,
                            cbPixel );
                    }
                }
            }
        }
    }

    return image_process_status_t::OK;
}

image_process_status_t ImageProcess_Rotate90CounterClockwise(
    const image_view_t &destination,
    const const_image_view_t &source ) noexcept
{
    const image_process_status_t status =
        ImageProcess_ValidateRotatedExtentOperation( destination, source );
    if ( status != image_process_status_t::OK ) {
        return status;
    }
    if ( destination.pixels.pData == source.pixels.pData ) {
        return image_process_status_t::IN_PLACE_NOT_SUPPORTED;
    }
    if ( ImageProcess_StorageOverlaps( destination, source ) ) {
        return image_process_status_t::OVERLAPPING_MEMORY;
    }

    // Ninety-degree transforms cannot be performed by this API in-place because
    // rectangular images change row layout and source bytes would be overwritten.
    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( source.desc.pixelFormat );
    const usize cbPixel = static_cast<usize>( pFormatInfo->cbPixel );
    const u32 nSourceWidth = source.desc.extent.nWidth;
    const u32 nSourceHeight = source.desc.extent.nHeight;
    for ( u32 iSlice = 0u;
          iSlice < source.desc.extent.nDepth;
          ++iSlice ) {
        // Use the same cache tile as clockwise rotation; only the destination
        // coordinate mapping changes.
        for ( u32 iTileColumn = 0u;
              iTileColumn < nSourceWidth;
              iTileColumn += kImageProcessRotationTileSize ) {
            const u32 iColumnEnd =
                iTileColumn + kImageProcessRotationTileSize < nSourceWidth
                    ? iTileColumn + kImageProcessRotationTileSize
                    : nSourceWidth;
            for ( u32 iTileRow = 0u;
                  iTileRow < nSourceHeight;
                  iTileRow += kImageProcessRotationTileSize ) {
                const u32 iRowEnd =
                    iTileRow + kImageProcessRotationTileSize < nSourceHeight
                        ? iTileRow + kImageProcessRotationTileSize
                        : nSourceHeight;
                for ( u32 iSourceColumn = iTileColumn;
                      iSourceColumn < iColumnEnd;
                      ++iSourceColumn ) {
                    byte *pDestinationRow = ImageProcess_GetRowUnchecked(
                        destination,
                        nSourceWidth - 1u - iSourceColumn,
                        iSlice );
                    for ( u32 iSourceRow = iTileRow;
                          iSourceRow < iRowEnd;
                          ++iSourceRow ) {
                        const byte *pSourceRow =
                            ImageProcess_GetRowUnchecked(
                                source,
                                iSourceRow,
                                iSlice );
                        ImageProcess_CopyPixel(
                            pDestinationRow +
                                static_cast<usize>( iSourceRow ) * cbPixel,
                            pSourceRow +
                                static_cast<usize>( iSourceColumn ) * cbPixel,
                            cbPixel );
                    }
                }
            }
        }
    }

    return image_process_status_t::OK;
}

const char *ImageProcess_StatusName(
    image_process_status_t status ) noexcept
{
    // Status spellings are consumed by tests and editor diagnostics.
    switch ( status ) {
        case image_process_status_t::OK:
            return "OK";
        case image_process_status_t::INVALID_SOURCE_VIEW:
            return "INVALID_SOURCE_VIEW";
        case image_process_status_t::INVALID_DESTINATION_VIEW:
            return "INVALID_DESTINATION_VIEW";
        case image_process_status_t::INVALID_REGION:
            return "INVALID_REGION";
        case image_process_status_t::SOURCE_REGION_OUT_OF_BOUNDS:
            return "SOURCE_REGION_OUT_OF_BOUNDS";
        case image_process_status_t::DESTINATION_REGION_OUT_OF_BOUNDS:
            return "DESTINATION_REGION_OUT_OF_BOUNDS";
        case image_process_status_t::PIXEL_FORMAT_MISMATCH:
            return "PIXEL_FORMAT_MISMATCH";
        case image_process_status_t::COLOR_SPACE_MISMATCH:
            return "COLOR_SPACE_MISMATCH";
        case image_process_status_t::ALPHA_MODE_MISMATCH:
            return "ALPHA_MODE_MISMATCH";
        case image_process_status_t::EXTENT_MISMATCH:
            return "EXTENT_MISMATCH";
        case image_process_status_t::INVALID_FILL_PIXEL:
            return "INVALID_FILL_PIXEL";
        case image_process_status_t::OVERLAPPING_MEMORY:
            return "OVERLAPPING_MEMORY";
        case image_process_status_t::IN_PLACE_NOT_SUPPORTED:
            return "IN_PLACE_NOT_SUPPORTED";
        default:
            return "UNKNOWN_IMAGE_PROCESS_STATUS";
    }
}

} // namespace cypher::common
