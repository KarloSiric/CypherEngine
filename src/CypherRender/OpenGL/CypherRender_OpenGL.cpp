//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL.cpp
//  Purpose: Implements the first hardware renderer backend using OpenGL 4.1.
//  Details: CypherSystem owns native SDL context operations. This file owns
//           GLAD, OpenGL capability discovery, frame clear/presentation, and
//           all backend state visible through the generic dispatch table.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL.h"

#include "CypherRender_OpenGL_Buffer.h"
#include "CypherRender_OpenGL_Local.h"

#include "CypherSystem/CypherSystem_OpenGL.h"

#include <glad/gl.h>

#include <algorithm> // std::min for bounded diagnostic-string copies.
#include <cmath>     // std::isfinite for defensive API value checks.
#include <cstring>   // std::strcmp and std::memcpy for extension/string handling.
#include <limits>    // Safe conversion from renderer extents to GLsizei.

namespace cypher::engine::render
{

gl_state_t glState{};

namespace
{

// GLAD asks for native entry points by name; System keeps SDL out of Renderer.
GLADapiproc GL_LoadProc( const char *name ) noexcept
{
    return reinterpret_cast<GLADapiproc>(
        ::cypher::engine::sys::GLimp_GetProcAddress( name ) );
}

template <::cypher::common::usize Capacity>
// Copies driver-owned strings because their lifetime ends with the GL context.
bool GL_CopyDriverString(
    char ( &destination )[Capacity],
    const GLubyte *source ) noexcept
{
    destination[0] = '\0';
    if ( source == nullptr ) {
        return false;
    }

    const char *text = reinterpret_cast<const char *>( source );
    const ::cypher::common::usize sourceLength = std::strlen( text );
    const ::cypher::common::usize copyLength = std::min(
        sourceLength,
        static_cast<::cypher::common::usize>( Capacity - 1u ) );
    std::memcpy( destination, text, copyLength );
    destination[copyLength] = '\0';
    return true;
}

bool GL_VersionAtLeast(
    const ::cypher::engine::sys::gl_context_info_t &contextInfo,
    const ::cypher::common::u8 major,
    const ::cypher::common::u8 minor ) noexcept
{
    return contextInfo.majorVersion > major ||
        ( contextInfo.majorVersion == major && contextInfo.minorVersion >= minor );
}

// Core-profile extension lookup avoids the deprecated GL_EXTENSIONS string.
bool GL_HasExtension( const char *extensionName ) noexcept
{
    if ( extensionName == nullptr || extensionName[0] == '\0' ||
         glGetStringi == nullptr ) {
        return false;
    }

    GLint extensionCount = 0;
    glGetIntegerv( GL_NUM_EXTENSIONS, &extensionCount );
    if ( extensionCount <= 0 ) {
        return false;
    }

    for ( GLint extensionIndex = 0; extensionIndex < extensionCount; ++extensionIndex ) {
        const GLubyte *extension = glGetStringi(
            GL_EXTENSIONS,
            static_cast<GLuint>( extensionIndex ) );
        if ( extension != nullptr && std::strcmp(
                reinterpret_cast<const char *>( extension ),
                extensionName ) == 0 ) {
            return true;
        }
    }

    return false;
}

// Converts a nonnegative GLint device limit into Cypher's unsigned contract.
bool GL_QueryUnsignedLimit(
    const GLenum query,
    ::cypher::common::u32 &valueOut ) noexcept
{
    GLint nativeValue = 0;
    glGetIntegerv( query, &nativeValue );
    if ( nativeValue < 0 ) {
        valueOut = 0u;
        return false;
    }

    valueOut = static_cast<::cypher::common::u32>( nativeValue );
    return true;
}

// Captures all numeric limits needed by the current renderer ABI in one pass.
bool GL_QueryLimits( render_limits_t &limitsOut ) noexcept
{
    limitsOut = {};
    GLint viewportDimensions[2]{};
    GLint64 uniformBlockBytes = 0;

    const bool scalarQueriesSucceeded =
        GL_QueryUnsignedLimit( GL_MAX_TEXTURE_SIZE, limitsOut.maxTexture2DSize ) &&
        GL_QueryUnsignedLimit( GL_MAX_3D_TEXTURE_SIZE, limitsOut.maxTexture3DSize ) &&
        GL_QueryUnsignedLimit( GL_MAX_CUBE_MAP_TEXTURE_SIZE, limitsOut.maxCubeMapSize ) &&
        GL_QueryUnsignedLimit( GL_MAX_ARRAY_TEXTURE_LAYERS, limitsOut.maxArrayTextureLayers ) &&
        GL_QueryUnsignedLimit( GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, limitsOut.maxCombinedTextureUnits ) &&
        GL_QueryUnsignedLimit( GL_MAX_VERTEX_ATTRIBS, limitsOut.maxVertexAttributes ) &&
        GL_QueryUnsignedLimit( GL_MAX_UNIFORM_BUFFER_BINDINGS, limitsOut.maxUniformBufferBindings ) &&
        GL_QueryUnsignedLimit( GL_MAX_COLOR_ATTACHMENTS, limitsOut.maxColorAttachments ) &&
        GL_QueryUnsignedLimit( GL_MAX_DRAW_BUFFERS, limitsOut.maxDrawBuffers ) &&
        GL_QueryUnsignedLimit( GL_MAX_SAMPLES, limitsOut.maxSamples );

    glGetIntegerv( GL_MAX_VIEWPORT_DIMS, viewportDimensions );
    glGetInteger64v( GL_MAX_UNIFORM_BLOCK_SIZE, &uniformBlockBytes );
    if ( !scalarQueriesSucceeded || viewportDimensions[0] <= 0 ||
         viewportDimensions[1] <= 0 || uniformBlockBytes <= 0 ) {
        limitsOut = {};
        return false;
    }

    limitsOut.maxViewportWidth = static_cast<::cypher::common::u32>( viewportDimensions[0] );
    limitsOut.maxViewportHeight = static_cast<::cypher::common::u32>( viewportDimensions[1] );
    limitsOut.maxUniformBlockBytes = static_cast<::cypher::common::u64>( uniformBlockBytes );
    return glGetError() == GL_NO_ERROR;
}

// Advertises only features callable through the GLAD profile compiled here.
render_capability_flags_t GL_QueryCapabilities(
    const ::cypher::engine::sys::gl_context_info_t &contextInfo,
    const render_limits_t &limits ) noexcept
{
    render_capability_flags_t capabilities = R_CAPABILITY_RASTERIZATION;

    if ( GL_VersionAtLeast( contextInfo, 3u, 1u ) ) capabilities |= R_CAPABILITY_INSTANCING;
    if ( GL_VersionAtLeast( contextInfo, 4u, 0u ) ) capabilities |= R_CAPABILITY_DRAW_INDIRECT;
    if ( GL_VersionAtLeast( contextInfo, 3u, 2u ) ) capabilities |= R_CAPABILITY_GEOMETRY_SHADER;
    if ( GL_VersionAtLeast( contextInfo, 4u, 0u ) ) capabilities |= R_CAPABILITY_TESSELLATION_SHADER;
    if ( GL_VersionAtLeast( contextInfo, 3u, 0u ) ) capabilities |= R_CAPABILITY_TEXTURE_ARRAYS;
    if ( GL_VersionAtLeast( contextInfo, 4u, 0u ) ) capabilities |= R_CAPABILITY_CUBE_MAP_ARRAYS;
    if ( GL_VersionAtLeast( contextInfo, 3u, 0u ) ) capabilities |= R_CAPABILITY_FLOAT_RENDER_TARGETS;
    if ( GL_VersionAtLeast( contextInfo, 3u, 3u ) ) capabilities |= R_CAPABILITY_GPU_TIMESTAMPS;
    if ( contextInfo.sRGBFramebuffer ) capabilities |= R_CAPABILITY_SRGB_FRAMEBUFFER;
    if ( limits.maxSamples >= 2u ) capabilities |= R_CAPABILITY_MULTISAMPLING;

    if ( GL_HasExtension( "GL_EXT_texture_filter_anisotropic" ) ||
         GL_HasExtension( "GL_ARB_texture_filter_anisotropic" ) ) {
        capabilities |= R_CAPABILITY_ANISOTROPIC_FILTERING;
    }
    if ( GL_HasExtension( "GL_ARB_texture_compression_bptc" ) ||
         GL_HasExtension( "GL_EXT_texture_compression_s3tc" ) ||
         GL_HasExtension( "GL_EXT_texture_compression_dxt1" ) ) {
        capabilities |= R_CAPABILITY_TEXTURE_COMPRESSION_BC;
    }
    if ( GL_HasExtension( "GL_ARB_ES3_compatibility" ) ||
         GL_HasExtension( "GL_OES_compressed_ETC2_RGB8_texture" ) ) {
        capabilities |= R_CAPABILITY_TEXTURE_COMPRESSION_ETC;
    }
    if ( GL_HasExtension( "GL_KHR_texture_compression_astc_ldr" ) ||
         GL_HasExtension( "GL_KHR_texture_compression_astc_hdr" ) ) {
        capabilities |= R_CAPABILITY_TEXTURE_COMPRESSION_ASTC;
    }

    // GLAD is currently generated for core OpenGL 4.1 without extension
    // entry points. Do not advertise debug callbacks, compute, SSBOs, or MDI
    // merely because a newer driver string mentions them.
    return capabilities;
}

// Maps backend-neutral presentation policy to SDL's OpenGL swap interval.
render_error_t GL_ApplyPresentMode(
    const render_present_mode_t requestedMode,
    render_present_mode_t &actualModeOut ) noexcept
{
    using namespace ::cypher::engine::sys;

    actualModeOut = requestedMode;
    switch ( requestedMode ) {
        case render_present_mode_t::IMMEDIATE:
            return GLimp_SetSwapInterval( gl_swap_interval_t::IMMEDIATE ) == sys_error_t::OK
                ? render_error_t::OK : render_error_t::ERR_PRESENT_FAILED;

        case render_present_mode_t::FIFO:
            return GLimp_SetSwapInterval( gl_swap_interval_t::SYNCHRONIZED ) == sys_error_t::OK
                ? render_error_t::OK : render_error_t::ERR_PRESENT_FAILED;

        case render_present_mode_t::FIFO_RELAXED:
            if ( GLimp_SetSwapInterval( gl_swap_interval_t::ADAPTIVE ) == sys_error_t::OK ) {
                return render_error_t::OK;
            }
            if ( GLimp_SetSwapInterval( gl_swap_interval_t::SYNCHRONIZED ) == sys_error_t::OK ) {
                actualModeOut = render_present_mode_t::FIFO;
                return render_error_t::OK;
            }
            return render_error_t::ERR_PRESENT_FAILED;

        case render_present_mode_t::MAILBOX:
            return render_error_t::ERR_UNSUPPORTED;

        case render_present_mode_t::COUNT:
        default:
            return render_error_t::ERR_INVALID_ARGUMENT;
    }
}

/*
================
GL_ConfigureWindow

Selects context attributes before SDL creates the native window. OpenGL 4.1
core is the portable baseline shared by macOS, Linux, and Windows builds.
================
*/
render_error_t GL_ConfigureWindow(
    const render_config_t &config,
    ::cypher::engine::sys::window_desc_t &windowDescription,
    void *backendState ) noexcept
{
    if ( backendState != &glState || glState.initialized ) {
        return render_error_t::ERR_INVALID_STATE;
    }

    using namespace ::cypher::engine::sys;
    windowDescription.graphicsApi = window_graphics_api_t::OPENGL;
    windowDescription.openGL = {};
    windowDescription.openGL.majorVersion = 4u;
    windowDescription.openGL.minorVersion = 1u;
    windowDescription.openGL.profile = gl_profile_t::CORE;
    windowDescription.openGL.flags = GLIMP_CONTEXT_FORWARD_COMPATIBLE;
    if ( config.validation == render_validation_t::VERBOSE ) {
        windowDescription.openGL.flags |= GLIMP_CONTEXT_DEBUG;
    }
    windowDescription.openGL.sampleCount = config.sampleCount;
    windowDescription.openGL.sRGBFramebuffer = config.sRGBFramebuffer;
    windowDescription.openGL.requireAcceleration = config.requireAcceleration;
    return render_error_t::OK;
}

/*
================
GL_Init

Creates and activates the context, loads entry points, discovers capabilities,
and publishes borrowed diagnostic strings through render_info_t. Every failure
path destroys partial native state before returning to the frontend.
================
*/
render_error_t GL_Init(
    ::cypher::engine::sys::window_t &window,
    const render_config_t &config,
    render_info_t &infoOut,
    void *backendState ) noexcept
{
    infoOut = {};
    if ( backendState != &glState ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( glState.initialized ) {
        return render_error_t::ERR_ALREADY_INITIALIZED;
    }
    if ( !::cypher::engine::sys::Sys_WindowIsValid( window ) ||
         window.graphicsApi != ::cypher::engine::sys::window_graphics_api_t::OPENGL ) {
        return render_error_t::ERR_WINDOW_INCOMPATIBLE;
    }

    using namespace ::cypher::engine::sys;          // using for naming simplification
    gl_context_t context{};
    const sys_error_t createResult = GLimp_CreateContext( window, context );
    if ( createResult != sys_error_t::OK ) {
        return render_error_t::ERR_CONTEXT_CREATE_FAILED;
    }
    if ( GLimp_MakeCurrent( window, context ) != sys_error_t::OK ) {
        (void)GLimp_DestroyContext( context );
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }
    if ( gladLoadGL( GL_LoadProc ) == 0 || GLAD_GL_VERSION_4_1 == 0 ) {
        (void)GLimp_DestroyContext( context );
        return render_error_t::ERR_ENTRYPOINT_LOAD_FAILED;
    }

    gl_context_info_t contextInfo{};
    if ( GLimp_QueryContextInfo( contextInfo ) != sys_error_t::OK ||
         !GL_VersionAtLeast( contextInfo, 4u, 1u ) ) {
        (void)GLimp_DestroyContext( context );
        return render_error_t::ERR_BACKEND_VERSION_UNSUPPORTED;
    }

    GL_ClearErrors();
    render_limits_t limits{};
    const bool stringsAvailable =
        GL_CopyDriverString( glState.apiName, glGetString( GL_VERSION ) ) &&
        GL_CopyDriverString( glState.deviceName, glGetString( GL_RENDERER ) ) &&
        GL_CopyDriverString( glState.vendorName, glGetString( GL_VENDOR ) ) &&
        GL_CopyDriverString( glState.driverVersion, glGetString( GL_VERSION ) ) &&
        GL_CopyDriverString(
            glState.shadingLanguageVersion,
            glGetString( GL_SHADING_LANGUAGE_VERSION ) );
    if ( !stringsAvailable || !GL_QueryLimits( limits ) ) {
        (void)GLimp_DestroyContext( context );
        glState = {};
        return render_error_t::ERR_DEVICE_QUERY_FAILED;
    }

    // The public API name is intentionally stable; the full implementation
    // version remains available through driverVersion.
    constexpr char apiName[] = "OpenGL";
    std::memcpy( glState.apiName, apiName, sizeof( apiName ) );

    glState.window = &window;
    glState.context = context;
    glState.config = config;
    glState.limits = limits;
    glState.drawableExtent = { window.width, window.height };
    glState.initialized = true;

    render_present_mode_t actualPresentMode = config.presentMode;
    const render_error_t presentResult = GL_ApplyPresentMode(
        config.presentMode,
        actualPresentMode );
    if ( presentResult != render_error_t::OK ) {
        (void)GLimp_DestroyContext( glState.context );
        glState = {};
        return presentResult;
    }
    glState.presentMode = actualPresentMode;

    const render_capability_flags_t capabilities = GL_QueryCapabilities(
        contextInfo,
        limits );
    if ( config.sRGBFramebuffer &&
         ( capabilities & R_CAPABILITY_SRGB_FRAMEBUFFER ) == 0u ) {
        (void)GLimp_DestroyContext( glState.context );
        glState = {};
        return render_error_t::ERR_CAPABILITY_MISSING;
    }
    if ( config.sampleCount != 0u && contextInfo.sampleCount != config.sampleCount ) {
        (void)GLimp_DestroyContext( glState.context );
        glState = {};
        return render_error_t::ERR_CAPABILITY_MISSING;
    }

    glViewport( 0, 0, static_cast<GLsizei>( window.width ), static_cast<GLsizei>( window.height ) );
    if ( contextInfo.sRGBFramebuffer ) glEnable( GL_FRAMEBUFFER_SRGB );
    else glDisable( GL_FRAMEBUFFER_SRGB );
    if ( contextInfo.sampleCount > 0u ) glEnable( GL_MULTISAMPLE );
    else glDisable( GL_MULTISAMPLE );
    const render_error_t nativeResult = GL_CheckErrors( config.validation );
    if ( nativeResult != render_error_t::OK ) {
        (void)GLimp_DestroyContext( glState.context );
        glState = {};
        return nativeResult;
    }

    infoOut.backend = render_backend_t::OPENGL;
    infoOut.mode = render_mode_t::RASTER;
    infoOut.presentMode = actualPresentMode;
    infoOut.capabilities = capabilities;
    infoOut.drawableExtent = glState.drawableExtent;
    infoOut.limits = limits;
    infoOut.apiMajorVersion = contextInfo.majorVersion;
    infoOut.apiMinorVersion = contextInfo.minorVersion;
    infoOut.sampleCount = contextInfo.sampleCount;
    infoOut.accelerated = contextInfo.accelerated;
    infoOut.apiName = glState.apiName;
    infoOut.deviceName = glState.deviceName;
    infoOut.vendorName = glState.vendorName;
    infoOut.driverVersion = glState.driverVersion;
    infoOut.shadingLanguageVersion = glState.shadingLanguageVersion;
    return render_error_t::OK;
}

// Drains native work and destroys the context before Host destroys its window.
render_error_t GL_Shutdown( void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( glState.frameActive ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;

    render_error_t result = render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    if ( glState.window != nullptr &&
         ::cypher::engine::sys::GLimp_MakeCurrent(
             *glState.window,
             glState.context ) == ::cypher::engine::sys::sys_error_t::OK ) {
        glFinish();
        result = GL_CheckErrors( glState.config.validation );
    }

    if ( ::cypher::engine::sys::GLimp_DestroyContext( glState.context ) !=
         ::cypher::engine::sys::sys_error_t::OK ) {
        return render_error_t::ERR_DEVICE_LOST;
    }

    glState = {};
    return result;
}

// Opens one frame, restores the default viewport, and clears requested buffers.
render_error_t GL_BeginFrame(
    const render_frame_info_t &frameInfo,
    void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized || glState.window == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( glState.frameActive ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    if ( frameInfo.drawableExtent.width == 0u || frameInfo.drawableExtent.height == 0u ||
         frameInfo.drawableExtent.width > glState.limits.maxViewportWidth ||
         frameInfo.drawableExtent.height > glState.limits.maxViewportHeight ||
         frameInfo.drawableExtent.width > static_cast<::cypher::common::u32>(
             std::numeric_limits<GLsizei>::max() ) ||
         frameInfo.drawableExtent.height > static_cast<::cypher::common::u32>(
             std::numeric_limits<GLsizei>::max() ) ) {
        return render_error_t::ERR_VIEW_INVALID;
    }
    if ( ::cypher::engine::sys::GLimp_MakeCurrent(
            *glState.window,
            glState.context ) != ::cypher::engine::sys::sys_error_t::OK ) {
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }

    GL_ClearErrors();
    glViewport(
        0,
        0,
        static_cast<GLsizei>( frameInfo.drawableExtent.width ),
        static_cast<GLsizei>( frameInfo.drawableExtent.height ) );

    GLbitfield clearMask = 0u;
    if ( ( frameInfo.clearFlags & R_CLEAR_COLOR ) != 0u ) {
        glClearColor(
            frameInfo.clearColor.red,
            frameInfo.clearColor.green,
            frameInfo.clearColor.blue,
            frameInfo.clearColor.alpha );
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    if ( ( frameInfo.clearFlags & R_CLEAR_DEPTH ) != 0u ) {
        glClearDepth( static_cast<GLdouble>( frameInfo.clearDepth ) );
        clearMask |= GL_DEPTH_BUFFER_BIT;
    }
    if ( ( frameInfo.clearFlags & R_CLEAR_STENCIL ) != 0u ) {
        glClearStencil( static_cast<GLint>( frameInfo.clearStencil ) );
        clearMask |= GL_STENCIL_BUFFER_BIT;
    }
    if ( clearMask != 0u ) {
        glClear( clearMask );
    }

    const render_error_t nativeResult = GL_CheckErrors( glState.config.validation );
    if ( nativeResult != render_error_t::OK ) {
        return nativeResult;
    }

    glState.drawableExtent = frameInfo.drawableExtent;
    glState.activeFrameIndex = frameInfo.frameIndex;
    glState.frameActive = true;
    return render_error_t::OK;
}

// Applies a drawable-size transition while no frame commands are in progress.
render_error_t GL_Resize(
    const render_extent_t &drawableExtent,
    void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( glState.frameActive ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;

    const bool zeroExtent = drawableExtent.width == 0u && drawableExtent.height == 0u;
    if ( zeroExtent ) {
        glState.drawableExtent = drawableExtent;
        return render_error_t::OK;
    }
    if ( drawableExtent.width == 0u || drawableExtent.height == 0u ||
         drawableExtent.width > glState.limits.maxViewportWidth ||
         drawableExtent.height > glState.limits.maxViewportHeight ||
         drawableExtent.width > static_cast<::cypher::common::u32>(
             std::numeric_limits<GLsizei>::max() ) ||
         drawableExtent.height > static_cast<::cypher::common::u32>(
             std::numeric_limits<GLsizei>::max() ) ) {
        return render_error_t::ERR_RESIZE_FAILED;
    }
    if ( glState.window == nullptr ||
         ::cypher::engine::sys::GLimp_MakeCurrent(
             *glState.window,
             glState.context ) != ::cypher::engine::sys::sys_error_t::OK ) {
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }

    GL_ClearErrors();
    glViewport(
        0,
        0,
        static_cast<GLsizei>( drawableExtent.width ),
        static_cast<GLsizei>( drawableExtent.height ) );
    const render_error_t nativeResult = GL_CheckErrors( glState.config.validation );
    if ( nativeResult == render_error_t::OK ) {
        glState.drawableExtent = drawableExtent;
    }
    return nativeResult;
}

// Presents exactly once and closes the frame even when the native swap fails.
render_error_t GL_EndFrame( void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized || glState.window == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !glState.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;

    const render_error_t nativeResult = GL_CheckErrors( glState.config.validation );
    const ::cypher::engine::sys::sys_error_t presentResult =
        ::cypher::engine::sys::GLimp_SwapWindow( *glState.window );
    glState.frameActive = false;

    if ( nativeResult != render_error_t::OK ) return nativeResult;
    return presentResult == ::cypher::engine::sys::sys_error_t::OK
        ? render_error_t::OK : render_error_t::ERR_PRESENT_FAILED;
}

// Changes swap policy outside a frame and records any supported fallback mode.
render_error_t GL_SetPresentMode(
    const render_present_mode_t presentMode,
    render_present_mode_t &actualModeOut,
    void *backendState ) noexcept
{
    actualModeOut = render_present_mode_t::FIFO;
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( glState.frameActive ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    if ( glState.window == nullptr ||
         ::cypher::engine::sys::GLimp_MakeCurrent(
             *glState.window,
             glState.context ) != ::cypher::engine::sys::sys_error_t::OK ) {
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }

    const render_error_t result = GL_ApplyPresentMode( presentMode, actualModeOut );
    if ( result == render_error_t::OK ) {
        glState.presentMode = actualModeOut;
    }
    return result;
}

// Provides the frontend's explicit GPU drain point for teardown and diagnostics.
render_error_t GL_WaitIdle( void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( glState.frameActive ) return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    if ( glState.window == nullptr ||
         ::cypher::engine::sys::GLimp_MakeCurrent(
             *glState.window,
             glState.context ) != ::cypher::engine::sys::sys_error_t::OK ) {
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }

    GL_ClearErrors();
    glFinish();
    return GL_CheckErrors( glState.config.validation );
}

// Immutable dispatch metadata; only state refers to mutable backend storage.
const backend_api_t glBackend = {
    .apiVersion = R_BACKEND_API_VERSION,
    .structSize = R_BACKEND_API_SIZE,
    .backend = render_backend_t::OPENGL,
    .name = "OpenGL 4.1",
    .ConfigureWindow = GL_ConfigureWindow,
    .Init = GL_Init,
    .Shutdown = GL_Shutdown,
    .BeginFrame = GL_BeginFrame,
    .Resize = GL_Resize,
    .EndFrame = GL_EndFrame,
    .SetPresentMode = GL_SetPresentMode,
    .WaitIdle = GL_WaitIdle,
    .CreateBuffer = GL_CreateBuffer,
    .UpdateBuffer = GL_UpdateBuffer,
    .MapBuffer = GL_MapBuffer,
    .FlushMappedBuffer = GL_FlushMappedBuffer,
    .InvalidateMappedBuffer = GL_InvalidateMappedBuffer,
    .UnmapBuffer = GL_UnmapBuffer,
    .DestroyBuffer = GL_DestroyBuffer,
    .state = &glState
};

} // namespace

// Backend selection borrows this table for the process lifetime.
const backend_api_t *GL_GetBackendAPI() noexcept
{
    return &glBackend;
}

} // namespace cypher::engine::render
