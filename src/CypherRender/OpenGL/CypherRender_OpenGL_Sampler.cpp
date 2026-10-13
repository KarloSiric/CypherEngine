//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Materializes immutable sampler policy using OpenGL 4.1 objects.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender_OpenGL_Sampler.h"
#include "CypherRender_OpenGL_Local.h"
#include <limits>

namespace cypher::engine::render
{
namespace
{
GLenum GL_MinFilter( const render_sampler_filter_t filter,
    const render_sampler_mip_filter_t mip ) noexcept
{
    switch ( mip ) {
        case render_sampler_mip_filter_t::NONE:
            return filter == render_sampler_filter_t::NEAREST ? GL_NEAREST :
                filter == render_sampler_filter_t::LINEAR ? GL_LINEAR : 0u;
        case render_sampler_mip_filter_t::NEAREST:
            return filter == render_sampler_filter_t::NEAREST ? GL_NEAREST_MIPMAP_NEAREST :
                filter == render_sampler_filter_t::LINEAR ? GL_LINEAR_MIPMAP_NEAREST : 0u;
        case render_sampler_mip_filter_t::LINEAR:
            return filter == render_sampler_filter_t::NEAREST ? GL_NEAREST_MIPMAP_LINEAR :
                filter == render_sampler_filter_t::LINEAR ? GL_LINEAR_MIPMAP_LINEAR : 0u;
        default: return 0u;
    }
}

GLenum GL_Filter( const render_sampler_filter_t filter ) noexcept
{
    switch ( filter ) {
        case render_sampler_filter_t::NEAREST: return GL_NEAREST;
        case render_sampler_filter_t::LINEAR: return GL_LINEAR;
        default: return 0u;
    }
}

GLenum GL_Address( const render_sampler_address_t address ) noexcept
{
    switch ( address ) {
        case render_sampler_address_t::REPEAT: return GL_REPEAT;
        case render_sampler_address_t::CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
        default: return 0u;
    }
}

GLenum GL_Compare( const render_sampler_compare_t compare ) noexcept
{
    switch ( compare ) {
        case render_sampler_compare_t::LESS: return GL_LESS;
        case render_sampler_compare_t::LESS_EQUAL: return GL_LEQUAL;
        case render_sampler_compare_t::GREATER: return GL_GREATER;
        case render_sampler_compare_t::GREATER_EQUAL: return GL_GEQUAL;
        case render_sampler_compare_t::EQUAL: return GL_EQUAL;
        case render_sampler_compare_t::NOT_EQUAL: return GL_NOTEQUAL;
        case render_sampler_compare_t::ALWAYS: return GL_ALWAYS;
        case render_sampler_compare_t::NEVER: return GL_NEVER;
        default: return 0u;
    }
}
} // namespace

render_error_t GL_CreateSampler( const render_sampler_desc_t &description,
    backend_sampler_t &samplerOut, void *backendState ) noexcept
{
    samplerOut = {};
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    const GLenum minFilter = GL_MinFilter( description.minFilter, description.mipFilter );
    const GLenum magFilter = GL_Filter( description.magFilter );
    const GLenum addressU = GL_Address( description.addressU );
    const GLenum addressV = GL_Address( description.addressV );
    const GLenum compareFunc = GL_Compare( description.compare );
    if ( minFilter == 0u || magFilter == 0u || addressU == 0u || addressV == 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( description.compare >= render_sampler_compare_t::COUNT ||
         ( description.compare != render_sampler_compare_t::NONE && compareFunc == 0u ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    GL_ClearErrors();
    GLuint sampler = 0u;
    glGenSamplers( 1, &sampler );
    if ( sampler == 0u ) {
        const auto result = GL_CheckErrors( glState.config.validation );
        return result != render_error_t::OK ? result : render_error_t::ERR_RESOURCE_CREATE_FAILED;
    }
    glSamplerParameteri( sampler, GL_TEXTURE_MIN_FILTER, static_cast<GLint>( minFilter ) );
    glSamplerParameteri( sampler, GL_TEXTURE_MAG_FILTER, static_cast<GLint>( magFilter ) );
    glSamplerParameteri( sampler, GL_TEXTURE_WRAP_S, static_cast<GLint>( addressU ) );
    glSamplerParameteri( sampler, GL_TEXTURE_WRAP_T, static_cast<GLint>( addressV ) );
    glSamplerParameteri( sampler, GL_TEXTURE_COMPARE_MODE,
        description.compare == render_sampler_compare_t::NONE
            ? GL_NONE : GL_COMPARE_REF_TO_TEXTURE );
    if ( description.compare != render_sampler_compare_t::NONE ) {
        glSamplerParameteri( sampler, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>( compareFunc ) );
    }
    const auto result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) {
        glDeleteSamplers( 1, &sampler );
        (void)GL_CheckErrors( glState.config.validation );
        return result;
    }
    samplerOut.value = sampler;
    return render_error_t::OK;
}

render_error_t GL_DestroySampler( backend_sampler_t sampler, void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( sampler.value == 0u || sampler.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    const GLuint name = static_cast<GLuint>( sampler.value );
    GL_ClearErrors();
    glDeleteSamplers( 1, &name );
    return GL_CheckErrors( glState.config.validation );
}
} // namespace cypher::engine::render
