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
#include "CypherRender/CypherRender_Sampler.h"
#include "CypherRender/CypherRender_Target.h"
#include "CypherRender/CypherRender_VertexInput.h"
#include "CypherRender/OpenGL/CypherRender_OpenGL_Local.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
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
    pipelineDesc.uniformBlockCount = 1u;
    pipelineDesc.uniformBlocks[0] = { "TestDraw", sizeof( uniforms ) };
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &pipeline ) == render::render_error_t::OK );
    CHECK( render::R_DestroyShader( shader ) == render::render_error_t::ERR_RESOURCE_BUSY );

    render::render_pipeline_handle_t invalidPipeline{};
    pipelineDesc.uniformBlocks[0].byteSize += 16u;
    CHECK( render::R_CreateGraphicsPipeline( pipelineDesc, &invalidPipeline ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    CHECK_FALSE( render::R_IsGraphicsPipelineValid( invalidPipeline ) );
    pipelineDesc.uniformBlocks[0].byteSize = sizeof( uniforms );
    pipelineDesc.vertexLayout.attributes[0].location = 1u;
    CHECK( render::R_CreateGraphicsPipeline( pipelineDesc, &invalidPipeline ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipelineDesc.vertexLayout = inputDesc.layout;
    pipelineDesc.depthWrite = false;
    render::render_pipeline_handle_t readOnlyDepth{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &readOnlyDepth ) == render::render_error_t::OK );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.uniformBuffers[0] = uniformBuffer;
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
    invalidDraw.uniformBuffers[0] = {};
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

    // The compare mode is pipeline state, not a global toggle. Reuse the same
    // indexed triangle and depth attachment while changing only that mode.
    // A red prepass provides the exact same rasterized depth for equality
    // cases; strict cases compare against clears safely away from 0.5.
    const float redUniforms[20]{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 1,0,0,1 };
    const auto redUniformBuffer = createBuffer( redUniforms, sizeof( redUniforms ),
        render::R_BUFFER_USAGE_UNIFORM, true );
    constexpr auto compareCount = static_cast<std::size_t>( render::render_depth_compare_t::COUNT );
    std::array<render::render_pipeline_handle_t, compareCount> comparePipelines{};
    pipelineDesc.depthTest = true;
    pipelineDesc.depthWrite = false;
    for ( std::size_t mode = 0u; mode < comparePipelines.size(); ++mode ) {
        pipelineDesc.depthCompare = static_cast<render::render_depth_compare_t>( mode );
        REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &comparePipelines[mode] ) ==
            render::render_error_t::OK );
    }
    struct depth_case_t {
        render::render_depth_compare_t compare;
        float clearDepth;
        bool redPrepass;
        bool expectGreen;
    };
    constexpr depth_case_t cases[]{
        { render::render_depth_compare_t::NEVER,         1.00f, true,  false },
        { render::render_depth_compare_t::LESS,          0.75f, false, true  },
        { render::render_depth_compare_t::LESS,          0.25f, false, false },
        { render::render_depth_compare_t::LESS,          1.00f, true,  false },
        { render::render_depth_compare_t::EQUAL,         1.00f, true,  true  },
        { render::render_depth_compare_t::EQUAL,         0.75f, false, false },
        { render::render_depth_compare_t::LESS_EQUAL,    1.00f, true,  true  },
        { render::render_depth_compare_t::LESS_EQUAL,    0.25f, false, false },
        { render::render_depth_compare_t::GREATER,       0.25f, false, true  },
        { render::render_depth_compare_t::GREATER,       0.75f, false, false },
        { render::render_depth_compare_t::GREATER,       1.00f, true,  false },
        { render::render_depth_compare_t::NOT_EQUAL,     1.00f, true,  false },
        { render::render_depth_compare_t::NOT_EQUAL,     0.75f, false, true  },
        { render::render_depth_compare_t::GREATER_EQUAL, 1.00f, true,  true  },
        { render::render_depth_compare_t::GREATER_EQUAL, 0.75f, false, false },
        { render::render_depth_compare_t::ALWAYS,        0.25f, false, true  }
    };
    for ( const auto &depthCase : cases ) {
        CAPTURE( static_cast<int>( depthCase.compare ), depthCase.clearDepth,
                 depthCase.redPrepass, depthCase.expectGreen );
        ++frame.frameIndex;
        frame.clearDepth = depthCase.clearDepth;
        REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
        if ( depthCase.redPrepass ) {
            draw.pipeline = pipeline; // Default LESS writes the red triangle at depth 0.5.
            draw.uniformBuffers[0] = redUniformBuffer;
            REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
        }
        draw.pipeline = comparePipelines[static_cast<std::size_t>( depthCase.compare )];
        draw.uniformBuffers[0] = uniformBuffer; // Green triangle at the same depth.
        REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
        common::u8 pixel[4]{};
        glReadBuffer( GL_BACK );
        glReadPixels( static_cast<GLint>( extent.width / 2u ),
                      static_cast<GLint>( extent.height / 2u ),
                      1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel );
        REQUIRE( glGetError() == GL_NO_ERROR );
        if ( depthCase.expectGreen ) {
            CHECK( pixel[0] <= 15u );
            CHECK( pixel[1] >= 240u );
            CHECK( pixel[2] <= 15u );
        } else if ( depthCase.redPrepass ) {
            CHECK( pixel[0] >= 240u );
            CHECK( pixel[1] <= 15u );
            CHECK( pixel[2] <= 15u );
        } else {
            CHECK( pixel[0] <= 15u );
            CHECK( pixel[1] <= 15u );
            CHECK( pixel[2] >= 240u );
        }
        REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    }
    for ( const auto comparePipeline : comparePipelines ) {
        REQUIRE( render::R_DestroyGraphicsPipeline( comparePipeline ) ==
            render::render_error_t::OK );
    }

    // A depth-only pass must leave the clear color intact while still writing
    // depth. Farther LESS geometry then fails, and a same-depth EQUAL color
    // pass proves pipeline switching restores color writes.
    pipelineDesc.depthCompare = render::render_depth_compare_t::LESS;
    pipelineDesc.depthWrite = true;
    pipelineDesc.colorWrite = false;
    render::render_pipeline_handle_t depthOnly{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &depthOnly ) ==
        render::render_error_t::OK );
    pipelineDesc.depthCompare = render::render_depth_compare_t::EQUAL;
    pipelineDesc.depthWrite = false;
    pipelineDesc.colorWrite = true;
    render::render_pipeline_handle_t colorAtEqualDepth{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &colorAtEqualDepth ) ==
        render::render_error_t::OK );
    const float farGreenUniforms[20]{
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0.5f,1, 0,1,0,1
    };
    const auto farGreenBuffer = createBuffer( farGreenUniforms, sizeof( farGreenUniforms ),
        render::R_BUFFER_USAGE_UNIFORM, true );
    ++frame.frameIndex;
    frame.clearDepth = 1.0f;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.pipeline = depthOnly;
    draw.uniformBuffers[0] = redUniformBuffer;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ),
               static_cast<GLint>( extent.height / 2u ), 2 );
    draw.pipeline = pipeline;
    draw.uniformBuffers[0] = farGreenBuffer;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ),
               static_cast<GLint>( extent.height / 2u ), 2 );
    draw.pipeline = colorAtEqualDepth;
    draw.uniformBuffers[0] = redUniformBuffer;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ),
               static_cast<GLint>( extent.height / 2u ), 0 );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( depthOnly ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( colorAtEqualDepth ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( farGreenBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( redUniformBuffer ) == render::render_error_t::OK );

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

void CheckLineDrawing( const render::render_extent_t extent )
{
    constexpr char vertex[] =
        "#version 410 core\n"
        "layout(location=0) in vec3 position;\n"
        "void main() { gl_Position=vec4(position,1.0); }\n";
    constexpr char fragment[] =
        "#version 410 core\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color=vec4(0.0,1.0,0.0,1.0); }\n";
    common::cooked_shader_view_t source{};
    source.nLanguageVersion = 410u;
    source.nStages = 2u;
    source.stages[0].stage = common::render_shader_stage_t::VERTEX;
    source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
    source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), sizeof( fragment ) };
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &source, "Line pixel smoke" }, &shader ) == render::render_error_t::OK );

    // Two one-pixel lines cross the center. Checking a small neighborhood avoids
    // depending on the driver's half-pixel tie breaking at the viewport center.
    const float positions[]{ -0.8f,0.0f,0.0f, 0.8f,0.0f,0.0f,
                              0.0f,-0.8f,0.0f, 0.0f,0.8f,0.0f };
    const common::u16 indices[]{ 0u,1u,2u,3u };
    const auto makeBuffer = []( const void *bytes, const common::u64 size,
                                const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( desc, &data, &result ) == render::render_error_t::OK );
        return result;
    };
    const auto vertexBuffer = makeBuffer( positions, sizeof( positions ), render::R_BUFFER_USAGE_VERTEX );
    const auto indexBuffer = makeBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
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
    pipelineDesc.topology = render::render_primitive_topology_t::LINE_LIST;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &pipeline ) == render::render_error_t::OK );

    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    frame.clearColor = { 0.0f,0.0f,1.0f,1.0f };
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.indexCount = 4u;
    draw.vertexCount = 4u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    std::array<common::u8, 5u * 5u * 4u> pixels{};
    glReadBuffer( GL_BACK );
    glReadPixels( static_cast<GLint>( extent.width / 2u ) - 2,
                  static_cast<GLint>( extent.height / 2u ) - 2,
                  5, 5, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
    REQUIRE( glGetError() == GL_NO_ERROR );
    bool foundLinePixel = false;
    for ( common::usize i = 0u; i < pixels.size(); i += 4u ) {
        if ( pixels[i] <= 40u && pixels[i + 1u] >= 200u && pixels[i + 2u] <= 40u ) {
            foundLinePixel = true;
            break;
        }
    }
    CHECK( foundLinePixel );
    std::array<common::u8, 4u> corner{};
    glReadPixels( 1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner.data() );
    REQUIRE( glGetError() == GL_NO_ERROR );
    CHECK( corner[0] <= 15u );
    CHECK( corner[1] <= 15u );
    CHECK( corner[2] >= 240u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vertexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( indexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
}

void CheckInstancedDrawing( const render::render_extent_t extent )
{
    constexpr char vertex[] =
        "#version 410 core\n"
        "layout(location=0) in vec3 position;\n"
        "layout(location=1) in vec2 translation;\n"
        "layout(location=2) in vec3 instanceColor;\n"
        "out vec3 vertexColor;\n"
        "void main() { gl_Position=vec4(position.xy+translation,position.z,1.0); vertexColor=instanceColor; }\n";
    constexpr char fragment[] =
        "#version 410 core\n"
        "in vec3 vertexColor; layout(location=0) out vec4 color;\n"
        "void main() { color=vec4(vertexColor,1.0); }\n";
    common::cooked_shader_view_t source{};
    source.nLanguageVersion = 410u;
    source.nStages = 2u;
    source.stages[0].stage = common::render_shader_stage_t::VERTEX;
    source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
    source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), sizeof( fragment ) };
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &source, "Instanced pixel smoke" }, &shader ) == render::render_error_t::OK );

    const float positions[]{ -0.18f,-0.22f,0.0f, 0.18f,-0.22f,0.0f, 0.0f,0.22f,0.0f };
    const float instances[]{ -0.5f,0.0f, 1.0f,0.0f,0.0f,
                              0.5f,0.0f, 0.0f,1.0f,0.0f };
    const common::u16 indices[]{ 0u, 1u, 2u };
    const auto makeBuffer = []( const void *bytes, const common::u64 size,
                                const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( desc, &data, &result ) == render::render_error_t::OK );
        return result;
    };
    const auto positionBuffer = makeBuffer( positions, sizeof( positions ), render::R_BUFFER_USAGE_VERTEX );
    const auto instanceBuffer = makeBuffer( instances, sizeof( instances ), render::R_BUFFER_USAGE_VERTEX );
    const auto indexBuffer = makeBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout.bindingCount = 2u;
    inputDesc.layout.bindings[0].stride = 3u * sizeof( float );
    inputDesc.layout.bindings[1].binding = 1u;
    inputDesc.layout.bindings[1].stride = 5u * sizeof( float );
    inputDesc.layout.bindings[1].inputRate = render::render_vertex_input_rate_t::PER_INSTANCE;
    inputDesc.layout.bindings[1].instanceDivisor = 1u;
    inputDesc.layout.attributeCount = 3u;
    inputDesc.layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDesc.layout.attributes[1] = { render::render_format_t::RG32_FLOAT, 0u, 1u, 1u };
    inputDesc.layout.attributes[2] = { render::render_format_t::RGB32_FLOAT, 2u * sizeof( float ), 2u, 1u };
    inputDesc.vertexBufferCount = 2u;
    inputDesc.vertexBuffers[0] = { positionBuffer, 0u, 0u };
    inputDesc.vertexBuffers[1] = { instanceBuffer, 0u, 1u };
    inputDesc.indexBuffer = { indexBuffer, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDesc, &input ) == render::render_error_t::OK );
    render::render_pipeline_desc_t pipelineDesc{};
    pipelineDesc.shader = shader;
    pipelineDesc.vertexLayout = inputDesc.layout;
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &pipeline ) == render::render_error_t::OK );

    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    frame.clearColor = { 0.0f, 0.0f, 1.0f, 1.0f };
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.indexCount = draw.vertexCount = 3u;
    const auto readPixel = [&]( const GLint x ) {
        std::array<common::u8, 4u> pixel{};
        glReadBuffer( GL_BACK );
        glReadPixels( x, static_cast<GLint>( extent.height / 2u ), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data() );
        REQUIRE( glGetError() == GL_NO_ERROR );
        return pixel;
    };
    const GLint left = static_cast<GLint>( extent.width / 4u );
    const GLint right = static_cast<GLint>( 3u * extent.width / 4u );
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.instanceCount = 1u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto firstLeft = readPixel( left );
    const auto firstRight = readPixel( right );
    CHECK( firstLeft[0] >= 240u );
    CHECK( firstLeft[1] <= 15u );
    CHECK( firstRight[2] >= 240u ); // Instance two has not been drawn.
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    ++frame.frameIndex;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.instanceCount = 2u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto secondLeft = readPixel( left );
    const auto secondRight = readPixel( right );
    CHECK( secondLeft[0] >= 240u );
    CHECK( secondRight[1] >= 240u );
    CHECK( secondRight[0] <= 15u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( positionBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( instanceBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( indexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
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
    pipe.sampledTextureCount = 1u;
    pipe.sampledTextures[0].name = "wrong_binding";
    CHECK( render::R_CreateGraphicsPipeline( pipe, &pipeline ) == render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipe.sampledTextures[0].name = "base_color";
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
        draw.sampledTextures[0] = texture;
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

void CheckPerSlotSamplerPolicy( const render::render_extent_t extent )
{
    constexpr char vertex[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "void main() { gl_Position=vec4(position,1.0); }\n";
    constexpr char fragment[] =
        "#version 410 core\n"
        "uniform sampler2D base_color; uniform sampler2D detail_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color=vec4(texture(base_color,vec2(0.4,0.5)).r,"
        "texture(detail_color,vec2(0.4,0.5)).b,0.0,1.0); }\n";
    common::cooked_shader_view_t source{};
    source.nLanguageVersion = 410u;
    source.nStages = 2u;
    source.stages[0].stage = common::render_shader_stage_t::VERTEX;
    source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
    source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), sizeof( fragment ) };
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &source, "Per-slot sampler pixel smoke" }, &shader ) ==
        render::render_error_t::OK );

    const float positions[]{ -1.0f,-1.0f,0.0f, 3.0f,-1.0f,0.0f, -1.0f,3.0f,0.0f };
    const common::u16 indices[]{ 0u,1u,2u };
    const auto buffer = []( const void *data, common::u64 size,
                            render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t contents{ data, size };
        render::render_buffer_handle_t out{};
        REQUIRE( render::R_CreateBuffer( desc, &contents, &out ) ==
            render::render_error_t::OK );
        return out;
    };
    const auto vb = buffer( positions, sizeof( positions ), render::R_BUFFER_USAGE_VERTEX );
    const auto ib = buffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout.bindingCount = 1u;
    inputDesc.layout.bindings[0].stride = sizeof( float ) * 3u;
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
    pipe.sampledTextureCount = 2u;
    pipe.sampledTextures[0].name = "base_color";
    pipe.sampledTextures[1].name = "detail_color";
    pipe.depthTest = false;
    pipe.depthWrite = false;
    pipe.cullBackFaces = false;
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipe, &pipeline ) == render::render_error_t::OK );

    // Both slots sample the SAME 2x1 image. Only sampler policy can explain
    // different colors after swapping nearest and linear between the slots.
    const common::u8 pixels[]{ 255u,0u,0u,255u, 0u,0u,255u,255u };
    render::render_texture_desc_t textureDesc{ 2u, 1u, false, false, false, "Sampler smoke image" };
    render::render_texture_handle_t texture{};
    REQUIRE( render::R_CreateTexture2D( textureDesc, { pixels, sizeof( pixels ) }, &texture ) ==
        render::render_error_t::OK );
    render::render_sampler_desc_t samplerDesc{};
    samplerDesc.minFilter = samplerDesc.magFilter = render::render_sampler_filter_t::NEAREST;
    samplerDesc.mipFilter = render::render_sampler_mip_filter_t::NONE;
    samplerDesc.addressU = samplerDesc.addressV = render::render_sampler_address_t::CLAMP_TO_EDGE;
    render::render_sampler_handle_t nearest{}, linear{};
    REQUIRE( render::R_CreateSampler( samplerDesc, &nearest ) == render::render_error_t::OK );
    samplerDesc.minFilter = samplerDesc.magFilter = render::render_sampler_filter_t::LINEAR;
    REQUIRE( render::R_CreateSampler( samplerDesc, &linear ) == render::render_error_t::OK );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.indexCount = draw.vertexCount = 3u;
    draw.sampledTextures[0] = texture;
    draw.sampledTextures[1] = texture;
    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    const auto sample = [&]( render::render_sampler_handle_t slot0,
                             render::render_sampler_handle_t slot1 ) {
        draw.samplers[0] = slot0;
        draw.samplers[1] = slot1;
        REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
        REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
        common::u8 pixel[4]{};
        glReadBuffer( GL_BACK );
        glReadPixels( static_cast<GLint>( extent.width / 2u ),
            static_cast<GLint>( extent.height / 2u ), 1, 1,
            GL_RGBA, GL_UNSIGNED_BYTE, pixel );
        REQUIRE( glGetError() == GL_NO_ERROR );
        REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
        return std::array<common::u8, 4u>{ pixel[0], pixel[1], pixel[2], pixel[3] };
    };
    const auto nearestRedLinearBlue = sample( nearest, linear );
    CHECK( nearestRedLinearBlue[0] >= 245u );
    CHECK( nearestRedLinearBlue[1] >= 60u );
    CHECK( nearestRedLinearBlue[1] <= 95u );
    const auto linearRedNearestBlue = sample( linear, nearest );
    CHECK( linearRedNearestBlue[0] >= 160u );
    CHECK( linearRedNearestBlue[0] <= 195u );
    CHECK( linearRedNearestBlue[1] <= 10u );
    CHECK( nearestRedLinearBlue[2] <= 5u );
    CHECK( linearRedNearestBlue[2] <= 5u );

    REQUIRE( render::R_DestroySampler( linear ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroySampler( nearest ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( texture ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( ib ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
}

void CheckTwoTextureTwoBlockDrawing( const render::render_extent_t extent )
{
    constexpr char vertex[] =
        "#version 410 core\n"
        "layout(location=0) in vec3 position;\n"
        "layout(std140) uniform Offset { vec4 offset; };\n"
        "void main() { gl_Position=vec4(position.xy+offset.xy,position.z,1.0); }\n";
    constexpr char fragment[] =
        "#version 410 core\n"
        "layout(std140) uniform MixWeights { vec4 weights; };\n"
        "uniform sampler2D base_color; uniform sampler2D detail_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color=texture(base_color,vec2(0.5))*weights.x"
        "+texture(detail_color,vec2(0.5))*weights.y; }\n";
    common::cooked_shader_view_t source{};
    source.nLanguageVersion = 410u;
    source.nStages = 2u;
    source.stages[0].stage = common::render_shader_stage_t::VERTEX;
    source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
    source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), sizeof( fragment ) };
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &source, "Two-slot pixel smoke" }, &shader ) == render::render_error_t::OK );

    const float positions[]{ -0.9f,-0.9f,0.0f, 0.9f,-0.9f,0.0f, 0.0f,0.9f,0.0f };
    const common::u16 indices[]{ 0u,1u,2u };
    const float offset[]{ 0.0f,0.0f,0.0f,0.0f };
    const float weights[]{ 0.25f,0.75f,0.0f,0.0f };
    const auto createBuffer = []( const void *data, common::u64 size,
                                  render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t contents{ data, size };
        render::render_buffer_handle_t buffer{};
        REQUIRE( render::R_CreateBuffer( desc, &contents, &buffer ) == render::render_error_t::OK );
        return buffer;
    };
    const auto vb = createBuffer( positions, sizeof( positions ), render::R_BUFFER_USAGE_VERTEX );
    const auto ib = createBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    const auto offsetBuffer = createBuffer( offset, sizeof( offset ), render::R_BUFFER_USAGE_UNIFORM );
    const auto weightBuffer = createBuffer( weights, sizeof( weights ), render::R_BUFFER_USAGE_UNIFORM );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout.bindingCount = 1u;
    inputDesc.layout.bindings[0].stride = 3u * sizeof( float );
    inputDesc.layout.attributeCount = 1u;
    inputDesc.layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDesc.vertexBufferCount = 1u;
    inputDesc.vertexBuffers[0] = { vb, 0u, 0u };
    inputDesc.indexBuffer = { ib, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDesc, &input ) == render::render_error_t::OK );

    render::render_pipeline_desc_t pipelineDesc{};
    pipelineDesc.shader = shader;
    pipelineDesc.vertexLayout = inputDesc.layout;
    pipelineDesc.uniformBlockCount = 2u;
    pipelineDesc.uniformBlocks[0] = { "Offset", sizeof( offset ) };
    pipelineDesc.uniformBlocks[1] = { "MixWeights", sizeof( weights ) };
    pipelineDesc.sampledTextureCount = 2u;
    pipelineDesc.sampledTextures[0].name = "base_color";
    pipelineDesc.sampledTextures[1].name = "detail_color";
    render::render_pipeline_handle_t failed{};
    auto invalid = pipelineDesc;
    invalid.sampledTextureCount = 1u;
    invalid.sampledTextures[1] = {};
    CHECK( render::R_CreateGraphicsPipeline( invalid, &failed ) ==
        render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    invalid = pipelineDesc;
    invalid.uniformBlockCount = 1u;
    invalid.uniformBlocks[1] = {};
    CHECK( render::R_CreateGraphicsPipeline( invalid, &failed ) ==
        render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    invalid = pipelineDesc;
    invalid.uniformBlocks[1].byteSize += 16u;
    CHECK( render::R_CreateGraphicsPipeline( invalid, &failed ) ==
        render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &pipeline ) == render::render_error_t::OK );

    const common::u8 redPixels[16]{ 255,0,0,255, 255,0,0,255, 255,0,0,255, 255,0,0,255 };
    const common::u8 greenPixels[16]{ 0,255,0,255, 0,255,0,255, 0,255,0,255, 0,255,0,255 };
    render::render_texture_desc_t textureDesc{ 2u, 2u, false, false, false, "Two-slot smoke" };
    render::render_texture_handle_t red{}, green{};
    REQUIRE( render::R_CreateTexture2D( textureDesc, { redPixels, sizeof( redPixels ) }, &red ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateTexture2D( textureDesc, { greenPixels, sizeof( greenPixels ) }, &green ) ==
        render::render_error_t::OK );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.uniformBuffers[0] = offsetBuffer;
    draw.uniformBuffers[1] = weightBuffer;
    draw.sampledTextures[0] = red;
    draw.sampledTextures[1] = green;
    draw.indexCount = draw.vertexCount = 3u;
    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    common::u8 pixel[4]{};
    glReadBuffer( GL_BACK );
    glReadPixels( static_cast<GLint>( extent.width / 2u ), static_cast<GLint>( extent.height / 2u ),
        1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel );
    REQUIRE( glGetError() == GL_NO_ERROR );
    CHECK( pixel[0] >= 62u );
    CHECK( pixel[0] <= 66u );
    CHECK( pixel[1] >= 189u );
    CHECK( pixel[1] <= 193u );
    CHECK( pixel[2] <= 2u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyTexture( green ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( red ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( weightBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( offsetBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( ib ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
}

void CheckOffscreenTarget( const render::render_extent_t extent )
{
    constexpr char vertex[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position=vec4(position,1.0); uv=position.xy*0.5+0.5; }\n";
    constexpr char solidFragment[] =
        "#version 410 core\nlayout(location=0) out vec4 color;\n"
        "void main() { color=vec4(1.0,0.0,0.0,1.0); }\n";
    constexpr char sampleFragment[] =
        "#version 410 core\nin vec2 uv; uniform sampler2D base_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color=texture(base_color,uv); }\n";
    const auto makeShader = [&]( const char *fragment, const common::u64 bytes ) {
        common::cooked_shader_view_t source{};
        source.nLanguageVersion = 410u;
        source.nStages = 2u;
        source.stages[0].stage = common::render_shader_stage_t::VERTEX;
        source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
        source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), bytes };
        render::render_shader_handle_t shader{};
        REQUIRE( render::R_CreateShader( { &source, "Offscreen target smoke" }, &shader ) == render::render_error_t::OK );
        return shader;
    };
    const auto solidShader = makeShader( solidFragment, sizeof( solidFragment ) );
    const auto sampleShader = makeShader( sampleFragment, sizeof( sampleFragment ) );

    // The first triangle is drawn to the target; the second covers presentation.
    const float vertices[]{
        -0.8f,-0.8f,0.0f, 0.8f,-0.8f,0.0f, 0.0f,0.8f,0.0f,
        -1.0f,-1.0f,0.0f, 3.0f,-1.0f,0.0f, -1.0f,3.0f,0.0f
    };
    const common::u16 indices[]{ 0u,1u,2u,3u,4u,5u };
    const auto makeBuffer = []( const void *bytes, const common::u64 size,
                                const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( desc, &data, &result ) == render::render_error_t::OK );
        return result;
    };
    const auto vb = makeBuffer( vertices, sizeof( vertices ), render::R_BUFFER_USAGE_VERTEX );
    const auto ib = makeBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout.bindingCount = 1u;
    inputDesc.layout.bindings[0].stride = 3u * sizeof( float );
    inputDesc.layout.attributeCount = 1u;
    inputDesc.layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDesc.vertexBufferCount = 1u;
    inputDesc.vertexBuffers[0] = { vb, 0u, 0u };
    inputDesc.indexBuffer = { ib, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDesc, &input ) == render::render_error_t::OK );

    render::render_pipeline_desc_t pipelineDesc{};
    pipelineDesc.shader = solidShader;
    pipelineDesc.vertexLayout = inputDesc.layout;
    render::render_pipeline_handle_t solidPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &solidPipeline ) == render::render_error_t::OK );
    pipelineDesc.shader = sampleShader;
    pipelineDesc.sampledTextureCount = 1u;
    pipelineDesc.sampledTextures[0].name = "base_color";
    render::render_pipeline_handle_t samplePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &samplePipeline ) == render::render_error_t::OK );

    render::render_texture_desc_t textureDesc{};
    textureDesc.width = extent.width;
    textureDesc.height = extent.height;
    textureDesc.sRGB = false;
    textureDesc.generateMips = false;
    textureDesc.repeat = false;
    textureDesc.renderTarget = true;
    render::render_texture_handle_t color{};
    REQUIRE( render::R_CreateTexture2D( textureDesc, {}, &color ) == render::render_error_t::OK );
    render::render_target_handle_t target{};
    REQUIRE( render::R_CreateRenderTarget( { color }, &target ) == render::render_error_t::OK );
    CHECK( render::R_DestroyTexture( color ) == render::render_error_t::ERR_RESOURCE_BUSY );

    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    frame.clearColor = { 0.0f, 0.0f, 1.0f, 1.0f };
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    render::render_target_pass_desc_t pass{};
    pass.target = target;
    pass.clearColor = { 0.0f, 1.0f, 0.0f, 1.0f };
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = solidPipeline;
    draw.vertexInput = input;
    draw.indexCount = 3u;
    draw.vertexCount = 6u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    draw.pipeline = samplePipeline;
    draw.sampledTextures[0] = color;
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( render::R_EndFrame() == render::render_error_t::ERR_COMMAND_STATE_INVALID );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );

    const auto readPresentation = [&]( const GLint x, const GLint y ) {
        std::array<common::u8, 4u> pixel{};
        glReadBuffer( GL_BACK );
        glReadPixels( x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data() );
        REQUIRE( glGetError() == GL_NO_ERROR );
        return pixel;
    };
    const GLint centerX = static_cast<GLint>( extent.width / 2u );
    const GLint centerY = static_cast<GLint>( extent.height / 2u );
    const auto beforeSampling = readPresentation( centerX, centerY );
    CHECK( beforeSampling[2] >= 240u ); // Target draw did not touch presentation.
    draw.firstIndex = 3u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto center = readPresentation( centerX, centerY );
    const auto corner = readPresentation( 1, 1 );
    CHECK( center[0] >= 240u );
    CHECK( center[1] <= 15u );
    CHECK( corner[1] >= 240u );
    CHECK( corner[0] <= 15u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyRenderTarget( target ) == render::render_error_t::OK );
    CHECK( render::R_IsTextureValid( color ) );
    REQUIRE( render::R_DestroyTexture( color ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( solidPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( samplePipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( ib ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( solidShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( sampleShader ) == render::render_error_t::OK );
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
    CheckLineDrawing( info->drawableExtent );
    CheckInstancedDrawing( info->drawableExtent );
    CheckSampledDrawing( info->drawableExtent );
    CheckPerSlotSamplerPolicy( info->drawableExtent );
    CheckTwoTextureTwoBlockDrawing( info->drawableExtent );
    CheckOffscreenTarget( info->drawableExtent );

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
