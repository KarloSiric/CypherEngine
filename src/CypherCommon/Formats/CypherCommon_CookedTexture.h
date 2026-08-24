//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedTexture.h
//  Purpose: Declares the backend-neutral cooked texture resource contract.
//  Details: Version 1 stores one tightly packed 2D image and an optional full
//           mip chain. Runtime views borrow immutable file bytes; image import,
//           GPU upload, and native graphics objects remain outside Common.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_FORMATS_COOKEDTEXTURE_H
#define CYPHER_COMMON_FORMATS_COOKEDTEXTURE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CookedResource.h"
#include "CypherCommon_RenderAsset.h"
#include "CypherCommon_RenderFormat.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_COOKED_TEXTURE_METADATA_CHUNK =
    Cy_MakeFourCC( 'T', 'X', 'M', 'D' ); // Texture descriptor and mip records.
inline constexpr fourcc_t CY_COOKED_TEXTURE_DATA_CHUNK =
    Cy_MakeFourCC( 'T', 'X', 'D', 'T' ); // One canonical pixel payload per mip.
inline constexpr fourcc_t CY_COOKED_TEXTURE_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'T', 'E', 'X' ); // Signature inside the metadata chunk.

inline constexpr format_version_t CY_COOKED_TEXTURE_METADATA_VERSION = 1u; // TXMD layout.
inline constexpr usize CY_COOKED_TEXTURE_METADATA_HEADER_SIZE = 64u; // Fixed header bytes.
inline constexpr usize CY_COOKED_TEXTURE_MIP_RECORD_SIZE = 32u; // Serialized mip record.
inline constexpr u32 CY_COOKED_TEXTURE_MAX_DIMENSION = 16384u; // V1 per-axis limit.
inline constexpr u32 CY_COOKED_TEXTURE_MAX_MIP_LEVELS = 15u; // 16K down to 1x1.
inline constexpr u64 CY_COOKED_TEXTURE_MAX_DATA_SIZE = 512u * CY_MIB; // All mip bytes.
inline constexpr u32 CY_COOKED_TEXTURE_METADATA_ALIGNMENT = 8u; // CYRS TXMD alignment.
inline constexpr u32 CY_COOKED_TEXTURE_DATA_ALIGNMENT = 16u; // Pixel chunk alignment.

// Numeric values in these enums are serialized and therefore versioned.
enum class render_texture_dimension_t : u32 {
    TEXTURE_2D = 1u // V1 supports ordinary two-dimensional textures only.
};

enum class render_texture_pixel_format_t : u32 {
    RGBA8_UNORM = 1u, // Four linear normalized 8-bit channels.
    RGBA8_SRGB = 2u,  // sRGB RGB channels with linear alpha.
    RGBA32_FLOAT = 3u // Four little-endian IEEE-754 float channels.
};

enum cooked_texture_flags_t : flags32_t {
    COOKED_TEXTURE_FLAG_NONE = 0u, // No optional texture features.
    COOKED_TEXTURE_FLAG_GENERATED_MIPS = CYPHER_BIT32( 0 ) // Full chain is required.
};

struct cooked_texture_desc_t {
    render_texture_dimension_t dimension{
        render_texture_dimension_t::TEXTURE_2D
    };
    render_texture_pixel_format_t pixelFormat{
        render_texture_pixel_format_t::RGBA8_SRGB
    };
    render_texture_usage_t usage{ render_texture_usage_t::COLOR };
    render_texture_color_space_t colorSpace{
        render_texture_color_space_t::SRGB
    };
    flags32_t flags{ COOKED_TEXTURE_FLAG_NONE }; // cooked_texture_flags_t bits.
    u32 nWidth{ 0u };       // Base-level texel width.
    u32 nHeight{ 0u };      // Base-level texel height.
    u32 nDepth{ 1u };       // Reserved for future 3D textures; V1 requires one.
    u32 nLayers{ 1u };      // Reserved for arrays; V1 requires one.
    u32 nFaces{ 1u };       // Reserved for cube maps; V1 requires one.
    u32 nMipLevels{ 0u };   // Number of serialized mip records and data chunks.
};

struct cooked_texture_mip_desc_t {
    u32 nLevel{ 0u };      // Zero-based mip level.
    u32 nWidth{ 0u };      // Level width after repeated halving.
    u32 nHeight{ 0u };     // Level height after repeated halving.
    u32 nDepth{ 1u };      // Level depth; one for V1 textures.
    u32 cbRowPitch{ 0u };  // Tightly packed bytes in one pixel row.
    u32 iDataChunk{ 0u };  // CYRS chunk-table index for this level.
    u64 cbData{ 0u };      // Exact uncompressed pixel payload size.
};

