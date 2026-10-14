//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/OpenGL/CypherRender_OpenGL.cpp
//  Purpose: Implements the first hardware renderer backend using OpenGL 4.1.
//  Details: CypherSystem owns standalone SDL context operations; tool hosts
//           may instead lend an existing context through public callbacks.
//           This file owns GLAD, capability discovery, frame work, and backend
//           state returned with the generic dispatch table as an instance.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_OpenGL.h"

#include "CypherRender_OpenGL_Buffer.h"
#include "CypherRender_OpenGL_Draw.h"
#include "CypherRender_OpenGL_Local.h"
#include "CypherRender_OpenGL_Sampler.h"
#include "CypherRender_OpenGL_Shader.h"
#include "CypherRender_OpenGL_Target.h"
#include "CypherRender_OpenGL_Texture.h"
#include "CypherRender_OpenGL_VertexInput.h"

#include "CypherSystem/Platform/CypherSystem_OpenGL.h"

#include <glad/gl.h>

#include <algorithm> // std::min for bounded diagnostic-string copies.
#include <cmath>     // std::isfinite for defensive API value checks.
#include <cstring>   // std::strlen and std::memcpy for driver strings.
#include <limits>    // Safe conversion from renderer extents to GLsizei.

namespace cypher::engine::render
{

gl_state_t glState{};

namespace
{

// GLAD asks for native entry points by name; System keeps SDL out of Renderer.
GLADapiproc GL_LoadProc( const char *name ) noexcept
{
    if ( glState.usesHostSurface ) {
        return glState.hostSurface.ResolveProcAddress != nullptr
            ? reinterpret_cast<GLADapiproc>(
                glState.hostSurface.ResolveProcAddress(
                    name, glState.hostSurface.userData ) )
            : nullptr;
    }
    return reinterpret_cast<GLADapiproc>(
        ::cypher::engine::sys::GLimp_GetProcAddress( name ) );
}

bool GL_ActivateContext() noexcept
{
    if ( glState.usesHostSurface ) {
        return glState.hostSurface.ActivateContext != nullptr &&
            glState.hostSurface.ActivateContext(
                glState.hostSurface.userData );
    }
    return glState.window != nullptr &&
        ::cypher::engine::sys::GLimp_MakeCurrent(
            *glState.window,
            glState.context ) == ::cypher::engine::sys::sys_error_t::OK;
}

bool GL_PrepareFrameTarget() noexcept
{
    return !glState.usesHostSurface ||
        glState.hostSurface.PrepareFrame == nullptr ||
        glState.hostSurface.PrepareFrame(
            glState.hostSurface.userData );
}

// The current renderer emits one fragment color. If a host FBO routes other
// fragment outputs to active draw buffers, those attachments could receive
// undefined values even though our indexed blend/write state touches slot 0.
render_error_t GL_ValidateHostDrawBuffers() noexcept
{
    if ( glState.limits.maxDrawBuffers == 0u ) {
        return render_error_t::ERR_RENDER_TARGET_INVALID;
    }
    bool hasExactlyOneColorOutput = true;
    for ( ::cypher::common::u32 index = 0u;
          index < glState.limits.maxDrawBuffers; ++index ) {
        GLint drawBuffer = GL_NONE;
        glGetIntegerv( static_cast<GLenum>( GL_DRAW_BUFFER0 + index ),
            &drawBuffer );
        if ( index == 0u ) hasExactlyOneColorOutput &= drawBuffer != GL_NONE;
        else hasExactlyOneColorOutput &= drawBuffer == GL_NONE;
    }
    const render_error_t queryResult = GL_CheckErrors( glState.config.validation );
    if ( queryResult != render_error_t::OK ) return queryResult;
    return hasExactlyOneColorOutput
        ? render_error_t::OK
        : render_error_t::ERR_RENDER_TARGET_INVALID;
}

void GL_SetEnabled( const GLenum capability, const GLboolean enabled ) noexcept
{
    if ( enabled == GL_TRUE ) glEnable( capability );
    else glDisable( capability );
}

void GL_SetIndexedEnabled(
    const GLenum capability, const GLuint index, const GLboolean enabled ) noexcept
{
    if ( enabled == GL_TRUE ) glEnablei( capability, index );
    else glDisablei( capability, index );
}

// All queries are limited to state the hosted frame, indexed slot-zero draw,
// declared uniform slots, and target pass actually modify. Other viewport/
// draw-buffer slots remain owned by the host and are never touched by those paths.
render_error_t GL_CaptureHostFrameState( gl_host_frame_state_t &state ) noexcept
{
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFramebuffer );
    glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &state.readFramebuffer );
    glGetFloati_v( GL_VIEWPORT, 0u, state.viewport );
    glGetIntegerv( GL_CURRENT_PROGRAM, &state.program );
    glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &state.vertexArray );
    glGetIntegerv( GL_ACTIVE_TEXTURE, &state.activeTexture );
    for ( common::u32 slot = 0u; slot < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++slot ) {
        glActiveTexture( GL_TEXTURE0 + slot );
        glGetIntegerv( GL_TEXTURE_BINDING_2D, &state.texture2DUnits[slot] );
        glGetIntegerv( GL_SAMPLER_BINDING, &state.samplerUnits[slot] );
    }
    glActiveTexture( static_cast<GLenum>( state.activeTexture ) );
    glGetIntegerv( GL_UNIFORM_BUFFER_BINDING, &state.uniformBuffer );
    for ( common::u32 slot = 0u; slot < R_MAX_UNIFORM_BLOCK_BINDINGS; ++slot ) {
        glGetIntegeri_v( GL_UNIFORM_BUFFER_BINDING, slot,
            &state.uniformBufferIndices[slot] );
        glGetInteger64i_v( GL_UNIFORM_BUFFER_START, slot,
            &state.uniformBufferStarts[slot] );
        glGetInteger64i_v( GL_UNIFORM_BUFFER_SIZE, slot,
            &state.uniformBufferSizes[slot] );
    }

    glGetIntegerv( GL_DEPTH_FUNC, &state.depthFunction );
    glGetIntegerv( GL_CULL_FACE_MODE, &state.cullFaceMode );
    glGetIntegerv( GL_FRONT_FACE, &state.frontFace );
    glGetIntegerv( GL_POLYGON_MODE, state.polygonMode );
    glGetIntegerv( GL_STENCIL_WRITEMASK, &state.stencilWriteMaskFront );
    glGetIntegerv( GL_STENCIL_BACK_WRITEMASK, &state.stencilWriteMaskBack );
    glGetIntegerv( GL_STENCIL_CLEAR_VALUE, &state.clearStencil );
    glGetFloatv( GL_LINE_WIDTH, &state.lineWidth );
    glGetFloatv( GL_POLYGON_OFFSET_FACTOR, &state.polygonOffsetFactor );
    glGetFloatv( GL_POLYGON_OFFSET_UNITS, &state.polygonOffsetUnits );
    glGetFloatv( GL_COLOR_CLEAR_VALUE, state.clearColor );
    glGetDoublev( GL_DEPTH_CLEAR_VALUE, &state.clearDepth );
    glGetBooleanv( GL_DEPTH_WRITEMASK, &state.depthWriteMask );

    state.scissorTest = glIsEnabledi( GL_SCISSOR_TEST, 0u );
    glGetIntegeri_v( GL_SCISSOR_BOX, 0u, state.scissorBox );
    state.depthTest = glIsEnabled( GL_DEPTH_TEST );
    state.cullFace = glIsEnabled( GL_CULL_FACE );
    state.stencilTest = glIsEnabled( GL_STENCIL_TEST );
    state.primitiveRestart = glIsEnabled( GL_PRIMITIVE_RESTART );
    state.rasterizerDiscard = glIsEnabled( GL_RASTERIZER_DISCARD );
    state.polygonOffsetFill = glIsEnabled( GL_POLYGON_OFFSET_FILL );
    state.polygonOffsetLine = glIsEnabled( GL_POLYGON_OFFSET_LINE );
    state.lineSmooth = glIsEnabled( GL_LINE_SMOOTH );
    state.framebufferSRGB = glIsEnabled( GL_FRAMEBUFFER_SRGB );
    state.multisample = glIsEnabled( GL_MULTISAMPLE );
    state.sampleAlphaToCoverage = glIsEnabled( GL_SAMPLE_ALPHA_TO_COVERAGE );
    state.sampleAlphaToOne = glIsEnabled( GL_SAMPLE_ALPHA_TO_ONE );
    state.sampleCoverage = glIsEnabled( GL_SAMPLE_COVERAGE );
    state.sampleMask = glIsEnabled( GL_SAMPLE_MASK );

    gl_host_draw_buffer_state_t &draw = state.drawBuffer0;
    draw.blend = glIsEnabledi( GL_BLEND, 0u );
    glGetBooleani_v( GL_COLOR_WRITEMASK, 0u, draw.colorMask );
    glGetIntegeri_v( GL_BLEND_EQUATION_RGB, 0u, &draw.blendEquationRGB );
    glGetIntegeri_v( GL_BLEND_EQUATION_ALPHA, 0u, &draw.blendEquationAlpha );
    glGetIntegeri_v( GL_BLEND_SRC_RGB, 0u, &draw.blendSourceRGB );
    glGetIntegeri_v( GL_BLEND_DST_RGB, 0u, &draw.blendDestinationRGB );
    glGetIntegeri_v( GL_BLEND_SRC_ALPHA, 0u, &draw.blendSourceAlpha );
    glGetIntegeri_v( GL_BLEND_DST_ALPHA, 0u, &draw.blendDestinationAlpha );
    const render_error_t result = GL_CheckErrors( glState.config.validation );
    if ( result != render_error_t::OK ) {
        // A failed query cannot produce a trustworthy snapshot. No frame
        // state has been changed yet, and the active texture was put back.
        state = {};
        return result;
    }
    state.captured = true;
    return render_error_t::OK;
}

