//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherRender/CypherRender_OpenGL_Smoke_Tests.cpp
//  Purpose: Exercises native OpenGL startup, cooked stages, buffers, and frames.
//  Details: The test uses a hidden 64x64 window. Machines without a usable
//           desktop graphics session skip this smoke check; deterministic
//           frontend and dispatch behavior remains covered by runtime tests.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Buffer.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_VertexInput.h"
#include "CypherRender/OpenGL/CypherRender_OpenGL_Local.h"
#include "CypherSystem/CypherSystem_Public.h"
#include "CypherSystem/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <cstring>
#include <string>

namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;
namespace common = ::cypher::common;

namespace
{

struct native_shader_scope_t {
    GLuint shader{ 0u };

    ~native_shader_scope_t()
    {
        if ( shader != 0u ) glDeleteShader( shader );
    }
};

void CheckCookedShaderCompilation()
{
    constexpr char vertexSource[] =
        "#version 410 core\n"
        "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    constexpr char fragmentSource[] =
        "#version 410 core\n"
        "layout(location = 0) out vec4 color;\n"
        "void main() { color = vec4(1.0); }\n";
    constexpr char brokenSource[] = "#version 410 core\nvoid main( {\n";

    common::cooked_shader_stage_source_t stages[2]{};
    stages[0].stage = common::render_shader_stage_t::VERTEX;
    stages[0].code = {
        reinterpret_cast<const common::byte *>( vertexSource ), sizeof( vertexSource )
    };
    stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    stages[1].code = {
        reinterpret_cast<const common::byte *>( fragmentSource ), sizeof( fragmentSource )
    };

    common::byte cookedBytes[1024]{};
    common::cooked_shader_view_t shaderView{};
    const auto readCookedStages = [&]() {
        const common::cooked_shader_desc_t description{};
        const common::usize required = common::CookedShader_RequiredSize(
            description, { stages, 2u } );
        REQUIRE( required > 0u );
        REQUIRE( required <= sizeof( cookedBytes ) );
        REQUIRE( common::CookedShader_Succeeded( common::CookedShader_Write(
            description, { stages, 2u }, {}, { cookedBytes, required } ) ) );
        REQUIRE( common::CookedShader_Succeeded( common::CookedShader_Read(
            { cookedBytes, required }, &shaderView ) ) );
        REQUIRE( shaderView.nStages == 2u );
    };

    readCookedStages();
    for ( common::u32 i = 0u; i < shaderView.nStages; ++i ) {
        CAPTURE( i );
        native_shader_scope_t native;
        REQUIRE( render::GL_CompileShaderStage(
            shaderView.stages[i], "Cooked OpenGL smoke shader", native.shader ) ==
            render::render_error_t::OK );
        REQUIRE( native.shader != 0u );
        CHECK( glIsShader( native.shader ) == GL_TRUE );
        GLint compiled = GL_FALSE;
        glGetShaderiv( native.shader, GL_COMPILE_STATUS, &compiled );
        CHECK( compiled == GL_TRUE );
    }
    CHECK( glGetError() == GL_NO_ERROR );

    // The cooked reader validates the container and source encoding. Native GLSL
    // compilation has its own failure result and must release its temporary object.
    stages[0].code = {
        reinterpret_cast<const common::byte *>( brokenSource ), sizeof( brokenSource )
    };
    readCookedStages();
    const common::cooked_shader_stage_view_t *brokenStage = common::CookedShader_FindStage(
        shaderView, common::render_shader_stage_t::VERTEX );
    REQUIRE( brokenStage != nullptr );
    native_shader_scope_t failed;
    CHECK( render::GL_CompileShaderStage(
        *brokenStage, "Intentionally invalid smoke shader", failed.shader ) ==
        render::render_error_t::ERR_SHADER_COMPILE_FAILED );
    CHECK( failed.shader == 0u );
    CHECK( glGetError() == GL_NO_ERROR );
}

void CheckIndexedDrawing( const render::render_extent_t extent )
{
    constexpr char vertexSource[] =
        "#version 410 core\n"
        "layout(location=0) in vec3 position;\n"
        "layout(std140) uniform TestDraw { mat4 transform; vec4 tint; };\n"
        "out vec4 vertexColor;\n"
        "void main() { gl_Position = transform * vec4(position,1.0); vertexColor=tint; }\n";
    constexpr char fragmentSource[] =
        "#version 410 core\n"
        "in vec4 vertexColor; layout(location=0) out vec4 color;\n"
        "void main() { color=vertexColor; }\n";
    constexpr char mismatchedFragment[] =
        "#version 410 core\n"
        "in vec3 vertexColor; layout(location=0) out vec4 color;\n"
        "void main() { color=vec4(vertexColor,1.0); }\n";
    common::cooked_shader_stage_source_t stages[2]{};
    stages[0].stage = common::render_shader_stage_t::VERTEX;
    stages[0].code = { reinterpret_cast<const common::byte *>( vertexSource ), sizeof( vertexSource ) };
    stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    stages[1].code = { reinterpret_cast<const common::byte *>( fragmentSource ), sizeof( fragmentSource ) };
    common::byte bytes[2048]{};
    common::cooked_shader_view_t view{};
    const auto cook = [&]() {
        const common::cooked_shader_desc_t desc{};
        const common::usize size = common::CookedShader_RequiredSize( desc, { stages, 2u } );
        REQUIRE( size <= sizeof( bytes ) );
        REQUIRE( common::CookedShader_Succeeded( common::CookedShader_Write(
            desc, { stages, 2u }, {}, { bytes, size } ) ) );
        REQUIRE( common::CookedShader_Succeeded( common::CookedShader_Read( { bytes, size }, &view ) ) );
    };
    cook();
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &view, "Indexed pixel smoke" }, &shader ) == render::render_error_t::OK );

    // Both stages compile separately, but their interface types cannot link.
    stages[1].code = { reinterpret_cast<const common::byte *>( mismatchedFragment ), sizeof( mismatchedFragment ) };
    cook();
    render::render_shader_handle_t failedShader{};
    CHECK( render::R_CreateShader( { &view, "Expected interface link failure" }, &failedShader ) ==
        render::render_error_t::ERR_SHADER_LINK_FAILED );
    CHECK_FALSE( render::R_IsShaderValid( failedShader ) );
    // Shader creation owns the source now; invalidate all borrowed cooked data.
    std::memset( bytes, 0, sizeof( bytes ) );
    view = common::cooked_shader_view_t{};

    const float vertices[9]{ -0.8f, -0.8f, 0.0f, 0.8f, -0.8f, 0.0f, 0.0f, 0.8f, 0.0f };
    const common::u16 indices[3]{ 0u, 1u, 2u };
    float uniforms[20]{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 1,0,0,1 };
    const auto createBuffer = []( const void *data, const common::u64 size,
                                 const render::render_buffer_usage_flags_t usage,
                                 const bool dynamic ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage | ( dynamic ? render::R_BUFFER_USAGE_TRANSFER_DESTINATION : 0u );
        desc.updatePolicy = dynamic ? render::render_buffer_update_t::DYNAMIC : render::render_buffer_update_t::IMMUTABLE;
        desc.memory = dynamic ? render::render_buffer_memory_t::UPLOAD : render::render_buffer_memory_t::DEVICE_LOCAL;
        const render::render_buffer_data_t contents{ data, size };
        render::render_buffer_handle_t buffer{};
        REQUIRE( render::R_CreateBuffer( desc, &contents, &buffer ) == render::render_error_t::OK );
        return buffer;
    };
    const auto vertexBuffer = createBuffer( vertices, sizeof( vertices ), render::R_BUFFER_USAGE_VERTEX, true );
    const auto indexBuffer = createBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX, false );
    const auto uniformBuffer = createBuffer( uniforms, sizeof( uniforms ), render::R_BUFFER_USAGE_UNIFORM, true );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout.bindingCount = 1u;
    inputDesc.layout.bindings[0].stride = 3u * sizeof( float );
    inputDesc.layout.attributeCount = 1u;
    inputDesc.layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDesc.vertexBufferCount = 1u;
    inputDesc.vertexBuffers[0] = { vertexBuffer, 0u, 0u };
    inputDesc.indexBuffer = { indexBuffer, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDesc, &input ) == render::render_error_t::OK );

    render::render_pipeline_desc_t pipelineDesc{};
    pipelineDesc.shader = shader;
    pipelineDesc.vertexLayout = inputDesc.layout;
    pipelineDesc.uniformBlockName = "TestDraw";
    pipelineDesc.uniformBlockBytes = sizeof( uniforms );
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &pipeline ) == render::render_error_t::OK );
    CHECK( render::R_DestroyShader( shader ) == render::render_error_t::ERR_RESOURCE_BUSY );

    render::render_pipeline_handle_t invalidPipeline{};
    pipelineDesc.uniformBlockBytes += 16u;
    CHECK( render::R_CreateGraphicsPipeline( pipelineDesc, &invalidPipeline ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    CHECK_FALSE( render::R_IsGraphicsPipelineValid( invalidPipeline ) );
    pipelineDesc.uniformBlockBytes = sizeof( uniforms );
    pipelineDesc.vertexLayout.attributes[0].location = 1u;
    CHECK( render::R_CreateGraphicsPipeline( pipelineDesc, &invalidPipeline ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipelineDesc.vertexLayout = inputDesc.layout;
    pipelineDesc.depthWrite = false;
    render::render_pipeline_handle_t readOnlyDepth{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &readOnlyDepth ) == render::render_error_t::OK );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.uniformBuffer = uniformBuffer;
    draw.indexCount = 3u;
    draw.vertexCount = 3u;
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_FRAME_NOT_ACTIVE );
    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    frame.clearColor = { 0,0,1,1 };
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );

    auto invalidDraw = draw;
    invalidDraw.firstIndex = 1u;
    CHECK( render::R_DrawIndexed( invalidDraw ) == render::render_error_t::ERR_INDEX_DATA_INVALID );
    invalidDraw = draw;
    invalidDraw.vertexCount = 4u;
    CHECK( render::R_DrawIndexed( invalidDraw ) == render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );
    invalidDraw = draw;
    invalidDraw.uniformBuffer = {};
    CHECK( render::R_DrawIndexed( invalidDraw ) == render::render_error_t::ERR_BINDING_MISMATCH );
    render::render_buffer_mapping_t mapping{};
    REQUIRE( render::R_MapBuffer( vertexBuffer, { 0u, sizeof( vertices ) }, render::R_BUFFER_MAP_WRITE, &mapping ) == render::render_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_UnmapBuffer( vertexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_MapBuffer( uniformBuffer, { 0u, sizeof( uniforms ) }, render::R_BUFFER_MAP_WRITE, &mapping ) == render::render_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_UnmapBuffer( uniformBuffer ) == render::render_error_t::OK );

    const auto readPixel = [&]( const GLint x, const GLint y, const int channel ) {
        common::u8 pixel[4]{};
        glReadBuffer( GL_BACK );
        glReadPixels( x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel );
        REQUIRE( glGetError() == GL_NO_ERROR );
        CAPTURE( pixel[0], pixel[1], pixel[2] );
        CHECK( pixel[channel] >= 240u );
        CHECK( pixel[( channel + 1 ) % 3] <= 15u );
        CHECK( pixel[( channel + 2 ) % 3] <= 15u );
    };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ), static_cast<GLint>( extent.height / 2u ), 0 );
    readPixel( 1, 1, 2 );
    // End with depth writes disabled, then verify next BeginFrame really clears
    // the previous 0.5 depth. The green triangle uses that same depth and LESS.
    draw.pipeline = readOnlyDepth;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    uniforms[16] = 0.0f;
    uniforms[17] = 1.0f;
    REQUIRE( render::R_UpdateBuffer( uniformBuffer, 0u, { uniforms, sizeof( uniforms ) } ) == render::render_error_t::OK );
    ++frame.frameIndex;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.pipeline = pipeline;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ), static_cast<GLint>( extent.height / 2u ), 1 );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsGraphicsPipelineValid( pipeline ) );
    render::render_pipeline_info_t staleInfo{};
    CHECK( render::R_GetGraphicsPipelineInfo( pipeline, &staleInfo ) == render::render_error_t::ERR_STALE_HANDLE );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vertexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( indexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( uniformBuffer ) == render::render_error_t::OK );
    // Leave the second pipeline and its shader live to verify dependency-order
    // shutdown. There must be no busy-reference failure from R_Shutdown below.
}

