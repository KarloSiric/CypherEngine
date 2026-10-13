//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Declares private OpenGL color-and-depth target operations.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_OPENGL_TARGET_H
#define CYPHER_ENGINE_RENDER_OPENGL_TARGET_H
#pragma once

#include "CypherRender/CypherRender_Backend.h"

namespace cypher::engine::render
{
CYPHER_NODISCARD render_error_t GL_CreateRenderTarget(
    const backend_render_target_desc_t &, backend_render_target_t &, void * ) noexcept;
CYPHER_NODISCARD render_error_t GL_DestroyRenderTarget(
    backend_render_target_t, void * ) noexcept;
CYPHER_NODISCARD render_error_t GL_BeginTargetPass(
    const backend_target_pass_desc_t &, void * ) noexcept;
CYPHER_NODISCARD render_error_t GL_EndTargetPass( void * ) noexcept;
CYPHER_NODISCARD render_error_t GL_ReadTargetRGBA8(
    const backend_target_readback_desc_t &, void * ) noexcept;
CYPHER_NODISCARD render_error_t GL_ReadTargetRGBA32F(
    const backend_target_float_readback_desc_t &, void * ) noexcept;

// Called with the context current during backend shutdown, after frontend
// resource systems have released their records. Also cleans up a partial
// teardown without leaving CPU-owned native target records behind.
CYPHER_NODISCARD render_error_t GL_TargetsShutdown() noexcept;
void GL_TargetsAbandon() noexcept; // CPU cleanup when the context cannot be activated.
} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_OPENGL_TARGET_H
