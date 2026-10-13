//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Owns immutable RGBA8 two-dimensional sampled images.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_TEXTURE_H
#define CYPHER_ENGINE_RENDER_TEXTURE_H
#pragma once

#include "CypherRender_Error.h"
#include "CypherRender_Types.h"
#include "CypherCommon/Formats/CypherCommon_CookedTexture.h"

namespace cypher::engine::render
{
using render_texture_handle_t = render_handle_t;
inline constexpr render_texture_handle_t R_INVALID_TEXTURE{};

// One-layer, one-face sampled images. D24 and RGBA16F are GPU-only offscreen
// target storage; RGBA32F is immutable uploaded linear color/data. Targets may
// be sampled after their pass ends.
enum class render_texture_format_t : ::cypher::common::u8 {
    RGBA8 = 0,
    D24,
    RGBA16F,
    RGBA32F,
    COUNT
};

// These immutable sampling defaults apply when a draw has no explicit sampler.
struct render_texture_desc_t {
    ::cypher::common::u32 width{ 0u };
    ::cypher::common::u32 height{ 0u };
    bool sRGB{ true };
    bool generateMips{ true };
    bool repeat{ true };
    const char *debugName{ nullptr }; // Borrowed only during creation.
    bool renderTarget{ false }; // GPU-only attachment; also sampleable after its pass.
    render_texture_format_t format{ render_texture_format_t::RGBA8 };
};
struct render_texture_data_t {
    const void *bytes{ nullptr };
    ::cypher::common::u64 byteSize{ 0u }; // RGBA8: 4; RGBA32F: 16 bytes/texel; targets: zero.
};
struct render_texture_info_t {
    ::cypher::common::u32 width{ 0u };
    ::cypher::common::u32 height{ 0u };
    bool sRGB{ true };
    bool generateMips{ true };
    bool repeat{ true };
    ::cypher::common::u32 mipLevels{ 0u };
    bool renderTarget{ false };
    render_texture_format_t format{ render_texture_format_t::RGBA8 };
    bool completeMipChain{ false }; // Generated or authored levels reach 1x1; usable with mip samplers.
};

// Calls run on the renderer thread. Creation consumes all supplied pixels before
// returning. A renderTarget texture has no initial pixels (bytes=nullptr,
// byteSize=0), is linear, single-level and clamp-to-edge; its contents become
// defined by a target pass. D24 and RGBA16F can only be renderTarget textures;
// RGBA32F is sampled-only and linear. Draw bindings are immediate.
CYPHER_NODISCARD render_error_t R_CreateTexture2D(
    const render_texture_desc_t &description,
    const render_texture_data_t &data,
    CY_OUT render_texture_handle_t *textureOut ) noexcept;
// Revalidates an owned CYTX view and synchronously uploads every authored mip.
// This adoption path accepts only fully resident, tightly packed RGBA8 or
// RGBA32F 2D color textures with one frame/layer/face and either one level or
// a complete authored mip chain. Float CYTX payloads require a little-endian
// host. The caller must keep the cooked file bytes alive and unchanged until
// the call returns; the renderer retains neither view nor borrowed pixels.
CYPHER_NODISCARD render_error_t R_CreateTexture2DFromCooked(
    const ::cypher::common::cooked_texture_view_t &cooked,
    CY_OUT render_texture_handle_t *textureOut ) noexcept;
CYPHER_NODISCARD render_error_t R_DestroyTexture( render_texture_handle_t texture ) noexcept;
CYPHER_NODISCARD bool R_IsTextureValid( render_texture_handle_t texture ) noexcept;
CYPHER_NODISCARD render_error_t R_GetTextureInfo(
    render_texture_handle_t texture, CY_OUT render_texture_info_t *infoOut ) noexcept;
} // namespace cypher::engine::render
#endif // CYPHER_ENGINE_RENDER_TEXTURE_H
