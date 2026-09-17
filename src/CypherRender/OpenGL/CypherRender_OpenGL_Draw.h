//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Declares private OpenGL pipeline and indexed-draw operations.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_OPENGL_DRAW_H
#define CYPHER_ENGINE_RENDER_OPENGL_DRAW_H
#pragma once

#include "CypherRender/CypherRender_Backend.h"

namespace cypher::engine::render
{
CYPHER_NODISCARD render_error_t GL_CreateGraphicsPipeline(
    const backend_pipeline_desc_t &description,
    backend_pipeline_t &pipelineOut, void *backendState ) noexcept;
CYPHER_NODISCARD render_error_t GL_DestroyGraphicsPipeline(
    backend_pipeline_t pipeline, void *backendState ) noexcept;
CYPHER_NODISCARD render_error_t GL_DrawIndexed(
    const backend_draw_indexed_desc_t &description, void *backendState ) noexcept;
} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_OPENGL_DRAW_H
