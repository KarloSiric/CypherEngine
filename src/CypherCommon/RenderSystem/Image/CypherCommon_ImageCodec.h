//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/Image/CypherCommon_ImageCodec.h
//  Purpose: Declares reusable image-file decoding for Cypher authoring tools.
//  Details: Codecs normalize PNG, JPEG, TGA, and EXR files into owned image
//           surfaces. The runtime image library remains independent of file
//           formats while importers, compilers, and Picasso share one boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_IMAGECODEC_H
#define CYPHER_COMMON_RENDERSYSTEM_IMAGECODEC_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Blob.h"
#include "CypherCommon_ImageSurface.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

// Identifies an external interchange format. This is deliberately separate
// from image_pixel_format_t, which describes decoded memory rather than a file.
enum class image_file_format_t : u8 {
    UNKNOWN = 0u, // No supported signature or trusted path hint.
    PNG,          // Portable Network Graphics, decoded to RGBA8 sRGB.
    JPEG,         // JPEG image, decoded to opaque RGBA8 sRGB.
    TGA,          // Truevision TGA, decoded to RGBA8 sRGB.
    EXR           // OpenEXR, decoded to linear RGBA32 float.
};

// Reports invalid input separately from resource limits and allocation failure.
// Imported files are untrusted data, so codec failures never require assertions.
enum class image_codec_status_t : u8 {
    OK = 0u,                // Decode or encode completed and published output.
    INVALID_ARGUMENT,      // Pointer, allocator, or source block is invalid.
    INVALID_OPTIONS,       // Decode limits or alignment policy is malformed.
    EMPTY_SOURCE,          // No encoded bytes were provided.
    DESTINATION_NOT_EMPTY, // Caller attempted to overwrite owned output.
    UNKNOWN_FORMAT,        // Neither signature nor trusted hint selected a codec.
    MALFORMED_DATA,        // Encoded bytes violate the selected file format.
    UNSUPPORTED_ENCODING,  // Container is known but its variant is unsupported.
    INVALID_DIMENSIONS,    // Width, height, or depth is structurally invalid.
    SIZE_LIMIT_EXCEEDED,   // Decoded layout exceeds configured or machine limits.
    NON_FINITE_COMPONENT,  // Floating-point source contains NaN or infinity.
    ALLOCATION_FAILED,     // Destination storage could not be acquired.
    UNSUPPORTED_PIXEL_FORMAT, // Encoder cannot consume this in-memory format.
    ENCODE_FAILED             // Codec failed while producing external bytes.
};

// Bounds hostile or accidental inputs before they become large allocations.
// The defaults cover 4K authoring comfortably while allowing larger source art.
struct image_decode_options_t {
    image_file_format_t formatHint{ image_file_format_t::UNKNOWN }; // Trusted path-derived fallback.
    u32 nMaximumDimension{ 16384u };               // Per-axis allocation bound.
    usize cbMaximumDecodedSize{ 512u * CY_MIB };   // Total owned-surface bound.
    usize cbRowAlignment{ 1u };                    // Required power-of-two row alignment.
};

// Carries useful metadata without making callers inspect a partially built
// destination. Descriptor and byte count are populated only on success.
struct image_decode_result_t {
    image_codec_status_t status{ image_codec_status_t::INVALID_ARGUMENT }; // Final codec status.
    image_file_format_t sourceFormat{ image_file_format_t::UNKNOWN };      // Selected decoder.
    image_desc_t desc{};       // Published surface descriptor on success.
    usize cbDecoded{ 0u };     // Allocated pixel bytes, including row padding.
};

// Maps a path extension to a format without touching the filesystem. Matching
// is ASCII case-insensitive and accepts .jpg as an alias for JPEG.
CYPHER_NODISCARD CYPHER_COMMON_API
image_file_format_t ImageCodec_FormatFromPath(
    string_view_t path ) noexcept;

// Detects formats with reliable magic values. TGA has no dependable signature,
// so callers must provide a TGA hint obtained from the source path.
CYPHER_NODISCARD CYPHER_COMMON_API
image_file_format_t ImageCodec_DetectFormat(
    binary_block_t source,
    image_file_format_t formatHint = image_file_format_t::UNKNOWN ) noexcept;

// Decodes into a canonical empty surface. Work is performed in temporary owned
// storage, so every failure leaves pSurfaceOut empty and safe to reuse.
CYPHER_NODISCARD CYPHER_COMMON_API
image_decode_result_t ImageCodec_Decode(
    binary_block_t source,
    const allocator_t *pAllocator,
    const image_decode_options_t &options,
    image_surface_t *pSurfaceOut ) noexcept;

// Encodes an RGBA8 surface as PNG into a canonical empty blob. The operation is
// transactional: output ownership is published only after encoding succeeds.
CYPHER_NODISCARD CYPHER_COMMON_API
image_codec_status_t ImageCodec_EncodePng(
    const const_image_view_t &source,
    const allocator_t *pAllocator,
    blob_t *pEncodedOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageCodec_FormatName( image_file_format_t format ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const char *ImageCodec_StatusName( image_codec_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_IMAGECODEC_H
