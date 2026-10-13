//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Owns OpenGL 4.1 offscreen color and/or depth framebuffer records.
// Details: Attachments belong to the texture system. Targets own their
//          framebuffer and, without a depth texture, a private D24 renderbuffer.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL_Target.h"
#include "CypherRender_OpenGL_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <new>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
struct gl_target_record_t {
    gl_target_record_t *next{ nullptr };
    GLuint framebuffer{ 0u };
    GLuint depthRenderbuffer{ 0u };
    render_extent_t extent{};
    bool hasColor{ false };
    render_texture_format_t colorFormat{ render_texture_format_t::RGBA8 };
};

// Compare tokens before dereferencing. The frontend owns generation checks;
// this list prevents a malformed backend token from becoming a raw pointer.
gl_target_record_t *glTargets = nullptr;

common::u64 GL_TargetToken( const gl_target_record_t *record ) noexcept
{
    return static_cast<common::u64>( reinterpret_cast<std::uintptr_t>( record ) );
}

gl_target_record_t *GL_FindTarget( const backend_render_target_t target ) noexcept
{
    for ( gl_target_record_t *record = glTargets; record != nullptr; record = record->next ) {
        if ( GL_TargetToken( record ) == target.value ) return record;
    }
    return nullptr;
}

void GL_FreeTargetRecord( gl_target_record_t *record ) noexcept
{
    record->~gl_target_record_t();
    common::Allocator_Free( common::Allocator_GetSystem(), record,
        sizeof( gl_target_record_t ), alignof( gl_target_record_t ) );
}

void GL_RestoreFramebufferBindings( const GLint draw, const GLint read,
    const GLint renderbuffer ) noexcept
{
    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, static_cast<GLuint>( draw ) );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, static_cast<GLuint>( read ) );
    glBindRenderbuffer( GL_RENDERBUFFER, static_cast<GLuint>( renderbuffer ) );
}

bool GL_TargetExtentSupported( const render_extent_t extent ) noexcept
{
    const auto glMax = static_cast<common::u32>( std::numeric_limits<GLsizei>::max() );
    return extent.width != 0u && extent.height != 0u &&
        extent.width <= glState.limits.maxTexture2DSize &&
        extent.height <= glState.limits.maxTexture2DSize &&
        extent.width <= glState.limits.maxViewportWidth &&
        extent.height <= glState.limits.maxViewportHeight &&
        extent.width <= glMax && extent.height <= glMax;
}
} // namespace

