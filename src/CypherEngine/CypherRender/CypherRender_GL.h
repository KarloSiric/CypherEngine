//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherRender/CypherRender_GL.h
//  Purpose: Declares the CypherRender Render GL module.
//  Details: This file participates in the renderer bootstrap and draw path. Keep API
//           boundaries clear so the renderer can grow from simple OpenGL startup into
//           a fuller rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_GL_H
#define CYPHER_ENGINE_RENDER_GL_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherRender_Error.h"
#include "CypherRender_Mesh.h"
#include "CypherSystem_Window.h"
#include "CypherMath_Matrix4.h"

namespace cypher::engine::render
{

/*
================
OpenGL Backend State

Opaque backend context kept out of higher-level renderer code.
================
*/
struct gl_state_t {
    void *context{ nullptr };
};

/*
================
OpenGL Context
================
*/
render_error_t CypherRenderGL_Init( const sys::window_t &window, bool vsync, gl_state_t &pGlState );

void CypherRenderGL_Shutdown( gl_state_t &pGlState );

render_error_t CypherRenderGL_BeginFrame( const sys::window_t &window );

render_error_t CypherRenderGL_EndFrame( const sys::window_t &window );

/*
================
OpenGL Shaders
================
*/
render_error_t CypherRenderGL_CreateShaderProgram( const char *szVertexSource, const char *szFragmentSource, common::u32 &nOutShaderProgramId );

render_error_t CypherRenderGL_BindShaderProgram( const common::u32 nShaderProgramId );

void CypherRenderGL_DestroyShaderProgram( const common::u32 nShaderProgramId );

/*
================
OpenGL Meshes
================
*/
render_error_t CypherRenderGL_MeshCreate( const vertex_t *vertices,
                               const common::u32 nVertexCount,
                               const common::u32 *indices,
                               const common::u32 nIndexCount,
                               mesh_t &meshOut );

void CypherRenderGL_MeshDestroy( mesh_t &mesh );

render_error_t CypherRenderGL_MeshDraw( const mesh_t &mesh );

render_error_t CypherRenderGL_SetUniformMat4(
    common::u32 nShaderProgramId,
    const char *szUniformName,
    const ::cypher::math::mat4_t &matrix );

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_GL_H
