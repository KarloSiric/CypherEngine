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
// A target requires linear RGBA8 color and accepts optional sampleable D24
// depth. Without explicit depth, it owns a private D24 renderbuffer. Targets
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
    render_clear_flags_t clearFlags{ R_CLEAR_COLOR | R_CLEAR_DEPTH };
    render_color_t clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    ::cypher::common::f32 clearDepth{ 1.0f };
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
} // namespace cypher::engine::render
#endif // CYPHER_ENGINE_RENDER_TARGET_H
