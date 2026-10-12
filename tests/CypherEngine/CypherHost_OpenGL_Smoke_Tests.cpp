//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherEngine/CypherHost_OpenGL_Smoke_Tests.cpp
//  Purpose: Verifies Host renderer lifecycle and visible draw submission.
//  Details: A native graphics probe separates unavailable desktop OpenGL from
//           Host regressions. A callback draws a triangle and reads pixels
//           before presentation. Test files stay in a private directory.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEngine/CypherHost/CypherHost.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>
#include <glad/gl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace host = ::cypher::engine::host;
namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;
namespace common = ::cypher::common;

namespace {

struct temporary_workspace_t {
    std::filesystem::path originalDirectory{ std::filesystem::current_path() };
    std::filesystem::path root{};

    ~temporary_workspace_t()
    {
        std::error_code error{};
        std::filesystem::current_path( originalDirectory, error );
        if ( !root.empty() ) {
            std::filesystem::remove_all( root, error );
        }
    }
};

struct graphics_probe_t {
    sys::window_t window{};
    bool systemInitialized{ false };

    ~graphics_probe_t()
    {
        if ( sys::Sys_WindowIsValid( window ) ) {
            (void)sys::Sys_DestroyWindow( window );
        }
        if ( systemInitialized ) {
            (void)sys::Sys_Shutdown();
        }
    }
};

struct host_guard_t {
    host::state_t state{};
    bool initialized{ false };

    ~host_guard_t()
    {
        if ( initialized ) {
            host::Host_Shutdown( state );
        }
    }
};

struct submission_probe_t {
    int calls{ 0 };
    bool frameActive{ false };
    ::cypher::common::u64 frameIndex{ 0u };
    render::render_extent_t extent{};
};

struct triangle_submission_t {
    render::render_shader_handle_t shader{};
    render::render_buffer_handle_t vertices{};
    render::render_buffer_handle_t indices{};
    render::render_vertex_input_handle_t vertexInput{};
    render::render_pipeline_handle_t pipeline{};
    common::u8 center[4]{};
    common::u8 corner[4]{};
    GLenum readbackError{ GL_NO_ERROR };
    int calls{ 0 };
    bool frameActive{ false };

    // Preserve cleanup on a failed REQUIRE while Host still owns the context.
    ~triangle_submission_t()
    {
        if ( !render::R_IsInitialized() ) return;
        if ( pipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( pipeline );
        if ( vertexInput.value != 0u ) (void)render::R_DestroyVertexInput( vertexInput );
        if ( indices.value != 0u ) (void)render::R_DestroyBuffer( indices );
        if ( vertices.value != 0u ) (void)render::R_DestroyBuffer( vertices );
        if ( shader.value != 0u ) (void)render::R_DestroyShader( shader );
    }
};

render::render_error_t SubmitTriangle(
    const render::render_frame_info_t &frameInfo,
    void *userData ) noexcept
{
    auto *triangle = static_cast<triangle_submission_t *>( userData );
    if ( triangle == nullptr ) return render::render_error_t::ERR_INVALID_ARGUMENT;
    ++triangle->calls;
    triangle->frameActive = render::R_IsFrameActive();

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = triangle->pipeline;
    draw.vertexInput = triangle->vertexInput;
    draw.indexCount = 3u;
    draw.vertexCount = 3u;
    const auto drawResult = render::R_DrawIndexed( draw );
    if ( drawResult != render::render_error_t::OK ) return drawResult;

    glReadBuffer( GL_BACK );
    glReadPixels( static_cast<GLint>( frameInfo.drawableExtent.width / 2u ),
        static_cast<GLint>( frameInfo.drawableExtent.height / 2u ),
        1, 1, GL_RGBA, GL_UNSIGNED_BYTE, triangle->center );
    glReadPixels( 1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, triangle->corner );
    triangle->readbackError = glGetError();
    return render::render_error_t::OK;
}

render::render_error_t RecordSubmission(
    const render::render_frame_info_t &frameInfo,
    void *userData ) noexcept
{
    auto *probe = static_cast<submission_probe_t *>( userData );
    if ( probe == nullptr ) return render::render_error_t::ERR_INVALID_ARGUMENT;
    ++probe->calls;
    probe->frameActive = render::R_IsFrameActive();
    probe->frameIndex = frameInfo.frameIndex;
    probe->extent = frameInfo.drawableExtent;
    return render::render_error_t::OK;
}

render::render_error_t FailSubmission(
    const render::render_frame_info_t &,
    void * ) noexcept
{
    return render::render_error_t::ERR_SUBMISSION_FAILED;
}

} // namespace

TEST_CASE( "runtime Host initializes, renders, and releases OpenGL",
           "[CypherHost][OpenGL][Smoke]" )
{
    temporary_workspace_t workspace{};
    workspace.root = std::filesystem::temp_directory_path() /
        ( "cypher_host_opengl_" +
          std::to_string( sys::Sys_GetCurrentProcessId() ) + "_" +
          std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) );

