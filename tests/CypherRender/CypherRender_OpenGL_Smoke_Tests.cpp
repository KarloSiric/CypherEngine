//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherRender/CypherRender_OpenGL_Smoke_Tests.cpp
//  Purpose: Exercises the complete native OpenGL renderer bootstrap path.
//  Details: The test uses a hidden 64x64 window. Machines without a usable
//           desktop graphics session skip this smoke check; deterministic
//           frontend and dispatch behavior remains covered by runtime tests.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Buffer.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherSystem/CypherSystem_Public.h"
#include "CypherSystem/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <cstring>
#include <string>

namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;

TEST_CASE( "OpenGL renderer clears and presents one hidden frame", "[CypherRender][OpenGL][Smoke]" )
{
    const std::filesystem::path userPath = std::filesystem::temp_directory_path() /
        ( "cypher_render_opengl_" + std::to_string( sys::Sys_GetCurrentProcessId() ) );
    const std::string userPathString = userPath.string();
    const char *arguments[] = {
        "cypher_render_opengl_smoke_tests",
        "-basedir",
        ".",
        "-userpath",
        userPathString.c_str()
    };
    const sys::init_info_t initInfo = {
        5,
        arguments,
        "CypherRenderOpenGLSmokeTests",
        "CypherTests"
    };

    const sys::sys_error_t systemResult = sys::Sys_Init( initInfo );
    if ( systemResult != sys::sys_error_t::OK ) {
        SKIP( "CypherSystem could not initialize a desktop graphics session" );
    }

    render::render_config_t config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = render::render_present_mode_t::IMMEDIATE;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    config.sRGBFramebuffer = false; // The smoke test verifies control flow, not framebuffer policy.
    config.requireAcceleration = false; // Permit virtualized CI graphics implementations.

    sys::window_desc_t windowDescription{};
    windowDescription.title = "CypherRender OpenGL Smoke Test";
    windowDescription.width = 64u;
    windowDescription.height = 64u;
    windowDescription.flags = sys::SYS_WINDOW_HIDDEN;

    const render::render_error_t configureResult =
        render::R_ConfigureWindow( config, windowDescription );
    REQUIRE( configureResult == render::render_error_t::OK );

    sys::window_t window{};
    const sys::sys_error_t windowResult = sys::Sys_CreateWindow( windowDescription, window );
    if ( windowResult != sys::sys_error_t::OK ) {
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host has no OpenGL 4.1-capable window system" );
    }

    const render::render_error_t rendererResult = render::R_Init( window, config );
    if ( rendererResult != render::render_error_t::OK ) {
        REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host graphics driver cannot run the OpenGL 4.1 backend" );
    }

    REQUIRE( render::R_IsInitialized() );
    const render::render_info_t *info = render::R_GetInfo();
    REQUIRE( info != nullptr );
    CHECK( info->backend == render::render_backend_t::OPENGL );
    CHECK( info->apiMajorVersion >= 4u );
    CHECK( ( info->capabilities & render::R_CAPABILITY_RASTERIZATION ) != 0u );
    CHECK( info->drawableExtent.width > 0u );
    CHECK( info->drawableExtent.height > 0u );

    ::cypher::common::u32 vertexWords[16]{};
    render::render_buffer_desc_t bufferDescription{};
    bufferDescription.byteSize = sizeof( vertexWords );
    bufferDescription.usage = render::R_BUFFER_USAGE_VERTEX |
        render::R_BUFFER_USAGE_TRANSFER_DESTINATION;
    bufferDescription.updatePolicy = render::render_buffer_update_t::DYNAMIC;
    bufferDescription.memory = render::render_buffer_memory_t::UPLOAD;
    bufferDescription.debugName = "OpenGL smoke vertex buffer";

    const render::render_buffer_data_t initialData{
        vertexWords,
        sizeof( vertexWords )
    };
    render::render_buffer_handle_t buffer{};
    REQUIRE( render::R_CreateBuffer( bufferDescription, &initialData, &buffer ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_IsBufferValid( buffer ) );

    render::render_buffer_info_t bufferInfo{};
    REQUIRE( render::R_GetBufferInfo( buffer, &bufferInfo ) ==
        render::render_error_t::OK );
    CHECK( bufferInfo.byteSize == sizeof( vertexWords ) );
    CHECK( bufferInfo.usage == bufferDescription.usage );
    CHECK_FALSE( bufferInfo.mapped );

    const ::cypher::common::u32 replacementWords[4]{ 1u, 2u, 3u, 4u };
    REQUIRE( render::R_UpdateBuffer(
        buffer,
        sizeof( ::cypher::common::u32 ) * 4u,
        { replacementWords, sizeof( replacementWords ) } ) ==
        render::render_error_t::OK );

    const render::render_buffer_range_t mappedRange{
        sizeof( ::cypher::common::u32 ) * 2u,
        sizeof( ::cypher::common::u32 ) * 8u
    };
    render::render_buffer_mapping_t mapping{};
    REQUIRE( render::R_MapBuffer(
        buffer,
        mappedRange,
        render::R_BUFFER_MAP_WRITE,
        &mapping ) == render::render_error_t::OK );
    REQUIRE( mapping.bytes != nullptr );
    CHECK( mapping.byteSize == mappedRange.byteSize );
    std::memset( mapping.bytes, 0x2A, static_cast<std::size_t>( mapping.byteSize ) );

    CHECK( render::R_MapBuffer(
        buffer,
        mappedRange,
        render::R_BUFFER_MAP_WRITE,
        &mapping ) == render::render_error_t::ERR_RESOURCE_BUSY );
    CHECK( render::R_DestroyBuffer( buffer ) ==
        render::render_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_FlushMappedBuffer( buffer, mappedRange ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_UnmapBuffer( buffer ) == render::render_error_t::OK );

    const ::cypher::common::handle_parts64_t bufferParts =
        ::cypher::common::Cy_Handle64Unpack( buffer );
    const render::render_buffer_handle_t wrongTypeHandle =
        ::cypher::common::Cy_Handle64Make(
            bufferParts.nIndex,
            bufferParts.nGeneration,
            static_cast<::cypher::common::u32>( render::render_object_type_t::TEXTURE ) );
    CHECK( render::R_GetBufferInfo( wrongTypeHandle, &bufferInfo ) ==
        render::render_error_t::ERR_RESOURCE_TYPE_MISMATCH );

    REQUIRE( render::R_DestroyBuffer( buffer ) == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsBufferValid( buffer ) );
    CHECK( render::R_GetBufferInfo( buffer, &bufferInfo ) ==
        render::render_error_t::ERR_STALE_HANDLE );

    // Leave one live buffer for R_Shutdown to exercise module-owned cleanup.
    render::render_buffer_handle_t shutdownOwnedBuffer{};
    REQUIRE( render::R_CreateBuffer(
        bufferDescription,
        nullptr,
        &shutdownOwnedBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_IsBufferValid( shutdownOwnedBuffer ) );

    render::render_frame_info_t frameInfo{};
    frameInfo.frameIndex = 1u;
    frameInfo.deltaSeconds = 1.0f / 60.0f;
    frameInfo.drawableExtent = info->drawableExtent;
    frameInfo.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    frameInfo.clearColor = { 0.1f, 0.2f, 0.3f, 1.0f };

    REQUIRE( render::R_BeginFrame( frameInfo ) == render::render_error_t::OK );
    REQUIRE( render::R_IsFrameActive() );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    REQUIRE_FALSE( render::R_IsFrameActive() );
    REQUIRE( render::R_WaitIdle() == render::render_error_t::OK );
    REQUIRE( render::R_Shutdown() == render::render_error_t::OK );
    REQUIRE_FALSE( render::R_IsInitialized() );

    REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );

    std::error_code cleanupError{};
    std::filesystem::remove_all( userPath, cleanupError );
}
