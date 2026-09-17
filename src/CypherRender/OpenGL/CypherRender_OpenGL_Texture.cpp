//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Uploads RGBA8/sRGB images without depending on host pixel-store state.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender_OpenGL_Texture.h"
#include "CypherRender_OpenGL_Local.h"
#include <limits>

namespace cypher::engine::render
{
render_error_t GL_CreateTexture2D( const render_texture_desc_t &description,
    const render_texture_data_t &data, backend_texture_t &textureOut, void *backendState ) noexcept
{
    textureOut = {};
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( description.width == 0u || description.height == 0u || data.bytes == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( description.width > glState.limits.maxTexture2DSize || description.height > glState.limits.maxTexture2DSize ||
         description.width > static_cast<::cypher::common::u32>( std::numeric_limits<GLsizei>::max() ) ||
         description.height > static_cast<::cypher::common::u32>( std::numeric_limits<GLsizei>::max() ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    const auto required = static_cast<::cypher::common::u64>( description.width ) * description.height * 4u;
    if ( data.byteSize != required ) return render_error_t::ERR_INVALID_ARGUMENT;
    // Qt and other hosts may leave pixel unpack buffers or non-default row
    // strides bound. Save and neutralize every setting affecting this 2D upload.
    GL_ClearErrors();
    GLint previousTexture = 0, previousUnpackBuffer = 0;
    GLint alignment = 0, rowLength = 0, skipRows = 0, skipPixels = 0;
    glGetIntegerv( GL_TEXTURE_BINDING_2D, &previousTexture );
    glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &previousUnpackBuffer );
    glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
    glGetIntegerv( GL_UNPACK_ROW_LENGTH, &rowLength );
    glGetIntegerv( GL_UNPACK_SKIP_ROWS, &skipRows );
    glGetIntegerv( GL_UNPACK_SKIP_PIXELS, &skipPixels );
    auto result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) return result;
    GLuint texture = 0;
    glGenTextures( 1, &texture );
    if ( texture == 0u ) {
        result = GL_CheckErrors( glState.config.validation );
        return result != render_error_t::OK ? result : render_error_t::ERR_RESOURCE_CREATE_FAILED;
    }
    glBindTexture( GL_TEXTURE_2D, texture );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0u );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
    glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, description.generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, description.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, description.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0 );
    if ( !description.generateMips ) glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
    glTexImage2D( GL_TEXTURE_2D, 0, description.sRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8,
        static_cast<GLsizei>( description.width ), static_cast<GLsizei>( description.height ),
        0, GL_RGBA, GL_UNSIGNED_BYTE, data.bytes );
    result = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK && description.generateMips ) {
        glGenerateMipmap( GL_TEXTURE_2D );
        result = GL_CheckErrors( glState.config.validation );
    }
    glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, rowLength );
    glPixelStorei( GL_UNPACK_SKIP_ROWS, skipRows );
    glPixelStorei( GL_UNPACK_SKIP_PIXELS, skipPixels );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>( previousUnpackBuffer ) );
    glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( previousTexture ) );
    const auto restored = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK ) result = restored;
    if ( result != render_error_t::OK ) {
        glDeleteTextures( 1, &texture );
        (void)GL_CheckErrors( glState.config.validation );
        return result;
    }
    textureOut.value = texture;
    return render_error_t::OK;
}
render_error_t GL_DestroyTexture( backend_texture_t texture, void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( texture.value == 0u || texture.value > std::numeric_limits<GLuint>::max() ) return render_error_t::ERR_INVALID_HANDLE;
    const GLuint name = static_cast<GLuint>( texture.value );
    GL_ClearErrors();
    glDeleteTextures( 1, &name );
    return GL_CheckErrors( glState.config.validation );
}
} // namespace cypher::engine::render
