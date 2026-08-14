//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_GL.cpp
//  Purpose: Implements the CypherRender Render GL module.
//  Details: This file participates in the renderer bootstrap and draw path. Keep API
//           boundaries clear so the renderer can grow from simple OpenGL startup into
//           a fuller rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-05-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_GL.h"
#include "CypherLog.h"
#include "CypherSystem_OpenGL.h"
#include "CypherSystem_Platform.h"

#include <SDL3/SDL.h>      // SDL owns window creation and GL context lifetime.
#include <cstddef>         // offsetof for vertex attribute layout.

namespace cypher::engine::render
{

/*
================
CypherRenderGL_Init

Creates the SDL OpenGL context and loads GL entry points through GLAD.
================
*/
render_error_t CypherRenderGL_Init( const sys::window_t &window, bool vsync, gl_state_t &pGlState )
{
    SDL_Window *pSdlWindow{ nullptr};

    if ( window.pNativeWindow == nullptr || !window.valid ) {
        LOG_ERROR( log::channel_t::RENDER, "OpenGL init failed: invalid native window." );
        return render_error_t::ERR_INVALID_WINDOW_CFG;
    }

    pSdlWindow = static_cast<SDL_Window *>( window.pNativeWindow );

    // macOS requires a forward-compatible core profile context.
    #ifdef CYPHER_PLATFORM_MACOS
        if (
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, SYS_GL_CONTEXT_MAJOR ) ||
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, SYS_GL_CONTEXT_MINOR ) ||
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE ) ||
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SYS_GL_CONTEXT_FLAGS ) ||
            !SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 ) ||
            !SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 )  ||
            !SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 ) )
        {
            LOG_ERROR( log::channel_t::RENDER, "SDL_GL_SetAttribute failed: %s", SDL_GetError() );

            return render_error_t::ERR_OPENGL_INIT;
        }
    // Windows and Linux can request the newer OpenGL profile.
    #elif defined( CYPHER_PLATFORM_WINDOWS ) || defined( CYPHER_PLATFORM_LINUX )
        if (
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, SYS_GL_CONTEXT_MAJOR ) ||
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, SYS_GL_CONTEXT_MINOR ) ||
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE ) ||
            !SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SYS_GL_CONTEXT_FLAGS ) ||
            !SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 ) ||
            !SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 )  ||
            !SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 ) )
        {
            LOG_ERROR( log::channel_t::RENDER, "SDL_GL_SetAttribute failed: %s", SDL_GetError() );

            return render_error_t::ERR_OPENGL_INIT;
        }
    #else
        #error "Unsupported platform for OpenGL context creation."
    #endif

    SDL_GLContext pGlContext{ nullptr };
    pGlContext = SDL_GL_CreateContext( pSdlWindow );

    if ( pGlContext == nullptr ) {
        LOG_ERROR( log::channel_t::RENDER, "SDL_GL_CreateContext failed: %s", SDL_GetError() );
        return render_error_t::ERR_OPENGL_INIT;
    }

    if ( !SDL_GL_SetSwapInterval( vsync ? 1 : 0 ) ) {
        LOG_ERROR( log::channel_t::RENDER, "SDL_GL_SetSwapInterval failed: %s", SDL_GetError() );
    }

    if ( !SDL_GL_MakeCurrent( pSdlWindow, pGlContext ) ) {
        SDL_GL_DestroyContext( pGlContext );
        LOG_ERROR( log::channel_t::RENDER, "SDL_GL_MakeCurrent failed: %s", SDL_GetError() );
        return render_error_t::ERR_OPENGL_INIT;
    }

    // GLAD must load function pointers after the context is current.
    const auto nGlVersion = gladLoadGL( reinterpret_cast<GLADloadfunc>( SDL_GL_GetProcAddress ) );

    if ( nGlVersion == 0 ) {
        SDL_GL_DestroyContext( pGlContext );
        LOG_ERROR( log::channel_t::RENDER, "Error loading GLAD library, cannot locate OpenGL functions." );
        return render_error_t::ERR_OPENGL_INIT;
    }

    if ( !GLAD_GL_VERSION_4_1 ) {
        SDL_GL_DestroyContext( pGlContext );
        LOG_ERROR( log::channel_t::RENDER, "OpenGL 4.1 core profile is required." );
        return render_error_t::ERR_OPENGL_INIT;
    }

    pGlState.context = pGlContext;

    const char *szGlVendor = reinterpret_cast<const char *>( glGetString( GL_VENDOR ) );
    const char *szGlRenderer = reinterpret_cast<const char *>( glGetString( GL_RENDERER ) );
    const char *glVersionString = reinterpret_cast<const char *>( glGetString( GL_VERSION ) );
    const char *szGlslVersionString = reinterpret_cast<const char *>( glGetString( GL_SHADING_LANGUAGE_VERSION ) );

    LOG_INFO( log::channel_t::RENDER, "OpenGL initialized: version='%s', glsl='%s', vendor='%s', renderer='%s', vsync=%u.",
                     glVersionString ? glVersionString : "<unknown>",
                     szGlslVersionString ? szGlslVersionString : "<unknown>",
                     szGlVendor ? szGlVendor : "<unknown>",
                     szGlRenderer ? szGlRenderer : "<unknown>",
                     vsync ? 1u : 0u );

    return render_error_t::OK;
}

