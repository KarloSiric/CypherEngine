//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherRender/CypherRender_Shader.cpp
//  Purpose: Implements the CypherRender Render Shader module.
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

#include "CypherRender_Shader.h"
#include "CypherFileSystem.h"
#include "CypherCommon_Print.h"
#include "CypherLog.h"
#include "CypherRender_GL.h"

#include <cstring>     // strcmp / strncpy for fixed-size shader names and paths.

namespace cypher::engine::render
{

namespace
{
char s_VertexShaderSource[CYPHER_RENDER_MAX_SHADER_SOURCE_SIZE + 1u]{};
char s_FragmentShaderSource[CYPHER_RENDER_MAX_SHADER_SOURCE_SIZE + 1u]{};
}

/*
================
CypherRender_ShaderRegistryInit
================
*/
void CypherRender_ShaderRegistryInit( shader_registry_t &szShaderRegistry )
{
    szShaderRegistry = {};

    return ;
}

/*
================
CypherRender_ShaderRegistryShutdown

Destroys all loaded GL shader programs before clearing the registry.
================
*/
void CypherRender_ShaderRegistryShutdown( shader_registry_t &szShaderRegistry )
{
    for ( common::u32 i = 0u; i < szShaderRegistry.nShaderCount; ++i ) {
        CypherRender_ShaderUnload( szShaderRegistry.shaders[i] );
    }

    szShaderRegistry = {};

    return ;
}

/*
================
CypherRender_ShaderLoad

Loads shader source through the file system and creates a GL shader program.
================
*/
render_error_t CypherRender_ShaderLoad( shader_registry_t &szShaderRegistry, const char *name, const char *szVertexPath, const char *szFragmentPath, shader_t **szOutShader )
{
    if ( name == nullptr || name[0] == '\0' ) {
        LOG_ERROR( log::channel_t::RENDER, "shader load failed: invalid shader name." );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_INVALID_FUNC_PARAMETER ), "CypherRender_ShaderLoad: failed passing invalid shader name.\n" );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( szVertexPath == nullptr || szVertexPath[0] == '\0' ) {
        LOG_ERROR( log::channel_t::RENDER, "shader load failed: invalid vertex shader path." );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_INVALID_FUNC_PARAMETER ), "CypherRender_ShaderLoad: failed passing invalid vertex shader path.\n" );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( szFragmentPath == nullptr || szFragmentPath[0] == '\0' ) {
        LOG_ERROR( log::channel_t::RENDER, "shader load failed: invalid fragment shader path." );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_INVALID_FUNC_PARAMETER ), "CypherRender_ShaderLoad: failed passing invalid fragment shader path.\n" );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( szOutShader == nullptr ) {
        LOG_ERROR( log::channel_t::RENDER, "shader load failed for '%s': out_shader is null.", name );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_INVALID_FUNC_PARAMETER ), "CypherRender_ShaderLoad: failed, the out_shader is nullptr.\n" );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    shader_t *szExistingShader = CypherRender_ShaderFind( szShaderRegistry, name );
    if ( szExistingShader != nullptr ) {
        *szOutShader = szExistingShader;
        LOG_DEBUG( log::channel_t::RENDER, "shader '%s' already loaded; reusing program=%u.", name, szExistingShader->nGlShaderProgramId );
        return render_error_t::OK;
    }

    if ( szShaderRegistry.nShaderCount >= CYPHER_RENDER_MAX_SHADERS ) {
        LOG_ERROR( log::channel_t::RENDER, "shader load failed for '%s': registry full (%u).", name, CYPHER_RENDER_MAX_SHADERS );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_SHADER_REGISTRY_FULL), "CypherRender_ShaderLoad: shader_registry is full; [{ %d out of %d }] ", szShaderRegistry.nShaderCount, CYPHER_RENDER_MAX_SHADERS );
        return render_error_t::ERR_SHADER_REGISTRY_FULL;
    }

    shader_t *shader = &szShaderRegistry.shaders[szShaderRegistry.nShaderCount];
    *shader = {};

    shader->nShaderId = szShaderRegistry.nShaderCount + 1u;
    std::strncpy( shader->name, name, CYPHER_RENDER_MAX_SHADER_NAME - 1u );
    std::strncpy( shader->szVertexPath, szVertexPath, CYPHER_RENDER_MAX_SHADER_PATH - 1u );
    std::strncpy( shader->szFragmentPath, szFragmentPath, CYPHER_RENDER_MAX_SHADER_PATH - 1u );

    char *szVertexSource = s_VertexShaderSource;
    char *szFragmentSource = s_FragmentShaderSource;

    common::u64 nVertexBytesRead{ 0u };
    common::u64 nFragmentBytesRead{ 0u };

    const auto vertexReadResult = fs::CypherFileSystem_ReadEntireFile( szVertexPath, szVertexSource, CYPHER_RENDER_MAX_SHADER_SOURCE_SIZE, nVertexBytesRead );

    if ( vertexReadResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "shader '%s' load failed: vertex source '%s' read failed: %s.", name, szVertexPath, fs::CypherFileSystem_ErrorDesc( vertexReadResult ) );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_SHADER_LOAD ), "CypherRender_ShaderLoad: failed reading vertex shader '%s'.\n", szVertexPath );
        return render_error_t::ERR_SHADER_LOAD;
    }
    szVertexSource[nVertexBytesRead] = '\0';

    const auto fragmentReadResult = fs::CypherFileSystem_ReadEntireFile( szFragmentPath, szFragmentSource, CYPHER_RENDER_MAX_SHADER_SOURCE_SIZE, nFragmentBytesRead );
    if ( fragmentReadResult != fs::fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "shader '%s' load failed: fragment source '%s' read failed: %s.", name, szFragmentPath, fs::CypherFileSystem_ErrorDesc( fragmentReadResult ) );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_SHADER_LOAD ), "CypherRender_ShaderLoad: failed reading fragment shader '%s'.\n", szFragmentPath );
        return render_error_t::ERR_SHADER_LOAD;
    }
    szFragmentSource[nFragmentBytesRead] = '\0';

    const auto shaderProgramResult = CypherRenderGL_CreateShaderProgram( szVertexSource, szFragmentSource, shader->nGlShaderProgramId );

    if ( shaderProgramResult != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "shader '%s' load failed: GL program creation failed: %s.", name, CypherRender_ErrorDesc( shaderProgramResult ) );
        COM_ERRORF( CypherRender_ErrorCode( shaderProgramResult), "CypherRender_ShaderLoad: CypherRenderGL_CreateShaderProgram: failed creating shader program '%s'.\n", name );
        *shader = {};
        return shaderProgramResult;
    }

    shader->loaded = true;
    szShaderRegistry.nShaderCount++;
    *szOutShader = shader;

    LOG_INFO( log::channel_t::RENDER, "shader '%s' loaded: program=%u, vertex='%s' (%llu bytes), fragment='%s' (%llu bytes).",
                     name,
                     shader->nGlShaderProgramId,
                     szVertexPath,
                     static_cast<unsigned long long>( nVertexBytesRead ),
                     szFragmentPath,
                     static_cast<unsigned long long>( nFragmentBytesRead ) );

    return render_error_t::OK;
}