void CheckSampledDrawing( const render::render_extent_t extent )
{
    constexpr char vertex[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "void main() { gl_Position = vec4(position,1.0); }\n";
    constexpr char fragment[] =
        "#version 410 core\nuniform sampler2D base_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color=texture(base_color,vec2(0.5)); }\n";
    common::cooked_shader_view_t source{};
    source.nLanguageVersion = 410u;
    source.nStages = 2u;
    source.stages[0].stage = common::render_shader_stage_t::VERTEX;
    source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
    source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), sizeof( fragment ) };
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &source, "Textured triangle" }, &shader ) == render::render_error_t::OK );

    const float vertices[]{ -0.9f,-0.9f,0, 0.9f,-0.9f,0, 0,0.9f,0 };
    const common::u16 indices[]{ 0,1,2 };
    auto buffer = []( const void *bytes, common::u64 size, render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( desc, &data, &result ) == render::render_error_t::OK );
        return result;
    };
    const auto vb = buffer( vertices, sizeof( vertices ), render::R_BUFFER_USAGE_VERTEX );
    const auto ib = buffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout.bindingCount = 1u;
    inputDesc.layout.bindings[0].stride = 12u;
    inputDesc.layout.attributeCount = 1u;
    inputDesc.layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDesc.vertexBufferCount = 1u;
    inputDesc.vertexBuffers[0] = { vb, 0u, 0u };
    inputDesc.indexBuffer = { ib, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDesc, &input ) == render::render_error_t::OK );
    render::render_pipeline_desc_t pipe{};
    pipe.shader = shader;
    pipe.vertexLayout = inputDesc.layout;
    render::render_pipeline_handle_t pipeline{};
    // Sampler use is explicit: missing or mismatched binding names cannot pass.
    CHECK( render::R_CreateGraphicsPipeline( pipe, &pipeline ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipe.sampledTextureName = "wrong_binding";
    CHECK( render::R_CreateGraphicsPipeline( pipe, &pipeline ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipe.sampledTextureName = "base_color";
    REQUIRE( render::R_CreateGraphicsPipeline( pipe, &pipeline ) == render::render_error_t::OK );

    // A named binding still needs the exact sampler2D type. A generic float
    // uniform or a cubemap must not be accepted by the single-2D API.
    for ( const char *unsupported : {
        "#version 410 core\nuniform samplerCube base_color; layout(location=0) out vec4 color; void main(){color=texture(base_color,vec3(1,0,0));}\n",
        "#version 410 core\nuniform float base_color; layout(location=0) out vec4 color; void main(){color=vec4(base_color);}\n" } ) {
        source.stages[1].code = { reinterpret_cast<const common::byte *>( unsupported ), std::strlen( unsupported ) + 1u };
        render::render_shader_handle_t unsupportedShader{};
        REQUIRE( render::R_CreateShader( { &source, "Unsupported sampled interface" }, &unsupportedShader ) == render::render_error_t::OK );
        auto invalidDesc = pipe;
        invalidDesc.shader = unsupportedShader;
        render::render_pipeline_handle_t invalid{};
        CHECK( render::R_CreateGraphicsPipeline( invalidDesc, &invalid ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
        REQUIRE( render::R_DestroyShader( unsupportedShader ) == render::render_error_t::OK );
    }

    common::byte pixels[16]{ 128,128,128,255, 128,128,128,255, 128,128,128,255, 128,128,128,255 };
    GLuint hostUnpack = 0u;
    glGenBuffers( 1, &hostUnpack );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, hostUnpack );
    glBufferData( GL_PIXEL_UNPACK_BUFFER, 8, nullptr, GL_STATIC_DRAW );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 8 );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 9 );
    glPixelStorei( GL_UNPACK_SKIP_ROWS, 2 );
    glPixelStorei( GL_UNPACK_SKIP_PIXELS, 3 );
    REQUIRE( glGetError() == GL_NO_ERROR );
    render::render_texture_desc_t textureDesc{ 2u, 2u, true, true, true, "sRGB smoke" };
    render::render_texture_handle_t srgb{}, linear{};
    REQUIRE( render::R_CreateTexture2D( textureDesc, { pixels, sizeof( pixels ) }, &srgb ) == render::render_error_t::OK );
    GLint captured = 0;
    glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &captured );
    CHECK( static_cast<GLuint>( captured ) == hostUnpack );
    glGetIntegerv( GL_UNPACK_ALIGNMENT, &captured ); CHECK( captured == 8 );
    glGetIntegerv( GL_UNPACK_ROW_LENGTH, &captured ); CHECK( captured == 9 );
    glGetIntegerv( GL_UNPACK_SKIP_ROWS, &captured ); CHECK( captured == 2 );
    glGetIntegerv( GL_UNPACK_SKIP_PIXELS, &captured ); CHECK( captured == 3 );
    textureDesc.sRGB = false;
    textureDesc.generateMips = false;
    textureDesc.repeat = false;
    REQUIRE( render::R_CreateTexture2D( textureDesc, { pixels, sizeof( pixels ) }, &linear ) == render::render_error_t::OK );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
    glDeleteBuffers( 1, &hostUnpack );
    glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
    glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.indexCount = draw.vertexCount = 3u;
    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    auto sampledPixel = [&]( render::render_texture_handle_t texture ) {
        REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
        draw.sampledTexture = texture;
        REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
        common::byte pixel[4]{};
        glReadBuffer( GL_BACK );
        glReadPixels( static_cast<GLint>( extent.width / 2u ), static_cast<GLint>( extent.height / 2u ),
            1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel );
        REQUIRE( glGetError() == GL_NO_ERROR );
        CHECK( pixel[0] == pixel[1] );
        CHECK( pixel[1] == pixel[2] );
        REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
        return pixel[0];
    };
    // This smoke surface is linear: sRGB decoding must be observable in pixels.
    const auto decoded = sampledPixel( srgb );
    CHECK( decoded >= 53u );
    CHECK( decoded <= 57u );
    const auto unorm = sampledPixel( linear );
    CHECK( unorm >= 126u );
    CHECK( unorm <= 130u );
    REQUIRE( render::R_DestroyTexture( srgb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( linear ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( ib ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
}

} // namespace

TEST_CASE( "OpenGL renderer links cooked shaders and verifies indexed pixels",
           "[CypherRender][OpenGL][Smoke]" )
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

    CheckCookedShaderCompilation();
    CheckIndexedDrawing( info->drawableExtent );
    CheckSampledDrawing( info->drawableExtent );

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

    const ::cypher::common::u16 indices[3]{ 0u, 1u, 2u };
    render::render_buffer_desc_t indexDescription{};
    indexDescription.byteSize = sizeof( indices );
    indexDescription.usage = render::R_BUFFER_USAGE_INDEX;
    indexDescription.updatePolicy = render::render_buffer_update_t::IMMUTABLE;
    indexDescription.memory = render::render_buffer_memory_t::DEVICE_LOCAL;
    indexDescription.debugName = "OpenGL smoke index buffer";
    const render::render_buffer_data_t indexData{ indices, sizeof( indices ) };

    render::render_buffer_handle_t indexBuffer{};
    REQUIRE( render::R_CreateBuffer(
        indexDescription,
        &indexData,
        &indexBuffer ) == render::render_error_t::OK );

    render::render_vertex_input_desc_t vertexInputDescription{};
    vertexInputDescription.layout.bindingCount = 1u;
    vertexInputDescription.layout.bindings[0].binding = 0u;
    vertexInputDescription.layout.bindings[0].stride = 16u;
    vertexInputDescription.layout.attributeCount = 2u;
    vertexInputDescription.layout.attributes[0] = {
        render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u
    };
    vertexInputDescription.layout.attributes[1] = {
        render::render_format_t::RGBA8_UNORM, 12u, 1u, 0u
    };
    vertexInputDescription.vertexBufferCount = 1u;
    vertexInputDescription.vertexBuffers[0] = { buffer, 0u, 0u };
    vertexInputDescription.indexBuffer = {
        indexBuffer,
        0u,
        render::render_index_type_t::UINT16
    };
    vertexInputDescription.debugName = "OpenGL smoke vertex input";

    REQUIRE( render::R_ValidateVertexInputDesc( vertexInputDescription ) ==
        render::render_error_t::OK );
    render::render_vertex_input_handle_t vertexInput{};
    REQUIRE( render::R_CreateVertexInput(
        vertexInputDescription,
        &vertexInput ) == render::render_error_t::OK );
    REQUIRE( render::R_IsVertexInputValid( vertexInput ) );

    render::render_vertex_input_info_t vertexInputInfo{};
    REQUIRE( render::R_GetVertexInputInfo( vertexInput, &vertexInputInfo ) ==
        render::render_error_t::OK );
    CHECK( vertexInputInfo.layout.attributeCount == 2u );
    CHECK( vertexInputInfo.vertexBuffers[0].buffer.value == buffer.value );
    CHECK( vertexInputInfo.indexBuffer.buffer.value == indexBuffer.value );

    // A VAO captures both native buffer names. Frontend references therefore
    // prevent either buffer from being destroyed while the input remains live.
    CHECK( render::R_DestroyBuffer( buffer ) ==
        render::render_error_t::ERR_RESOURCE_BUSY );
    CHECK( render::R_DestroyBuffer( indexBuffer ) ==
        render::render_error_t::ERR_RESOURCE_BUSY );

    const ::cypher::common::handle_parts64_t vertexInputParts =
        ::cypher::common::Cy_Handle64Unpack( vertexInput );
    const render::render_vertex_input_handle_t wrongVertexInputType =
        ::cypher::common::Cy_Handle64Make(
            vertexInputParts.nIndex,
            vertexInputParts.nGeneration,
            static_cast<::cypher::common::u32>( render::render_object_type_t::BUFFER ) );
    CHECK( render::R_GetVertexInputInfo(
        wrongVertexInputType,
        &vertexInputInfo ) == render::render_error_t::ERR_RESOURCE_TYPE_MISMATCH );

    REQUIRE( render::R_DestroyVertexInput( vertexInput ) ==
        render::render_error_t::OK );
    CHECK_FALSE( render::R_IsVertexInputValid( vertexInput ) );
    CHECK( render::R_GetVertexInputInfo( vertexInput, &vertexInputInfo ) ==
        render::render_error_t::ERR_STALE_HANDLE );

    const ::cypher::common::handle_parts64_t bufferParts =
        ::cypher::common::Cy_Handle64Unpack( buffer );
    const render::render_buffer_handle_t wrongTypeHandle =
        ::cypher::common::Cy_Handle64Make(
            bufferParts.nIndex,
            bufferParts.nGeneration,
            static_cast<::cypher::common::u32>( render::render_object_type_t::TEXTURE ) );
    CHECK( render::R_GetBufferInfo( wrongTypeHandle, &bufferInfo ) ==
        render::render_error_t::ERR_RESOURCE_TYPE_MISMATCH );

    REQUIRE( render::R_DestroyBuffer( indexBuffer ) == render::render_error_t::OK );
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

    render::render_vertex_input_desc_t shutdownVertexInputDescription =
        vertexInputDescription;
    shutdownVertexInputDescription.vertexBuffers[0].buffer = shutdownOwnedBuffer;
    shutdownVertexInputDescription.indexBuffer = {};
    render::render_vertex_input_handle_t shutdownOwnedVertexInput{};
    REQUIRE( render::R_CreateVertexInput(
        shutdownVertexInputDescription,
        &shutdownOwnedVertexInput ) == render::render_error_t::OK );
    REQUIRE( render::R_IsVertexInputValid( shutdownOwnedVertexInput ) );

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
