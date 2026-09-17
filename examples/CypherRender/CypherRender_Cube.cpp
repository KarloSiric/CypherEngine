//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_Cube.cpp
//  Purpose: Demonstrates the first complete public-renderer drawing path.
//  Details: Loads a real cooked shader through the loose-directory VFS, draws
//           an indexed cube, and owns the System/Renderer startup and shutdown.
//           No graphics-driver or native-window API crosses into this host.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/FileSystem/CypherCommon_VfsDirectory.h"
#include "CypherCommon/Mathlib/CypherMath_Matrix4.h"
#include "CypherCommon/Mathlib/CypherMath_Quaternion.h"
#include "CypherRender/CypherRender_Draw.h"
#include "CypherRender/CypherRender_Pipeline.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_Shader.h"
#include "CypherSystem/CypherSystem_Public.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#ifndef CYPHER_RENDER_CUBE_SHADER_PATH
    #define CYPHER_RENDER_CUBE_SHADER_PATH "cooked/render_smoke/cube.cyshader_c"
#endif

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;

namespace
{

struct cube_vertex_t {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert( sizeof( cube_vertex_t ) == 32u );
static_assert( offsetof( cube_vertex_t, normal ) == 12u );
static_assert( offsetof( cube_vertex_t, uv ) == 24u );

// Four vertices per face preserve hard normals and a complete UV square.
// Every face's two triangles are counterclockwise when viewed from outside.
constexpr cube_vertex_t CUBE_VERTICES[] = {
    { { 1, -1, -1 }, { 1, 0, 0 }, { 0, 0 } },
    { { 1, 1, -1 }, { 1, 0, 0 }, { 1, 0 } },
    { { 1, 1, 1 }, { 1, 0, 0 }, { 1, 1 } },
    { { 1, -1, 1 }, { 1, 0, 0 }, { 0, 1 } },
    { { -1, 1, -1 }, { -1, 0, 0 }, { 0, 0 } },
    { { -1, -1, -1 }, { -1, 0, 0 }, { 1, 0 } },
    { { -1, -1, 1 }, { -1, 0, 0 }, { 1, 1 } },
    { { -1, 1, 1 }, { -1, 0, 0 }, { 0, 1 } },
    { { 1, 1, -1 }, { 0, 1, 0 }, { 0, 0 } },
    { { -1, 1, -1 }, { 0, 1, 0 }, { 1, 0 } },
    { { -1, 1, 1 }, { 0, 1, 0 }, { 1, 1 } },
    { { 1, 1, 1 }, { 0, 1, 0 }, { 0, 1 } },
    { { -1, -1, -1 }, { 0, -1, 0 }, { 0, 0 } },
    { { 1, -1, -1 }, { 0, -1, 0 }, { 1, 0 } },
    { { 1, -1, 1 }, { 0, -1, 0 }, { 1, 1 } },
    { { -1, -1, 1 }, { 0, -1, 0 }, { 0, 1 } },
    { { -1, -1, 1 }, { 0, 0, 1 }, { 0, 0 } },
    { { 1, -1, 1 }, { 0, 0, 1 }, { 1, 0 } },
    { { 1, 1, 1 }, { 0, 0, 1 }, { 1, 1 } },
    { { -1, 1, 1 }, { 0, 0, 1 }, { 0, 1 } },
    { { -1, 1, -1 }, { 0, 0, -1 }, { 0, 0 } },
    { { 1, 1, -1 }, { 0, 0, -1 }, { 1, 0 } },
    { { 1, -1, -1 }, { 0, 0, -1 }, { 1, 1 } },
    { { -1, -1, -1 }, { 0, 0, -1 }, { 0, 1 } }
};

constexpr common::u16 CUBE_INDICES[] = {
    0, 1, 2, 0, 2, 3,       4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23
};

// mat4_t is column-major. These byte offsets exactly match GLSL std140 mat4s.
struct alignas( 16 ) cube_transforms_t {
    math::mat4_t model;
    math::mat4_t view;
    math::mat4_t projection;
    alignas( 16 ) float tint[4];
};
static_assert( sizeof( cube_transforms_t ) == 208u );
static_assert( offsetof( cube_transforms_t, view ) == 64u );
static_assert( offsetof( cube_transforms_t, projection ) == 128u );
static_assert( offsetof( cube_transforms_t, tint ) == 192u );

struct cube_options_t {
    const char *shaderPath{ CYPHER_RENDER_CUBE_SHADER_PATH };
    common::u32 frameLimit{ 0u }; // Zero is an interactive run.
    bool hidden{ false };
    bool help{ false };
};

struct cube_state_t {
    sys::window_t window{};
    render::render_shader_handle_t shader{};
    render::render_pipeline_handle_t pipeline{};
    render::render_vertex_input_handle_t vertexInput{};
    render::render_buffer_handle_t vertices{};
    render::render_buffer_handle_t indices{};
    render::render_buffer_handle_t transforms{};
};

bool CheckRender( render::render_error_t result, const char *operation ) noexcept
{
    if ( result == render::render_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s (%s)\n", operation,
        render::R_ErrorName( result ), render::R_ErrorDescription( result ) );
    return false;
}

bool CheckSystem( sys::sys_error_t result, const char *operation ) noexcept
{
    if ( result == sys::sys_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s\n", operation, sys::Sys_ErrorName( result ) );
    return false;
}

bool ParseOptions( int argc, char **argv, cube_options_t &options ) noexcept
{
    for ( int i = 1; i < argc; ++i ) {
        if ( std::strcmp( argv[i], "--hidden" ) == 0 ) options.hidden = true;
        else if ( std::strcmp( argv[i], "--help" ) == 0 ) options.help = true;
        else if ( std::strcmp( argv[i], "--shader" ) == 0 && i + 1 < argc ) {
            options.shaderPath = argv[++i];
        } else if ( std::strcmp( argv[i], "--frames" ) == 0 && i + 1 < argc ) {
            const char *first = argv[++i];
            const char *last = first + std::strlen( first );
            const auto parsed = std::from_chars( first, last, options.frameLimit );
            if ( parsed.ec != std::errc{} || parsed.ptr != last || options.frameLimit == 0u ) {
                std::fprintf( stderr, "--frames requires a positive 32-bit integer.\n" );
                return false;
            }
        } else {
            std::fprintf( stderr, "Unknown option or missing value: %s\n", argv[i] );
            return false;
        }
    }
    // Hidden runs should always terminate without needing keyboard input.
    if ( options.hidden && options.frameLimit == 0u ) options.frameLimit = 120u;
    return true;
}

bool LoadShader( const char *nativePath, render::render_shader_handle_t &shader )
{
    const std::filesystem::path path = std::filesystem::absolute( nativePath );
    const std::string root = path.parent_path().string();
    const std::string filename = path.filename().string();
    common::vfs_directory_t directory{};
    const common::vfs_status_t mountResult = common::VfsDirectory_Init(
        &directory, { root.data(), root.size() } );
    if ( mountResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Shader directory: %s (%s)\n", root.c_str(),
            common::Vfs_StatusName( mountResult ) );
        return false;
    }

    common::blob_t bytes{};
    if ( !common::Blob_Init( &bytes, common::Allocator_GetSystem() ) ) {
        common::VfsDirectory_Shutdown( &directory );
        std::fprintf( stderr, "Could not initialize cooked shader storage.\n" );
        return false;
    }
    const common::vfs_t vfs = common::VfsDirectory_Make( &directory );
    constexpr common::usize MAX_SHADER_FILE_BYTES = 40u * 1024u * 1024u;
    const common::vfs_status_t readResult = common::Vfs_ReadAll(
        &vfs, { filename.data(), filename.size() }, MAX_SHADER_FILE_BYTES, &bytes );
    common::VfsDirectory_Shutdown( &directory );
    if ( readResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Read cooked shader %s: %s\n", nativePath,
            common::Vfs_StatusName( readResult ) );
        return false;
    }