render_error_t GL_RestoreHostFrameState( gl_host_frame_state_t &state ) noexcept
{
    if ( !state.captured ) return render_error_t::OK;

    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, static_cast<GLuint>( state.drawFramebuffer ) );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, static_cast<GLuint>( state.readFramebuffer ) );
    glViewportIndexedfv( 0u, state.viewport );
    glScissorIndexed( 0u, state.scissorBox[0], state.scissorBox[1],
        static_cast<GLsizei>( state.scissorBox[2] ),
        static_cast<GLsizei>( state.scissorBox[3] ) );
    GL_SetIndexedEnabled( GL_SCISSOR_TEST, 0u, state.scissorTest );
    glColorMaski( 0u, state.drawBuffer0.colorMask[0], state.drawBuffer0.colorMask[1],
        state.drawBuffer0.colorMask[2], state.drawBuffer0.colorMask[3] );
    glDepthMask( state.depthWriteMask );
    glStencilMaskSeparate( GL_FRONT, static_cast<GLuint>( state.stencilWriteMaskFront ) );
    glStencilMaskSeparate( GL_BACK, static_cast<GLuint>( state.stencilWriteMaskBack ) );
    glClearColor( state.clearColor[0], state.clearColor[1],
        state.clearColor[2], state.clearColor[3] );
    glClearDepth( state.clearDepth );
    glClearStencil( state.clearStencil );

    GL_SetEnabled( GL_DEPTH_TEST, state.depthTest );
    glDepthFunc( static_cast<GLenum>( state.depthFunction ) );
    GL_SetEnabled( GL_CULL_FACE, state.cullFace );
    glCullFace( static_cast<GLenum>( state.cullFaceMode ) );
    glFrontFace( static_cast<GLenum>( state.frontFace ) );
    GL_SetIndexedEnabled( GL_BLEND, 0u, state.drawBuffer0.blend );
    glBlendEquationSeparatei( 0u,
        static_cast<GLenum>( state.drawBuffer0.blendEquationRGB ),
        static_cast<GLenum>( state.drawBuffer0.blendEquationAlpha ) );
    glBlendFuncSeparatei( 0u,
        static_cast<GLenum>( state.drawBuffer0.blendSourceRGB ),
        static_cast<GLenum>( state.drawBuffer0.blendDestinationRGB ),
        static_cast<GLenum>( state.drawBuffer0.blendSourceAlpha ),
        static_cast<GLenum>( state.drawBuffer0.blendDestinationAlpha ) );
    glLineWidth( state.lineWidth );
    glPolygonOffset( state.polygonOffsetFactor, state.polygonOffsetUnits );
    GL_SetEnabled( GL_STENCIL_TEST, state.stencilTest );
    GL_SetEnabled( GL_PRIMITIVE_RESTART, state.primitiveRestart );
    GL_SetEnabled( GL_RASTERIZER_DISCARD, state.rasterizerDiscard );
    GL_SetEnabled( GL_POLYGON_OFFSET_FILL, state.polygonOffsetFill );
    GL_SetEnabled( GL_POLYGON_OFFSET_LINE, state.polygonOffsetLine );
    GL_SetEnabled( GL_LINE_SMOOTH, state.lineSmooth );
    // Core profile accepts only FRONT_AND_BACK; both captured values are the
    // same mode for the portable OpenGL 4.1 renderer.
    glPolygonMode( GL_FRONT_AND_BACK,
        static_cast<GLenum>( state.polygonMode[0] ) );
    GL_SetEnabled( GL_FRAMEBUFFER_SRGB, state.framebufferSRGB );
    GL_SetEnabled( GL_MULTISAMPLE, state.multisample );
    GL_SetEnabled( GL_SAMPLE_ALPHA_TO_COVERAGE, state.sampleAlphaToCoverage );
    GL_SetEnabled( GL_SAMPLE_ALPHA_TO_ONE, state.sampleAlphaToOne );
    GL_SetEnabled( GL_SAMPLE_COVERAGE, state.sampleCoverage );
    GL_SetEnabled( GL_SAMPLE_MASK, state.sampleMask );

    glUseProgram( static_cast<GLuint>( state.program ) );
    glBindVertexArray( static_cast<GLuint>( state.vertexArray ) );
    for ( common::u32 slot = 0u; slot < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++slot ) {
        glActiveTexture( GL_TEXTURE0 + slot );
        glBindTexture( GL_TEXTURE_2D, static_cast<GLuint>( state.texture2DUnits[slot] ) );
        glBindSampler( slot, static_cast<GLuint>( state.samplerUnits[slot] ) );
    }
    glActiveTexture( static_cast<GLenum>( state.activeTexture ) );
    for ( common::u32 slot = 0u; slot < R_MAX_UNIFORM_BLOCK_BINDINGS; ++slot ) {
        if ( state.uniformBufferIndices[slot] != 0 &&
             state.uniformBufferSizes[slot] > 0 ) {
            glBindBufferRange( GL_UNIFORM_BUFFER, slot,
                static_cast<GLuint>( state.uniformBufferIndices[slot] ),
                static_cast<GLintptr>( state.uniformBufferStarts[slot] ),
                static_cast<GLsizeiptr>( state.uniformBufferSizes[slot] ) );
        } else {
            glBindBufferBase( GL_UNIFORM_BUFFER, slot,
                static_cast<GLuint>( state.uniformBufferIndices[slot] ) );
        }
    }
    glBindBuffer( GL_UNIFORM_BUFFER, static_cast<GLuint>( state.uniformBuffer ) );

    state.captured = false;
    return GL_CheckErrors( glState.config.validation );
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
    const ::cypher::common::u16 actualMajor,
    const ::cypher::common::u16 actualMinor,
    const ::cypher::common::u8 major,
    const ::cypher::common::u8 minor ) noexcept
{
    return actualMajor > major ||
        ( actualMajor == major && actualMinor >= minor );
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
        GL_QueryUnsignedLimit( GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,
            limitsOut.uniformBufferOffsetAlignment ) &&
        GL_QueryUnsignedLimit( GL_MAX_COLOR_ATTACHMENTS, limitsOut.maxColorAttachments ) &&
        GL_QueryUnsignedLimit( GL_MAX_DRAW_BUFFERS, limitsOut.maxDrawBuffers ) &&
        GL_QueryUnsignedLimit( GL_MAX_SAMPLES, limitsOut.maxSamples );

    glGetIntegerv( GL_MAX_VIEWPORT_DIMS, viewportDimensions );
    glGetInteger64v( GL_MAX_UNIFORM_BLOCK_SIZE, &uniformBlockBytes );
    if ( !scalarQueriesSucceeded || limitsOut.uniformBufferOffsetAlignment == 0u ||
         viewportDimensions[0] <= 0 ||
         viewportDimensions[1] <= 0 || uniformBlockBytes <= 0 ) {
        limitsOut = {};
        return false;
    }

    limitsOut.maxViewportWidth = static_cast<::cypher::common::u32>( viewportDimensions[0] );
    limitsOut.maxViewportHeight = static_cast<::cypher::common::u32>( viewportDimensions[1] );
    limitsOut.maxUniformBlockBytes = static_cast<::cypher::common::u64>( uniformBlockBytes );
    return glGetError() == GL_NO_ERROR;
}

