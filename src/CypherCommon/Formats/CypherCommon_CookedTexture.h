//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedTexture.h
//  Purpose: Declares the backend-neutral cooked texture resource contract.
//  Details: Version 2 describes target-ready storage, explicit alpha semantics,
//           canonical texture subresources, and an independently hashed payload
//           chunk for every mip/layer/face/frame entry. Version 1 remains readable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//  - Extended to the CYTX version 2 subresource contract on 2026-09-17
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
    Cy_MakeFourCC( 'T', 'X', 'M', 'D' ); // Texture descriptor and subresources.
inline constexpr fourcc_t CY_COOKED_TEXTURE_DATA_CHUNK =
    Cy_MakeFourCC( 'T', 'X', 'D', 'T' ); // One payload per texture subresource.
inline constexpr fourcc_t CY_COOKED_TEXTURE_METADATA_MAGIC =
    Cy_MakeFourCC( 'C', 'T', 'E', 'X' ); // Signature inside the metadata chunk.

// Compatibility versions live beside their payload layouts. The shared current
// render-resource identity must advance in lockstep with the writer below.
inline constexpr format_version_t CY_COOKED_TEXTURE_RESOURCE_VERSION_V1 = 1u;
inline constexpr format_version_t CY_COOKED_TEXTURE_RESOURCE_VERSION_V2 = 2u;
inline constexpr format_version_t CY_COOKED_TEXTURE_RESOURCE_VERSION_CURRENT =
    CY_COOKED_TEXTURE_RESOURCE_VERSION_V2;
static_assert(
    CY_RENDER_TEXTURE_RESOURCE_VERSION ==
        CY_COOKED_TEXTURE_RESOURCE_VERSION_CURRENT,
    "CYTX writer and shared render-resource versions must match" );

inline constexpr format_version_t CY_COOKED_TEXTURE_METADATA_VERSION_V1 = 1u;
inline constexpr format_version_t CY_COOKED_TEXTURE_METADATA_VERSION_V2 = 2u;
inline constexpr format_version_t CY_COOKED_TEXTURE_METADATA_VERSION =
    CY_COOKED_TEXTURE_METADATA_VERSION_V2;
inline constexpr usize CY_COOKED_TEXTURE_METADATA_HEADER_SIZE_V1 = 64u;
inline constexpr usize CY_COOKED_TEXTURE_MIP_RECORD_SIZE_V1 = 32u;
inline constexpr usize CY_COOKED_TEXTURE_METADATA_HEADER_SIZE = 128u;
inline constexpr usize CY_COOKED_TEXTURE_SUBRESOURCE_RECORD_SIZE = 64u;
inline constexpr u32 CY_COOKED_TEXTURE_MAX_DIMENSION = 16384u;
inline constexpr u32 CY_COOKED_TEXTURE_MAX_MIP_LEVELS = 15u;
inline constexpr u32 CY_COOKED_TEXTURE_MAX_LAYERS = 256u;
inline constexpr u32 CY_COOKED_TEXTURE_MAX_FRAMES = 256u;
inline constexpr u32 CY_COOKED_TEXTURE_CUBE_FACE_COUNT = 6u;
inline constexpr u32 CY_COOKED_TEXTURE_MAX_SUBRESOURCES = 1024u;
inline constexpr u64 CY_COOKED_TEXTURE_MAX_DATA_SIZE = 512u * CY_MIB;
// Explicit aggregate spelling used by the v2 compiler and its capacity tests.
// Keep the original public name as the serialized reader's compatibility API.
inline constexpr u64 CY_COOKED_TEXTURE_MAX_TOTAL_DATA_SIZE =
    CY_COOKED_TEXTURE_MAX_DATA_SIZE;
inline constexpr u32 CY_COOKED_TEXTURE_METADATA_ALIGNMENT = 8u;
inline constexpr u32 CY_COOKED_TEXTURE_DATA_ALIGNMENT = 16u;

// Numeric values in these enums are serialized and therefore permanent.
// TEXTURE_2D retains its version 1 value.
enum class render_texture_dimension_t : u32 {
    TEXTURE_2D = 1u,
    TEXTURE_1D = 2u,
    TEXTURE_3D = 3u,
    TEXTURE_CUBE = 4u
};

