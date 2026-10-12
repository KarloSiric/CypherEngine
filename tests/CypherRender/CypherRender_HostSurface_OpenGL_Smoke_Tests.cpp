//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherRender_HostSurface_OpenGL_Smoke_Tests.cpp
//  Purpose: Verifies rendering through a host-owned OpenGL context.
//  Details: The host creates and ultimately destroys the context. CypherRender
//           borrows it, prepares frames, and releases only renderer resources.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_Target.h"
#include "CypherSystem/Platform/CypherSystem_OpenGL.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>

#include <glad/gl.h>

#include <filesystem>
#include <string>

namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;

namespace
{

struct hosted_context_t {
    sys::window_t *pWindow{ nullptr };
    sys::gl_context_t *pContext{ nullptr };
    int nActivations{ 0 };
    int nFramePreparations{ 0 };
    GLuint presentationFramebuffer{ 0u };
    GLuint presentationReadFramebuffer{ 0u };
};

bool ActivateHostedContext( void *pUserData ) noexcept
{
    auto *pHost = static_cast<hosted_context_t *>( pUserData );
    if ( pHost == nullptr || pHost->pWindow == nullptr ||
         pHost->pContext == nullptr ) {
        return false;
    }
    ++pHost->nActivations;
    return sys::GLimp_MakeCurrent(
        *pHost->pWindow,
        *pHost->pContext ) == sys::sys_error_t::OK;
}

render::render_host_proc_t ResolveHostedProcedure(
    const char *pProcedureName,
    void * ) noexcept
{
    return reinterpret_cast<render::render_host_proc_t>(
        sys::GLimp_GetProcAddress( pProcedureName ) );
}

bool PrepareHostedFrame( void *pUserData ) noexcept
{
    auto *pHost = static_cast<hosted_context_t *>( pUserData );
    if ( pHost == nullptr ) return false;
    ++pHost->nFramePreparations;
    if ( pHost->presentationFramebuffer != 0u ) {
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pHost->presentationFramebuffer );
        glBindFramebuffer( GL_READ_FRAMEBUFFER, pHost->presentationReadFramebuffer );
    }
    return true;
}

} // namespace

