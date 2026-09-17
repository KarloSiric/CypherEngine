//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL_Shader.cpp
//  Purpose: Compiles cooked GLSL stages and links private OpenGL programs.
//  Details: Creation publishes only complete programs, reports bounded driver
//           diagnostics, and releases temporary objects on every exit path.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL_Shader.h"
#include "CypherRender_OpenGL_Local.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <glad/gl.h>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;

namespace
{

void GL_LogShaderCompileFailure(
    const GLuint shader,
    const char *stageName,
    const char *debugName ) noexcept
{
    char context[256]{};
    std::snprintf(
        context, sizeof( context ),
        "OpenGL %s shader compilation failed: %.160s",
        stageName, debugName != nullptr ? debugName : "<unnamed>" );
    CY_LOG_WRITE_ERROR(
        Error, Render, R_ErrorCode( render_error_t::ERR_SHADER_COMPILE_FAILED ),
        context );

    GLchar log[4096]{};
    GLint required = 0;
    GLsizei written = 0;
    glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &required );
    glGetShaderInfoLog(
        shader, static_cast<GLsizei>( sizeof( log ) ), &written, log );
    log[sizeof( log ) - 1u] = '\0';

    if ( GL_CheckErrors( glState.config.validation ) != render_error_t::OK ) {
        CY_LOG_WRITE( Warning, Render, "Could not retrieve the shader compiler log." );
        return;
    }
    CY_LOG_WRITE_ERROR(
        Error, Render, R_ErrorCode( render_error_t::ERR_SHADER_COMPILE_FAILED ),
        written > 0 ? log : "The shader compiler returned no diagnostic text." );
    if ( required > static_cast<GLint>( sizeof( log ) ) ) {
        CY_LOG_WRITE( Warning, Render, "Shader compiler log truncated to 4095 bytes." );
    }
}

void GL_LogProgramLinkFailure( const GLuint program, const char *debugName ) noexcept
{
    char context[256]{};
    std::snprintf( context, sizeof( context ), "OpenGL program linking failed: %.160s",
                   debugName != nullptr ? debugName : "<unnamed>" );
    CY_LOG_WRITE_ERROR( Error, Render, R_ErrorCode( render_error_t::ERR_SHADER_LINK_FAILED ), context );

    GLchar log[4096]{};
    GLint required = 0;
    GLsizei written = 0;
    glGetProgramiv( program, GL_INFO_LOG_LENGTH, &required );
    glGetProgramInfoLog( program, static_cast<GLsizei>( sizeof( log ) ), &written, log );
    log[sizeof( log ) - 1u] = '\0';
    if ( GL_CheckErrors( glState.config.validation ) != render_error_t::OK ) {
        CY_LOG_WRITE( Warning, Render, "Could not retrieve the program linker log." );
        return;
    }
    CY_LOG_WRITE_ERROR( Error, Render, R_ErrorCode( render_error_t::ERR_SHADER_LINK_FAILED ),
                        written > 0 ? log : "The program linker returned no diagnostic text." );
    if ( required > static_cast<GLint>( sizeof( log ) ) ) {
        CY_LOG_WRITE( Warning, Render, "Program linker log truncated to 4095 bytes." );
    }
}

// The GL-owned version string was copied into this bounded buffer during init.
// Accept the standard major.minor prefix and ignore the optional vendor suffix.
common::u32 GL_ShaderLanguageVersion() noexcept
{
    const char *begin = glState.shadingLanguageVersion;
    const char *end = begin + sizeof( glState.shadingLanguageVersion );
    common::u32 major = 0u;
    common::u32 minor = 0u;
    const auto majorResult = std::from_chars( begin, end, major );
    if ( majorResult.ec != std::errc{} || majorResult.ptr == end ||
         *majorResult.ptr != '.' || major == 0u || major > 99u ) return 0u;
    const char *minorBegin = majorResult.ptr + 1;
    const auto minorResult = std::from_chars( minorBegin, end, minor );
    if ( minorResult.ec != std::errc{} || minor > 99u ||
         minorResult.ptr - minorBegin > 2 ) return 0u;
    if ( minorResult.ptr - minorBegin == 1 ) minor *= 10u;
    return major * 100u + minor;
}

