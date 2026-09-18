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
#include "CypherSystem/CypherSystem_OpenGL.h"
#include "CypherSystem/CypherSystem_Public.h"
#include "CypherSystem/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>

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
    const render::render_error_t rendererResult =
        render::R_InitHostSurface( surface, config );
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

    render::render_frame_info_t frame{};
    frame.frameIndex = 1u;
    frame.deltaSeconds = 1.0f / 60.0f;
    frame.drawableExtent = resizedExtent;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    CHECK( host.nFramePreparations == 1 );

    const int nActivationsBeforeShutdown = host.nActivations;
    REQUIRE( render::R_Shutdown() == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsInitialized() );
    CHECK( host.nActivations > nActivationsBeforeShutdown );
    // CypherRender borrowed the context; ownership remains with this host.
    REQUIRE( sys::GLimp_IsContextValid( context ) );
    REQUIRE( sys::GLimp_DestroyContext( context ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );

    std::error_code cleanupError{};
    std::filesystem::remove_all( userPath, cleanupError );
}