render_error_t GL_CreateRenderTarget( const backend_render_target_desc_t &description,
    backend_render_target_t &targetOut, void *backendState ) noexcept
{
    targetOut = {};
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( glState.frameActive ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    const bool hasColor = description.color.value != 0u;
    const bool hasDepth = description.depth.value != 0u;
    if ( ( !hasColor && !hasDepth ) ||
         description.color.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    if ( hasColor && description.colorFormat != render_texture_format_t::RGBA8 &&
         description.colorFormat != render_texture_format_t::RGBA16F ) {
        return render_error_t::ERR_RENDER_TARGET_INVALID;
    }
    if ( description.depth.value > std::numeric_limits<GLuint>::max() ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    if ( !GL_TargetExtentSupported( description.extent ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }

    void *storage = common::Allocator_Allocate( common::Allocator_GetSystem(),
        sizeof( gl_target_record_t ), alignof( gl_target_record_t ) );
    if ( storage == nullptr ) return render_error_t::ERR_OUT_OF_MEMORY;
    auto *record = new ( storage ) gl_target_record_t{};

    GL_ClearErrors();
    GLint previousDraw = 0;
    GLint previousRead = 0;
    GLint previousRenderbuffer = 0;
    GLint maxRenderbufferSize = 0;
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw );
    glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousRead );
    glGetIntegerv( GL_RENDERBUFFER_BINDING, &previousRenderbuffer );
    if ( description.depth.value == 0u ) {
        glGetIntegerv( GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize );
    }
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) {
        GL_FreeTargetRecord( record );
        return result;
    }
    if ( description.depth.value == 0u &&
         ( maxRenderbufferSize <= 0 ||
           description.extent.width > static_cast<common::u32>( maxRenderbufferSize ) ||
           description.extent.height > static_cast<common::u32>( maxRenderbufferSize ) ) ) {
        GL_FreeTargetRecord( record );
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }

    glGenFramebuffers( 1, &record->framebuffer );
    if ( description.depth.value == 0u ) {
        glGenRenderbuffers( 1, &record->depthRenderbuffer );
    }
    result = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK &&
         ( record->framebuffer == 0u ||
           ( description.depth.value == 0u && record->depthRenderbuffer == 0u ) ) ) {
        result = render_error_t::ERR_RESOURCE_CREATE_FAILED;
    }
    if ( result == render_error_t::OK ) {
        glBindFramebuffer( GL_FRAMEBUFFER, record->framebuffer );
        if ( hasColor ) {
            glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, static_cast<GLuint>( description.color.value ), 0 );
        }
        if ( hasDepth ) {
            glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_TEXTURE_2D, static_cast<GLuint>( description.depth.value ), 0 );
        } else {
            glBindRenderbuffer( GL_RENDERBUFFER, record->depthRenderbuffer );
            glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                static_cast<GLsizei>( description.extent.width ),
                static_cast<GLsizei>( description.extent.height ) );
            glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_RENDERBUFFER, record->depthRenderbuffer );
        }
        glDrawBuffer( hasColor ? GL_COLOR_ATTACHMENT0 : GL_NONE );
        glReadBuffer( hasColor ? GL_COLOR_ATTACHMENT0 : GL_NONE );
        result = GL_CheckErrors( glState.config.validation );
        if ( result == render_error_t::OK ) {
            const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
            result = GL_CheckErrors( glState.config.validation );
            if ( result == render_error_t::OK && status != GL_FRAMEBUFFER_COMPLETE ) {
                result = render_error_t::ERR_FRAMEBUFFER_INCOMPLETE;
            }
        }
    }

    // GL_FRAMEBUFFER binds draw and read targets together. Restore both
    // independently because hosts may have supplied different FBOs.
    GL_RestoreFramebufferBindings( previousDraw, previousRead, previousRenderbuffer );
    const render_error_t restoreResult = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK ) result = restoreResult;
    if ( result != render_error_t::OK ) {
        if ( record->depthRenderbuffer != 0u ) glDeleteRenderbuffers( 1, &record->depthRenderbuffer );
        if ( record->framebuffer != 0u ) glDeleteFramebuffers( 1, &record->framebuffer );
        (void)GL_CheckErrors( glState.config.validation );
        GL_FreeTargetRecord( record );
        return result;
    }

    record->extent = description.extent;
    record->hasColor = hasColor;
    record->colorFormat = description.colorFormat;
    record->next = glTargets;
    glTargets = record;
    targetOut.value = GL_TargetToken( record );
    return render_error_t::OK;
}