    const std::filesystem::path basePath = workspace.root / "base";
    const std::filesystem::path userPath = workspace.root / "user";
    REQUIRE( std::filesystem::create_directories( basePath / "config" ) );
    REQUIRE( std::filesystem::create_directories( userPath ) );

    {
        std::ofstream configFile( basePath / "config" / "default.cfg" );
        REQUIRE( configFile.is_open() );
        configFile << "set r_width 64\n"
                   << "set r_height 64\n"
                   << "set r_vsync 0\n";
        configFile.close();
        REQUIRE( configFile.good() );
    }
    std::filesystem::current_path( workspace.root );

    const std::string basePathString = basePath.string();
    const std::string userPathString = userPath.string();
    const char *arguments[] = {
        "cypher_host_opengl_smoke_tests",
        "-basedir", basePathString.c_str(),
        "-userpath", userPathString.c_str()
    };

    // Probe the renderer, not just context creation. Virtualized drivers can
    // report OpenGL 4.1 while missing entry points CypherRender needs.
    {
        graphics_probe_t probe{};
        const sys::init_info_t initInfo{
            5, arguments, "CypherHostOpenGLSmokeTests", "CypherTests"
        };
        REQUIRE( sys::Sys_Init( initInfo ) == sys::sys_error_t::OK );
        probe.systemInitialized = true;

        render::render_config_t config = render::R_DefaultConfig();
        config.presentMode = render::render_present_mode_t::IMMEDIATE;
        sys::window_desc_t windowDescription{};
        windowDescription.title = "CypherHost OpenGL Probe";
        windowDescription.width = 64u;
        windowDescription.height = 64u;
        windowDescription.flags = sys::SYS_WINDOW_HIDDEN;
        REQUIRE( render::R_ConfigureWindow( config, windowDescription ) ==
            render::render_error_t::OK );

        const sys::sys_error_t windowResult =
            sys::Sys_CreateWindow( windowDescription, probe.window );
        if ( windowResult == sys::sys_error_t::ERR_GRAPHICS_CONFIG_UNSUPPORTED ||
             windowResult == sys::sys_error_t::ERR_INTERNAL_ERROR ) {
            SKIP( "No OpenGL 4.1-capable desktop window is available" );
        }
        REQUIRE( windowResult == sys::sys_error_t::OK );

        const render::render_error_t rendererProbe =
            render::R_Init( probe.window, config );
        if ( rendererProbe == render::render_error_t::ERR_CONTEXT_CREATE_FAILED ||
             rendererProbe == render::render_error_t::ERR_CONTEXT_ACTIVATE_FAILED ||
             rendererProbe == render::render_error_t::ERR_ENTRYPOINT_LOAD_FAILED ||
             rendererProbe == render::render_error_t::ERR_BACKEND_VERSION_UNSUPPORTED ||
             rendererProbe == render::render_error_t::ERR_CAPABILITY_MISSING ) {
            SKIP( "The desktop driver cannot run the required OpenGL 4.1 backend" );
        }
        REQUIRE( rendererProbe == render::render_error_t::OK );
        REQUIRE( render::R_Shutdown() == render::render_error_t::OK );
    }

    REQUIRE_FALSE( sys::Sys_IsInitialized() );
    REQUIRE_FALSE( render::R_IsInitialized() );

