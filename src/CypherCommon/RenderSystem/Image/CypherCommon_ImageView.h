//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageView.h
//  Purpose: Declares validated non-owning access to uncompressed image pixels.
//  Details: Image views preserve row and slice pitch, expose bounded row and
//           pixel ranges, and never allocate, copy, decode, or own memory.
//
//  History:
//  - Created by Karlo Siric on 2026-08-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGEVIEW_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGEVIEW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ImageFormat.h"

namespace cypher::common
{

// Reports why borrowed image memory cannot safely represent its descriptor.
// Descriptor-specific details remain available through ImageFormat_ValidateDesc.
enum class image_view_status_t : u8 {
    OK = 0u,              // Descriptor and borrowed storage agree.
    INVALID_DESCRIPTOR,  // Image metadata is structurally invalid.
    NULL_PIXEL_DATA,     // Non-empty image has no backing pointer.
    ROW_PITCH_TOO_SMALL, // One row cannot contain every logical pixel.
    SLICE_PITCH_TOO_SMALL,// One slice cannot contain every padded row.
    BUFFER_TOO_SMALL,    // Borrowed range cannot contain every padded slice.
    ARITHMETIC_OVERFLOW  // Required byte count cannot be represented.
};

// Validates descriptor metadata, pitches, pixel storage, and all size arithmetic.
// Validation never reads the pixel contents and never takes ownership.
CYPHER_NODISCARD CYPHER_COMMON_API
image_view_status_t ImageView_Validate(
    const const_image_view_t &view ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
image_view_status_t ImageView_Validate(
    const image_view_t &view ) noexcept;

// Convenience checks for API boundaries where only validity is needed.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageView_IsValid(
    const const_image_view_t &view ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ImageView_IsValid(
    const image_view_t &view ) noexcept;

// Produces an immutable view over the same borrowed memory. No pixels are copied.
CYPHER_NODISCARD CYPHER_COMMON_API
const_image_view_t ImageView_AsConst(
    const image_view_t &view ) noexcept;

// Returns the logical pixel bytes for one row, excluding alignment padding.
// Invalid views or out-of-range coordinates return an empty block/span.
CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t ImageView_GetRow(
    const const_image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t ImageView_GetRow(
    const image_view_t &view,
    u32 iRow,
    u32 iSlice ) noexcept;

// Pixel access is intended for tools and isolated operations. Hot image loops
// should acquire a row once and advance through it directly.
CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t ImageView_GetPixel(
    const const_image_view_t &view,
    u32 iColumn,
    u32 iRow,
    u32 iSlice ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t ImageView_GetPixel(
    const image_view_t &view,
    u32 iColumn,
    u32 iRow,
    u32 iSlice ) noexcept;

// Returns a stable diagnostic name suitable for logs, tests, and editor output.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageView_StatusName(
    image_view_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGEVIEW_H
