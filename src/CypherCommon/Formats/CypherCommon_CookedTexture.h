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
    Cy_MakeFourCC( 'T', 'X', 'M', 'D' );
inline constexpr fourcc_t CY_COOKED_TEXTURE_DATA_CHUNK =
    Cy_MakeFourCC( 'T', 'X', 'D', 'T' );
inline constexpr fourcc_t CY_COOKED_TEXTURE_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'T', 'E', 'X' );

inline constexpr format_version_t CY_COOKED_TEXTURE_METADATA_VERSION = 1u;
inline constexpr usize CY_COOKED_TEXTURE_METADATA_HEADER_SIZE = 64u;
inline constexpr usize CY_COOKED_TEXTURE_MIP_RECORD_SIZE = 32u;
inline constexpr u32 CY_COOKED_TEXTURE_MAX_DIMENSION = 16384u;
inline constexpr u32 CY_COOKED_TEXTURE_MAX_MIP_LEVELS = 15u;
inline constexpr u64 CY_COOKED_TEXTURE_MAX_DATA_SIZE = 512u * CY_MIB;
inline constexpr u32 CY_COOKED_TEXTURE_METADATA_ALIGNMENT = 8u;
inline constexpr u32 CY_COOKED_TEXTURE_DATA_ALIGNMENT = 16u;

// Numeric values in these enums are serialized and therefore versioned.
enum class render_texture_dimension_t : u32 {
    TEXTURE_2D = 1u
};

enum class render_texture_pixel_format_t : u32 {
    RGBA8_UNORM = 1u,
    RGBA8_SRGB = 2u,
    RGBA32_FLOAT = 3u
};

enum cooked_texture_flags_t : flags32_t {
    COOKED_TEXTURE_FLAG_NONE = 0u,
    COOKED_TEXTURE_FLAG_GENERATED_MIPS = CYPHER_BIT32( 0 )
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
    flags32_t flags{ COOKED_TEXTURE_FLAG_NONE };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 nLayers{ 1u };
    u32 nFaces{ 1u };
    u32 nMipLevels{ 0u };
};

struct cooked_texture_mip_desc_t {
    u32 nLevel{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    u32 iDataChunk{ 0u };
    u64 cbData{ 0u };
};

// Pixel bytes supplied to the writer are already in canonical format. Multi-byte
// channel values use little-endian storage, matching the CYRS container contract.
struct cooked_texture_mip_source_t {
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    binary_block_t pixels{};
};

struct cooked_texture_mip_view_t {
    u32 nLevel{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    binary_block_t pixels{};
    content_hash_t contentHash{};
};

struct cooked_texture_view_t {
    cooked_texture_desc_t desc{};
    content_hash_t sourceHash{};
    cooked_texture_mip_view_t mips[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    u32 nMipLevels{ 0u };
};

enum class cooked_texture_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUTPUT_TOO_SMALL,
    RESOURCE_ERROR,
    INVALID_RESOURCE_TYPE,
    VERSION_MISMATCH,
    INVALID_CHUNK_COUNT,
    INVALID_METADATA_CHUNK,
    INVALID_METADATA,
    INVALID_DIMENSION,
    INVALID_PIXEL_FORMAT,
    INVALID_USAGE,
    INVALID_COLOR_SPACE,
    INVALID_COMBINATION,
    INVALID_FLAGS,
    INVALID_EXTENT,
    MIP_LIMIT_EXCEEDED,
    INVALID_MIP_CHAIN,
    INVALID_DATA_CHUNK,
    INVALID_DATA,
    CONTENT_HASH_MISMATCH,
    NON_CANONICAL_LAYOUT
};

struct cooked_texture_result_t {
    cooked_texture_status_t status{ cooked_texture_status_t::OK };
    cooked_resource_status_t resourceStatus{
        cooked_resource_status_t::OK
    };
    usize cbRead{ 0u };
    usize cbWritten{ 0u };
    usize cbRequired{ 0u };
    usize iMip{ CY_INVALID_SIZE };
    usize iChunk{ CY_INVALID_SIZE };
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