render_error_t GL_DestroyRenderTarget( const backend_render_target_t target,
    void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( glState.frameActive ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    gl_target_record_t **link = &glTargets;
    while ( *link != nullptr && GL_TargetToken( *link ) != target.value ) {
        link = &( *link )->next;
    }
    if ( *link == nullptr ) return render_error_t::ERR_INVALID_HANDLE;
    gl_target_record_t *record = *link;

    GL_ClearErrors();
    if ( record->depthRenderbuffer != 0u ) {
        glDeleteRenderbuffers( 1, &record->depthRenderbuffer );
    }
    glDeleteFramebuffers( 1, &record->framebuffer );
    const render_error_t result = GL_CheckErrors( glState.config.validation );
    // A GL deletion error is rare, but leave the record linked so shutdown can
    // attempt cleanup again instead of silently dropping ownership.
    if ( result != render_error_t::OK ) return result;
    *link = record->next;
    GL_FreeTargetRecord( record );
    return render_error_t::OK;
}

render_error_t GL_BeginTargetPass( const backend_target_pass_desc_t &description,
    void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !glState.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( glState.targetPassActive ) return render_error_t::ERR_COMMAND_STATE_INVALID;
    const gl_target_record_t *record = GL_FindTarget( description.target );
    if ( record == nullptr ) return render_error_t::ERR_INVALID_HANDLE;
    if ( description.extent.width != record->extent.width ||
         description.extent.height != record->extent.height ||
         ( description.clearFlags & ~( R_CLEAR_COLOR | R_CLEAR_DEPTH ) ) != 0u ||
         ( !record->hasColor && ( description.clearFlags & R_CLEAR_COLOR ) != 0u ) ||
         !std::isfinite( description.clearDepth ) || description.clearDepth < 0.0f ||
         description.clearDepth > 1.0f ||
         !std::isfinite( description.clearColor.red ) ||
         !std::isfinite( description.clearColor.green ) ||
         !std::isfinite( description.clearColor.blue ) ||
         !std::isfinite( description.clearColor.alpha ) ) {
        return render_error_t::ERR_RENDER_TARGET_INVALID;
    }

    GL_ClearErrors();
    glBindFramebuffer( GL_FRAMEBUFFER, record->framebuffer );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) {
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>( glState.presentationDrawFramebuffer ) );
        glBindFramebuffer( GL_READ_FRAMEBUFFER,
            static_cast<GLuint>( glState.presentationReadFramebuffer ) );
        if ( glState.usesHostSurface ) {
            glViewportIndexedfv( 0u, glState.presentationViewport );
        } else {
            glViewport( static_cast<GLint>( glState.presentationViewport[0] ),
                static_cast<GLint>( glState.presentationViewport[1] ),
                static_cast<GLsizei>( glState.presentationViewport[2] ),
                static_cast<GLsizei>( glState.presentationViewport[3] ) );
        }
        (void)GL_CheckErrors( glState.config.validation );
        return result;
    }
    if ( glState.usesHostSurface ) {
        glViewportIndexedf( 0u, 0.0f, 0.0f,
            static_cast<GLfloat>( record->extent.width ),
            static_cast<GLfloat>( record->extent.height ) );
        glColorMaski( 0u, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glDisablei( GL_SCISSOR_TEST, 0u );
    } else {
        glViewport( 0, 0, static_cast<GLsizei>( record->extent.width ),
            static_cast<GLsizei>( record->extent.height ) );
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glDisable( GL_SCISSOR_TEST );
    }
    glDepthMask( GL_TRUE );
    GLbitfield clearMask = 0u;
    if ( ( description.clearFlags & R_CLEAR_COLOR ) != 0u ) {
        glClearColor( description.clearColor.red, description.clearColor.green,
            description.clearColor.blue, description.clearColor.alpha );
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    if ( ( description.clearFlags & R_CLEAR_DEPTH ) != 0u ) {
        glClearDepth( static_cast<GLdouble>( description.clearDepth ) );
        clearMask |= GL_DEPTH_BUFFER_BIT;
    }
    if ( clearMask != 0u ) glClear( clearMask );
    result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) {
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>( glState.presentationDrawFramebuffer ) );
        glBindFramebuffer( GL_READ_FRAMEBUFFER,
            static_cast<GLuint>( glState.presentationReadFramebuffer ) );
        if ( glState.usesHostSurface ) {
            glViewportIndexedfv( 0u, glState.presentationViewport );
        } else {
            glViewport( static_cast<GLint>( glState.presentationViewport[0] ),
                static_cast<GLint>( glState.presentationViewport[1] ),
                static_cast<GLsizei>( glState.presentationViewport[2] ),
                static_cast<GLsizei>( glState.presentationViewport[3] ) );
        }
        (void)GL_CheckErrors( glState.config.validation );
        return result;
    }

    glState.targetPassExtent = record->extent;
    glState.targetPassActive = true;
    return render_error_t::OK;
}

render_error_t GL_EndTargetPass( void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !glState.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( !glState.targetPassActive ) return render_error_t::ERR_COMMAND_STATE_INVALID;

    GL_ClearErrors();
    glBindFramebuffer( GL_DRAW_FRAMEBUFFER,
        static_cast<GLuint>( glState.presentationDrawFramebuffer ) );
    glBindFramebuffer( GL_READ_FRAMEBUFFER,
        static_cast<GLuint>( glState.presentationReadFramebuffer ) );
    if ( glState.usesHostSurface ) {
        glViewportIndexedfv( 0u, glState.presentationViewport );
    } else {
        glViewport( static_cast<GLint>( glState.presentationViewport[0] ),
            static_cast<GLint>( glState.presentationViewport[1] ),
            static_cast<GLsizei>( glState.presentationViewport[2] ),
            static_cast<GLsizei>( glState.presentationViewport[3] ) );
    }
    const render_error_t result = GL_CheckErrors( glState.config.validation );
    // The frontend ends this pass even when restoration reports an error.
    // Keep backend frame state aligned so the caller can still close the frame.
    glState.targetPassActive = false;
    glState.targetPassExtent = {};
    return result;
}