// Legacy source/API names retained for existing version 1 cooker clients. Version 2
// serializes render_format_t as storageFormat and maps these three formats exactly.
enum class render_texture_pixel_format_t : u32 {
    UNKNOWN = 0u,
    RGBA8_UNORM = 1u,
    RGBA8_SRGB = 2u,
    RGBA32_FLOAT = 3u
};

enum class cooked_texture_alpha_mode_t : u32 {
    AUTO = 0u,          // Writer input only; resolved before serialization.
    NONE = 1u,          // No meaningful alpha channel.
    STRAIGHT = 2u,      // Unassociated color alpha.
    PREMULTIPLIED = 3u, // RGB is already multiplied by alpha.
    MASK = 4u,          // Coverage/cutout alpha.
    DATA = 5u           // Alpha stores an independent data channel.
};

enum class cooked_texture_target_t : u32 {
    PORTABLE = 1u, // Backend-neutral fallback or development payload.
    DESKTOP = 2u,  // Desktop target profile, commonly BC-capable.
    APPLE = 3u,    // Apple target profile.
    MOBILE = 4u,   // Mobile target profile.
    WEB = 5u       // Browser/WebGPU target profile.
};

enum class cooked_texture_residency_t : u32 {
    FULLY_RESIDENT = 1u, // Every subresource is required at publication time.
    MIP_STREAMED = 2u    // Coarse tail is resident; sharper mips may stream later.
};

enum cooked_texture_flags_t : flags32_t {
    COOKED_TEXTURE_FLAG_NONE = 0u,
    COOKED_TEXTURE_FLAG_GENERATED_MIPS = CYPHER_BIT32( 0 )
};

// A storage unit may be one texel or one compressed texel block. CYTX does not
// perform compression; it validates already encoded payloads using this layout.
struct cooked_texture_format_layout_t {
    u32 nBlockWidth{ 0u };
    u32 nBlockHeight{ 0u };
    u32 nBlockDepth{ 0u };
    u32 cbBlock{ 0u };
    bool_t bBlockCompressed{ CY_FALSE };
    bool_t bSrgb{ CY_FALSE };
    bool_t bHasAlpha{ CY_FALSE };
};

struct cooked_texture_desc_t {
    // Version 1 API fields remain first so existing callers keep their defaults.
    render_texture_dimension_t dimension{
        render_texture_dimension_t::TEXTURE_2D
    };
    render_texture_pixel_format_t pixelFormat{
        render_texture_pixel_format_t::RGBA8_SRGB
    }; // Legacy mirror of storageFormat when representable.
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

    // Version 2 fields. Zero counts are accepted as writer-side auto values and
    // are always resolved to explicit values in serialized metadata and views.
    render_format_t storageFormat{ render_format_t::UNKNOWN };
    cooked_texture_alpha_mode_t alphaMode{ cooked_texture_alpha_mode_t::AUTO };
    f32 alphaCutoff{ 0.5f }; // Coverage threshold; meaningful for MASK alpha.
    cooked_texture_target_t target{ cooked_texture_target_t::PORTABLE };
    cooked_texture_residency_t residency{
        cooked_texture_residency_t::FULLY_RESIDENT
    };
    u32 nFrames{ 1u };
    u32 nSubresources{ 0u };
    u32 nResidentMipLevels{ 0u };
    u32 nStreamingPriority{ 0u }; // Stable 0..255 scheduling hint.
    u32 iResidentFirstSubresource{ 0u }; // Derived canonical tail index.
    u32 nResidentSubresources{ 0u };     // Derived canonical tail count.
    u64 cbResidentData{ 0u };            // Derived encoded tail payload bytes.
    u64 cbData{ 0u };                    // Derived encoded total payload bytes.
};

