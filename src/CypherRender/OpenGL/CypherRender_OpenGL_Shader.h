//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_Shader.h
//  Purpose: Declares private OpenGL shader-program lifecycle operations.
//  Details: Linked program names remain behind backend_shader_t. Temporary
//           stage objects and driver diagnostics belong to the implementation.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_OPENGL_SHADER_H
#define CYPHER_ENGINE_RENDER_OPENGL_SHADER_H
#pragma once

#include "CypherRender/CypherRender_Backend.h"

namespace cypher::engine::render
{

// Implements the synchronous borrowing and rollback contract of the callback.
CYPHER_NODISCARD render_error_t GL_CreateShader(
    const render_shader_desc_t &description,
    backend_shader_t &shaderOut,
    void *backendState ) noexcept;

// Releases the linked program after the frontend has checked its dependencies.
CYPHER_NODISCARD render_error_t GL_DestroyShader(
    backend_shader_t shader,
    void *backendState ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_OPENGL_SHADER_H