render_error_t GL_ValidateProgramDesc( const render_shader_desc_t &description ) noexcept
{
    if ( description.cookedShader == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    const common::cooked_shader_view_t &cooked = *description.cookedShader;
    if ( cooked.backend != common::render_shader_backend_t::OPENGL ||
         cooked.kind != common::render_shader_program_kind_t::GRAPHICS ||
         cooked.flags != common::COOKED_SHADER_FLAG_NONE ||
         !common::CookedShader_SupportsLanguage( cooked.languageProfile, cooked.nLanguageVersion ) ||
         cooked.nStages != common::CY_COOKED_SHADER_MAX_STAGES ) {
        return render_error_t::ERR_SHADER_DATA_INVALID;
    }
    for ( common::u32 index = 0u; index < cooked.nStages; ++index ) {
        if ( cooked.stages[index].stage != common::render_shader_stage_t::VERTEX &&
             cooked.stages[index].stage != common::render_shader_stage_t::FRAGMENT ) {
            return render_error_t::ERR_SHADER_STAGE_UNSUPPORTED;
        }
    }
    if ( common::CookedShader_FindStage( cooked, common::render_shader_stage_t::VERTEX ) == nullptr ||
         common::CookedShader_FindStage( cooked, common::render_shader_stage_t::FRAGMENT ) == nullptr ) {
        return render_error_t::ERR_SHADER_DATA_INVALID;
    }
    const common::u32 supportedVersion = GL_ShaderLanguageVersion();
    if ( supportedVersion == 0u ) return render_error_t::ERR_DEVICE_QUERY_FAILED;
    return cooked.nLanguageVersion <= supportedVersion
        ? render_error_t::OK : render_error_t::ERR_BACKEND_VERSION_UNSUPPORTED;
}

render_error_t GL_ReleaseProgramStages(
    const GLuint program, const GLuint ( &stages )[2], const common::u32 attachedMask ) noexcept
{
    for ( common::u32 index = 0u; index < 2u; ++index ) {
        if ( stages[index] == 0u ) continue;
        if ( ( attachedMask & ( 1u << index ) ) != 0u ) glDetachShader( program, stages[index] );
        glDeleteShader( stages[index] );
    }
    return GL_CheckErrors( glState.config.validation );
}

} // namespace