    host_guard_t runningHost{};
    runningHost.state.config.argc = 5;
    runningHost.state.config.argv = arguments;
    const host::host_error_t initResult = host::Host_Init( runningHost.state );
    runningHost.initialized = initResult == host::host_error_t::OK;
    if ( initResult != host::host_error_t::OK ) {
        CHECK_FALSE( render::R_IsInitialized() );
        CHECK_FALSE( sys::Sys_WindowIsValid( runningHost.state.window ) );
        CHECK_FALSE( sys::Sys_IsInitialized() );
        FAIL( "Host_Init failed after a compatible graphics context was probed" );
    }

    REQUIRE( host::Host_IsRunning( runningHost.state ) );
    REQUIRE( runningHost.state.rendererInitialized );
    REQUIRE( render::R_IsInitialized() );
    REQUIRE( runningHost.state.window.graphicsApi ==
        sys::window_graphics_api_t::OPENGL );
    REQUIRE( render::R_GetInfo() != nullptr );
    CHECK( render::R_GetInfo()->backend == render::render_backend_t::OPENGL );
    CHECK( host::Host_Init( runningHost.state ) == host::host_error_t::ERR_IS_INIT );
    CHECK( host::Host_IsRunning( runningHost.state ) );
    CHECK( render::R_IsInitialized() );

    host::Host_BeginFrame( runningHost.state );
    host::Host_Update( runningHost.state );
    CHECK( host::Host_Render( runningHost.state ) == render::render_error_t::OK );
    host::Host_EndFrame( runningHost.state );

    CHECK( runningHost.state.frame.index == 1u );
    CHECK( host::Host_IsRunning( runningHost.state ) );
    CHECK( render::R_IsInitialized() );
    CHECK_FALSE( render::R_IsFrameActive() );
    REQUIRE( render::R_GetInfo() != nullptr );
    CHECK( render::R_GetInfo()->drawableExtent.width ==
        runningHost.state.window.width );
    CHECK( render::R_GetInfo()->drawableExtent.height ==
        runningHost.state.window.height );

    submission_probe_t submission{};
    host::Host_BeginFrame( runningHost.state );
    host::Host_Update( runningHost.state );
    CHECK( host::Host_Render( runningHost.state, RecordSubmission, &submission ) ==
        render::render_error_t::OK );
    host::Host_EndFrame( runningHost.state );
    CHECK( submission.calls == 1 );
    CHECK( submission.frameActive );
    CHECK( submission.frameIndex == 2u );
    CHECK( submission.extent.width == runningHost.state.window.width );
    CHECK( submission.extent.height == runningHost.state.window.height );
    CHECK( host::Host_IsRunning( runningHost.state ) );
    CHECK_FALSE( render::R_IsFrameActive() );