/*
================
CypherRenderGL_Shutdown
================
*/
void CypherRenderGL_Shutdown( gl_state_t &pGlState )
{
    if ( pGlState.context == nullptr ) {
        return ;
    }

    SDL_GL_DestroyContext( static_cast<SDL_GLContext>( pGlState.context ) );
    pGlState.context = nullptr;

    LOG_INFO( log::channel_t::RENDER, "OpenGL Context shutdown completed." );

    return ;
}

/*
================
CypherRenderGL_BeginFrame

Sets per-frame GL state and clears the back buffer.
================
*/
render_error_t CypherRenderGL_BeginFrame( const sys::window_t &window )
{
    if ( window.pNativeWindow == nullptr || !window.valid ) {
        LOG_ERROR( log::channel_t::RENDER, "GL begin frame failed: invalid native window." );
        return render_error_t::ERR_INVALID_WINDOW_CFG;
    }

    if ( window.width == 0u || window.height == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "GL begin frame failed: invalid viewport %ux%u.", window.width, window.height );
        return render_error_t::ERR_INVALID_VIEWPORT;
    }

    glViewport( 0, 0, static_cast<GLsizei>( window.width ), static_cast<GLsizei>( window.height ) );

    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );

    glClearColor( 0.04f, 0.045f, 0.05f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    return render_error_t::OK;
}

/*
================
CypherRenderGL_EndFrame
================
*/
render_error_t CypherRenderGL_EndFrame( const sys::window_t &window )
{
    SDL_Window *pSdlWindow{ nullptr };
    if ( !window.valid || window.pNativeWindow == nullptr ) {
        LOG_ERROR( log::channel_t::RENDER, "GL end frame failed: invalid native window." );
        return render_error_t::ERR_INVALID_WINDOW_CFG;
    }

    pSdlWindow = static_cast<SDL_Window *>( window.pNativeWindow );

    SDL_GL_SwapWindow( pSdlWindow );

    return render_error_t::OK;
}

/*
================
CypherRenderGL_CompileShader

Compiles a single OpenGL shader stage and returns its temporary object id.
================
*/
GLuint CypherRenderGL_CompileShader( const GLenum szShaderType, const char *szShaderSource )
{
    if ( szShaderSource == nullptr || szShaderSource[0] == '\0' ) {
        LOG_ERROR( log::channel_t::RENDER, "OpenGL shader compile failed: shader source is empty." );
        return 0;
    }

    GLuint nShaderId = glCreateShader( szShaderType );
    glShaderSource( nShaderId, 1, &szShaderSource, nullptr );
    glCompileShader( nShaderId );

    GLint compileStatus = GL_FALSE;

    glGetShaderiv( nShaderId, GL_COMPILE_STATUS, &compileStatus );
    if ( compileStatus != GL_TRUE ) {
        char infoLog[2024]{};
        glGetShaderInfoLog( nShaderId, sizeof( infoLog ), nullptr, infoLog );

        LOG_ERROR( log::channel_t::RENDER, "CypherRenderGL_CompileShader: OpenGL shader compile failed: %s\n", infoLog );
        glDeleteShader( nShaderId );
        return 0;
    }

    return nShaderId;
}