// Version 1 metadata record retained by the compatibility reader and legacy API.
struct cooked_texture_mip_desc_t {
    u32 nLevel{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    u32 iDataChunk{ 0u };
    u64 cbData{ 0u };
};

// Version 2 canonical order is mip, frame, layer, face. This keeps all coarse
// resident-tail levels contiguous at the end of the CYTX payload.
struct cooked_texture_subresource_desc_t {
    u32 nMipLevel{ 0u };
    u32 nFrame{ 0u };
    u32 nLayer{ 0u };
    u32 nFace{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    u32 cbSlicePitch{ 0u };
    u32 iDataChunk{ 0u };
    flags32_t flags{ 0u }; // Reserved; version 2 requires zero.
    u32 nReserved{ 0u };
    u64 cbData{ 0u };
    u64 nReserved64{ 0u };
};

// Existing 2D cooker entry point. Pixels are already in canonical storage format.
struct cooked_texture_mip_source_t {
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    binary_block_t pixels{};
};

struct cooked_texture_subresource_source_t {
    u32 nMipLevel{ 0u };
    u32 nFrame{ 0u };
    u32 nLayer{ 0u };
    u32 nFace{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    u32 cbSlicePitch{ 0u };
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

struct cooked_texture_subresource_view_t {
    u32 nMipLevel{ 0u };
    u32 nFrame{ 0u };
    u32 nLayer{ 0u };
    u32 nFace{ 0u };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 nDepth{ 1u };
    u32 cbRowPitch{ 0u };
    u32 cbSlicePitch{ 0u };
    binary_block_t pixels{};
    content_hash_t contentHash{};
};

struct cooked_texture_view_t {
    cooked_texture_desc_t desc{};
    content_hash_t sourceHash{};
    cooked_texture_mip_view_t mips[CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    u32 nMipLevels{ 0u };
    u32 nSubresources{ 0u };
    format_version_t nResourceVersion{ 0u };
    binary_block_t fileBytes{};     // Borrowed complete CYTX file.
    binary_block_t metadataBytes{}; // Borrowed validated TXMD payload.
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
    NON_CANONICAL_LAYOUT,
    INVALID_ALPHA_MODE,
    INVALID_TARGET,
    INVALID_RESIDENCY,
    SUBRESOURCE_LIMIT_EXCEEDED,
    INVALID_SUBRESOURCE_LAYOUT
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
    usize iSubresource{ CY_INVALID_SIZE };
    usize iChunk{ CY_INVALID_SIZE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedTexture_BytesPerPixel(
    render_texture_pixel_format_t pixelFormat ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedTexture_FormatLayout(
    render_format_t storageFormat,
    cooked_texture_format_layout_t *pLayoutOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedTexture_FullMipCount(
    u32 nWidth,
    u32 nHeight,
    u32 nDepth = 1u ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
u32 CookedTexture_SubresourceCount(
    const cooked_texture_desc_t &texture ) noexcept;

// Returns the version 2 TXMD size for an explicit subresource count.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedTexture_MetadataSize( u32 nSubresources ) noexcept;

// Backward-compatible 2D writer API. It emits CYTX version 2.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedTexture_RequiredSize(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_mip_source_t> mips ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedTexture_RequiredSizeSubresources(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_source_t> subresources ) noexcept;

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
cooked_texture_result_t CookedTexture_WriteSubresources(
    const cooked_texture_desc_t &texture,
    span_t<const cooked_texture_subresource_source_t> subresources,
    content_hash_t sourceHash,
    byte_span_t output ) noexcept;

// Reads both CYTX version 1 and version 2 and publishes only after full validation.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_texture_result_t CookedTexture_Read(
    binary_block_t input,
    cooked_texture_view_t *pTextureOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const cooked_texture_mip_view_t *CookedTexture_FindMip(
    const cooked_texture_view_t &texture,
    u32 nLevel ) noexcept;

// Retrieves any version 2 mip/frame/layer/face entry without storing a large
// fixed subresource table in every runtime texture view. Version 1 supports only
// frame/layer/face zero.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedTexture_GetSubresource(
    const cooked_texture_view_t &texture,
    u32 nMipLevel,
    u32 nFrame,
    u32 nLayer,
    u32 nFace,
    cooked_texture_subresource_view_t *pSubresourceOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedTexture_Succeeded(
    const cooked_texture_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CookedTexture_StatusName(
    cooked_texture_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_COOKEDTEXTURE_H