    // The renderer's own smoke test proves drawing; this frame proves that
    // Host's callback reaches the same drawable before Host presents it.
    {
        triangle_submission_t triangle{};
        constexpr char vertexSource[] =
            "#version 410 core\n"
            "layout(location=0) in vec3 position;\n"
            "void main() { gl_Position=vec4(position,1.0); }\n";
        constexpr char fragmentSource[] =
            "#version 410 core\n"
            "layout(location=0) out vec4 color;\n"
            "void main() { color=vec4(1.0,0.0,0.0,1.0); }\n";
        common::cooked_shader_view_t shaderView{};
        shaderView.nLanguageVersion = 410u;
        shaderView.nStages = 2u;
        shaderView.stages[0].stage = common::render_shader_stage_t::VERTEX;
        shaderView.stages[0].code = {
            reinterpret_cast<const common::byte *>( vertexSource ), sizeof( vertexSource ) };
        shaderView.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        shaderView.stages[1].code = {
            reinterpret_cast<const common::byte *>( fragmentSource ), sizeof( fragmentSource ) };
        REQUIRE( render::R_CreateShader( { &shaderView, "Host triangle smoke" },
            &triangle.shader ) == render::render_error_t::OK );

        constexpr float vertices[]{
            -0.8f, -0.8f, 0.0f,
             0.8f, -0.8f, 0.0f,
             0.0f,  0.8f, 0.0f
        };
        constexpr common::u16 indices[]{ 0u, 1u, 2u };
        render::render_buffer_desc_t vertexDesc{};
        vertexDesc.byteSize = sizeof( vertices );
        vertexDesc.usage = render::R_BUFFER_USAGE_VERTEX;
        const render::render_buffer_data_t vertexData{
            vertices, sizeof( vertices ) };
        REQUIRE( render::R_CreateBuffer( vertexDesc, &vertexData,
            &triangle.vertices ) == render::render_error_t::OK );

        render::render_buffer_desc_t indexDesc{};
        indexDesc.byteSize = sizeof( indices );
        indexDesc.usage = render::R_BUFFER_USAGE_INDEX;
        const render::render_buffer_data_t indexData{
            indices, sizeof( indices ) };
        REQUIRE( render::R_CreateBuffer( indexDesc, &indexData,
            &triangle.indices ) == render::render_error_t::OK );

        render::render_vertex_input_desc_t input{};
        input.layout.bindingCount = 1u;
        input.layout.bindings[0].stride = 3u * sizeof( float );
        input.layout.attributeCount = 1u;
        input.layout.attributes[0] = {
            render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
        input.vertexBufferCount = 1u;
        input.vertexBuffers[0] = { triangle.vertices, 0u, 0u };
        input.indexBuffer = {
            triangle.indices, 0u, render::render_index_type_t::UINT16 };
        REQUIRE( render::R_CreateVertexInput( input, &triangle.vertexInput ) ==
            render::render_error_t::OK );

        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = triangle.shader;
        pipeline.vertexLayout = input.layout;
        REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &triangle.pipeline ) ==
            render::render_error_t::OK );

        host::Host_BeginFrame( runningHost.state );
        host::Host_Update( runningHost.state );
        REQUIRE( host::Host_Render( runningHost.state, SubmitTriangle, &triangle ) ==
            render::render_error_t::OK );
        host::Host_EndFrame( runningHost.state );
        CHECK( triangle.calls == 1 );
        CHECK( triangle.frameActive );
        CHECK( triangle.readbackError == GL_NO_ERROR );
        CHECK( triangle.center[0] >= 220u );
        CHECK( triangle.center[1] <= 35u );
        CHECK( triangle.center[2] <= 35u );
        CHECK( triangle.corner[0] <= 80u );
        CHECK( triangle.center[0] > triangle.corner[0] + 120u );
        CHECK( host::Host_IsRunning( runningHost.state ) );
        CHECK_FALSE( render::R_IsFrameActive() );

        REQUIRE( render::R_WaitIdle() == render::render_error_t::OK );
        REQUIRE( render::R_DestroyGraphicsPipeline( triangle.pipeline ) ==
            render::render_error_t::OK );
        triangle.pipeline = {};
        REQUIRE( render::R_DestroyVertexInput( triangle.vertexInput ) ==
            render::render_error_t::OK );
        triangle.vertexInput = {};
        REQUIRE( render::R_DestroyBuffer( triangle.indices ) ==
            render::render_error_t::OK );
        triangle.indices = {};
        REQUIRE( render::R_DestroyBuffer( triangle.vertices ) ==
            render::render_error_t::OK );
        triangle.vertices = {};
        REQUIRE( render::R_DestroyShader( triangle.shader ) ==
            render::render_error_t::OK );
        triangle.shader = {};
    }

    host::Host_BeginFrame( runningHost.state );
    host::Host_Update( runningHost.state );
    CHECK( host::Host_Render( runningHost.state, FailSubmission, nullptr ) ==
        render::render_error_t::ERR_SUBMISSION_FAILED );
    host::Host_EndFrame( runningHost.state );
    CHECK_FALSE( host::Host_IsRunning( runningHost.state ) );
    CHECK_FALSE( render::R_IsFrameActive() );

    host::Host_Shutdown( runningHost.state );
    runningHost.initialized = false;
    CHECK_FALSE( render::R_IsInitialized() );
    CHECK_FALSE( runningHost.state.rendererInitialized );
    CHECK_FALSE( sys::Sys_WindowIsValid( runningHost.state.window ) );
    CHECK_FALSE( sys::Sys_IsInitialized() );
}