/*
================
CypherRender_ShaderFind
================
*/
shader_t *CypherRender_ShaderFind( shader_registry_t &registry, const char *name )
{
    if ( name == nullptr || name[0] == '\0' ) {
        return nullptr;
    }

    for ( int i = 0; i < registry.nShaderCount; ++i ) {
        if ( std::strcmp( registry.shaders[i].name, name ) == 0 ) {
            return &registry.shaders[i];
        }
    }

    return nullptr;
}

/*
================
CypherRender_ShaderBind
================
*/
render_error_t CypherRender_ShaderBind( const shader_t &shader )
{
    if ( shader.nGlShaderProgramId == 0  || !shader.loaded ) {
        LOG_ERROR( log::channel_t::RENDER, "shader bind failed: invalid shader program id=%u loaded=%u.", shader.nGlShaderProgramId, shader.loaded ? 1u : 0u );
        COM_ERRORF( CypherRender_ErrorCode( render_error_t::ERR_INVALID_FUNC_PARAMETER ), "CypherRender_BindShader: Invalid shader program id passed; %d\n", shader.nGlShaderProgramId );
        return render_error_t::ERR_SHADER_LOAD;
    }

    return CypherRenderGL_BindShaderProgram( shader.nGlShaderProgramId );
}

/*
================
CypherRender_ShaderUnload
================
*/
void CypherRender_ShaderUnload( shader_t &shader )
{
    if ( !shader.loaded ) {
        return ;
    }

    if ( shader.nGlShaderProgramId != 0u ) {
        CypherRenderGL_DestroyShaderProgram( shader.nGlShaderProgramId );
    }

    shader = {};
    return ;
}

render_error_t CypherRender_ShaderSetMat4( const shader_t &shader, const char *szUniformName, const math::mat4_t &matrix )
{
    if ( !shader.loaded || shader.nGlShaderProgramId == 0u ) {
        return render_error_t::ERR_SHADER_BIND;
    }

    if ( szUniformName == nullptr || szUniformName[0] == '\0' ) {
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    return CypherRenderGL_SetUniformMat4( shader.nGlShaderProgramId, szUniformName, matrix );
}

}       // namespace cypher::engine::render