render_error_t GL_ReadTargetRGBA8( const backend_target_readback_desc_t &description,
    void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !glState.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( glState.targetPassActive ) return render_error_t::ERR_COMMAND_STATE_INVALID;
    const gl_target_record_t *record = GL_FindTarget( description.target );
    if ( record == nullptr ) return render_error_t::ERR_INVALID_HANDLE;
    if ( !record->hasColor || record->colorFormat != render_texture_format_t::RGBA8 ) {
        return render_error_t::ERR_CAPTURE_UNSUPPORTED;
    }

    const render_readback_rect_t &rect = description.rect;
    if ( description.pixelsOut == nullptr || rect.width == 0u || rect.height == 0u ||
         rect.x >= record->extent.width || rect.y >= record->extent.height ||
         rect.width > record->extent.width - rect.x ||
         rect.height > record->extent.height - rect.y ||
         ( description.rowPitchBytes & 3u ) != 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    const common::u64 rowBytes = static_cast<common::u64>( rect.width ) * 4u;
    if ( rowBytes > description.rowPitchBytes ) return render_error_t::ERR_INVALID_ARGUMENT;
    const common::u64 rowStart = static_cast<common::u64>( rect.height - 1u ) *
        description.rowPitchBytes;
    if ( rowStart > std::numeric_limits<common::u64>::max() - rowBytes ||
         rowStart + rowBytes > std::numeric_limits<common::usize>::max() ||
         description.byteCapacity < rowStart + rowBytes ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    // A host may enter with a pixel-pack buffer and nondefault row layout.
    // Without unbinding that buffer, glReadPixels treats pixelsOut as an offset
    // into the host's PBO. Capture every state we change before touching it.
    GL_ClearErrors();
    GLint previousReadFramebuffer = 0;
    GLint previousPackBuffer = 0;
    GLint previousPackAlignment = 0;
    GLint previousPackRowLength = 0;
    GLint previousPackSkipRows = 0;
    GLint previousPackSkipPixels = 0;
    glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer );
    glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer );
    glGetIntegerv( GL_PACK_ALIGNMENT, &previousPackAlignment );
    glGetIntegerv( GL_PACK_ROW_LENGTH, &previousPackRowLength );
    glGetIntegerv( GL_PACK_SKIP_ROWS, &previousPackSkipRows );
    glGetIntegerv( GL_PACK_SKIP_PIXELS, &previousPackSkipPixels );
    const render_error_t captureResult = GL_CheckErrors( glState.config.validation );
    if ( captureResult != render_error_t::OK ) return captureResult;

    glBindFramebuffer( GL_READ_FRAMEBUFFER, record->framebuffer );
    glBindBuffer( GL_PIXEL_PACK_BUFFER, 0u );
    glPixelStorei( GL_PACK_ALIGNMENT, 1 );
    glPixelStorei( GL_PACK_ROW_LENGTH,
        static_cast<GLint>( description.rowPitchBytes / 4u ) );
    glPixelStorei( GL_PACK_SKIP_ROWS, 0 );
    glPixelStorei( GL_PACK_SKIP_PIXELS, 0 );
    render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK ) {
        glReadPixels( static_cast<GLint>( rect.x ), static_cast<GLint>( rect.y ),
            static_cast<GLsizei>( rect.width ), static_cast<GLsizei>( rect.height ),
            GL_RGBA, GL_UNSIGNED_BYTE, description.pixelsOut );
        result = GL_CheckErrors( glState.config.validation );
    }

    glBindFramebuffer( GL_READ_FRAMEBUFFER, static_cast<GLuint>( previousReadFramebuffer ) );
    glBindBuffer( GL_PIXEL_PACK_BUFFER, static_cast<GLuint>( previousPackBuffer ) );
    glPixelStorei( GL_PACK_ALIGNMENT, previousPackAlignment );
    glPixelStorei( GL_PACK_ROW_LENGTH, previousPackRowLength );
    glPixelStorei( GL_PACK_SKIP_ROWS, previousPackSkipRows );
    glPixelStorei( GL_PACK_SKIP_PIXELS, previousPackSkipPixels );
    const render_error_t restoreResult = GL_CheckErrors( glState.config.validation );
    if ( result == render_error_t::OK ) result = restoreResult;
    return result == render_error_t::ERR_FAILED
        ? render_error_t::ERR_CAPTURE_FAILED : result;
}

render_error_t GL_TargetsShutdown() noexcept
{
    GL_ClearErrors();
    while ( glTargets != nullptr ) {
        gl_target_record_t *record = glTargets;
        glTargets = record->next;
        if ( record->depthRenderbuffer != 0u ) {
            glDeleteRenderbuffers( 1, &record->depthRenderbuffer );
        }
        glDeleteFramebuffers( 1, &record->framebuffer );
        GL_FreeTargetRecord( record );
    }
    return GL_CheckErrors( glState.config.validation );
}

void GL_TargetsAbandon() noexcept
{
    while ( glTargets != nullptr ) {
        gl_target_record_t *record = glTargets;
        glTargets = record->next;
        GL_FreeTargetRecord( record );
    }
}
} // namespace cypher::engine::render
