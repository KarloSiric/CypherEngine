//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Declares one color-and-depth offscreen render target and its pass.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_TARGET_H
#define CYPHER_ENGINE_RENDER_TARGET_H
#pragma once

#include "CypherRender_Texture.h"

namespace cypher::engine::render
{
using render_target_handle_t = render_handle_t;
inline constexpr render_target_handle_t R_INVALID_TARGET{};

// The caller owns both attachments; the target retains them until destruction.
// A target needs at least one attachment: linear RGBA8/RGBA16F color, sampleable
// D24 depth, or both. With color alone, it owns a private D24 renderbuffer. Targets
// are single-sample and fixed-size; recreate attachments to resize.
struct render_target_desc_t {
    render_texture_handle_t color{};
    render_texture_handle_t depth{};
};

struct render_target_info_t {
    render_texture_handle_t color{};
    render_extent_t extent{};
    render_texture_handle_t depth{};
};

struct render_target_pass_desc_t {
    render_target_handle_t target{};
    // Depth-only targets must omit R_CLEAR_COLOR explicitly.
    render_clear_flags_t clearFlags{ R_CLEAR_COLOR | R_CLEAR_DEPTH };
    render_color_t clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    ::cypher::common::f32 clearDepth{ 1.0f };
};

// A rectangle of physical pixels in a target's color attachment. The origin
// is the bottom-left pixel, matching target-pass viewport coordinates.
struct render_readback_rect_t {
    ::cypher::common::u32 x{ 0u };
    ::cypher::common::u32 y{ 0u };
    ::cypher::common::u32 width{ 0u };
    ::cypher::common::u32 height{ 0u };
};

// Resource creation/destruction runs between frames. A pass begins inside an
// active frame and is non-nestable. EndTargetPass restores that frame's actual
// presentation framebuffer and viewport, including a host-owned nonzero FBO.
// Sampling either active attachment is forbidden until the pass ends.
CYPHER_NODISCARD render_error_t R_CreateRenderTarget(
    const render_target_desc_t &description,
    CY_OUT render_target_handle_t *targetOut ) noexcept;
CYPHER_NODISCARD render_error_t R_DestroyRenderTarget( render_target_handle_t target ) noexcept;
CYPHER_NODISCARD bool R_IsRenderTargetValid( render_target_handle_t target ) noexcept;
CYPHER_NODISCARD render_error_t R_GetRenderTargetInfo(
    render_target_handle_t target, CY_OUT render_target_info_t *infoOut ) noexcept;
CYPHER_NODISCARD render_error_t R_BeginTargetPass(
    const render_target_pass_desc_t &description ) noexcept;
CYPHER_NODISCARD render_error_t R_EndTargetPass() noexcept;

// Synchronously copies an RGBA8 color attachment to caller-owned storage
// while a frame is active and no target pass is open. RGBA16F color targets
// return ERR_CAPTURE_UNSUPPORTED without touching output. Rows start at the
// rectangle's bottom edge and advance by rowPitchBytes. Each row contains
// width * 4 RGBA bytes; padding is left untouched. byteCapacity need only
// cover the last row's pixels, not the final row's padding. The pitch must be
// at least width * 4 and a multiple of four. Validation failures leave the
// output untouched. This is a blocking diagnostic/capture operation, not a
// general asynchronous GPU transfer path.
CYPHER_NODISCARD render_error_t R_ReadTargetRGBA8(
    render_target_handle_t target, const render_readback_rect_t &rect,
    ::cypher::common::byte *pixelsOut, ::cypher::common::u64 byteCapacity,
    ::cypher::common::u32 rowPitchBytes ) noexcept;

// Synchronously converts an RGBA16F target to caller-owned RGBA32F pixels.
// Each render_color_t occupies 16 bytes in red/green/blue/alpha order. Rows
// start at the rectangle's bottom edge; padding remains untouched. The byte
// capacity must cover the last row's pixels, but not its trailing padding.
// Pitch must be a multiple of 16 and at least width * 16. Invalid input and
// unsupported target formats do not modify the destination. This is a
// blocking diagnostic/capture operation, not an asynchronous transfer.
CYPHER_NODISCARD render_error_t R_ReadTargetRGBA32F(
    render_target_handle_t target, const render_readback_rect_t &rect,
    render_color_t *pixelsOut, ::cypher::common::u64 byteCapacity,
    ::cypher::common::u32 rowPitchBytes ) noexcept;
} // namespace cypher::engine::render
#endif // CYPHER_ENGINE_RENDER_TARGET_H