// A capability means the public renderer can use the feature, not merely that
// this driver exposes its OpenGL entry point. Keep reserved bits clear until
// their backend-neutral resource and command paths exist.
render_capability_flags_t GL_QueryCapabilities(
    const ::cypher::common::u16 apiMajorVersion,
    const ::cypher::common::u16 apiMinorVersion,
    const bool sRGBFramebuffer,
    const render_limits_t &limits ) noexcept
{
    render_capability_flags_t capabilities = R_CAPABILITY_RASTERIZATION;

    if ( GL_VersionAtLeast( apiMajorVersion, apiMinorVersion, 3u, 1u ) ) capabilities |= R_CAPABILITY_INSTANCING;
    if ( GL_VersionAtLeast( apiMajorVersion, apiMinorVersion, 3u, 0u ) ) capabilities |= R_CAPABILITY_FLOAT_RENDER_TARGETS;
    if ( sRGBFramebuffer ) capabilities |= R_CAPABILITY_SRGB_FRAMEBUFFER;
    // The window's default framebuffer can be multisampled. Offscreen targets
    // remain single-sample until an explicit resolve path is implemented.
    if ( limits.maxSamples >= 2u ) capabilities |= R_CAPABILITY_MULTISAMPLING;
    return capabilities;
}

// Maps backend-neutral presentation policy to SDL's OpenGL swap interval.
render_error_t GL_ApplyPresentMode(
    const render_present_mode_t requestedMode,
    render_present_mode_t &actualModeOut ) noexcept
{
    if ( glState.usesHostSurface ) {
        if ( glState.hostSurface.SetPresentMode == nullptr ) {
            actualModeOut = glState.hostSurface.presentMode;
            return requestedMode == actualModeOut
                ? render_error_t::OK
                : render_error_t::ERR_UNSUPPORTED;
        }

        actualModeOut = glState.hostSurface.presentMode;
        return glState.hostSurface.SetPresentMode(
            requestedMode,
            &actualModeOut,
            glState.hostSurface.userData ) &&
            actualModeOut < render_present_mode_t::COUNT
            ? render_error_t::OK
            : render_error_t::ERR_PRESENT_FAILED;
    }

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
    if ( gladLoadGL( GL_LoadProc ) == 0 || GLAD_GL_VERSION_4_1 == 0 ||
         glGenSamplers == nullptr || glSamplerParameteri == nullptr ||
         glDeleteSamplers == nullptr || glBindSampler == nullptr ) {
        (void)GLimp_DestroyContext( context );
        return render_error_t::ERR_ENTRYPOINT_LOAD_FAILED;
    }

    gl_context_info_t contextInfo{};
    if ( GLimp_QueryContextInfo( contextInfo ) != sys_error_t::OK ||
         !GL_VersionAtLeast(
             contextInfo.majorVersion, contextInfo.minorVersion, 4u, 1u ) ) {
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
        contextInfo.majorVersion,
        contextInfo.minorVersion,
        contextInfo.sRGBFramebuffer,
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
    // Capability and policy are separate: a capable default framebuffer must
    // still store shader-authored sRGB values unchanged when conversion is off.
    GL_SetEnabled( GL_FRAMEBUFFER_SRGB,
        config.sRGBFramebuffer ? GL_TRUE : GL_FALSE );
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

/*
================
GL_InitHostSurface

Borrows a current-capable context from an editor host. The backend owns every
resource created through CypherRender, but never destroys the context and never
presents when the host omitted a Present callback.
================
*/
render_error_t GL_InitHostSurface(
    const render_host_surface_desc_t &surface,
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
    if ( surface.backend != render_backend_t::OPENGL ||
         surface.ActivateContext == nullptr ||
         surface.ResolveProcAddress == nullptr ) {
        return render_error_t::ERR_WINDOW_INCOMPATIBLE;
    }

    glState.hostSurface = surface;
    glState.config = config;
    glState.drawableExtent = surface.drawableExtent;
    glState.usesHostSurface = true;

    if ( !GL_ActivateContext() ) {
        glState = {};
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }
    if ( gladLoadGL( GL_LoadProc ) == 0 || GLAD_GL_VERSION_4_1 == 0 ||
         glGenSamplers == nullptr || glSamplerParameteri == nullptr ||
         glDeleteSamplers == nullptr || glBindSampler == nullptr ) {
        glState = {};
        return render_error_t::ERR_ENTRYPOINT_LOAD_FAILED;
    }
    if ( !GL_VersionAtLeast(
            surface.apiMajorVersion, surface.apiMinorVersion, 4u, 1u ) ) {
        glState = {};
        return render_error_t::ERR_BACKEND_VERSION_UNSUPPORTED;
    }

    GL_ClearErrors();
    GLint actualMajor = 0;
    GLint actualMinor = 0;
    glGetIntegerv( GL_MAJOR_VERSION, &actualMajor );
    glGetIntegerv( GL_MINOR_VERSION, &actualMinor );
    render_limits_t limits{};
    const bool stringsAvailable =
        GL_CopyDriverString( glState.apiName, glGetString( GL_VERSION ) ) &&
        GL_CopyDriverString( glState.deviceName, glGetString( GL_RENDERER ) ) &&
        GL_CopyDriverString( glState.vendorName, glGetString( GL_VENDOR ) ) &&
        GL_CopyDriverString( glState.driverVersion, glGetString( GL_VERSION ) ) &&
        GL_CopyDriverString(
            glState.shadingLanguageVersion,
            glGetString( GL_SHADING_LANGUAGE_VERSION ) );
    if ( !stringsAvailable || actualMajor <= 0 || actualMinor < 0 ||
         static_cast<::cypher::common::u16>( actualMajor ) != surface.apiMajorVersion ||
         static_cast<::cypher::common::u16>( actualMinor ) != surface.apiMinorVersion ||
         !GL_QueryLimits( limits ) ) {
        glState = {};
        return render_error_t::ERR_DEVICE_QUERY_FAILED;
    }

    constexpr char apiName[] = "OpenGL";
    std::memcpy( glState.apiName, apiName, sizeof( apiName ) );
    glState.limits = limits;
    glState.initialized = true;

    render_present_mode_t actualPresentMode = surface.presentMode;
    const render_error_t presentResult = GL_ApplyPresentMode(
        config.presentMode,
        actualPresentMode );
    if ( presentResult != render_error_t::OK ) {
        glState = {};
        return presentResult;
    }
    glState.presentMode = actualPresentMode;

    const render_capability_flags_t capabilities = GL_QueryCapabilities(
        surface.apiMajorVersion,
        surface.apiMinorVersion,
        surface.sRGBFramebuffer,
        limits );
    if ( config.sRGBFramebuffer &&
         ( capabilities & R_CAPABILITY_SRGB_FRAMEBUFFER ) == 0u ) {
        glState = {};
        return render_error_t::ERR_CAPABILITY_MISSING;
    }
    if ( config.sampleCount != surface.sampleCount ) {
        glState = {};
        return render_error_t::ERR_CAPABILITY_MISSING;
    }

    // A borrowed context may be in the middle of the host's own render pass.
    // Init verifies the advertised surface but leaves its viewport and fixed
    // GL state alone; a hosted BeginFrame owns changes only for that frame.
    const render_error_t nativeResult = GL_CheckErrors( config.validation );
    if ( nativeResult != render_error_t::OK ) {
        glState = {};
        return nativeResult;
    }

    infoOut.backend = render_backend_t::OPENGL;
    infoOut.mode = render_mode_t::RASTER;
    infoOut.presentMode = actualPresentMode;
    infoOut.capabilities = capabilities;
    infoOut.drawableExtent = surface.drawableExtent;
    infoOut.limits = limits;
    infoOut.apiMajorVersion = surface.apiMajorVersion;
    infoOut.apiMinorVersion = surface.apiMinorVersion;
    infoOut.sampleCount = surface.sampleCount;
    infoOut.accelerated = surface.accelerated;
    infoOut.apiName = glState.apiName;
    infoOut.deviceName = glState.deviceName;
    infoOut.vendorName = glState.vendorName;
    infoOut.driverVersion = glState.driverVersion;
    infoOut.shadingLanguageVersion = glState.shadingLanguageVersion;
    return render_error_t::OK;
}

// Drains native work and destroys only a backend-owned standalone context.
render_error_t GL_Shutdown( void *backendState ) noexcept
{
    if ( backendState != &glState ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( !glState.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( glState.frameActive || glState.targetPassActive ) {
        return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    }

    render_error_t result = render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    if ( GL_ActivateContext() ) {
        glFinish();
        result = GL_CheckErrors( glState.config.validation );
        const render_error_t targetResult = GL_TargetsShutdown();
        if ( result == render_error_t::OK ) result = targetResult;
    } else {
        // Native objects die with a standalone context; a host may recover its
        // borrowed one, but we cannot issue GL deletion without activation.
        GL_TargetsAbandon();
    }

    if ( !glState.usesHostSurface &&
         ::cypher::engine::sys::GLimp_DestroyContext( glState.context ) !=
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
    if ( !glState.initialized ||
         ( !glState.usesHostSurface && glState.window == nullptr ) ) {
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
    if ( !GL_ActivateContext() ) {
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }
    if ( !GL_PrepareFrameTarget() ) {
        return render_error_t::ERR_PRESENT_TARGET_LOST;
    }

    if ( glState.usesHostSurface ) {
        const render_error_t drawBufferResult = GL_ValidateHostDrawBuffers();
        if ( drawBufferResult != render_error_t::OK ) return drawBufferResult;
        // The host's PrepareFrame may select a nonzero presentation FBO. Its
        // GL state is captured before we clear or draw anything in that context.
        const render_error_t captureResult = GL_CaptureHostFrameState(
            glState.hostFrameState );
        if ( captureResult != render_error_t::OK ) return captureResult;
    } else {
        GL_ClearErrors();
    }
    if ( !glState.usesHostSurface ) {
        // A standalone frame always presents the default framebuffer. A host
        // may instead have prepared an FBO with a nonzero native name.
        glBindFramebuffer( GL_FRAMEBUFFER, 0u );
    }
    GLint presentationDraw = glState.hostFrameState.drawFramebuffer;
    GLint presentationRead = glState.hostFrameState.readFramebuffer;
    if ( glState.usesHostSurface ) {
        glViewportIndexedf( 0u, 0.0f, 0.0f,
            static_cast<GLfloat>( frameInfo.drawableExtent.width ),
            static_cast<GLfloat>( frameInfo.drawableExtent.height ) );
        GL_SetEnabled( GL_FRAMEBUFFER_SRGB,
            glState.config.sRGBFramebuffer ? GL_TRUE : GL_FALSE );
        GL_SetEnabled( GL_MULTISAMPLE,
            glState.config.sampleCount > 0u ? GL_TRUE : GL_FALSE );
    } else {
        glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &presentationDraw );
        glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &presentationRead );
        glViewport( 0, 0,
            static_cast<GLsizei>( frameInfo.drawableExtent.width ),
            static_cast<GLsizei>( frameInfo.drawableExtent.height ) );
        GL_SetEnabled( GL_FRAMEBUFFER_SRGB,
            glState.config.sRGBFramebuffer ? GL_TRUE : GL_FALSE );
    }

    GLbitfield clearMask = 0u;
    // Clear operations honor write masks. A previous depth-read-only pipeline
    // must not prevent this frame from clearing its depth attachment.
    if ( glState.usesHostSurface ) {
        glColorMaski( 0u, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glDisablei( GL_SCISSOR_TEST, 0u );
    } else {
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        glDisable( GL_SCISSOR_TEST );
    }
    glDepthMask( GL_TRUE );
    glStencilMask( ~0u );
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
    if ( glState.usesHostSurface ) {
        // glClear(GL_COLOR_BUFFER_BIT) broadcasts to every active host draw
        // buffer. The renderer currently owns only color output zero.
        if ( ( clearMask & GL_COLOR_BUFFER_BIT ) != 0u ) {
            const GLfloat color[4]{ frameInfo.clearColor.red,
                frameInfo.clearColor.green, frameInfo.clearColor.blue,
                frameInfo.clearColor.alpha };
            glClearBufferfv( GL_COLOR, 0, color );
        }
        if ( ( clearMask & GL_DEPTH_BUFFER_BIT ) != 0u ) {
            const GLfloat depth = frameInfo.clearDepth;
            glClearBufferfv( GL_DEPTH, 0, &depth );
        }
        if ( ( clearMask & GL_STENCIL_BUFFER_BIT ) != 0u ) {
            const GLint stencil = static_cast<GLint>( frameInfo.clearStencil );
            glClearBufferiv( GL_STENCIL, 0, &stencil );
        }
    } else if ( clearMask != 0u ) {
        glClear( clearMask );
    }

    GLfloat presentationViewport[4]{};
    glGetFloati_v( GL_VIEWPORT, 0u, presentationViewport );
    const render_error_t nativeResult = GL_CheckErrors( glState.config.validation );
    if ( nativeResult != render_error_t::OK ) {
        if ( glState.usesHostSurface ) {
            (void)GL_RestoreHostFrameState( glState.hostFrameState );
        }
        return nativeResult;
    }

    glState.drawableExtent = frameInfo.drawableExtent;
    glState.activeFrameIndex = frameInfo.frameIndex;
    glState.presentationDrawFramebuffer = presentationDraw;
    glState.presentationReadFramebuffer = presentationRead;
    for ( unsigned index = 0u; index < 4u; ++index ) {
        glState.presentationViewport[index] = presentationViewport[index];
    }
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
    if ( glState.usesHostSurface ) {
        // PrepareFrame will supply the actual presentation target and set the
        // viewport when the next frame begins. A resize outside a frame must
        // not leave the editor's live GL state changed.
        glState.drawableExtent = drawableExtent;
        return render_error_t::OK;
    }
    if ( !GL_ActivateContext() ) {
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
    if ( !glState.initialized ||
         ( !glState.usesHostSurface && glState.window == nullptr ) ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !glState.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( glState.targetPassActive ) return render_error_t::ERR_COMMAND_STATE_INVALID;

    render_error_t nativeResult = GL_CheckErrors( glState.config.validation );
    bool presentSucceeded = true;
    if ( glState.usesHostSurface ) {
        const render_error_t restoreResult = GL_RestoreHostFrameState(
            glState.hostFrameState );
        if ( nativeResult == render_error_t::OK ) nativeResult = restoreResult;
        if ( glState.hostSurface.Present != nullptr ) {
            presentSucceeded = glState.hostSurface.Present(
                glState.hostSurface.userData );
        } else {
            // Widget hosts composite their target after paint returns. Flush is
            // sufficient; swapping here would race or double-present the host.
            glFlush();
            if ( nativeResult == render_error_t::OK ) {
                nativeResult = GL_CheckErrors( glState.config.validation );
            }
        }
    } else {
        presentSucceeded = ::cypher::engine::sys::GLimp_SwapWindow(
            *glState.window ) == ::cypher::engine::sys::sys_error_t::OK;
    }
    glState.frameActive = false;
    glState.presentationDrawFramebuffer = 0;
    glState.presentationReadFramebuffer = 0;

    if ( nativeResult != render_error_t::OK ) return nativeResult;
    return presentSucceeded
        ? render_error_t::OK
        : render_error_t::ERR_PRESENT_FAILED;
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
    if ( !GL_ActivateContext() ) {
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
    if ( !GL_ActivateContext() ) {
        return render_error_t::ERR_CONTEXT_ACTIVATE_FAILED;
    }

    GL_ClearErrors();
    glFinish();
    return GL_CheckErrors( glState.config.validation );
}

// Immutable dispatch metadata. The selected instance supplies mutable state.
const backend_api_t glBackend = {
    .apiVersion = R_BACKEND_API_VERSION,
    .structSize = R_BACKEND_API_SIZE,
    .backend = render_backend_t::OPENGL,
    .name = "OpenGL 4.1",
    .ConfigureWindow = GL_ConfigureWindow,
    .Init = GL_Init,
    .InitHostSurface = GL_InitHostSurface,
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
    .CreateVertexInput = GL_CreateVertexInput,
    .DestroyVertexInput = GL_DestroyVertexInput,
    .CreateShader = GL_CreateShader,
    .DestroyShader = GL_DestroyShader,
    .CreateGraphicsPipeline = GL_CreateGraphicsPipeline,
    .DestroyGraphicsPipeline = GL_DestroyGraphicsPipeline,
    .DrawIndexed = GL_DrawIndexed,
    .CreateTexture2D = GL_CreateTexture2D,
    .DestroyTexture = GL_DestroyTexture,
    .CreateTexture2DLevels = GL_CreateTexture2DLevels,
    .CreateSampler = GL_CreateSampler,
    .DestroySampler = GL_DestroySampler,
    .CreateRenderTarget = GL_CreateRenderTarget,
    .DestroyRenderTarget = GL_DestroyRenderTarget,
    .BeginTargetPass = GL_BeginTargetPass,
    .EndTargetPass = GL_EndTargetPass,
    .ReadTargetRGBA8 = GL_ReadTargetRGBA8,
    .ReadTargetRGBA32F = GL_ReadTargetRGBA32F
};

} // namespace

// Backend selection borrows this table for the process lifetime.
const backend_api_t *GL_GetBackendAPI() noexcept
{
    return &glBackend;
}

// Selecting the proper backend instance that we need and in this case
const backend_instance_t GL_GetBackendInstance() noexcept
{
    return {
        .api = GL_GetBackendAPI(),
        .state = &glState
    };
}

} // namespace cypher::engine::render