// Called on the renderer thread with this backend's context current.
// Success transfers one temporary stage object to the caller, which must delete it.
render_error_t GL_CompileShaderStage(
    const common::cooked_shader_stage_view_t &stage,
    const char *debugName,
    GLuint &shaderOut ) noexcept
{
    shaderOut = 0u;
    if ( !glState.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    GLenum nativeStage = 0u;
    const char *stageName = nullptr;
    switch ( stage.stage ) {
        case common::render_shader_stage_t::VERTEX:
            nativeStage = GL_VERTEX_SHADER;
            stageName = "vertex";
            break;
        case common::render_shader_stage_t::FRAGMENT:
            nativeStage = GL_FRAGMENT_SHADER;
            stageName = "fragment";
            break;
        default:
            return render_error_t::ERR_SHADER_STAGE_UNSUPPORTED;
    }

    if ( stage.codeFormat != common::render_shader_code_format_t::GLSL_UTF8 ||
         stage.flags != common::COOKED_SHADER_STAGE_FLAG_NONE ||
         stage.code.pData == nullptr || stage.code.cbSize <= 1u ||
         stage.code.cbSize > common::CY_COOKED_SHADER_MAX_CODE_SIZE ) {
        return render_error_t::ERR_SHADER_DATA_INVALID;
    }

    const common::usize sourceSize = stage.code.cbSize - 1u;
    if ( stage.code.pData[sourceSize] != static_cast<common::byte>( '\0' ) ||
         std::memchr( stage.code.pData, '\0', sourceSize ) != nullptr ) {
        return render_error_t::ERR_SHADER_DATA_INVALID;
    }
    if ( sourceSize > static_cast<common::usize>(
             std::numeric_limits<GLint>::max() ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }

    const GLchar *source = reinterpret_cast<const GLchar *>( stage.code.pData );
    const GLint sourceLength = static_cast<GLint>( sourceSize );

    GL_ClearErrors();
    const GLuint shader = glCreateShader( nativeStage );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( shader == 0u ) {
        return result != render_error_t::OK
            ? result : render_error_t::ERR_RESOURCE_CREATE_FAILED;
    }

    if ( result == render_error_t::OK ) {
        glShaderSource( shader, 1, &source, &sourceLength );
        result = GL_CheckErrors( glState.config.validation );
    }

    GLint compiled = GL_FALSE;
    if ( result == render_error_t::OK ) {
        glCompileShader( shader );
        glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
        result = GL_CheckErrors( glState.config.validation );
    }
    if ( result == render_error_t::OK && compiled != GL_TRUE ) {
        GL_LogShaderCompileFailure( shader, stageName, debugName );
        result = render_error_t::ERR_SHADER_COMPILE_FAILED;
    }
    if ( result != render_error_t::OK ) {
        glDeleteShader( shader );
        const render_error_t cleanupResult = GL_CheckErrors( glState.config.validation );
        if ( cleanupResult != render_error_t::OK ) {
            CY_LOG_WRITE_ERROR(
                Error, Render, R_ErrorCode( cleanupResult ),
                "Failed to delete a temporary shader after creation failure." );
        }
        return result;
    }

    shaderOut = shader;
    return render_error_t::OK;
}

render_error_t GL_CreateShader(
    const render_shader_desc_t &description, backend_shader_t &shaderOut, void *backendState ) noexcept
{
    shaderOut = R_INVALID_BACKEND_SHADER;
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    render_error_t result = GL_ValidateProgramDesc( description );
    if ( result != render_error_t::OK ) return result;

    GLuint stages[2]{};
    const common::cooked_shader_view_t &cooked = *description.cookedShader;
    const auto *vertex = common::CookedShader_FindStage( cooked, common::render_shader_stage_t::VERTEX );
    const auto *fragment = common::CookedShader_FindStage( cooked, common::render_shader_stage_t::FRAGMENT );
    result = GL_CompileShaderStage( *vertex, description.debugName, stages[0] );
    if ( result == render_error_t::OK ) {
        result = GL_CompileShaderStage( *fragment, description.debugName, stages[1] );
    }

    GLuint program = 0u;
    common::u32 attachedMask = 0u;
    if ( result == render_error_t::OK ) {
        program = glCreateProgram();
        result = GL_CheckErrors( glState.config.validation );
        if ( program == 0u && result == render_error_t::OK ) result = render_error_t::ERR_RESOURCE_CREATE_FAILED;
    }
    for ( common::u32 index = 0u; index < 2u && result == render_error_t::OK; ++index ) {
        glAttachShader( program, stages[index] );
        result = GL_CheckErrors( glState.config.validation );
        if ( result == render_error_t::OK ) attachedMask |= 1u << index;
    }
    if ( result == render_error_t::OK ) {
        GLint linked = GL_FALSE;
        glLinkProgram( program );
        glGetProgramiv( program, GL_LINK_STATUS, &linked );
        result = GL_CheckErrors( glState.config.validation );
        if ( result == render_error_t::OK && linked != GL_TRUE ) {
            GL_LogProgramLinkFailure( program, description.debugName );
            result = render_error_t::ERR_SHADER_LINK_FAILED;
        }
    }

    const render_error_t cleanupResult = GL_ReleaseProgramStages( program, stages, attachedMask );
    if ( cleanupResult != render_error_t::OK ) {
        CY_LOG_WRITE_ERROR( Error, Render, R_ErrorCode( cleanupResult ),
                            "Failed to release temporary stages after program creation." );
        if ( result == render_error_t::OK ) result = cleanupResult;
    }
    if ( result != render_error_t::OK ) {
        if ( program != 0u ) {
            glDeleteProgram( program );
            const render_error_t deleteResult = GL_CheckErrors( glState.config.validation );
            if ( deleteResult != render_error_t::OK ) {
                CY_LOG_WRITE_ERROR( Error, Render, R_ErrorCode( deleteResult ),
                                    "Failed to delete a program after creation failure." );
            }
        }
        return result;
    }

    shaderOut.value = program;
    return render_error_t::OK;
}

render_error_t GL_DestroyShader( const backend_shader_t shader, void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !R_IsBackendShaderValid( shader ) || shader.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    GL_ClearErrors();
    glDeleteProgram( static_cast<GLuint>( shader.value ) );
    return GL_CheckErrors( glState.config.validation );
}

} // namespace cypher::engine::render