// Pixel bytes supplied to the writer are already in canonical format. Multi-byte
// channel values use little-endian storage, matching the CYRS container contract.
struct cooked_texture_mip_source_t {
    u32 nWidth{ 0u };      // Supplied mip width.
    u32 nHeight{ 0u };     // Supplied mip height.
    u32 nDepth{ 1u };      // Supplied mip depth.
    u32 cbRowPitch{ 0u };  // Supplied tightly packed row size.
    binary_block_t pixels{}; // Borrowed canonical bytes copied by the writer.
};

struct cooked_texture_mip_view_t {
    u32 nLevel{ 0u };      // Zero-based mip level.
    u32 nWidth{ 0u };      // Borrowed level width.
    u32 nHeight{ 0u };     // Borrowed level height.
    u32 nDepth{ 1u };      // Borrowed level depth.
    u32 cbRowPitch{ 0u };  // Bytes in one row.
    binary_block_t pixels{}; // Immutable view into the source CYRS file.
    content_hash_t contentHash{}; // Verified hash of pixels.
};

struct cooked_texture_view_t {
    cooked_texture_desc_t desc{}; // Decoded and validated texture metadata.
    content_hash_t sourceHash{};  // Optional authored-source identity.
    cooked_texture_mip_view_t mips[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    u32 nMipLevels{ 0u }; // Active entries in mips.
};

enum class cooked_texture_status_t : u8 {
    OK = 0u,               // Texture operation completed.
    INVALID_ARGUMENT,     // Input, span, output, or aliasing contract is invalid.
    OUTPUT_TOO_SMALL,     // Destination cannot hold canonical output.
    RESOURCE_ERROR,       // Underlying CYRS validation or writing failed.
    INVALID_RESOURCE_TYPE,// CYRS payload is not a cooked texture.
    VERSION_MISMATCH,     // Cooked texture resource version is unsupported.
    INVALID_CHUNK_COUNT,  // Mip count and CYRS chunk count disagree.
    INVALID_METADATA_CHUNK,// TXMD descriptor violates the format contract.
    INVALID_METADATA,     // TXMD header or mip records are malformed.
    INVALID_DIMENSION,    // Texture dimensionality is unsupported.
    INVALID_PIXEL_FORMAT, // Pixel encoding is unsupported.
    INVALID_USAGE,        // Color, normal, or data usage enum is invalid.
    INVALID_COLOR_SPACE,  // sRGB/linear enum is invalid.
    INVALID_COMBINATION,  // Usage, color space, and pixel format conflict.
    INVALID_FLAGS,        // Unknown persisted flag bits are set.
    INVALID_EXTENT,       // Width, height, depth, layer, or face count is invalid.
    MIP_LIMIT_EXCEEDED,   // Mip count is zero or outside format limits.
    INVALID_MIP_CHAIN,    // Mip dimensions, pitches, sizes, or order disagree.
    INVALID_DATA_CHUNK,   // TXDT descriptor does not match its mip record.
    INVALID_DATA,         // Supplied pixel block is missing or has the wrong size.
    CONTENT_HASH_MISMATCH,// Metadata or pixel payload hash failed.
    NON_CANONICAL_LAYOUT  // Chunks, padding, or offsets are not deterministic.
};

struct cooked_texture_result_t {
    cooked_texture_status_t status{ cooked_texture_status_t::OK }; // Texture-layer result.
    cooked_resource_status_t resourceStatus{
        cooked_resource_status_t::OK
    }; // Underlying CYRS error when status is RESOURCE_ERROR.
    usize cbRead{ 0u };                 // Validated input bytes.
    usize cbWritten{ 0u };              // Published output bytes.
    usize cbRequired{ 0u };             // Exact destination requirement.
    usize iMip{ CY_INVALID_SIZE };      // First offending mip, when known.
    usize iChunk{ CY_INVALID_SIZE };    // First offending CYRS chunk, when known.
};

CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedTexture_BytesPerPixel(
    render_texture_pixel_format_t pixelFormat ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedTexture_FullMipCount(
    u32 nWidth,
    u32 nHeight,
    u32 nDepth = 1u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedTexture_MetadataSize( u32 nMipLevels ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedTexture_RequiredSize(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
cooked_texture_result_t CookedTexture_WriteMetadata(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_desc_t> mips,
    byte_span_t output ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
cooked_texture_result_t CookedTexture_Write(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
cooked_texture_result_t CookedTexture_Read(
    binary_block_t input,
    cooked_texture_view_t *pTextureOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_texture_mip_view_t *CookedTexture_FindMip(
    const cooked_texture_view_t &texture,
    u32 nLevel ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedTexture_Succeeded(
    const cooked_texture_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CookedTexture_StatusName(
    cooked_texture_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_COOKEDTEXTURE_H
