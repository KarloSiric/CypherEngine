//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL.h
//  Purpose: Exposes the compiled OpenGL backend to the renderer frontend.
//  Details: OpenGL types and GLAD remain private to the implementation. The
//           frontend receives only the backend-neutral dispatch table.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_OPENGL_H
#define CYPHER_ENGINE_RENDER_OPENGL_H

#pragma once

#include "CypherRender/CypherRender_Backend.h"

namespace cypher::engine::render
{

CYPHER_NODISCARD const backend_api_t *GL_GetBackendAPI() noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_OPENGL_H
