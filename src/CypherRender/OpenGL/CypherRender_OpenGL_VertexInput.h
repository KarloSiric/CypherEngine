//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_VertexInput.h
//  Purpose: Declares private OpenGL vertex-array object operations.
//  Details: VAO names remain behind backend_vertex_input_t and never cross the
//           renderer frontend/backend boundary.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_OPENGL_VERTEX_INPUT_H
#define CYPHER_ENGINE_RENDER_OPENGL_VERTEX_INPUT_H
#pragma once

#include "CypherRender/CypherRender_Backend.h"

namespace cypher::engine::render
{

CYPHER_NODISCARD render_error_t GL_CreateVertexInput(
    const backend_vertex_input_desc_t &description,
    backend_vertex_input_t &vertexInputOut,
    void *backendState ) noexcept;

CYPHER_NODISCARD render_error_t GL_DestroyVertexInput(
    backend_vertex_input_t vertexInput,
    void *backendState ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_OPENGL_VERTEX_INPUT_H