TEST_CASE( "renderer borrows and preserves a host-owned OpenGL context",
           "[CypherRender][HostSurface][OpenGL][Smoke]" )
{
    const std::filesystem::path userPath =
        std::filesystem::temp_directory_path() /
        ( "cypher_render_host_surface_" +
          std::to_string( sys::Sys_GetCurrentProcessId() ) );
    const std::string userPathString = userPath.string();
    const char *arguments[] = {
        "cypher_render_host_surface_opengl_smoke_tests",
        "-basedir",
        ".",
        "-userpath",
        userPathString.c_str()
    };
    const sys::init_info_t initInfo{
        5,
        arguments,
        "CypherRenderHostSurfaceSmokeTests",
        "CypherTests"
    };

    if ( sys::Sys_Init( initInfo ) != sys::sys_error_t::OK ) {
        SKIP( "CypherSystem could not initialize a desktop graphics session" );
    }

    render::render_config_t config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = render::render_present_mode_t::IMMEDIATE;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    config.sRGBFramebuffer = false;
    config.requireAcceleration = false;

    sys::window_desc_t windowDescription{};
    windowDescription.title = "CypherRender Host Surface Smoke Test";
    windowDescription.width = 64u;
    windowDescription.height = 64u;
    windowDescription.flags = sys::SYS_WINDOW_HIDDEN;
    REQUIRE( render::R_ConfigureWindow( config, windowDescription ) ==
        render::render_error_t::OK );

    sys::window_t window{};
    if ( sys::Sys_CreateWindow( windowDescription, window ) !=
         sys::sys_error_t::OK ) {
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host has no OpenGL 4.1-capable window system" );
    }

    sys::gl_context_t context{};
    if ( sys::GLimp_CreateContext( window, context ) != sys::sys_error_t::OK ) {
        REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host graphics driver cannot create an OpenGL 4.1 context" );
    }

    REQUIRE( sys::GLimp_MakeCurrent( window, context ) == sys::sys_error_t::OK );
    sys::gl_context_info_t contextInfo{};
    REQUIRE( sys::GLimp_QueryContextInfo( contextInfo ) == sys::sys_error_t::OK );

    hosted_context_t host{ &window, &context };
    render::render_host_surface_desc_t surface{};
    surface.backend = render::render_backend_t::OPENGL;
    surface.drawableExtent = { window.width, window.height };
    surface.presentMode = render::render_present_mode_t::IMMEDIATE;
    surface.apiMajorVersion = contextInfo.majorVersion;
    surface.apiMinorVersion = contextInfo.minorVersion;
    surface.sampleCount = contextInfo.sampleCount;
    surface.sRGBFramebuffer = contextInfo.sRGBFramebuffer;
    surface.accelerated = contextInfo.accelerated;
    surface.userData = &host;
    surface.ActivateContext = ActivateHostedContext;
    surface.ResolveProcAddress = ResolveHostedProcedure;
    surface.PrepareFrame = PrepareHostedFrame;
    // Present stays null: this exercises host-owned widget presentation.

    REQUIRE( render::R_ValidateHostSurface( surface ) ==
        render::render_error_t::OK );
    render::render_config_t hostedConfig = config;
    hostedConfig.backend = render::render_backend_t::AUTO;
    const render::render_error_t rendererResult =
        render::R_InitHostSurface( surface, hostedConfig );
    if ( rendererResult != render::render_error_t::OK ) {
        REQUIRE( sys::GLimp_DestroyContext( context ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host graphics driver cannot initialize the borrowed OpenGL 4.1 surface" );
    }
    REQUIRE( render::R_IsInitialized() );
    CHECK( host.nActivations > 0 );

    const render::render_info_t *pInfo = render::R_GetInfo();
    REQUIRE( pInfo != nullptr );
    CHECK( pInfo->backend == render::render_backend_t::OPENGL );
    CHECK( pInfo->apiMajorVersion == contextInfo.majorVersion );
    CHECK( pInfo->apiMinorVersion == contextInfo.minorVersion );
    CHECK( pInfo->sampleCount == contextInfo.sampleCount );

    const int nActivationsAfterInit = host.nActivations;
    const render::render_extent_t resizedExtent{ 48u, 32u };
    REQUIRE( render::R_Resize( resizedExtent ) == render::render_error_t::OK );
    REQUIRE( render::R_GetInfo() != nullptr );
    CHECK( render::R_GetInfo()->drawableExtent.width == resizedExtent.width );
    CHECK( render::R_GetInfo()->drawableExtent.height == resizedExtent.height );
    CHECK( host.nActivations > nActivationsAfterInit );

    // A host widget may present into its own nonzero FBO. The renderer must
    // restore this exact binding and viewport after an offscreen pass.
    GLuint hostColor = 0u;
    glGenTextures( 1, &hostColor );
    glBindTexture( GL_TEXTURE_2D, hostColor );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 48, 32, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glGenFramebuffers( 1, &host.presentationFramebuffer );
    glBindFramebuffer( GL_FRAMEBUFFER, host.presentationFramebuffer );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, hostColor, 0 );
    REQUIRE( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
    GLuint hostReadColor = 0u;
    glGenTextures( 1, &hostReadColor );
    glBindTexture( GL_TEXTURE_2D, hostReadColor );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 48, 32, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glGenFramebuffers( 1, &host.presentationReadFramebuffer );
    glBindFramebuffer( GL_FRAMEBUFFER, host.presentationReadFramebuffer );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, hostReadColor, 0 );
    REQUIRE( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
    REQUIRE( glGetError() == GL_NO_ERROR );

    render::render_texture_handle_t targetColor{};
    const render::render_texture_desc_t targetColorDesc{
        16u, 16u, false, false, false, "Hosted offscreen color", true };
    REQUIRE( render::R_CreateTexture2D( targetColorDesc, {}, &targetColor ) ==
        render::render_error_t::OK );
    render::render_target_handle_t target{};
    REQUIRE( render::R_CreateRenderTarget( { targetColor }, &target ) ==
        render::render_error_t::OK );

    render::render_frame_info_t frame{};
    frame.frameIndex = 1u;
    frame.deltaSeconds = 1.0f / 60.0f;
    frame.drawableExtent = resizedExtent;
    frame.clearFlags = render::R_CLEAR_COLOR;
    frame.clearColor = { 0.0f, 1.0f, 0.0f, 1.0f };
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    GLint bound = 0;
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) == host.presentationFramebuffer );
    render::render_target_pass_desc_t pass{};
    pass.target = target;
    pass.clearColor = { 1.0f, 0.0f, 0.0f, 1.0f };
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) != host.presentationFramebuffer );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) == host.presentationFramebuffer );
    glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) == host.presentationReadFramebuffer );
    GLint viewport[4]{};
    glGetIntegerv( GL_VIEWPORT, viewport );
    CHECK( viewport[2] == 48 );
    CHECK( viewport[3] == 32 );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, host.presentationFramebuffer );
    glReadBuffer( GL_COLOR_ATTACHMENT0 );
    unsigned char hostPixel[4]{};
    glReadPixels( 24, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hostPixel );
    CHECK( hostPixel[1] >= 240u );
    CHECK( hostPixel[0] <= 15u );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, host.presentationReadFramebuffer );
    REQUIRE( glGetError() == GL_NO_ERROR );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    CHECK( host.nFramePreparations == 1 );

    REQUIRE( render::R_DestroyRenderTarget( target ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( targetColor ) == render::render_error_t::OK );

    const int nActivationsBeforeShutdown = host.nActivations;
    REQUIRE( render::R_Shutdown() == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsInitialized() );
    CHECK( host.nActivations > nActivationsBeforeShutdown );
    // CypherRender borrowed the context; ownership remains with this host.
    REQUIRE( sys::GLimp_IsContextValid( context ) );
    CHECK( glIsFramebuffer( host.presentationFramebuffer ) == GL_TRUE );
    CHECK( glIsFramebuffer( host.presentationReadFramebuffer ) == GL_TRUE );
    glDeleteFramebuffers( 1, &host.presentationFramebuffer );
    glDeleteFramebuffers( 1, &host.presentationReadFramebuffer );
    glDeleteTextures( 1, &hostColor );
    glDeleteTextures( 1, &hostReadColor );
    REQUIRE( sys::GLimp_DestroyContext( context ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );

    std::error_code cleanupError{};
    std::filesystem::remove_all( userPath, cleanupError );
}