/*
================
CypherRenderGL_CreateShaderProgram

Compiles vertex/fragment shader stages and links them into one GL program.
================
*/
render_error_t CypherRenderGL_CreateShaderProgram( const char *szVertexSource, const char *szFragmentSource, common::u32 &nOutShaderProgramId )
{
    nOutShaderProgramId = 0;

    if ( szVertexSource == nullptr || szVertexSource[0] == '\0' ) {
        LOG_ERROR( log::channel_t::RENDER, "shader program creation failed: vertex source is empty." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( szFragmentSource == nullptr || szFragmentSource[0] == '\0') {
        LOG_ERROR( log::channel_t::RENDER, "shader program creation failed: fragment source is empty." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    GLuint nVertexShaderId = CypherRenderGL_CompileShader( GL_VERTEX_SHADER, szVertexSource );
    if ( nVertexShaderId == 0 ) {
        return render_error_t::ERR_SHADER_COMPILE;
    }

    GLuint nFragmentShaderId = CypherRenderGL_CompileShader( GL_FRAGMENT_SHADER, szFragmentSource );
    if ( nFragmentShaderId == 0 ) {
        glDeleteShader( nVertexShaderId );
        return render_error_t::ERR_SHADER_COMPILE;
    }

    GLuint nShaderProgramId = glCreateProgram();
    glAttachShader( nShaderProgramId, nVertexShaderId );
    glAttachShader( nShaderProgramId, nFragmentShaderId );
    glLinkProgram( nShaderProgramId );

    glDeleteShader( nVertexShaderId );
    glDeleteShader( nFragmentShaderId );

    GLint linkStatus = GL_FALSE;

    glGetProgramiv( nShaderProgramId, GL_LINK_STATUS, &linkStatus );
    if ( linkStatus != GL_TRUE ) {
        char infoLog[2024]{};
        glGetProgramInfoLog( nShaderProgramId, sizeof( infoLog ), nullptr, infoLog );
        LOG_ERROR( log::channel_t::RENDER, "OpenGL shader program link failed: %s", infoLog );
        glDeleteProgram( nShaderProgramId );
        return render_error_t::ERR_SHADER_LINK;
    }

    nOutShaderProgramId = static_cast<common::u32>( nShaderProgramId );

    return render_error_t::OK;
}

/*
================
CypherRenderGL_BindShaderProgram
================
*/
render_error_t CypherRenderGL_BindShaderProgram( const common::u32 nShaderProgramId )
{
    if ( nShaderProgramId == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "shader program bind failed: program id is zero." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    glUseProgram( nShaderProgramId );

    return render_error_t::OK;
}

/*
================
CypherRenderGL_DestroyShaderProgram
================
*/
void CypherRenderGL_DestroyShaderProgram( const common::u32 nShaderProgramId )
{
    if ( nShaderProgramId == 0u ) {
        return ;
    }

    glDeleteProgram( nShaderProgramId );

    return ;
}

/*
================
CypherRenderGL_MeshCreate

Uploads mesh vertex/index data and records VAO/VBO/EBO handles.
================
*/
render_error_t CypherRenderGL_MeshCreate( const vertex_t *vertices,
                               const common::u32 nVertexCount,
                               const common::u32 *indices,
                               const common::u32 nIndexCount,
                               mesh_t &meshOut )
{
    if ( vertices == nullptr || nVertexCount == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "GL mesh create failed: invalid vertices pointer/count=%u.", nVertexCount );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( indices == nullptr || nIndexCount == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "GL mesh create failed: invalid indices pointer/count=%u.", nIndexCount );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    GLuint nVertexArrayId{ 0u };
    GLuint nVertexBufferId{ 0u };
    GLuint nElementBufferId{ 0u };

    glGenVertexArrays( 1, &nVertexArrayId );
    glGenBuffers( 1, &nVertexBufferId );
    glGenBuffers( 1, &nElementBufferId );

    if ( nVertexArrayId == 0u || nVertexBufferId == 0u || nElementBufferId == 0u ) {
        if ( nElementBufferId != 0u ) {
            glDeleteBuffers( 1, &nElementBufferId );
        }
        if ( nVertexBufferId != 0u ) {
            glDeleteBuffers( 1, &nVertexBufferId );
        }
        if ( nVertexArrayId != 0u ) {
            glDeleteVertexArrays( 1, &nVertexArrayId );
        }

        LOG_ERROR( log::channel_t::RENDER, "GL mesh create failed: failed generating buffers vao=%u, vbo=%u, ebo=%u.", static_cast<common::u32>( nVertexArrayId ), static_cast<common::u32>( nVertexBufferId ), static_cast<common::u32>( nElementBufferId ) );
        return render_error_t::ERR_OPENGL_INIT;
    }

    glBindVertexArray( nVertexArrayId );

    glBindBuffer( GL_ARRAY_BUFFER, nVertexBufferId );
    glBufferData( GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>( nVertexCount * sizeof( vertex_t ) ),
                  vertices,
                  GL_STATIC_DRAW );

    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, nElementBufferId );
    glBufferData( GL_ELEMENT_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>( nIndexCount * sizeof( common::u32 ) ),
                  indices,
                  GL_STATIC_DRAW );

    glVertexAttribPointer( 0,
                           3,
                           GL_FLOAT,
                           GL_FALSE,
                           sizeof( vertex_t ),
                           reinterpret_cast<const void *>( offsetof( vertex_t, position ) ) );
    glEnableVertexAttribArray( 0 );

    glVertexAttribPointer( 1,
                           3,
                           GL_FLOAT,
                           GL_FALSE,
                           sizeof( vertex_t ),
                           reinterpret_cast<const void *>( offsetof( vertex_t, color ) ) );
    glEnableVertexAttribArray( 1 );

    glBindVertexArray( 0 );

    meshOut.nVertexCount = nVertexCount;
    meshOut.nIndexCount = nIndexCount;
    meshOut.nGlVao = static_cast<common::u32>( nVertexArrayId );
    meshOut.nGlVbo = static_cast<common::u32>( nVertexBufferId );
    meshOut.nGlEbo = static_cast<common::u32>( nElementBufferId );
    meshOut.loaded = true;

    LOG_DEBUG( log::channel_t::RENDER, "GL mesh uploaded: vertices=%u, indices=%u, vao=%u, vbo=%u, ebo=%u.", nVertexCount, nIndexCount, meshOut.nGlVao, meshOut.nGlVbo, meshOut.nGlEbo );

    return render_error_t::OK;
}

/*
================
CypherRenderGL_MeshDestroy
================
*/
void CypherRenderGL_MeshDestroy( mesh_t &mesh )
{
    if ( mesh.nGlEbo != 0u ) {
        GLuint nElementBufferId = static_cast<GLuint>( mesh.nGlEbo );
        glDeleteBuffers( 1, &nElementBufferId );
    }

    if ( mesh.nGlVbo != 0u ) {
        GLuint nVertexBufferId = static_cast<GLuint>( mesh.nGlVbo );
        glDeleteBuffers( 1, &nVertexBufferId );
    }

    if ( mesh.nGlVao != 0u ) {
        GLuint nVertexArrayId = static_cast<GLuint>( mesh.nGlVao );
        glDeleteVertexArrays( 1, &nVertexArrayId );
    }

    mesh.nGlVao = 0u;
    mesh.nGlVbo = 0u;
    mesh.nGlEbo = 0u;
    mesh.loaded = false;

    return ;
}

/*
================
CypherRenderGL_MeshDraw
================
*/
render_error_t CypherRenderGL_MeshDraw( const mesh_t &mesh )
{
    if ( !mesh.loaded || mesh.nGlVao == 0u || mesh.nIndexCount == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "GL mesh draw failed: loaded=%u, vao=%u, indices=%u.", mesh.loaded ? 1u : 0u, mesh.nGlVao, mesh.nIndexCount );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    glBindVertexArray( static_cast<GLuint>( mesh.nGlVao ) );
    glDrawElements( GL_TRIANGLES, static_cast<GLsizei>( mesh.nIndexCount ), GL_UNSIGNED_INT, nullptr );
    glBindVertexArray( 0 );

    return render_error_t::OK;
}

render_error_t CypherRenderGL_SetUniformMat4(
    common::u32 nShaderProgramId,
    const char *szUniformName,
    const ::cypher::math::mat4_t &matrix )
{
    if ( nShaderProgramId == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "set uniform mat4 failed: shader program id is zero." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( szUniformName == nullptr || szUniformName[0] == '\0' ) {
        LOG_ERROR( log::channel_t::RENDER, "set uniform mat4 failed: invalid uniform name." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    const GLint nUniformLocation = glad_glGetUniformLocation( static_cast<GLuint>( nShaderProgramId ), szUniformName );

    if ( nUniformLocation < 0 ) {
        LOG_ERROR( log::channel_t::RENDER, "set uniform mat4 failed: uniform '%s' not found in program=%u.", szUniformName, nShaderProgramId );
        return render_error_t::ERR_SHADER_UNIFORM;
    }

    glUseProgram( static_cast<GLuint>( nShaderProgramId ) );

    glad_glUniformMatrix4fv( nUniformLocation, 1, GL_FALSE, matrix.m );

    return render_error_t::OK;
}

}       // namespace cypher::engine::render
