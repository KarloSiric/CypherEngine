//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Declares OpenGL 4.1 immutable sampler creation and destruction.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_OPENGL_SAMPLER_H
#define CYPHER_ENGINE_RENDER_OPENGL_SAMPLER_H
#pragma once
#include "CypherRender/CypherRender_Backend.h"

namespace cypher::engine::render
{
CYPHER_NODISCARD render_error_t GL_CreateSampler(
    const render_sampler_desc_t &, backend_sampler_t &, void * ) noexcept;
CYPHER_NODISCARD render_error_t GL_DestroySampler( backend_sampler_t, void * ) noexcept;
} // namespace cypher::engine::render
#endif // CYPHER_ENGINE_RENDER_OPENGL_SAMPLER_H
