//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageSurface.h
//  Purpose: Declares allocator-backed ownership of uncompressed image pixels.
//  Details: An image surface owns one pixel allocation and exposes borrowed views
//           for codecs, image processing, texture cooking, and editor tools.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGESURFACE_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGESURFACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_ImageView.h"

namespace cypher::common
{

// Controls the initial contents of a newly allocated pixel buffer.
enum class image_surface_init_t : u8 {
    UNINITIALIZED = 0u, // Leaves bytes untouched when every pixel will be replaced.
    ZEROED              // Clears pixels and padding for deterministic initial data.
};

// Reports why an owning image operation could not be completed.
enum class image_surface_status_t : u8 {
    OK = 0u,                 // The requested operation completed successfully.
    NULL_SURFACE,            // A required surface pointer was null.
    DESTINATION_NOT_EMPTY,   // Creation would overwrite an existing allocation.
    INVALID_ALLOCATOR,       // The supplied allocator cannot allocate and free memory.
    INVALID_DESCRIPTOR,      // Dimensions, format, color space, or alpha mode are invalid.
    INVALID_ROW_ALIGNMENT,   // Row alignment is zero or not a power of two.
    INVALID_INITIALIZATION,  // The initialization policy is not a recognized value.
    ARITHMETIC_OVERFLOW,     // Calculating the required allocation size overflowed.
    INVALID_SOURCE_VIEW,     // Source pixels do not form a valid readable image view.
    DESCRIPTOR_MISMATCH,     // Source and destination describe different pixels.
    OVERLAPPING_SOURCE,      // A distinct source aliases destination storage.
    ALLOCATION_FAILED,       // The allocator could not provide the required memory.
    INVALID_SURFACE_STATE    // Stored descriptor, layout, and allocation disagree.
};

// Owns one uncompressed image allocation.
//
// The structure is intentionally non-copyable because copying its allocation
// accidentally would create two owners of the same memory. Explicit clone and
// move operations define when ownership or pixel data changes hands.
struct image_surface_t {
    image_surface_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( image_surface_t );
    ~image_surface_t() noexcept;

    // Describes the dimensions and interpretation of the stored pixels.
    image_desc_t desc{}; // Dimensions and semantic interpretation of active pixels.

    // Records physical row, slice, and total allocation sizes.
    image_layout_t layout{}; // Active row, slice, and total byte layout.

    // Alignment used to calculate row pitch. Allocation alignment is stored
    // separately because retained storage can satisfy several row layouts.
    usize cbRowAlignment{ 0u }; // Power-of-two pitch alignment used by this image.

    // Owns the pixel pointer and remembers its reusable allocation capacity.
    // allocation.cbSize may exceed layout.cbTotalSize after a smaller recreation.
    owned_allocation_t allocation{}; // Pixel ownership plus reusable capacity.
};

// Creates storage inside an empty surface using the requested row alignment.
// Codecs and generators should request UNINITIALIZED storage, acquire its writable
// view, and write directly into the final allocation instead of decoding elsewhere.
CYPHER_NODISCARD CYPHER_COMMON_API
image_surface_status_t ImageSurface_Create(
    image_surface_t *pSurface,
    const allocator_t *pAllocator,
    const image_desc_t &desc,
    image_surface_init_t initialization,
    usize cbRowAlignment = 1u ) noexcept;

// Creates an owned image by copying the logical pixels from a borrowed view.
CYPHER_NODISCARD CYPHER_COMMON_API
image_surface_status_t ImageSurface_CreateFromView(
    image_surface_t *pSurface,
    const allocator_t *pAllocator,
    const const_image_view_t &source,
    usize cbRowAlignment = 1u ) noexcept;

// Copies logical pixels into existing compatible storage without allocating.
// Destination padding is cleared to keep hashing and cooked output deterministic.
CYPHER_NODISCARD CYPHER_COMMON_API
image_surface_status_t ImageSurface_CopyFromView(
    image_surface_t *pSurface,
    const const_image_view_t &source ) noexcept;

// Replaces a live surface transactionally. Compatible capacity is reused; when
// growth or alignment requires allocation, failure leaves the old image intact.
CYPHER_NODISCARD CYPHER_COMMON_API
image_surface_status_t ImageSurface_Recreate(
    image_surface_t *pSurface,
    const image_desc_t &desc,
    image_surface_init_t initialization,
    usize cbRowAlignment = 1u ) noexcept;

// Releases owned pixels and restores the canonical empty state.
CYPHER_COMMON_API void ImageSurface_Destroy(
    image_surface_t *pSurface ) noexcept;

// Queries whether the surface is empty or contains a structurally valid image.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageSurface_IsEmpty( const image_surface_t *pSurface ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageSurface_IsValid( const image_surface_t *pSurface ) noexcept;

// Returns active image bytes and retained allocation capacity respectively.
// Empty or invalid surfaces report zero.
CYPHER_NODISCARD CYPHER_COMMON_API
usize ImageSurface_GetByteSize( const image_surface_t *pSurface ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ImageSurface_GetCapacity( const image_surface_t *pSurface ) noexcept;

// Borrows writable or read-only access without transferring ownership.
CYPHER_NODISCARD CYPHER_COMMON_API
image_view_t ImageSurface_GetView( image_surface_t *pSurface ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const_image_view_t ImageSurface_GetView(
    const image_surface_t *pSurface ) noexcept;

// Clears active image storage, including row and slice padding but excluding any
// retained capacity beyond the current layout.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageSurface_ZeroPixels( image_surface_t *pSurface ) noexcept;

// Transfers or exchanges complete surface ownership without copying pixels.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageSurface_Move(
    image_surface_t *pDestination,
    image_surface_t *pSource ) noexcept;

CYPHER_COMMON_API void ImageSurface_Swap(
    image_surface_t *pLeft,
    image_surface_t *pRight ) noexcept;

// Returns a stable name for logs, diagnostics, tests, and tool output.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageSurface_StatusName(
    image_surface_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGESURFACE_H