    common::cooked_shader_view_t view{};
    const common::cooked_shader_result_t cookedResult =
        common::CookedShader_Read( common::Blob_Block( &bytes ), &view );
    if ( !common::CookedShader_Succeeded( cookedResult ) ) {
        std::fprintf( stderr, "Invalid cooked shader %s: %s\n", nativePath,
            common::CookedShader_StatusName( cookedResult.status ) );
        return false;
    }

    const render::render_shader_desc_t description{ &view, "First-draw cube shader" };
    // Creation consumes the view synchronously. The blob may die on return.
    return CheckRender( render::R_CreateShader( description, &shader ), "Create shader" );
}

bool CreateBuffer( render::render_buffer_handle_t &buffer, const void *bytes,
    common::u64 size, render::render_buffer_usage_flags_t usage,
    bool stream, const char *name ) noexcept
{
    render::render_buffer_desc_t description{};
    description.byteSize = size;
    description.usage = usage;
    description.updatePolicy = stream ? render::render_buffer_update_t::STREAM
                                     : render::render_buffer_update_t::IMMUTABLE;
    description.memory = stream ? render::render_buffer_memory_t::UPLOAD
                                : render::render_buffer_memory_t::DEVICE_LOCAL;
    description.debugName = name;
    const render::render_buffer_data_t data{ bytes, size };
    return CheckRender( render::R_CreateBuffer( description, &data, &buffer ), name );
}

bool CreateCube( cube_state_t &state, const char *shaderPath )
{
    if ( !LoadShader( shaderPath, state.shader ) ) return false;
    const cube_transforms_t initial{
        math::CY_MAT4_IDENTITY,
        math::CY_MAT4_IDENTITY,
        math::CY_MAT4_IDENTITY,
        { 0.10f, 0.55f, 0.76f, 1.0f } };
    if ( !CreateBuffer( state.vertices, CUBE_VERTICES, sizeof( CUBE_VERTICES ),
            render::R_BUFFER_USAGE_VERTEX, false, "Cube vertex buffer" ) ||
         !CreateBuffer( state.indices, CUBE_INDICES, sizeof( CUBE_INDICES ),
            render::R_BUFFER_USAGE_INDEX, false, "Cube index buffer" ) ||
         !CreateBuffer( state.transforms, &initial, sizeof( initial ),
            render::R_BUFFER_USAGE_UNIFORM | render::R_BUFFER_USAGE_TRANSFER_DESTINATION,
            true, "Cube transform buffer" ) ) return false;

    render::render_vertex_input_desc_t input{};
    input.layout.bindingCount = 1u;
    input.layout.bindings[0].stride = sizeof( cube_vertex_t );
    input.layout.attributeCount = 3u;
    input.layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    input.layout.attributes[1] = { render::render_format_t::RGB32_FLOAT, 12u, 1u, 0u };
    input.layout.attributes[2] = { render::render_format_t::RG32_FLOAT, 24u, 2u, 0u };
    input.vertexBufferCount = 1u;
    input.vertexBuffers[0] = { state.vertices, 0u, 0u };
    input.indexBuffer = { state.indices, 0u, render::render_index_type_t::UINT16 };
    input.debugName = "Cube vertex input";
    if ( !CheckRender( render::R_CreateVertexInput( input, &state.vertexInput ),
            "Create cube vertex input" ) ) return false;

    render::render_pipeline_desc_t pipeline{};
    pipeline.shader = state.shader;
    pipeline.vertexLayout = input.layout;
    pipeline.depthTest = true;
    pipeline.depthWrite = true;
    pipeline.cullBackFaces = true;
    pipeline.frontCounterClockwise = true;
    pipeline.alphaBlend = false;
    pipeline.uniformBlockName = "Transforms";
    pipeline.uniformBlockBytes = sizeof( cube_transforms_t );
    pipeline.debugName = "Depth-tested cube pipeline";
    return CheckRender( render::R_CreateGraphicsPipeline( pipeline, &state.pipeline ),
        "Create cube pipeline" );
}

bool RunFrames( cube_state_t &state, const cube_options_t &options ) noexcept
{
    const double started = sys::Sys_TimeNowSeconds();
    double previous = started;
    const double timeout = std::max( 30.0, 10.0 + options.frameLimit / 15.0 );
    float rotationTime = 0.0f;
    common::u32 renderedFrames = 0u;
    render::render_extent_t extent = render::R_GetInfo()->drawableExtent;

    while ( !sys::Sys_IsQuitRequested() && !sys::Sys_WindowShouldClose( state.window ) ) {
        sys::Sys_PollWindowEvents( state.window );
        sys::sys_event_t event{};
        while ( sys::Sys_PollEvent( event ) ) {
            if ( event.type == sys::sys_event_type_t::QUIT_REQUESTED ||
                 ( event.type == sys::sys_event_type_t::KEY &&
                   event.payload.key.key == sys::sys_key_t::ESCAPE &&
                   event.payload.key.action == sys::sys_input_action_t::PRESSED ) ) {
                sys::Sys_RequestQuit();
            }
        }
        if ( sys::Sys_IsQuitRequested() || sys::Sys_WindowShouldClose( state.window ) ) break;

        const double now = sys::Sys_TimeNowSeconds();
        const float delta = static_cast<float>( std::clamp( now - previous, 0.0, 0.1 ) );
        previous = now;
        if ( options.frameLimit != 0u && now - started > timeout ) {
            std::fprintf( stderr, "Timed out before completing %u frames.\n", options.frameLimit );
            return false;
        }
        if ( state.window.minimized || state.window.width == 0u || state.window.height == 0u ) {
            sys::Sys_SleepMilliseconds( 16u );
            continue;
        }
        if ( extent.width != state.window.width || extent.height != state.window.height ) {
            extent = { state.window.width, state.window.height };
            if ( !CheckRender( render::R_Resize( extent ), "Resize drawable" ) ) return false;
        }

        // Finite runs have deterministic animation for smoke checks. Interactive
        // runs advance with elapsed time, capped after debugger/focus pauses.
        rotationTime += options.frameLimit == 0u ? delta : 1.0f / 60.0f;
        const math::quat_t yaw = math::Quat_FromUnitAxisAngle(
            math::CY_VEC3_UP, math::Angle_FromRadians( rotationTime * 0.55f ) );
        const math::quat_t tilt = math::Quat_FromUnitAxisAngle(
            math::CY_VEC3_FORWARD, math::Angle_FromRadians( 0.18f + rotationTime * 0.21f ) );
        cube_transforms_t transforms{};
        transforms.model = math::Mat4_FromQuaternion( math::Quat_Multiply( yaw, tilt ) );
        transforms.tint[0] = 0.10f;
        transforms.tint[1] = 0.55f;
        transforms.tint[2] = 0.76f;
        transforms.tint[3] = 1.0f;
        if ( !math::Mat4_TryLookAtRH( { 4.0f, -6.0f, 3.2f }, math::CY_VEC3_ZERO,
                 math::CY_VEC3_UP, 0.0001f, &transforms.view ) ||
             !math::Mat4_TryPerspectiveRH( math::Angle_FromDegrees( 45.0f ),
                 static_cast<float>( extent.width ) / static_cast<float>( extent.height ),
                 0.1f, 100.0f, math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
                 &transforms.projection ) ) {
            std::fprintf( stderr, "Could not construct cube camera matrices.\n" );
            return false;
        }
        if ( !CheckRender( render::R_UpdateBuffer( state.transforms, 0u,
                 { &transforms, sizeof( transforms ) } ), "Upload transforms" ) ) return false;

        render::render_frame_info_t frame{};
        frame.frameIndex = renderedFrames;
        frame.deltaSeconds = delta;
        frame.drawableExtent = extent;
        frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
        frame.clearColor = { 0.035f, 0.045f, 0.065f, 1.0f };
        if ( !CheckRender( render::R_BeginFrame( frame ), "Begin frame" ) ) return false;
        render::render_draw_indexed_desc_t draw{};
        draw.pipeline = state.pipeline;
        draw.vertexInput = state.vertexInput;
        draw.uniformBuffer = state.transforms;
        draw.indexCount = 36u;
        draw.vertexCount = 24u;
        const bool drawOK = CheckRender( render::R_DrawIndexed( draw ), "Draw cube" );
        const bool endOK = CheckRender( render::R_EndFrame(), "End frame" );
        if ( !drawOK || !endOK ) return false;
        ++renderedFrames;
        if ( options.frameLimit != 0u && renderedFrames >= options.frameLimit ) break;
    }
    std::printf( "Rendered %u indexed cube frames.\n", renderedFrames );
    return options.frameLimit == 0u || renderedFrames == options.frameLimit;
}

bool Shutdown( cube_state_t &state ) noexcept
{
    bool success = true;
    if ( render::R_IsInitialized() ) {
        if ( render::R_IsFrameActive() ) {
            success = CheckRender( render::R_EndFrame(), "Finish active frame" ) && success;
        }
        success = CheckRender( render::R_WaitIdle(), "Wait for renderer" ) && success;
        // Release references before owners. Shutdown also catches any object
        // left live after an earlier failure, while the context still exists.
        if ( state.pipeline.value != 0u ) {
            success = CheckRender( render::R_DestroyGraphicsPipeline( state.pipeline ),
                "Destroy pipeline" ) && success;
        }
        if ( state.vertexInput.value != 0u ) {
            success = CheckRender( render::R_DestroyVertexInput( state.vertexInput ),
                "Destroy vertex input" ) && success;
        }
        for ( const auto buffer : { state.transforms, state.indices, state.vertices } ) {
            if ( buffer.value != 0u ) {
                success = CheckRender( render::R_DestroyBuffer( buffer ), "Destroy buffer" ) && success;
            }
        }
        if ( state.shader.value != 0u ) {
            success = CheckRender( render::R_DestroyShader( state.shader ), "Destroy shader" ) && success;
        }
        success = CheckRender( render::R_Shutdown(), "Shut down renderer" ) && success;
    }
    if ( state.window.valid ) {
        success = CheckSystem( sys::Sys_DestroyWindow( state.window ), "Destroy window" ) && success;
    }
    if ( sys::Sys_IsInitialized() ) {
        success = CheckSystem( sys::Sys_Shutdown(), "Shut down System" ) && success;
    }
    return success;
}

bool Run( int argc, char **argv, const cube_options_t &options, cube_state_t &state )
{
    const sys::init_info_t init{ argc, argv, "CypherRenderCube", "CypherEngine" };
    if ( !CheckSystem( sys::Sys_Init( init ), "Initialize System" ) ) return false;
    render::render_config_t config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = options.hidden ? render::render_present_mode_t::IMMEDIATE
                                       : render::render_present_mode_t::FIFO;
    config.sRGBFramebuffer = false;
    config.requireAcceleration = false;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    sys::window_desc_t window{};
    window.title = "Cypher Renderer | First Draw | Esc to exit";
    window.width = 1100u;
    window.height = 760u;
    if ( options.hidden ) window.flags |= sys::SYS_WINDOW_HIDDEN;
    if ( !CheckRender( render::R_ConfigureWindow( config, window ), "Configure window" ) ||
         !CheckSystem( sys::Sys_CreateWindow( window, state.window ), "Create window" ) ||
         !CheckRender( render::R_Init( state.window, config ), "Initialize renderer" ) ||
         !CreateCube( state, options.shaderPath ) ) return false;
    std::printf( "Cooked shader: %s\n24 vertices, 36 indices, 192-byte transform block.\n",
        options.shaderPath );
    return RunFrames( state, options );
}

} // namespace

int main( int argc, char **argv )
{
    cube_options_t options{};
    if ( !ParseOptions( argc, argv, options ) ) return 2;
    if ( options.help ) {
        std::puts( "Usage: cypher_render_cube [--shader PATH] [--frames N] [--hidden]\n"
                   "Escape or close the window to quit. Resize changes the projection.\n"
                   "--shader reads an existing .cyshader_c; cooking is an offline step.\n"
                   "--frames makes animation deterministic and exits after N frames.\n"
                   "--hidden defaults to 120 frames when --frames is omitted." );
        return 0;
    }
    cube_state_t state{};
    bool success = false;
    try {
        success = Run( argc, argv, options, state );
    } catch ( const std::exception &error ) {
        std::fprintf( stderr, "Cube demo: %s\n", error.what() );
    }
    const bool shutdownOK = Shutdown( state );
    return success && shutdownOK ? 0 : 1;
}
