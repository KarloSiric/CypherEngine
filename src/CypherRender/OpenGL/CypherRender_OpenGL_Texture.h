//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Declares immutable OpenGL 2D image upload and destruction.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_ENGINE_RENDER_OPENGL_TEXTURE_H
#define CYPHER_ENGINE_RENDER_OPENGL_TEXTURE_H
#pragma once
#include "CypherRender/CypherRender_Backend.h"
namespace cypher::engine::render
{
CYPHER_NODISCARD render_error_t GL_CreateTexture2D(
    const render_texture_desc_t &, const render_texture_data_t &,
    backend_texture_t &, void * ) noexcept;
CYPHER_NODISCARD render_error_t GL_DestroyTexture( backend_texture_t, void * ) noexcept;
}
#endif
