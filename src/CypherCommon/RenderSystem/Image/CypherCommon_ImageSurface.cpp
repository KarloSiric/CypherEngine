//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageSurface.cpp
//  Purpose: Implements allocator-backed ownership of uncompressed image pixels.
//  Details: Surface creation validates every layout before allocation, preserves
//           existing images on failed recreation, and exposes memory only through
//           validated borrowed image views.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ImageSurface.h"

#include "CypherCommon_ImageProcess.h"
#include "CypherCommon_MemoryOps.h"

namespace cypher::common
{

namespace
{

// A destroyed or default-constructed surface carries no allocation metadata.
// image_extent_t deliberately keeps depth one in this canonical empty state.
bool_t ImageSurface_IsCanonicalEmpty(
    const image_surface_t &surface ) noexcept
{
    return surface.desc.extent.nWidth == 0u &&
           surface.desc.extent.nHeight == 0u &&
           surface.desc.extent.nDepth == 1u &&
           surface.desc.pixelFormat == image_pixel_format_t::UNKNOWN &&
           surface.desc.colorSpace == image_color_space_t::UNKNOWN &&
           surface.desc.alphaMode == image_alpha_mode_t::NONE &&
           surface.layout.cbRowPitch == 0u &&
           surface.layout.cbSlicePitch == 0u &&
           surface.layout.cbTotalSize == 0u &&
           surface.cbRowAlignment == 0u &&
           surface.allocation.pData == nullptr &&
           surface.allocation.cbSize == 0u &&
           surface.allocation.nAlignment == 0u &&
           surface.allocation.pAllocator == nullptr;
}

// Surface diagnostics group descriptor failures while preserving the two layout
// failures callers can act on directly: alignment and arithmetic overflow.
image_surface_status_t ImageSurface_FromFormatStatus(
    image_format_status_t status ) noexcept
{
    switch ( status ) {
        case image_format_status_t::OK:
            return image_surface_status_t::OK;
        case image_format_status_t::INVALID_ALIGNMENT:
            return image_surface_status_t::INVALID_ROW_ALIGNMENT;
        case image_format_status_t::ARITHMETIC_OVERFLOW:
            return image_surface_status_t::ARITHMETIC_OVERFLOW;
        default:
            return image_surface_status_t::INVALID_DESCRIPTOR;
    }
}

bool_t ImageSurface_IsInitializationValid(
    image_surface_init_t initialization ) noexcept
{
    return initialization == image_surface_init_t::UNINITIALIZED ||
           initialization == image_surface_init_t::ZEROED;
}

bool_t ImageSurface_LayoutsEqual(
    const image_layout_t &left,
    const image_layout_t &right ) noexcept
{
    return left.cbRowPitch == right.cbRowPitch &&
           left.cbSlicePitch == right.cbSlicePitch &&
           left.cbTotalSize == right.cbTotalSize;
}

image_surface_status_t ImageSurface_FromProcessStatus(
    image_process_status_t status ) noexcept
{
    switch ( status ) {
        case image_process_status_t::OK:
            return image_surface_status_t::OK;
        case image_process_status_t::INVALID_SOURCE_VIEW:
            return image_surface_status_t::INVALID_SOURCE_VIEW;
        case image_process_status_t::PIXEL_FORMAT_MISMATCH:
        case image_process_status_t::COLOR_SPACE_MISMATCH:
        case image_process_status_t::ALPHA_MODE_MISMATCH:
        case image_process_status_t::EXTENT_MISMATCH:
            return image_surface_status_t::DESCRIPTOR_MISMATCH;
        case image_process_status_t::OVERLAPPING_MEMORY:
            return image_surface_status_t::OVERLAPPING_SOURCE;
        default:
            return image_surface_status_t::INVALID_SURFACE_STATE;
    }
}

// Processing kernels preserve borrowed-view padding. Owned surfaces clear their
// own row padding after a successful copy so hashes and cooked output remain
// deterministic regardless of the allocator's previous contents.
void ImageSurface_ClearRowPadding(
    image_surface_t &surface ) noexcept
{
    const image_format_info_t *pFormatInfo =
        ImageFormat_GetInfo( surface.desc.pixelFormat );
    const usize cbLogicalRow =
        static_cast<usize>( surface.desc.extent.nWidth ) *
        static_cast<usize>( pFormatInfo->cbPixel );
    const usize cbPadding = surface.layout.cbRowPitch - cbLogicalRow;
    if ( cbPadding == 0u ) {
        return;
    }

    byte *pPixels = static_cast<byte *>( surface.allocation.pData );
    for ( u32 iSlice = 0u;
          iSlice < surface.desc.extent.nDepth;
          ++iSlice ) {
        for ( u32 iRow = 0u;
              iRow < surface.desc.extent.nHeight;
              ++iRow ) {
            const usize iRowOffset =
                static_cast<usize>( iSlice ) * surface.layout.cbSlicePitch +
                static_cast<usize>( iRow ) * surface.layout.cbRowPitch;
            Cy_MemZero(
                pPixels + iRowOffset + cbLogicalRow,
                cbPadding );
        }
    }
}

// Swap the ownership tuple as raw metadata. owned_allocation_t intentionally
// rejects move-assignment into a live destination, so std::swap is unsuitable.
void ImageSurface_SwapAllocations(
    owned_allocation_t &left,
    owned_allocation_t &right ) noexcept
{
    void *pData = left.pData;
    const usize cbSize = left.cbSize;
    const usize nAlignment = left.nAlignment;
    const allocator_t *pAllocator = left.pAllocator;

    left.pData = right.pData;
    left.cbSize = right.cbSize;
    left.nAlignment = right.nAlignment;
    left.pAllocator = right.pAllocator;

    right.pData = pData;
    right.cbSize = cbSize;
    right.nAlignment = nAlignment;
    right.pAllocator = pAllocator;
}

} // namespace

image_surface_t::~image_surface_t() noexcept
{
    // Explicit destruction is available to C-style callers, while the destructor
    // prevents an owning surface from leaking when its enclosing scope exits.
    ImageSurface_Destroy( this );
}

image_surface_status_t ImageSurface_Create(
    image_surface_t *pSurface,
    const allocator_t *pAllocator,
    const image_desc_t &desc,
    image_surface_init_t initialization,
    usize cbRowAlignment ) noexcept
{
    if ( pSurface == nullptr ) {
        return image_surface_status_t::NULL_SURFACE;
    }
    if ( !ImageSurface_IsCanonicalEmpty( *pSurface ) ) {
        return image_surface_status_t::DESTINATION_NOT_EMPTY;
    }
    if ( !Allocator_IsValid( pAllocator ) ) {
        return image_surface_status_t::INVALID_ALLOCATOR;
    }
    if ( !ImageSurface_IsInitializationValid( initialization ) ) {
        return image_surface_status_t::INVALID_INITIALIZATION;
    }

    const image_layout_result_t layoutResult =
        ImageFormat_CalculateLayout( desc, cbRowAlignment );
    if ( layoutResult.status != image_format_status_t::OK ) {
        return ImageSurface_FromFormatStatus( layoutResult.status );
    }

    // Metadata is published only after allocation succeeds. A failed allocator
    // therefore leaves the destination in its original canonical empty state.
    if ( !Allocator_AllocateOwned(
             &pSurface->allocation,
             pAllocator,
             layoutResult.layout.cbTotalSize,
             cbRowAlignment ) ) {
        return image_surface_status_t::ALLOCATION_FAILED;
    }

    pSurface->desc = desc;
    pSurface->layout = layoutResult.layout;
    pSurface->cbRowAlignment = cbRowAlignment;

    if ( initialization == image_surface_init_t::ZEROED ) {
        Cy_MemZero(
            pSurface->allocation.pData,
            pSurface->allocation.cbSize );
    }

    return image_surface_status_t::OK;
}

image_surface_status_t ImageSurface_CreateFromView(
    image_surface_t *pSurface,
    const allocator_t *pAllocator,
    const const_image_view_t &source,
    usize cbRowAlignment ) noexcept
{
    if ( pSurface == nullptr ) {
        return image_surface_status_t::NULL_SURFACE;
    }
    if ( !ImageSurface_IsCanonicalEmpty( *pSurface ) ) {
        return image_surface_status_t::DESTINATION_NOT_EMPTY;
    }
    if ( !Allocator_IsValid( pAllocator ) ) {
        return image_surface_status_t::INVALID_ALLOCATOR;
    }
    if ( ImageView_Validate( source ) != image_view_status_t::OK ) {
        return image_surface_status_t::INVALID_SOURCE_VIEW;
    }

    // Allocate without clearing the complete image. Large tightly packed images
    // are overwritten in full, and padded images clear only their padding below.
    const image_surface_status_t createStatus = ImageSurface_Create(
        pSurface,
        pAllocator,
        source.desc,
        image_surface_init_t::UNINITIALIZED,
        cbRowAlignment );
    if ( createStatus != image_surface_status_t::OK ) {
        return createStatus;
    }

    const image_process_status_t processStatus = ImageProcess_Copy(
        ImageSurface_GetView( pSurface ),
        source );
    const image_surface_status_t copyStatus =
        ImageSurface_FromProcessStatus( processStatus );
    if ( copyStatus != image_surface_status_t::OK ) {
        ImageSurface_Destroy( pSurface );
    } else {
        ImageSurface_ClearRowPadding( *pSurface );
    }
    return copyStatus;
}

image_surface_status_t ImageSurface_CopyFromView(
    image_surface_t *pSurface,
    const const_image_view_t &source ) noexcept
{
    if ( pSurface == nullptr ) {
        return image_surface_status_t::NULL_SURFACE;
    }
    if ( !ImageSurface_IsValid( pSurface ) ) {
        return image_surface_status_t::INVALID_SURFACE_STATE;
    }
    const image_process_status_t processStatus = ImageProcess_Copy(
        ImageSurface_GetView( pSurface ),
        source );
    const image_surface_status_t surfaceStatus =
        ImageSurface_FromProcessStatus( processStatus );
    if ( surfaceStatus == image_surface_status_t::OK ) {
        ImageSurface_ClearRowPadding( *pSurface );
    }
    return surfaceStatus;
}

image_surface_status_t ImageSurface_Recreate(
    image_surface_t *pSurface,
    const image_desc_t &desc,
    image_surface_init_t initialization,
    usize cbRowAlignment ) noexcept
{
    if ( pSurface == nullptr ) {
        return image_surface_status_t::NULL_SURFACE;
    }
    if ( !ImageSurface_IsValid( pSurface ) ) {
        return image_surface_status_t::INVALID_SURFACE_STATE;
    }
    if ( !ImageSurface_IsInitializationValid( initialization ) ) {
        return image_surface_status_t::INVALID_INITIALIZATION;
    }

    const image_layout_result_t layoutResult =
        ImageFormat_CalculateLayout( desc, cbRowAlignment );
    if ( layoutResult.status != image_format_status_t::OK ) {
        return ImageSurface_FromFormatStatus( layoutResult.status );
    }

    // Retain a compatible allocation when it already covers the requested image.
    // This is the normal path for repeated processing and editor preview updates.
    if ( pSurface->allocation.nAlignment >= cbRowAlignment &&
         pSurface->allocation.cbSize >= layoutResult.layout.cbTotalSize ) {
        pSurface->desc = desc;
        pSurface->layout = layoutResult.layout;
        pSurface->cbRowAlignment = cbRowAlignment;
        if ( initialization == image_surface_init_t::ZEROED ) {
            Cy_MemZero(
                pSurface->allocation.pData,
                pSurface->layout.cbTotalSize );
        }
        return image_surface_status_t::OK;
    }

    // Build the replacement separately. The original allocation stays valid if
    // validation or allocation fails at any point.
    image_surface_t replacement{};
    const image_surface_status_t createStatus = ImageSurface_Create(
        &replacement,
        pSurface->allocation.pAllocator,
        desc,
        initialization,
        cbRowAlignment );
    if ( createStatus != image_surface_status_t::OK ) {
        return createStatus;
    }

    ImageSurface_Swap( pSurface, &replacement );
    return image_surface_status_t::OK;
}

void ImageSurface_Destroy( image_surface_t *pSurface ) noexcept
{
    if ( pSurface == nullptr ) {
        return;
    }

    // Descriptor corruption must not prevent safe cleanup when the allocation
    // ownership record itself still identifies a valid allocator and block.
    const bool_t bValidOwnership =
        Allocator_OwnedIsValid( &pSurface->allocation );
    CY_ASSERT_MSG(
        bValidOwnership,
        "ImageSurface_Destroy encountered invalid allocation ownership." );
    if ( !bValidOwnership ) {
        return;
    }

    Allocator_FreeOwned( &pSurface->allocation );
    pSurface->desc = {};
    pSurface->layout = {};
    pSurface->cbRowAlignment = 0u;
}

bool_t ImageSurface_IsEmpty(
    const image_surface_t *pSurface ) noexcept
{
    return pSurface != nullptr &&
           ImageSurface_IsCanonicalEmpty( *pSurface );
}

bool_t ImageSurface_IsValid(
    const image_surface_t *pSurface ) noexcept
{
    if ( pSurface == nullptr ||
         pSurface->allocation.pData == nullptr ||
         !Allocator_OwnedIsValid( &pSurface->allocation ) ) {
        return CY_FALSE;
    }

    // Recalculate the expected layout instead of trusting stored metadata. This
    // catches stale descriptors, incorrect pitches, and mismatched byte counts.
    const image_layout_result_t expected =
        ImageFormat_CalculateLayout(
            pSurface->desc,
            pSurface->cbRowAlignment );
    return expected.status == image_format_status_t::OK &&
           pSurface->allocation.nAlignment >= pSurface->cbRowAlignment &&
           ImageSurface_LayoutsEqual( pSurface->layout, expected.layout ) &&
           pSurface->allocation.cbSize >= expected.layout.cbTotalSize;
}

usize ImageSurface_GetByteSize(
    const image_surface_t *pSurface ) noexcept
{
    return ImageSurface_IsValid( pSurface )
        ? pSurface->layout.cbTotalSize
        : 0u;
}

usize ImageSurface_GetCapacity(
    const image_surface_t *pSurface ) noexcept
{
    return ImageSurface_IsValid( pSurface )
        ? pSurface->allocation.cbSize
        : 0u;
}

image_view_t ImageSurface_GetView(
    image_surface_t *pSurface ) noexcept
{
    if ( !ImageSurface_IsValid( pSurface ) ) {
        return {};
    }

    return {
        pSurface->desc,
        {
            static_cast<byte *>( pSurface->allocation.pData ),
            pSurface->layout.cbTotalSize
        },
        pSurface->layout.cbRowPitch,
        pSurface->layout.cbSlicePitch
    };
}

const_image_view_t ImageSurface_GetView(
    const image_surface_t *pSurface ) noexcept
{
    if ( !ImageSurface_IsValid( pSurface ) ) {
        return {};
    }

    return {
        pSurface->desc,
        {
            static_cast<const byte *>( pSurface->allocation.pData ),
            pSurface->layout.cbTotalSize
        },
        pSurface->layout.cbRowPitch,
        pSurface->layout.cbSlicePitch
    };
}

bool_t ImageSurface_ZeroPixels(
    image_surface_t *pSurface ) noexcept
{
    if ( !ImageSurface_IsValid( pSurface ) ) {
        return CY_FALSE;
    }

    Cy_MemZero(
        pSurface->allocation.pData,
        pSurface->layout.cbTotalSize );
    return CY_TRUE;
}

bool_t ImageSurface_Move(
    image_surface_t *pDestination,
    image_surface_t *pSource ) noexcept
{
    if ( pDestination == nullptr ||
         pSource == nullptr ||
         pDestination == pSource ||
         !ImageSurface_IsCanonicalEmpty( *pDestination ) ) {
        return CY_FALSE;
    }

    if ( ImageSurface_IsCanonicalEmpty( *pSource ) ) {
        return CY_TRUE;
    }
    if ( !ImageSurface_IsValid( pSource ) ) {
        return CY_FALSE;
    }

    if ( !Allocator_MoveOwned(
             &pDestination->allocation,
             &pSource->allocation ) ) {
        return CY_FALSE;
    }

    pDestination->desc = pSource->desc;
    pDestination->layout = pSource->layout;
    pDestination->cbRowAlignment = pSource->cbRowAlignment;
    pSource->desc = {};
    pSource->layout = {};
    pSource->cbRowAlignment = 0u;
    return CY_TRUE;
}

void ImageSurface_Swap(
    image_surface_t *pLeft,
    image_surface_t *pRight ) noexcept
{
    if ( pLeft == nullptr || pRight == nullptr || pLeft == pRight ) {
        return;
    }

    const bool_t bLeftValid =
        ImageSurface_IsCanonicalEmpty( *pLeft ) ||
        ImageSurface_IsValid( pLeft );
    const bool_t bRightValid =
        ImageSurface_IsCanonicalEmpty( *pRight ) ||
        ImageSurface_IsValid( pRight );
    CY_ASSERT_MSG(
        bLeftValid && bRightValid,
        "ImageSurface_Swap requires empty or valid surfaces." );
    if ( !bLeftValid || !bRightValid ) {
        return;
    }

    const image_desc_t desc = pLeft->desc;
    const image_layout_t layout = pLeft->layout;
    const usize cbRowAlignment = pLeft->cbRowAlignment;
    pLeft->desc = pRight->desc;
    pLeft->layout = pRight->layout;
    pLeft->cbRowAlignment = pRight->cbRowAlignment;
    pRight->desc = desc;
    pRight->layout = layout;
    pRight->cbRowAlignment = cbRowAlignment;
    ImageSurface_SwapAllocations(
        pLeft->allocation,
        pRight->allocation );
}

const char *ImageSurface_StatusName(
    image_surface_status_t status ) noexcept
{
    // These names are part of diagnostics and tool output, so keep them stable.
    switch ( status ) {
        case image_surface_status_t::OK:                     return "OK";
        case image_surface_status_t::NULL_SURFACE:           return "NULL_SURFACE";
        case image_surface_status_t::DESTINATION_NOT_EMPTY:  return "DESTINATION_NOT_EMPTY";
        case image_surface_status_t::INVALID_ALLOCATOR:      return "INVALID_ALLOCATOR";
        case image_surface_status_t::INVALID_DESCRIPTOR:     return "INVALID_DESCRIPTOR";
        case image_surface_status_t::INVALID_ROW_ALIGNMENT:  return "INVALID_ROW_ALIGNMENT";
        case image_surface_status_t::INVALID_INITIALIZATION: return "INVALID_INITIALIZATION";
        case image_surface_status_t::ARITHMETIC_OVERFLOW:    return "ARITHMETIC_OVERFLOW";
        case image_surface_status_t::INVALID_SOURCE_VIEW:    return "INVALID_SOURCE_VIEW";
        case image_surface_status_t::DESCRIPTOR_MISMATCH:    return "DESCRIPTOR_MISMATCH";
        case image_surface_status_t::OVERLAPPING_SOURCE:     return "OVERLAPPING_SOURCE";
        case image_surface_status_t::ALLOCATION_FAILED:      return "ALLOCATION_FAILED";
        case image_surface_status_t::INVALID_SURFACE_STATE:  return "INVALID_SURFACE_STATE";
        default:                                              return "UNKNOWN_IMAGE_SURFACE_STATUS";
    }
}

} // namespace cypher::common
