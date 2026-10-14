//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Executes one prepared shadow/color/presentation view in a host frame.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_SCENE_VIEW_H
#define CYPHER_ENGINE_RENDER_SCENE_VIEW_H
#pragma once

#include "CypherRender/CypherRender_Draw.h"
#include "CypherRender/CypherRender_Target.h"

namespace cypher::engine::render
{

// Borrowed, ordered draw records. A null pointer is valid only for zero count.
// Records and their renderer resources must remain live/unchanged until return.
struct render_scene_draw_span_t {
    const render_draw_indexed_desc_t *draws{ nullptr };
    ::cypher::common::u32 count{ 0u };
};

struct render_scene_view_desc_t {
    // Zero target skips the shadow pass, and then shadowDraws must be empty.
    // A supplied target is depth-only; clears and draw state remain explicit.
    render_target_pass_desc_t shadowPass{ {}, R_CLEAR_DEPTH };
    render_scene_draw_span_t shadowDraws{};
    // Required color target, typically linear RGBA16F. The caller has already
    // ordered BLEND far-to-near. This function preserves each span's order.
    render_target_pass_desc_t cameraPass{};
    render_scene_draw_span_t opaqueDraws{};
    render_scene_draw_span_t blendDraws{};
    render_scene_draw_span_t additiveDraws{};
    // Slot zero must sample cameraPass's color. The caller supplies a complete
    // fullscreen draw and its shader (e.g. tone mapping and output encoding).
    render_draw_indexed_desc_t outputDraw{};
    // Zero target draws into the frame's prepared presentation framebuffer;
    // other outputPass fields are then ignored. A supplied color target allows
    // offscreen composition/readback. No two pass attachments may overlap.
    render_target_pass_desc_t outputPass{};
};

enum class render_scene_view_stage_t : ::cypher::common::u8 {
    VALIDATION = 0u,
    SHADOW,
    CAMERA,
    OUTPUT,
    COMPLETE
};

struct render_scene_view_result_t {
    render_error_t error{ render_error_t::OK }; // First validation/execution error.
    render_error_t cleanupError{ render_error_t::OK }; // EndTargetPass failure, if any.
    render_scene_view_stage_t stage{ render_scene_view_stage_t::VALIDATION };
    ::cypher::common::u32 shadowDraws{ 0u };
    ::cypher::common::u32 opaqueDraws{ 0u };
    ::cypher::common::u32 blendDraws{ 0u };
    ::cypher::common::u32 additiveDraws{ 0u };
    bool outputDrawn{ false };
};

// Renderer-thread only, allocation-free, inside an active frame with no target
// pass open. Preflights spans, pass clears, target availability/attachments and
// output texture feedback before any GPU work. Draws still validate resources,
// ranges and pipeline state individually; execution is not atomic. Counts name
// only draws that returned OK; a failed draw may already have reached the GPU.
// Stops on the first error and ends each pass it opened, preserving a draw error
// separately from a cleanup error. Never opens/closes the host's frame, resets
// caller storage, owns resources, or closes an existing caller pass. A host
// that began the frame must end it after return, including failure; a rejected
// existing caller pass must first be closed by that caller. Shaders determine lighting,
// tone mapping and transfer semantics; this is a fixed sequence, not a graph.
CYPHER_NODISCARD render_scene_view_result_t R_ExecuteSceneView(
    const render_scene_view_desc_t &description ) noexcept;

} // namespace cypher::engine::render
#endif // CYPHER_ENGINE_RENDER_SCENE_VIEW_H
