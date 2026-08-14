//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Shader.h
//  Purpose: Declares the CypherRender Render Shader module.
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

#ifndef CYPHER_ENGINE_RENDER_SHADER_H
#define CYPHER_ENGINE_RENDER_SHADER_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Matrix4.h"
#include "Engine/CypherCommon.h"
#include "CypherRender_Error.h"

namespace cypher::engine::render
{

/*
================
Shader Limits
================
*/
constexpr common::u32 CYPHER_RENDER_MAX_SHADER_NAME            = 64u;
constexpr common::u32 CYPHER_RENDER_MAX_SHADER_PATH            = 256u;
constexpr common::u32 CYPHER_RENDER_MAX_SHADERS                = 1024u;
constexpr common::u64 CYPHER_RENDER_MAX_SHADER_SOURCE_SIZE     = 1024u * 1024u;    // 1MB for shaders.

/*
================
Shader Types
================
*/
struct shader_t {
    common::u32 nShaderId{ 0 };
    char name[CYPHER_RENDER_MAX_SHADER_NAME]{};
    char szVertexPath[CYPHER_RENDER_MAX_SHADER_PATH]{};
    char szFragmentPath[CYPHER_RENDER_MAX_SHADER_PATH]{};

    common::u32 nGlShaderProgramId{ 0 };
    bool loaded{ false };
};

struct shader_registry_t {
    shader_t shaders[CYPHER_RENDER_MAX_SHADERS]{};
    common::u32 nShaderCount{ 0 };
};

/*
================
Shader Registry API
================
*/
void CypherRender_ShaderRegistryInit( shader_registry_t &szShaderRegistry );

void CypherRender_ShaderRegistryShutdown( shader_registry_t &szShaderRegistry );

render_error_t CypherRender_ShaderLoad( shader_registry_t &szShaderRegistry, const char *name, const char *szVertexPath, const char *szFragmentPath, shader_t **szOutShader );

shader_t *CypherRender_ShaderFind( shader_registry_t &registry, const char *name );

render_error_t CypherRender_ShaderBind( const shader_t &shader );

void CypherRender_ShaderUnload( shader_t &shader );

render_error_t CypherRender_ShaderSetMat4(
    const shader_t &shader,
    const char *szUniformName,
    const ::cypher::math::mat4_t &matrix );

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_SHADER_H
