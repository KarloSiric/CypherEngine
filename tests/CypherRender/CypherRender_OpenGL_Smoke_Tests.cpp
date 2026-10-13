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
#include "CypherRender/Runtime/CypherRender_MaterialBridge.h"
#include "CypherRender/Runtime/CypherRender_MeshBridge.h"
#include "CypherRender/Runtime/CypherRender_UniformBatch.h"
#include "CypherCommon/Formats/CypherCommon_CookedTexture.h"
#include "CypherResource/CypherResource_RenderAssets.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <cstring>
#include <string>
#include <vector>

namespace render = ::cypher::engine::render;
namespace resource = ::cypher::engine::resource;
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

    // The preceding FILL draw covers the center. Reuse the exact geometry and
    // shader with LINE mode, then switch back to FILL in the same frame. A
    // pixel near the bottom edge proves LINE actually rasterized geometry;
    // the center must remain the blue clear color until FILL is restored.
    pipelineDesc.polygonMode = render::render_polygon_mode_t::LINE;
    render::render_pipeline_handle_t wireframe{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &wireframe ) ==
        render::render_error_t::OK );
    render::render_pipeline_info_t wireframeInfo{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( wireframe, &wireframeInfo ) ==
        render::render_error_t::OK );
    CHECK( wireframeInfo.polygonMode == render::render_polygon_mode_t::LINE );
    pipelineDesc.polygonMode = render::render_polygon_mode_t::FILL;

    ++frame.frameIndex;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.pipeline = wireframe;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ),
               static_cast<GLint>( extent.height / 2u ), 2 );
    std::array<common::u8, 5u * 5u * 4u> edgePixels{};
    glReadBuffer( GL_BACK );
    glReadPixels( static_cast<GLint>( extent.width / 2u ) - 2,
                  static_cast<GLint>( extent.height / 10u ) - 2,
                  5, 5, GL_RGBA, GL_UNSIGNED_BYTE, edgePixels.data() );
    REQUIRE( glGetError() == GL_NO_ERROR );
    bool edgeWasDrawn = false;
    for ( std::size_t i = 0u; i < edgePixels.size(); i += 4u ) {
        if ( edgePixels[i] <= 15u && edgePixels[i + 1u] >= 240u &&
             edgePixels[i + 2u] <= 15u ) {
            edgeWasDrawn = true;
            break;
        }
    }
    CHECK( edgeWasDrawn );
    draw.pipeline = pipeline;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ),
               static_cast<GLint>( extent.height / 2u ), 1 );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( wireframe ) == render::render_error_t::OK );

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

    // The same triangle straddles the center line. Clip its red draw to the
    // right half, then draw green without a scissor in the same frame. The
    // left pixel changing only on the second draw proves the clip is per draw.
    ++frame.frameIndex;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.pipeline = readOnlyDepth;
    draw.uniformBuffers[0] = redUniformBuffer;
    draw.scissor = { true, extent.width / 2u, 0u,
                     extent.width / 2u, extent.height };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ) - 8,
               static_cast<GLint>( extent.height / 2u ), 2 );
    readPixel( static_cast<GLint>( extent.width / 2u ) + 8,
               static_cast<GLint>( extent.height / 2u ), 0 );
    draw.uniformBuffers[0] = uniformBuffer;
    draw.scissor = {};
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 2u ) - 8,
               static_cast<GLint>( extent.height / 2u ), 1 );
    readPixel( static_cast<GLint>( extent.width / 2u ) + 8,
               static_cast<GLint>( extent.height / 2u ), 1 );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    // The same triangle should move with the viewport transform, without
    // clipping or changing its vertices. A following ordinary draw must use
    // the full drawable again, rather than inheriting the previous viewport.
    ++frame.frameIndex;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.pipeline = readOnlyDepth;
    draw.uniformBuffers[0] = redUniformBuffer;
    draw.viewport = { true, 0u, 0u, extent.width / 2u, extent.height };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    GLint activeViewport[4]{};
    glGetIntegerv( GL_VIEWPORT, activeViewport );
    CHECK( activeViewport[0] == 0 );
    CHECK( activeViewport[1] == 0 );
    CHECK( activeViewport[2] == static_cast<GLint>( extent.width / 2u ) );
    CHECK( activeViewport[3] == static_cast<GLint>( extent.height ) );
    readPixel( static_cast<GLint>( extent.width / 4u ),
               static_cast<GLint>( extent.height / 2u ), 0 );
    readPixel( static_cast<GLint>( extent.width / 2u ),
               static_cast<GLint>( extent.height / 2u ), 2 );
    draw.viewport = {};
    draw.uniformBuffers[0] = uniformBuffer;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    glGetIntegerv( GL_VIEWPORT, activeViewport );
    CHECK( activeViewport[0] == 0 );
    CHECK( activeViewport[1] == 0 );
    CHECK( activeViewport[2] == static_cast<GLint>( extent.width ) );
    CHECK( activeViewport[3] == static_cast<GLint>( extent.height ) );
    readPixel( static_cast<GLint>( extent.width / 2u ),
               static_cast<GLint>( extent.height / 2u ), 1 );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    // One upload holds two object records. The draw selects each record by
    // an aligned UBO range and maps the same geometry into opposite viewports.
    const render::render_info_t *deviceInfo = render::R_GetInfo();
    REQUIRE( deviceInfo != nullptr );
    const common::u64 alignment = deviceInfo->limits.uniformBufferOffsetAlignment;
    REQUIRE( alignment > 0u );
    const common::u64 recordStride =
        ( sizeof( redUniforms ) + alignment - 1u ) / alignment * alignment;
    render::render_uniform_batch_t uniformBatch{};
    REQUIRE( render::R_CreateUniformBatch(
        recordStride + sizeof( uniforms ), nullptr, &uniformBatch ) ==
        render::render_error_t::OK );
    render::render_uniform_record_t redRecord{};
    render::render_uniform_record_t greenRecord{};
    REQUIRE( render::R_AppendUniformRecord(
        &uniformBatch, redUniforms, sizeof( redUniforms ), &redRecord ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_AppendUniformRecord(
        &uniformBatch, uniforms, sizeof( uniforms ), &greenRecord ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CommitUniformBatch( &uniformBatch ) ==
        render::render_error_t::OK );
    ++frame.frameIndex;
    frame.clearDepth = 1.0f;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.pipeline = pipeline;
    REQUIRE( render::R_ApplyUniformRecordToDraw(
        uniformBatch, redRecord, 0u, &draw ) == render::render_error_t::OK );
    draw.viewport = { true, 0u, 0u, extent.width / 2u, extent.height };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_ApplyUniformRecordToDraw(
        uniformBatch, greenRecord, 0u, &draw ) == render::render_error_t::OK );
    draw.viewport.x = extent.width / 2u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    readPixel( static_cast<GLint>( extent.width / 4u ),
               static_cast<GLint>( extent.height / 2u ), 0 );
    readPixel( static_cast<GLint>( extent.width * 3u / 4u ),
               static_cast<GLint>( extent.height / 2u ), 1 );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    REQUIRE( render::R_DestroyUniformBatch( &uniformBatch ) ==
        render::render_error_t::OK );

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
    // The authored second level is green while the base level is red. Sampling
    // LOD 1 below proves the backend uploaded authored data instead of calling
    // glGenerateMipmap on the base image.
    const common::byte authoredPixels[20]{
        255u,0u,0u,255u, 255u,0u,0u,255u,
        255u,0u,0u,255u, 255u,0u,0u,255u,
        0u,255u,0u,255u
    };
    common::cooked_texture_desc_t cookedDesc{};
    cookedDesc.pixelFormat = common::render_texture_pixel_format_t::RGBA8_UNORM;
    cookedDesc.storageFormat = common::render_format_t::RGBA8_UNORM;
    cookedDesc.colorSpace = common::render_texture_color_space_t::LINEAR;
    cookedDesc.nWidth = cookedDesc.nHeight = 2u;
    cookedDesc.nMipLevels = 2u;
    const common::cooked_texture_mip_source_t authoredMips[2]{
        { 2u, 2u, 1u, 8u, { authoredPixels, 16u } },
        { 1u, 1u, 1u, 4u, { authoredPixels + 16u, 4u } }
    };
    const auto cookedSource = common::span_t<const common::cooked_texture_mip_source_t>{
        authoredMips, 2u };
    const auto cookedSize = common::CookedTexture_RequiredSize( cookedDesc, cookedSource );
    REQUIRE( cookedSize > 0u );
    std::vector<common::byte> cookedFile( cookedSize );
    REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Write(
        cookedDesc, cookedSource, {}, { cookedFile.data(), cookedFile.size() } ) ) );
    common::cooked_texture_view_t cookedView{};
    REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Read(
        { cookedFile.data(), cookedFile.size() }, &cookedView ) ) );
    render::render_texture_handle_t authored{};
    REQUIRE( render::R_CreateTexture2DFromCooked( cookedView, &authored ) ==
        render::render_error_t::OK );
    render::render_texture_info_t authoredInfo{};
    REQUIRE( render::R_GetTextureInfo( authored, &authoredInfo ) == render::render_error_t::OK );
    CHECK( authoredInfo.mipLevels == 2u );
    CHECK( authoredInfo.completeMipChain );
    CHECK_FALSE( authoredInfo.generateMips );
    glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &captured );
    CHECK( static_cast<GLuint>( captured ) == hostUnpack );
    glGetIntegerv( GL_UNPACK_ALIGNMENT, &captured ); CHECK( captured == 8 );
    glGetIntegerv( GL_UNPACK_ROW_LENGTH, &captured ); CHECK( captured == 9 );
    glGetIntegerv( GL_UNPACK_SKIP_ROWS, &captured ); CHECK( captured == 2 );
    glGetIntegerv( GL_UNPACK_SKIP_PIXELS, &captured ); CHECK( captured == 3 );
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
    constexpr char mipFragment[] =
        "#version 410 core\nuniform sampler2D base_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color=textureLod(base_color,vec2(0.5),1.0); }\n";
    source.stages[1].code = { reinterpret_cast<const common::byte *>( mipFragment ), sizeof( mipFragment ) };
    render::render_shader_handle_t mipShader{};
    REQUIRE( render::R_CreateShader( { &source, "Authored mip smoke" }, &mipShader ) ==
        render::render_error_t::OK );
    auto mipPipe = pipe;
    mipPipe.shader = mipShader;
    render::render_pipeline_handle_t mipPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( mipPipe, &mipPipeline ) == render::render_error_t::OK );
    render::render_sampler_desc_t mipSamplerDesc{};
    mipSamplerDesc.minFilter = mipSamplerDesc.magFilter = render::render_sampler_filter_t::NEAREST;
    mipSamplerDesc.mipFilter = render::render_sampler_mip_filter_t::NEAREST;
    render::render_sampler_handle_t mipSampler{};
    REQUIRE( render::R_CreateSampler( mipSamplerDesc, &mipSampler ) == render::render_error_t::OK );
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    draw.pipeline = mipPipeline;
    draw.sampledTextures[0] = authored;
    draw.samplers[0] = mipSampler;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    common::byte mipPixel[4]{};
    glReadBuffer( GL_BACK );
    glReadPixels( static_cast<GLint>( extent.width / 2u ), static_cast<GLint>( extent.height / 2u ),
        1, 1, GL_RGBA, GL_UNSIGNED_BYTE, mipPixel );
    REQUIRE( glGetError() == GL_NO_ERROR );
    CHECK( mipPixel[0] <= 2u );
    CHECK( mipPixel[1] >= 253u );
    CHECK( mipPixel[2] <= 2u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    REQUIRE( render::R_DestroySampler( mipSampler ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( mipPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( mipShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( authored ) == render::render_error_t::OK );
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
    constexpr char depthSampleFragment[] =
        "#version 410 core\nin vec2 uv; uniform sampler2D base_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { float d=texture(base_color,uv).r; color=vec4(d,d,d,1.0); }\n";
    constexpr char shadowSampleFragment[] =
        "#version 410 core\nin vec2 uv; uniform sampler2DShadow shadow_map;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { float low=texture(shadow_map,vec3(uv,0.25));"
        " float high=texture(shadow_map,vec3(uv,0.75)); color=vec4(low,high,0.0,1.0); }\n";
    constexpr char inactiveShadowFragment[] =
        "#version 410 core\nuniform sampler2DShadow shadow_map;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color=vec4(1.0); }\n";
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
    const auto depthSampleShader = makeShader( depthSampleFragment, sizeof( depthSampleFragment ) );
    const auto shadowSampleShader = makeShader( shadowSampleFragment, sizeof( shadowSampleFragment ) );
    const auto inactiveShadowShader = makeShader( inactiveShadowFragment, sizeof( inactiveShadowFragment ) );

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
    pipelineDesc.depthTest = false; // Sampling overlays replace earlier presentation pixels.
    pipelineDesc.depthWrite = false;
    pipelineDesc.sampledTextureCount = 1u;
    pipelineDesc.sampledTextures[0].name = "base_color";
    render::render_pipeline_handle_t samplePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &samplePipeline ) == render::render_error_t::OK );
    pipelineDesc.shader = depthSampleShader;
    render::render_pipeline_handle_t depthSamplePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &depthSamplePipeline ) == render::render_error_t::OK );
    render::render_pipeline_handle_t invalidShadowPipeline{};
    pipelineDesc.sampledTextures[0].kind = render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D;
    CHECK( render::R_CreateGraphicsPipeline( pipelineDesc, &invalidShadowPipeline ) ==
        render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipelineDesc.shader = shadowSampleShader;
    pipelineDesc.sampledTextures[0] = { "shadow_map",
        render::render_sampled_texture_kind_t::SCALAR_2D };
    CHECK( render::R_CreateGraphicsPipeline( pipelineDesc, &invalidShadowPipeline ) ==
        render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipelineDesc.sampledTextures[0].kind = render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D;
    render::render_pipeline_handle_t shadowSamplePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &shadowSamplePipeline ) == render::render_error_t::OK );
    pipelineDesc.shader = inactiveShadowShader;
    CHECK( render::R_CreateGraphicsPipeline( pipelineDesc, &invalidShadowPipeline ) ==
        render::render_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    pipelineDesc.shader = solidShader;
    pipelineDesc.sampledTextureCount = 0u;
    pipelineDesc.sampledTextures[0] = {};
    pipelineDesc.depthTest = true;
    pipelineDesc.depthWrite = true;
    pipelineDesc.colorWrite = false;
    render::render_pipeline_handle_t depthOnlyPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &depthOnlyPipeline ) == render::render_error_t::OK );

    render::render_texture_desc_t textureDesc{};
    textureDesc.width = extent.width / 2u;
    textureDesc.height = extent.height / 2u;
    textureDesc.sRGB = false;
    textureDesc.generateMips = false;
    textureDesc.repeat = false;
    textureDesc.renderTarget = true;
    render::render_texture_handle_t color{};
    REQUIRE( render::R_CreateTexture2D( textureDesc, {}, &color ) == render::render_error_t::OK );
    textureDesc.format = render::render_texture_format_t::D24;
    render::render_texture_handle_t depth{};
    REQUIRE( render::R_CreateTexture2D( textureDesc, {}, &depth ) == render::render_error_t::OK );
    render::render_texture_handle_t depthOnlyTexture{};
    REQUIRE( render::R_CreateTexture2D( textureDesc, {}, &depthOnlyTexture ) == render::render_error_t::OK );
    render::render_sampler_desc_t compareDesc{};
    compareDesc.minFilter = compareDesc.magFilter = render::render_sampler_filter_t::NEAREST;
    compareDesc.addressU = compareDesc.addressV = render::render_sampler_address_t::CLAMP_TO_EDGE;
    compareDesc.compare = render::render_sampler_compare_t::LESS_EQUAL;
    render::render_sampler_handle_t compareSampler{};
    REQUIRE( render::R_CreateSampler( compareDesc, &compareSampler ) == render::render_error_t::OK );
    render::render_target_handle_t target{};
    REQUIRE( render::R_CreateRenderTarget( { color, depth }, &target ) == render::render_error_t::OK );
    render::render_target_handle_t depthOnlyTarget{};
    REQUIRE( render::R_CreateRenderTarget( { {}, depthOnlyTexture }, &depthOnlyTarget ) ==
        render::render_error_t::OK );
    CHECK( render::R_DestroyTexture( color ) == render::render_error_t::ERR_RESOURCE_BUSY );
    CHECK( render::R_DestroyTexture( depth ) == render::render_error_t::ERR_RESOURCE_BUSY );
    CHECK( render::R_DestroyTexture( depthOnlyTexture ) == render::render_error_t::ERR_RESOURCE_BUSY );

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
    draw.viewport = { true, 0u, 0u, textureDesc.width + 1u,
                      textureDesc.height };
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_DRAW_DESC_INVALID );
    draw.viewport = { true, 0u, 0u, textureDesc.width, textureDesc.height };
    // This rectangle fits the drawable but overruns the smaller active target.
    draw.scissor = { true, 0u, 0u, textureDesc.width + 1u,
                     textureDesc.height };
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_DRAW_DESC_INVALID );
    draw.scissor = { true, textureDesc.width / 2u, 0u,
                     textureDesc.width / 2u, textureDesc.height };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    draw.scissor = {};
    draw.viewport = {};
    draw.pipeline = samplePipeline;
    draw.sampledTextures[0] = color;
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_COMMAND_STATE_INVALID );
    draw.sampledTextures[0] = depth;
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( render::R_EndFrame() == render::render_error_t::ERR_COMMAND_STATE_INVALID );
    // Seed two known bottom-edge rows to prove readback row orientation, not
    // just pixel values. This deliberately uses native GL inside the smoke
    // fixture; normal callers interact with the target through renderer APIs.
    glEnable( GL_SCISSOR_TEST );
    glScissor( 4, 4, 2, 1 );
    glClearColor( 1.0f, 0.0f, 0.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );
    glScissor( 4, 5, 2, 1 );
    glClearColor( 0.0f, 0.0f, 1.0f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );
    glDisable( GL_SCISSOR_TEST );
    REQUIRE( glGetError() == GL_NO_ERROR );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );

    // Read a color target while the frame is still active. A host can leave
    // both an unrelated read FBO and a pixel-pack buffer bound, with a custom
    // pack layout. Readback must use caller memory, write bottom-up rows at the
    // requested pitch, and return all host GL state exactly as it found it.
    GLint previousReadFbo = 0;
    GLint previousPackBuffer = 0;
    GLint previousPackAlignment = 0;
    GLint previousPackRowLength = 0;
    GLint previousPackSkipRows = 0;
    GLint previousPackSkipPixels = 0;
    glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo );
    glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer );
    glGetIntegerv( GL_PACK_ALIGNMENT, &previousPackAlignment );
    glGetIntegerv( GL_PACK_ROW_LENGTH, &previousPackRowLength );
    glGetIntegerv( GL_PACK_SKIP_ROWS, &previousPackSkipRows );
    glGetIntegerv( GL_PACK_SKIP_PIXELS, &previousPackSkipPixels );
    REQUIRE( glGetError() == GL_NO_ERROR );
    GLuint unrelatedReadFbo = 0u;
    GLuint unrelatedPackBuffer = 0u;
    glGenFramebuffers( 1, &unrelatedReadFbo );
    glGenBuffers( 1, &unrelatedPackBuffer );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, unrelatedReadFbo );
    glBindBuffer( GL_PIXEL_PACK_BUFFER, unrelatedPackBuffer );
    glBufferData( GL_PIXEL_PACK_BUFFER, 64, nullptr, GL_STREAM_READ );
    glPixelStorei( GL_PACK_ALIGNMENT, 8 );
    glPixelStorei( GL_PACK_ROW_LENGTH, 13 );
    glPixelStorei( GL_PACK_SKIP_ROWS, 2 );
    glPixelStorei( GL_PACK_SKIP_PIXELS, 3 );
    REQUIRE( glGetError() == GL_NO_ERROR );
    std::array<common::u8, 32u> targetPixels{};
    targetPixels.fill( 0xcdu );
    const render::render_readback_rect_t readRect{ 4u, 4u, 2u, 2u };
    CHECK( render::R_ReadTargetRGBA8( target, readRect, targetPixels.data(), 23u, 16u ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
    for ( const common::u8 byte : targetPixels ) CHECK( byte == 0xcdu );
    REQUIRE( render::R_ReadTargetRGBA8( target, readRect, targetPixels.data(), 24u, 16u ) ==
        render::render_error_t::OK );
    for ( const common::u32 row : { 0u, 1u } ) {
        for ( const common::u32 x : { 0u, 1u } ) {
            const common::u32 offset = row * 16u + x * 4u;
            CHECK( targetPixels[offset + 0u] == ( row == 0u ? 255u : 0u ) );
            CHECK( targetPixels[offset + 1u] == 0u );
            CHECK( targetPixels[offset + 2u] == ( row == 0u ? 0u : 255u ) );
            CHECK( targetPixels[offset + 3u] >= 240u );
        }
    }
    for ( common::u32 i = 8u; i < 16u; ++i ) CHECK( targetPixels[i] == 0xcdu );
    for ( common::u32 i = 24u; i < 32u; ++i ) CHECK( targetPixels[i] == 0xcdu );
    GLint capturedPackState = 0;
    glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &capturedPackState );
    CHECK( capturedPackState == static_cast<GLint>( unrelatedReadFbo ) );
    glGetIntegerv( GL_PIXEL_PACK_BUFFER_BINDING, &capturedPackState );
    CHECK( capturedPackState == static_cast<GLint>( unrelatedPackBuffer ) );
    glGetIntegerv( GL_PACK_ALIGNMENT, &capturedPackState ); CHECK( capturedPackState == 8 );
    glGetIntegerv( GL_PACK_ROW_LENGTH, &capturedPackState ); CHECK( capturedPackState == 13 );
    glGetIntegerv( GL_PACK_SKIP_ROWS, &capturedPackState ); CHECK( capturedPackState == 2 );
    glGetIntegerv( GL_PACK_SKIP_PIXELS, &capturedPackState ); CHECK( capturedPackState == 3 );
    REQUIRE( glGetError() == GL_NO_ERROR );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, static_cast<GLuint>( previousReadFbo ) );
    glBindBuffer( GL_PIXEL_PACK_BUFFER, static_cast<GLuint>( previousPackBuffer ) );
    glPixelStorei( GL_PACK_ALIGNMENT, previousPackAlignment );
    glPixelStorei( GL_PACK_ROW_LENGTH, previousPackRowLength );
    glPixelStorei( GL_PACK_SKIP_ROWS, previousPackSkipRows );
    glPixelStorei( GL_PACK_SKIP_PIXELS, previousPackSkipPixels );
    glDeleteFramebuffers( 1, &unrelatedReadFbo );
    glDeleteBuffers( 1, &unrelatedPackBuffer );
    REQUIRE( glGetError() == GL_NO_ERROR );

    const auto readPresentation = [&]( const GLint x, const GLint y ) {
        std::array<common::u8, 4u> pixel{};
        glReadBuffer( GL_BACK );
        glReadPixels( x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data() );
        REQUIRE( glGetError() == GL_NO_ERROR );
        return pixel;
    };
    const GLint sampleRightX = static_cast<GLint>( extent.width * 5u / 8u );
    const GLint sampleLeftX = static_cast<GLint>( extent.width * 3u / 8u );
    const GLint sampleY = static_cast<GLint>( extent.height / 2u );
    const auto beforeSampling = readPresentation( sampleRightX, sampleY );
    CHECK( beforeSampling[2] >= 240u ); // Target draw did not touch presentation.
    draw.firstIndex = 3u;
    draw.sampledTextures[0] = color;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto right = readPresentation( sampleRightX, sampleY );
    const auto left = readPresentation( sampleLeftX, sampleY );
    const auto corner = readPresentation( 1, 1 );
    CHECK( right[0] >= 240u );
    CHECK( right[1] <= 15u );
    CHECK( left[1] >= 240u ); // Same triangle, left half was clipped in target.
    CHECK( left[0] <= 15u );
    CHECK( corner[1] >= 240u );
    CHECK( corner[0] <= 15u );

    // The same pass wrote depth 0.5 under its clipped triangle and left 1.0
    // elsewhere. Reading D24 as a plain sampler2D after EndTargetPass proves
    // that it is a sampleable attachment, not an unobservable renderbuffer.
    draw.pipeline = depthSamplePipeline;
    draw.sampledTextures[0] = depth;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto nearDepth = readPresentation( sampleRightX, sampleY );
    const auto farDepth = readPresentation( sampleLeftX, sampleY );
    CHECK( nearDepth[0] >= 110u );
    CHECK( nearDepth[0] <= 145u );
    CHECK( nearDepth[1] >= 110u );
    CHECK( nearDepth[1] <= 145u );
    CHECK( farDepth[0] >= 240u );
    CHECK( farDepth[1] >= 240u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    // A depth-only FBO has no color route. Draw depth at 0.5, then sample its
    // D24 attachment on the presentation framebuffer. An untouched texel must
    // remain at the depth clear value of 1.0.
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    pass.target = depthOnlyTarget;
    CHECK( render::R_BeginTargetPass( pass ) == render::render_error_t::ERR_INVALID_ARGUMENT );
    pass.clearFlags = render::R_CLEAR_DEPTH;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    GLint drawBuffer = -1;
    GLint readBuffer = -1;
    glGetIntegerv( GL_DRAW_BUFFER0, &drawBuffer );
    glGetIntegerv( GL_READ_BUFFER, &readBuffer );
    CHECK( drawBuffer == GL_NONE );
    CHECK( readBuffer == GL_NONE );
    REQUIRE( glGetError() == GL_NO_ERROR );
    draw.pipeline = depthOnlyPipeline;
    draw.sampledTextures[0] = {};
    draw.firstIndex = 0u;
    draw.indexCount = 3u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    draw.pipeline = depthSamplePipeline;
    draw.sampledTextures[0] = depthOnlyTexture;
    CHECK( render::R_DrawIndexed( draw ) == render::render_error_t::ERR_COMMAND_STATE_INVALID );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    const auto beforeDepthOnlySample = readPresentation( sampleRightX, sampleY );
    CHECK( beforeDepthOnlySample[2] >= 240u );
    draw.firstIndex = 3u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto depthOnlyNear = readPresentation( sampleRightX, sampleY );
    const auto depthOnlyFar = readPresentation( 1, 1 );
    CHECK( depthOnlyNear[0] >= 110u );
    CHECK( depthOnlyNear[0] <= 145u );
    CHECK( depthOnlyNear[1] >= 110u );
    CHECK( depthOnlyNear[1] <= 145u );
    CHECK( depthOnlyFar[0] >= 240u );
    CHECK( depthOnlyFar[1] >= 240u );

    // The depth-only pass wrote 0.5 inside the triangle and cleared the rest
    // to 1.0. A nearest LEQUAL shadow sampler must pass reference 0.25 on
    // both texels, but reject reference 0.75 only over the drawn triangle.
    draw.pipeline = shadowSamplePipeline;
    draw.samplers[0] = compareSampler;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto shadowed = readPresentation( sampleRightX, sampleY );
    const auto clearDepth = readPresentation( 1, 1 );
    CHECK( shadowed[0] >= 240u );
    CHECK( shadowed[1] <= 15u );
    CHECK( clearDepth[0] >= 240u );
    CHECK( clearDepth[1] >= 240u );
    draw.pipeline = depthSamplePipeline;
    draw.samplers[0] = {};
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    const auto rawAfterCompare = readPresentation( sampleRightX, sampleY );
    CHECK( rawAfterCompare[0] >= 110u );
    CHECK( rawAfterCompare[0] <= 145u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyRenderTarget( target ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyRenderTarget( depthOnlyTarget ) == render::render_error_t::OK );
    CHECK( render::R_IsTextureValid( color ) );
    CHECK( render::R_IsTextureValid( depth ) );
    REQUIRE( render::R_DestroyTexture( color ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depthOnlyTexture ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroySampler( compareSampler ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( solidPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( samplePipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( depthSamplePipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( shadowSamplePipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( depthOnlyPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( ib ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( solidShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( sampleShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( depthSampleShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shadowSampleShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( inactiveShadowShader ) == render::render_error_t::OK );
}

void CheckHDRTargetToneMap( const render::render_extent_t extent )
{
    // The first pass writes values that an RGBA8 attachment would clamp. The
    // second pass must sample those original values before mapping to RGBA8.
    constexpr char vertex[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "void main() { gl_Position=vec4(position,1.0); }\n";
    constexpr char hdrFragment[] =
        "#version 410 core\nlayout(location=0) out vec4 color;\n"
        "void main() { color=vec4(4.0,1.0,0.0,1.0); }\n";
    constexpr char toneMapFragment[] =
        "#version 410 core\nuniform sampler2D scene_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { vec3 hdr=texture(scene_color,vec2(0.5)).rgb;"
        " color=vec4(hdr/(vec3(1.0)+hdr),1.0); }\n";
    const auto makeShader = [&]( const char *fragment, const common::u64 size ) {
        common::cooked_shader_view_t source{};
        source.nLanguageVersion = 410u;
        source.nStages = 2u;
        source.stages[0].stage = common::render_shader_stage_t::VERTEX;
        source.stages[0].code = {
            reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
        source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        source.stages[1].code = {
            reinterpret_cast<const common::byte *>( fragment ), size };
        render::render_shader_handle_t shader{};
        REQUIRE( render::R_CreateShader( { &source, "HDR target pixel oracle" },
            &shader ) == render::render_error_t::OK );
        return shader;
    };
    const auto hdrShader = makeShader( hdrFragment, sizeof( hdrFragment ) );
    const auto toneMapShader = makeShader( toneMapFragment, sizeof( toneMapFragment ) );

    constexpr float vertices[]{ -1.0f,-1.0f,0.0f, 3.0f,-1.0f,0.0f,
                                -1.0f,3.0f,0.0f };
    constexpr common::u16 indices[]{ 0u,1u,2u };
    const auto makeBuffer = []( const void *bytes, const common::u64 size,
                                const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t description{};
        description.byteSize = size;
        description.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t buffer{};
        REQUIRE( render::R_CreateBuffer( description, &data, &buffer ) ==
            render::render_error_t::OK );
        return buffer;
    };
    const auto vertexBuffer = makeBuffer( vertices, sizeof( vertices ),
        render::R_BUFFER_USAGE_VERTEX );
    const auto indexBuffer = makeBuffer( indices, sizeof( indices ),
        render::R_BUFFER_USAGE_INDEX );
    render::render_vertex_input_desc_t inputDescription{};
    inputDescription.layout.bindingCount = 1u;
    inputDescription.layout.bindings[0].stride = 3u * sizeof( float );
    inputDescription.layout.attributeCount = 1u;
    inputDescription.layout.attributes[0] = {
        render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDescription.vertexBufferCount = 1u;
    inputDescription.vertexBuffers[0] = { vertexBuffer, 0u, 0u };
    inputDescription.indexBuffer = {
        indexBuffer, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDescription, &input ) ==
        render::render_error_t::OK );

    render::render_pipeline_desc_t pipelineDescription{};
    pipelineDescription.vertexLayout = inputDescription.layout;
    pipelineDescription.shader = hdrShader;
    pipelineDescription.depthTest = false;
    pipelineDescription.depthWrite = false;
    pipelineDescription.cullBackFaces = false;
    render::render_pipeline_handle_t hdrPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDescription,
        &hdrPipeline ) == render::render_error_t::OK );
    pipelineDescription.shader = toneMapShader;
    pipelineDescription.sampledTextureCount = 1u;
    pipelineDescription.sampledTextures[0].name = "scene_color";
    render::render_pipeline_handle_t toneMapPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDescription,
        &toneMapPipeline ) == render::render_error_t::OK );

    render::render_texture_desc_t textureDescription{};
    textureDescription.width = extent.width / 2u;
    textureDescription.height = extent.height / 2u;
    textureDescription.sRGB = false;
    textureDescription.generateMips = false;
    textureDescription.repeat = false;
    textureDescription.renderTarget = true;
    textureDescription.format = render::render_texture_format_t::RGBA16F;
    render::render_texture_handle_t hdrColor{};
    REQUIRE( render::R_CreateTexture2D( textureDescription, {}, &hdrColor ) ==
        render::render_error_t::OK );
    textureDescription.format = render::render_texture_format_t::RGBA8;
    render::render_texture_handle_t ldrColor{};
    REQUIRE( render::R_CreateTexture2D( textureDescription, {}, &ldrColor ) ==
        render::render_error_t::OK );
    render::render_target_handle_t hdrTarget{}, ldrTarget{};
    REQUIRE( render::R_CreateRenderTarget( { hdrColor, {} }, &hdrTarget ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateRenderTarget( { ldrColor, {} }, &ldrTarget ) ==
        render::render_error_t::OK );

    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    render::render_draw_indexed_desc_t draw{};
    draw.vertexInput = input;
    draw.indexCount = draw.vertexCount = 3u;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    render::render_target_pass_desc_t pass{};
    pass.target = hdrTarget;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = hdrPipeline;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    const render::render_readback_rect_t center{
        textureDescription.width / 2u, textureDescription.height / 2u, 1u, 1u };
    std::array<common::u8, 4u> rejectedReadback{ 0xcdu, 0xcdu, 0xcdu, 0xcdu };
    CHECK( render::R_ReadTargetRGBA8( hdrTarget, center, rejectedReadback.data(),
        rejectedReadback.size(), 4u ) == render::render_error_t::ERR_CAPTURE_UNSUPPORTED );
    for ( const auto channel : rejectedReadback ) CHECK( channel == 0xcdu );

    pass.target = ldrTarget;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = toneMapPipeline;
    draw.sampledTextures[0] = hdrColor;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    std::array<common::u8, 4u> pixel{};
    REQUIRE( render::R_ReadTargetRGBA8( ldrTarget, center, pixel.data(),
        pixel.size(), 4u ) == render::render_error_t::OK );
    INFO( "Tonemapped RGBA8 center: " << static_cast<int>( pixel[0] ) << ", "
        << static_cast<int>( pixel[1] ) << ", "
        << static_cast<int>( pixel[2] ) << ", "
        << static_cast<int>( pixel[3] ) );
    CHECK( pixel[0] >= 200u ); // 4 / (1 + 4) = 0.8, not clipped 1 / 2.
    CHECK( pixel[0] <= 208u );
    CHECK( pixel[1] >= 124u ); // 1 / (1 + 1) = 0.5.
    CHECK( pixel[1] <= 132u );
    CHECK( pixel[2] <= 4u );
    CHECK( pixel[3] >= 250u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyRenderTarget( ldrTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyRenderTarget( hdrTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( ldrColor ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( hdrColor ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( toneMapPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( hdrPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( indexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vertexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( toneMapShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( hdrShader ) == render::render_error_t::OK );
}

void CheckHDRTranslucentCompositing( const render::render_extent_t extent )
{
    // The color-only HDR target supplies a private D24 attachment. An opaque
    // near surface covers its left half; two translucent fullscreen surfaces
    // are then submitted far-to-near. The right pixel depends on draw order,
    // while the left pixel proves both translucent draws obey opaque depth.
    // An opaque probe drawn afterward in the upper right proves that the
    // translucent surfaces did not write their own depths.
    constexpr char vertex[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "out vec2 uv; void main() { gl_Position=vec4(position,1.0);"
        " uv=position.xy*0.5+0.5; }\n";
    constexpr char paintFragment[] =
        "#version 410 core\nlayout(std140) uniform Paint { vec4 tint; };\n"
        "layout(location=0) out vec4 color; void main() { color=tint; }\n";
    constexpr char toneMapFragment[] =
        "#version 410 core\nin vec2 uv; uniform sampler2D scene_color;\n"
        "layout(location=0) out vec4 color; void main() {"
        " vec3 hdr=texture(scene_color,uv).rgb;"
        " color=vec4(hdr/(vec3(1.0)+hdr),1.0); }\n";
    const auto makeShader = [&]( const char *fragment, const common::u64 size ) {
        common::cooked_shader_view_t source{};
        source.nLanguageVersion = 410u;
        source.nStages = 2u;
        source.stages[0].stage = common::render_shader_stage_t::VERTEX;
        source.stages[0].code = {
            reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
        source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        source.stages[1].code = {
            reinterpret_cast<const common::byte *>( fragment ), size };
        render::render_shader_handle_t shader{};
        REQUIRE( render::R_CreateShader( { &source, "HDR transparency pixel oracle" },
            &shader ) == render::render_error_t::OK );
        return shader;
    };
    const auto paintShader = makeShader( paintFragment, sizeof( paintFragment ) );
    const auto toneMapShader = makeShader( toneMapFragment, sizeof( toneMapFragment ) );

    // Each indexed triangle covers the target. Index order chooses its fixed
    // clip-space depth: opaque 0.125, far translucent 0.75, near translucent 0.5.
    constexpr float vertices[]{
        -1,-1,-0.75f, 3,-1,-0.75f, -1,3,-0.75f,
        -1,-1, 0.50f, 3,-1, 0.50f, -1,3, 0.50f,
        -1,-1, 0.00f, 3,-1, 0.00f, -1,3, 0.00f
    };
    constexpr common::u16 indices[]{ 0,1,2,3,4,5,6,7,8 };
    constexpr float opaqueGreen[]{ 0.0f,2.0f,0.0f,1.0f };
    constexpr float farBlue[]{ 0.0f,0.0f,2.0f,0.5f };
    constexpr float nearRed[]{ 2.0f,0.0f,0.0f,0.5f };
    constexpr float additiveTint[]{ 0.5f,0.25f,0.125f,0.5f };
    const auto makeBuffer = []( const void *bytes, const common::u64 size,
                                const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t description{};
        description.byteSize = size;
        description.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t buffer{};
        REQUIRE( render::R_CreateBuffer( description, &data, &buffer ) ==
            render::render_error_t::OK );
        return buffer;
    };
    const auto vertexBuffer = makeBuffer( vertices, sizeof( vertices ), render::R_BUFFER_USAGE_VERTEX );
    const auto indexBuffer = makeBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    const auto greenBuffer = makeBuffer( opaqueGreen, sizeof( opaqueGreen ), render::R_BUFFER_USAGE_UNIFORM );
    const auto blueBuffer = makeBuffer( farBlue, sizeof( farBlue ), render::R_BUFFER_USAGE_UNIFORM );
    const auto redBuffer = makeBuffer( nearRed, sizeof( nearRed ), render::R_BUFFER_USAGE_UNIFORM );
    const auto additiveBuffer = makeBuffer( additiveTint, sizeof( additiveTint ),
        render::R_BUFFER_USAGE_UNIFORM );
    render::render_vertex_input_desc_t inputDescription{};
    inputDescription.layout.bindingCount = 1u;
    inputDescription.layout.bindings[0].stride = 3u * sizeof( float );
    inputDescription.layout.attributeCount = 1u;
    inputDescription.layout.attributes[0] = {
        render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDescription.vertexBufferCount = 1u;
    inputDescription.vertexBuffers[0] = { vertexBuffer, 0u, 0u };
    inputDescription.indexBuffer = {
        indexBuffer, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDescription, &input ) ==
        render::render_error_t::OK );

    render::render_pipeline_desc_t pipelineDescription{};
    pipelineDescription.vertexLayout = inputDescription.layout;
    pipelineDescription.shader = paintShader;
    pipelineDescription.cullBackFaces = false;
    pipelineDescription.uniformBlockCount = 1u;
    pipelineDescription.uniformBlocks[0] = { "Paint", sizeof( opaqueGreen ) };
    render::render_pipeline_handle_t opaquePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDescription,
        &opaquePipeline ) == render::render_error_t::OK );
    pipelineDescription.blendMode = render::render_blend_mode_t::STRAIGHT_ALPHA;
    pipelineDescription.depthWrite = false;
    render::render_pipeline_handle_t translucentPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDescription,
        &translucentPipeline ) == render::render_error_t::OK );
    pipelineDescription.blendMode = render::render_blend_mode_t::ADDITIVE;
    render::render_pipeline_handle_t additivePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDescription,
        &additivePipeline ) == render::render_error_t::OK );
    pipelineDescription.shader = toneMapShader;
    pipelineDescription.blendMode = render::render_blend_mode_t::DISABLED;
    pipelineDescription.depthTest = false;
    pipelineDescription.uniformBlockCount = 0u;
    pipelineDescription.uniformBlocks[0] = {};
    pipelineDescription.sampledTextureCount = 1u;
    pipelineDescription.sampledTextures[0].name = "scene_color";
    render::render_pipeline_handle_t toneMapPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDescription,
        &toneMapPipeline ) == render::render_error_t::OK );

    render::render_texture_desc_t textureDescription{};
    textureDescription.width = extent.width / 2u;
    textureDescription.height = extent.height / 2u;
    textureDescription.sRGB = false;
    textureDescription.generateMips = false;
    textureDescription.repeat = false;
    textureDescription.renderTarget = true;
    textureDescription.format = render::render_texture_format_t::RGBA16F;
    render::render_texture_handle_t hdrColor{};
    REQUIRE( render::R_CreateTexture2D( textureDescription, {}, &hdrColor ) ==
        render::render_error_t::OK );
    textureDescription.format = render::render_texture_format_t::RGBA8;
    render::render_texture_handle_t ldrColor{};
    REQUIRE( render::R_CreateTexture2D( textureDescription, {}, &ldrColor ) ==
        render::render_error_t::OK );
    textureDescription.format = render::render_texture_format_t::D24;
    render::render_texture_handle_t ldrDepth{};
    REQUIRE( render::R_CreateTexture2D( textureDescription, {}, &ldrDepth ) ==
        render::render_error_t::OK );
    render::render_target_handle_t hdrTarget{}, ldrTarget{};
    REQUIRE( render::R_CreateRenderTarget( { hdrColor, {} }, &hdrTarget ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateRenderTarget( { ldrColor, ldrDepth }, &ldrTarget ) ==
        render::render_error_t::OK );

    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    render::render_target_pass_desc_t pass{};
    pass.target = hdrTarget;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    render::render_draw_indexed_desc_t draw{};
    draw.vertexInput = input;
    draw.vertexCount = 9u;
    draw.indexCount = 3u;
    draw.pipeline = opaquePipeline;
    draw.uniformBuffers[0] = greenBuffer;
    draw.scissor = { true, 0u, 0u, textureDescription.width / 2u,
                     textureDescription.height };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    draw.pipeline = translucentPipeline;
    draw.scissor = {};
    draw.firstIndex = 3u;
    draw.uniformBuffers[0] = blueBuffer;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    draw.firstIndex = 6u;
    draw.uniformBuffers[0] = redBuffer;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    draw.pipeline = opaquePipeline;
    draw.firstIndex = 3u;
    draw.uniformBuffers[0] = greenBuffer;
    draw.scissor = { true, textureDescription.width / 2u,
                     textureDescription.height / 2u,
                     textureDescription.width / 2u,
                     textureDescription.height / 2u };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );

    pass.target = ldrTarget;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = toneMapPipeline;
    draw.firstIndex = 0u;
    draw.uniformBuffers[0] = {};
    draw.scissor = {};
    draw.sampledTextures[0] = hdrColor;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    const auto readPixel = [&]( const common::u32 x, const common::u32 y ) {
        std::array<common::u8, 4u> pixel{};
        const render::render_readback_rect_t rect{ x, y, 1u, 1u };
        REQUIRE( render::R_ReadTargetRGBA8( ldrTarget, rect, pixel.data(),
            pixel.size(), 4u ) == render::render_error_t::OK );
        return pixel;
    };
    const auto left = readPixel( textureDescription.width / 4u,
        textureDescription.height / 2u );
    const auto right = readPixel( 3u * textureDescription.width / 4u,
        textureDescription.height / 4u );
    const auto depthProbe = readPixel( 3u * textureDescription.width / 4u,
        3u * textureDescription.height / 4u );
    INFO( "Opaque left RGBA: " << static_cast<int>( left[0] ) << ", "
        << static_cast<int>( left[1] ) << ", " << static_cast<int>( left[2] ) );
    CHECK( left[0] <= 5u );
    CHECK( left[1] >= 165u ); // 2 / (1 + 2), unaffected by transparent draws.
    CHECK( left[1] <= 175u );
    CHECK( left[2] <= 5u );
    INFO( "Transparent right RGBA: " << static_cast<int>( right[0] ) << ", "
        << static_cast<int>( right[1] ) << ", " << static_cast<int>( right[2] ) );
    CHECK( right[0] >= 123u ); // Red: 2 * 0.5 = 1, then 1 / (1 + 1).
    CHECK( right[0] <= 133u );
    CHECK( right[1] <= 5u );
    CHECK( right[2] >= 80u ); // Blue: 2 * 0.5 * (1 - 0.5) = 0.5.
    CHECK( right[2] <= 90u );
    CHECK( left[3] >= 250u );
    CHECK( right[3] >= 250u );
    CHECK( depthProbe[0] <= 5u );
    CHECK( depthProbe[1] >= 165u );
    CHECK( depthProbe[1] <= 175u );
    CHECK( depthProbe[2] <= 5u );

    // Read the RGBA8 attachment directly: additive color uses src alpha but
    // keeps the destination alpha. An opaque draw behind the additive surface
    // must still pass the depth test because additive draws do not write depth.
    pass.clearColor = { 0.125f, 0.25f, 0.375f, 0.25f };
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = additivePipeline;
    draw.firstIndex = 6u; // Clip-space z = 0: window depth 0.5.
    draw.uniformBuffers[0] = additiveBuffer;
    draw.sampledTextures[0] = {};
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    draw.pipeline = opaquePipeline;
    draw.firstIndex = 3u; // Clip-space z = 0.5: window depth 0.75.
    draw.uniformBuffers[0] = greenBuffer;
    draw.scissor = { true, textureDescription.width / 2u,
                     textureDescription.height / 2u,
                     textureDescription.width / 2u,
                     textureDescription.height / 2u };
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    const auto additive = readPixel( textureDescription.width / 4u,
        textureDescription.height / 4u );
    const auto additiveDepthProbe = readPixel( 3u * textureDescription.width / 4u,
        3u * textureDescription.height / 4u );
    CAPTURE( additive[0], additive[1], additive[2], additive[3] );
    CHECK( additive[0] >= 92u ); // 0.125 + 0.5 * 0.5 = 0.375.
    CHECK( additive[0] <= 100u );
    CHECK( additive[1] >= 92u ); // 0.25 + 0.25 * 0.5 = 0.375.
    CHECK( additive[1] <= 100u );
    CHECK( additive[2] >= 108u ); // 0.375 + 0.125 * 0.5 = 0.4375.
    CHECK( additive[2] <= 116u );
    CHECK( additive[3] >= 60u ); // Destination alpha 0.25 is preserved.
    CHECK( additive[3] <= 68u );
    CHECK( additiveDepthProbe[0] <= 5u );
    CHECK( additiveDepthProbe[1] >= 250u );
    CHECK( additiveDepthProbe[2] <= 5u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyRenderTarget( ldrTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyRenderTarget( hdrTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( ldrDepth ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( ldrColor ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( hdrColor ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( toneMapPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( additivePipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( translucentPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( opaquePipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( redBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( additiveBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( blueBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( greenBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( indexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vertexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( toneMapShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( paintShader ) == render::render_error_t::OK );
}

void CheckAlphaMaskCutout( const render::render_extent_t extent )
{
    // A 2x1 texture splits a fullscreen surface at the center. Both texels
    // are red, but only the right texel's alpha meets the 0.5 cutoff.
    constexpr char vertex[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position=vec4(position,1.0); uv=position.xy*0.5+0.5; }\n";
    constexpr char maskFragment[] =
        "#version 410 core\nin vec2 uv; uniform sampler2D alphaMap;\n"
        "layout(std140) uniform MaskConstants { vec4 cutoff; };\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { if (texture(alphaMap,uv).a < cutoff.x) discard;"
        " color=vec4(1.0,0.0,0.0,1.0); }\n";
    constexpr char probeFragment[] =
        "#version 410 core\nlayout(location=0) out vec4 color;\n"
        "void main() { color=vec4(0.0,1.0,0.0,1.0); }\n";
    constexpr char shadowFragment[] =
        "#version 410 core\nin vec2 uv; uniform sampler2DShadow shadowMap;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { float visible=texture(shadowMap,vec3(uv,0.5));"
        " color=vec4(vec3(visible),1.0); }\n";
    const auto makeShader = [&]( const char *fragment, const common::u64 byteSize ) {
        common::cooked_shader_view_t source{};
        source.nLanguageVersion = 410u;
        source.nStages = 2u;
        source.stages[0].stage = common::render_shader_stage_t::VERTEX;
        source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
        source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), byteSize };
        render::render_shader_handle_t shader{};
        REQUIRE( render::R_CreateShader( { &source, "Alpha mask pixel oracle" }, &shader ) ==
            render::render_error_t::OK );
        return shader;
    };
    const auto maskShader = makeShader( maskFragment, sizeof( maskFragment ) );
    const auto probeShader = makeShader( probeFragment, sizeof( probeFragment ) );
    const auto shadowShader = makeShader( shadowFragment, sizeof( shadowFragment ) );

    // The masked surface sits at depth 0.25; the later green probe at 0.5.
    const float vertices[]{
        -1.0f,-1.0f,-0.5f, 3.0f,-1.0f,-0.5f, -1.0f,3.0f,-0.5f,
        -1.0f,-1.0f, 0.0f, 3.0f,-1.0f, 0.0f, -1.0f,3.0f, 0.0f
    };
    const common::u16 indices[]{ 0u, 1u, 2u, 3u, 4u, 5u };
    const auto makeBuffer = []( const void *bytes, const common::u64 byteSize,
                                const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = byteSize;
        desc.usage = usage;
        const render::render_buffer_data_t data{ bytes, byteSize };
        render::render_buffer_handle_t buffer{};
        REQUIRE( render::R_CreateBuffer( desc, &data, &buffer ) == render::render_error_t::OK );
        return buffer;
    };
    const auto vb = makeBuffer( vertices, sizeof( vertices ), render::R_BUFFER_USAGE_VERTEX );
    const auto ib = makeBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    const float cutoffData[4]{ 0.5f, 0.0f, 0.0f, 0.0f };
    const auto maskUniform = makeBuffer( cutoffData, sizeof( cutoffData ),
        render::R_BUFFER_USAGE_UNIFORM );
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
    pipelineDesc.shader = maskShader;
    pipelineDesc.vertexLayout = inputDesc.layout;
    pipelineDesc.sampledTextureCount = 1u;
    pipelineDesc.sampledTextures[0] = { "alphaMap",
        render::render_sampled_texture_kind_t::SCALAR_2D };
    pipelineDesc.uniformBlockCount = 1u;
    pipelineDesc.uniformBlocks[0] = { "MaskConstants", 16u };
    render::render_pipeline_handle_t maskPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &maskPipeline ) ==
        render::render_error_t::OK );
    pipelineDesc.colorWrite = false;
    render::render_pipeline_handle_t maskDepthPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &maskDepthPipeline ) ==
        render::render_error_t::OK );
    pipelineDesc.shader = probeShader;
    pipelineDesc.colorWrite = true;
    pipelineDesc.sampledTextureCount = 0u;
    pipelineDesc.sampledTextures[0] = {};
    pipelineDesc.uniformBlockCount = 0u;
    pipelineDesc.uniformBlocks[0] = {};
    render::render_pipeline_handle_t probePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &probePipeline ) ==
        render::render_error_t::OK );
    pipelineDesc.shader = shadowShader;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.sampledTextureCount = 1u;
    pipelineDesc.sampledTextures[0] = { "shadowMap",
        render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D };
    render::render_pipeline_handle_t shadowPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &shadowPipeline ) ==
        render::render_error_t::OK );

    constexpr common::u8 alphaPixels[]{
        255u, 0u, 0u, 64u, 255u, 0u, 0u, 192u
    };
    render::render_texture_desc_t alphaDesc{ 2u, 1u, false, false, false,
        "Cutout alpha texture" };
    render::render_texture_handle_t alpha{};
    REQUIRE( render::R_CreateTexture2D( alphaDesc,
        { alphaPixels, sizeof( alphaPixels ) }, &alpha ) == render::render_error_t::OK );
    render::render_sampler_desc_t nearestDesc{};
    nearestDesc.minFilter = nearestDesc.magFilter = render::render_sampler_filter_t::NEAREST;
    nearestDesc.addressU = nearestDesc.addressV = render::render_sampler_address_t::CLAMP_TO_EDGE;
    render::render_sampler_handle_t nearest{};
    REQUIRE( render::R_CreateSampler( nearestDesc, &nearest ) == render::render_error_t::OK );
    nearestDesc.compare = render::render_sampler_compare_t::LESS_EQUAL;
    render::render_sampler_handle_t compare{};
    REQUIRE( render::R_CreateSampler( nearestDesc, &compare ) == render::render_error_t::OK );

    render::render_texture_desc_t targetDesc{};
    targetDesc.width = extent.width;
    targetDesc.height = extent.height;
    targetDesc.sRGB = false;
    targetDesc.generateMips = false;
    targetDesc.repeat = false;
    targetDesc.renderTarget = true;
    targetDesc.format = render::render_texture_format_t::RGBA8;
    render::render_texture_handle_t color{};
    REQUIRE( render::R_CreateTexture2D( targetDesc, {}, &color ) == render::render_error_t::OK );
    targetDesc.format = render::render_texture_format_t::D24;
    render::render_texture_handle_t sceneDepth{}, shadowDepth{};
    REQUIRE( render::R_CreateTexture2D( targetDesc, {}, &sceneDepth ) == render::render_error_t::OK );
    REQUIRE( render::R_CreateTexture2D( targetDesc, {}, &shadowDepth ) == render::render_error_t::OK );
    render::render_target_handle_t sceneTarget{}, shadowTarget{};
    REQUIRE( render::R_CreateRenderTarget( { color, sceneDepth }, &sceneTarget ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateRenderTarget( { {}, shadowDepth }, &shadowTarget ) ==
        render::render_error_t::OK );

    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    render::render_target_pass_desc_t pass{};
    pass.target = sceneTarget;
    pass.clearColor = { 0.0f, 0.0f, 1.0f, 1.0f };
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = maskPipeline;
    draw.vertexInput = input;
    draw.indexCount = 3u;
    draw.vertexCount = 6u;
    draw.sampledTextures[0] = alpha;
    draw.samplers[0] = nearest;
    draw.uniformBuffers[0] = maskUniform;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );

    const auto readScene = [&]( const common::u32 x ) {
        std::array<common::u8, 4u> pixel{};
        REQUIRE( render::R_ReadTargetRGBA8( sceneTarget,
            { x, extent.height / 2u, 1u, 1u }, pixel.data(), pixel.size(), 4u ) ==
            render::render_error_t::OK );
        return pixel;
    };
    const common::u32 leftX = extent.width / 4u;
    const common::u32 rightX = extent.width * 3u / 4u;
    const auto cutout = readScene( leftX );
    const auto opaque = readScene( rightX );
    CAPTURE( cutout[0], cutout[1], cutout[2], opaque[0], opaque[1], opaque[2] );
    CHECK( cutout[0] <= 15u );
    CHECK( cutout[1] <= 15u );
    CHECK( cutout[2] >= 240u );
    CHECK( opaque[0] >= 240u );
    CHECK( opaque[1] <= 15u );
    CHECK( opaque[2] <= 15u );

    // Preserve both attachments. The later depth-tested probe passes through
    // discarded texels and fails behind surviving cutout fragments.
    pass.clearFlags = 0u;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = probePipeline;
    draw.firstIndex = 3u;
    draw.sampledTextures[0] = {};
    draw.samplers[0] = {};
    draw.uniformBuffers[0] = {};
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    const auto uncovered = readScene( leftX );
    const auto blocked = readScene( rightX );
    CAPTURE( uncovered[0], uncovered[1], uncovered[2], blocked[0], blocked[1], blocked[2] );
    CHECK( uncovered[0] <= 15u );
    CHECK( uncovered[1] >= 240u );
    CHECK( uncovered[2] <= 15u );
    CHECK( blocked[0] >= 240u );
    CHECK( blocked[1] <= 15u );
    CHECK( blocked[2] <= 15u );

    // Reuse the cutout shader in a depth-only light pass. A receiver at depth
    // 0.5 is lit where alpha discarded and shadowed where depth 0.25 survived.
    pass = {};
    pass.target = shadowTarget;
    pass.clearFlags = render::R_CLEAR_DEPTH;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = maskDepthPipeline;
    draw.firstIndex = 0u;
    draw.sampledTextures[0] = alpha;
    draw.samplers[0] = nearest;
    draw.uniformBuffers[0] = maskUniform;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );

    pass = {};
    pass.target = sceneTarget;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = shadowPipeline;
    draw.firstIndex = 3u;
    draw.sampledTextures[0] = shadowDepth;
    draw.samplers[0] = compare;
    draw.uniformBuffers[0] = {};
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    const auto lit = readScene( leftX );
    const auto shadowed = readScene( rightX );
    CAPTURE( lit[0], lit[1], lit[2], shadowed[0], shadowed[1], shadowed[2] );
    CHECK( lit[0] >= 240u );
    CHECK( lit[1] >= 240u );
    CHECK( lit[2] >= 240u );
    CHECK( shadowed[0] <= 15u );
    CHECK( shadowed[1] <= 15u );
    CHECK( shadowed[2] <= 15u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyRenderTarget( shadowTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyRenderTarget( sceneTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( shadowDepth ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( sceneDepth ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( color ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroySampler( compare ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroySampler( nearest ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( alpha ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( shadowPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( probePipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( maskDepthPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( maskPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( maskUniform ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( ib ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shadowShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( probeShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( maskShader ) == render::render_error_t::OK );
}

void CheckShadowPassLighting( const render::render_extent_t extent )
{
    // In light clip space the left triangle writes depth 0.5. A fullscreen
    // receiver at depth 0.75 then samples that texel through slot 1, while
    // the right side samples a clear-depth texel and remains lit.
    constexpr char vertex[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "layout(std140) uniform LightConstants { mat4 lightVP; };\n"
        "layout(std140) uniform ObjectConstants { mat4 model; };\n"
        "out vec3 worldPosition;\n"
        "void main() { vec4 world=model*vec4(position,1.0);"
        " worldPosition=world.xyz; gl_Position=lightVP*world; }\n";
    constexpr char depthFragment[] = "#version 410 core\nvoid main() {}\n";
    constexpr char litFragment[] =
        "#version 410 core\nin vec3 worldPosition;\n"
        "layout(std140) uniform LightConstants { mat4 lightVP; };\n"
        "uniform sampler2D albedo; uniform sampler2DShadow shadowMap;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { vec4 clip=lightVP*vec4(worldPosition,1.0);"
        " vec3 ndc=clip.xyz/clip.w; vec2 uv=ndc.xy*0.5+0.5;"
        " float reference=ndc.z*0.5+0.5-0.00025;"
        " float visible=texture(shadowMap,vec3(uv,reference));"
        " float base=texture(albedo,vec2(0.5)).r;"
        " color=vec4(vec3(base*(0.2+0.7*visible)),1.0); }\n";
    const auto makeShader = [&]( const char *fragment, const common::u64 bytes ) {
        common::cooked_shader_view_t source{};
        source.nLanguageVersion = 410u;
        source.nStages = 2u;
        source.stages[0].stage = common::render_shader_stage_t::VERTEX;
        source.stages[0].code = { reinterpret_cast<const common::byte *>( vertex ), sizeof( vertex ) };
        source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        source.stages[1].code = { reinterpret_cast<const common::byte *>( fragment ), bytes };
        render::render_shader_handle_t result{};
        REQUIRE( render::R_CreateShader( { &source, "Shadow pixel oracle" }, &result ) ==
            render::render_error_t::OK );
        return result;
    };
    const auto depthShader = makeShader( depthFragment, sizeof( depthFragment ) );
    const auto litShader = makeShader( litFragment, sizeof( litFragment ) );

    const float vertices[]{
        -1.0f,-1.0f,0.0f, 0.0f,-1.0f,0.0f, -1.0f,1.0f,0.0f,
        -1.0f,-1.0f,0.5f, 3.0f,-1.0f,0.5f, -1.0f,3.0f,0.5f
    };
    const common::u16 indices[]{ 0u, 1u, 2u, 3u, 4u, 5u };
    const float identity[]{
        1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f,
        0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f
    };
    const auto makeBuffer = []( const void *bytes, const common::u64 size,
                                const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( desc, &data,
            &result ) == render::render_error_t::OK );
        return result;
    };
    const auto vb = makeBuffer( vertices, sizeof( vertices ), render::R_BUFFER_USAGE_VERTEX );
    const auto ib = makeBuffer( indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    const auto lightBuffer = makeBuffer( identity, sizeof( identity ), render::R_BUFFER_USAGE_UNIFORM );
    const auto objectBuffer = makeBuffer( identity, sizeof( identity ), render::R_BUFFER_USAGE_UNIFORM );

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
    pipelineDesc.shader = depthShader;
    pipelineDesc.vertexLayout = inputDesc.layout;
    pipelineDesc.colorWrite = false;
    pipelineDesc.uniformBlockCount = 2u;
    pipelineDesc.uniformBlocks[0] = { "LightConstants", sizeof( identity ) };
    pipelineDesc.uniformBlocks[1] = { "ObjectConstants", sizeof( identity ) };
    render::render_pipeline_handle_t depthPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &depthPipeline ) ==
        render::render_error_t::OK );
    pipelineDesc.shader = litShader;
    pipelineDesc.colorWrite = true;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.sampledTextureCount = 2u;
    pipelineDesc.sampledTextures[0] = { "albedo",
        render::render_sampled_texture_kind_t::SCALAR_2D };
    pipelineDesc.sampledTextures[1] = { "shadowMap",
        render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D };
    render::render_pipeline_handle_t litPipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &litPipeline ) ==
        render::render_error_t::OK );

    render::render_texture_desc_t targetDesc{};
    targetDesc.width = extent.width;
    targetDesc.height = extent.height;
    targetDesc.sRGB = false;
    targetDesc.generateMips = false;
    targetDesc.repeat = false;
    targetDesc.renderTarget = true;
    targetDesc.format = render::render_texture_format_t::D24;
    render::render_texture_handle_t depth{};
    REQUIRE( render::R_CreateTexture2D( targetDesc, {}, &depth ) == render::render_error_t::OK );
    targetDesc.format = render::render_texture_format_t::RGBA8;
    render::render_texture_handle_t color{};
    REQUIRE( render::R_CreateTexture2D( targetDesc, {}, &color ) == render::render_error_t::OK );
    render::render_target_handle_t depthTarget{}, colorTarget{};
    REQUIRE( render::R_CreateRenderTarget( { {}, depth }, &depthTarget ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateRenderTarget( { color, {} }, &colorTarget ) ==
        render::render_error_t::OK );

    constexpr common::u8 whitePixel[4]{ 255u, 255u, 255u, 255u };
    render::render_texture_desc_t whiteDesc{};
    whiteDesc.width = 1u;
    whiteDesc.height = 1u;
    whiteDesc.sRGB = false;
    whiteDesc.generateMips = false;
    whiteDesc.repeat = false;
    render::render_texture_handle_t white{};
    REQUIRE( render::R_CreateTexture2D( whiteDesc, { whitePixel, sizeof( whitePixel ) },
        &white ) == render::render_error_t::OK );
    render::render_sampler_desc_t compareDesc{};
    compareDesc.minFilter = compareDesc.magFilter = render::render_sampler_filter_t::NEAREST;
    compareDesc.addressU = compareDesc.addressV = render::render_sampler_address_t::CLAMP_TO_EDGE;
    compareDesc.compare = render::render_sampler_compare_t::LESS_EQUAL;
    render::render_sampler_handle_t compare{};
    REQUIRE( render::R_CreateSampler( compareDesc, &compare ) == render::render_error_t::OK );

    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    render::render_target_pass_desc_t pass{};
    pass.target = depthTarget;
    pass.clearFlags = render::R_CLEAR_DEPTH;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = depthPipeline;
    draw.vertexInput = input;
    draw.uniformBuffers[0] = lightBuffer;
    draw.uniformBuffers[1] = objectBuffer;
    draw.indexCount = 3u;
    draw.vertexCount = 6u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );

    pass = {};
    pass.target = colorTarget;
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    draw.pipeline = litPipeline;
    draw.firstIndex = 3u;
    draw.sampledTextures[0] = white;
    draw.sampledTextures[1] = depth;
    draw.samplers[1] = compare;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );

    common::byte shadowed[4]{};
    common::byte lit[4]{};
    REQUIRE( render::R_ReadTargetRGBA8( colorTarget,
        { extent.width / 4u, extent.height / 4u, 1u, 1u },
        shadowed, sizeof( shadowed ), 4u ) == render::render_error_t::OK );
    REQUIRE( render::R_ReadTargetRGBA8( colorTarget,
        { extent.width * 3u / 4u, extent.height / 4u, 1u, 1u },
        lit, sizeof( lit ), 4u ) == render::render_error_t::OK );
    CHECK( shadowed[0] <= 80u );
    CHECK( lit[0] >= 180u );
    CHECK( lit[0] > shadowed[0] + 100u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    REQUIRE( render::R_DestroyRenderTarget( colorTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyRenderTarget( depthTarget ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroySampler( compare ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( white ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( color ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( litPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( depthPipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( objectBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( lightBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( ib ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vb ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( litShader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( depthShader ) == render::render_error_t::OK );
}

void CheckCookedMaterialBundleDrawing( const render::render_extent_t extent )
{
    const auto materialPath = common::StringView_FromCString( "materials/native.cymat_c" );
    const auto shaderPath = common::StringView_FromCString( "shaders/native.cyshader_c" );
    const auto texturePath = common::StringView_FromCString( "textures/native.cytex_c" );
    const auto bindingName = common::StringView_FromCString( "AlbedoMap" );

    constexpr char vertexSource[] =
        "#version 410 core\n"
        "layout(location=0) in vec3 position;\n"
        "void main(){ gl_Position=vec4(position,1.0); }\n";
    constexpr char fragmentSource[] =
        "#version 410 core\n"
        "uniform sampler2D AlbedoMap;\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){ color=texture(AlbedoMap,vec2(0.5)); }\n";
    common::cooked_shader_binding_source_t sampledImage{};
    sampledImage.name = bindingName;
    sampledImage.kind = common::render_shader_binding_kind_t::SAMPLED_TEXTURE;
    sampledImage.resourceType = common::render_shader_resource_type_t::TEXTURE_2D;
    sampledImage.stageMask = common::RENDER_SHADER_STAGE_MASK_FRAGMENT;
    sampledImage.flags = common::COOKED_SHADER_BINDING_FLAG_REQUIRED |
        common::COOKED_SHADER_BINDING_FLAG_MATERIAL;
    REQUIRE( common::CookedShader_MakeLogicalBindingId(
        bindingName, &sampledImage.nLogicalBinding ) );
    const common::cooked_shader_interface_source_t shaderInterface{
        { &sampledImage, 1u }
    };
    common::content_hash_t interfaceHash{};
    REQUIRE( common::CookedShader_ComputeInterfaceHash(
        shaderInterface, &interfaceHash ) );
    common::cooked_shader_stage_source_t stages[2]{};
    stages[0].stage = common::render_shader_stage_t::VERTEX;
    stages[0].code = {
        reinterpret_cast<const common::byte *>( vertexSource ), sizeof( vertexSource )
    };
    stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    stages[1].code = {
        reinterpret_cast<const common::byte *>( fragmentSource ), sizeof( fragmentSource )
    };
    std::vector<common::byte> shaderBytes( common::CookedShader_RequiredSizeV3(
        {}, { stages, 2u }, shaderInterface ) );
    REQUIRE_FALSE( shaderBytes.empty() );
    REQUIRE( common::CookedShader_Succeeded( common::CookedShader_WriteV3(
        {}, { stages, 2u }, shaderInterface,
        common::ContentHash_String( common::StringView_FromCString(
            "shaders/native.cyshader" ) ),
        { shaderBytes.data(), shaderBytes.size() } ) ) );

    constexpr common::byte redPixel[4]{ 255u, 0u, 0u, 255u };
    common::cooked_texture_desc_t textureDescription{};
    textureDescription.pixelFormat = common::render_texture_pixel_format_t::RGBA8_UNORM;
    textureDescription.colorSpace = common::render_texture_color_space_t::LINEAR;
    textureDescription.nWidth = 1u;
    textureDescription.nHeight = 1u;
    textureDescription.nMipLevels = 1u;
    const common::cooked_texture_mip_source_t mip{
        1u, 1u, 1u, 4u, { redPixel, sizeof( redPixel ) }
    };
    std::vector<common::byte> textureBytes( common::CookedTexture_RequiredSize(
        textureDescription, { &mip, 1u } ) );
    REQUIRE_FALSE( textureBytes.empty() );
    REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Write(
        textureDescription, { &mip, 1u },
        common::ContentHash_String( common::StringView_FromCString(
            "textures/native.cytex" ) ),
        { textureBytes.data(), textureBytes.size() } ) ) );

    common::cooked_material_texture_source_v2_t materialTexture{};
    materialTexture.binding = bindingName;
    materialTexture.texture = common::StringView_FromCString( "textures/native.cytex" );
    materialTexture.nLogicalBinding = sampledImage.nLogicalBinding;
    common::cooked_material_source_v2_t materialDescription{};
    materialDescription.shader = common::StringView_FromCString( "shaders/native.cyshader" );
    materialDescription.shaderInterfaceHash = interfaceHash;
    materialDescription.textures = { &materialTexture, 1u };
    std::vector<common::byte> materialBytes( common::CookedMaterial_RequiredSizeV2(
        materialDescription ) );
    REQUIRE_FALSE( materialBytes.empty() );
    REQUIRE( common::CookedMaterial_Succeeded( common::CookedMaterial_WriteV2(
        materialDescription,
        common::ContentHash_String( common::StringView_FromCString(
            "materials/native.cymat" ) ),
        { materialBytes.data(), materialBytes.size() } ) ) );

    struct memory_file_t {
        common::string_view_t path{};
        common::binary_block_t contents{};
    };
    struct memory_vfs_t {
        memory_file_t files[3]{};
    } memory{};
    memory.files[0] = { materialPath, { materialBytes.data(), materialBytes.size() } };
    memory.files[1] = { shaderPath, { shaderBytes.data(), shaderBytes.size() } };
    memory.files[2] = { texturePath, { textureBytes.data(), textureBytes.size() } };
    static const common::vfs_ops_t operations{
        []( void *userData, common::string_view_t path, common::usize maximum,
            common::blob_t *destination ) noexcept -> common::vfs_status_t {
            const auto &files = *static_cast<const memory_vfs_t *>( userData );
            for ( const auto &file : files.files ) {
                if ( !common::StringView_Equals( path, file.path ) ) continue;
                if ( file.contents.cbSize > maximum ) return common::vfs_status_t::SIZE_LIMIT;
                return common::Blob_Assign( destination, file.contents )
                    ? common::vfs_status_t::OK : common::vfs_status_t::OUT_OF_MEMORY;
            }
            return common::vfs_status_t::NOT_FOUND;
        }, nullptr, nullptr, nullptr
    };
    const common::vfs_t vfs{ &operations, &memory, common::VFS_CAPABILITY_READ_ALL };
    resource::render_asset_loader_context_t loader{};
    REQUIRE( resource::Res_InitRenderAssetLoader(
        &loader, resource::Res_DefaultRenderAssetLoaderConfig( &vfs ) ) );
    resource::resource_manager_t resources{};
    auto resourceConfig = resource::Res_DefaultConfig();
    resourceConfig.cResourceCapacity = 8u;
    resourceConfig.cTypeCapacity = 3u;
    REQUIRE( resource::Res_Init( &resources, resourceConfig ) == resource::resource_error_t::OK );
    REQUIRE( resource::Res_RegisterRenderAssetLoaders(
        &resources, &loader ) == resource::resource_error_t::OK );

    render::render_vertex_layout_t layout{};
    layout.bindingCount = 1u;
    layout.bindings[0].stride = 3u * sizeof( float );
    layout.attributeCount = 1u;
    layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    render::render_material_bundle_desc_t bundleDescription{};
    bundleDescription.pResources = &resources;
    bundleDescription.cookedMaterialPath = materialPath;
    bundleDescription.vertexLayout = layout;
    bundleDescription.debugName = "Native cooked material smoke";
    render::render_material_bundle_t bundle{};
    const auto adopted = render::R_CreateCookedMaterialBundle(
        bundleDescription, &bundle );
    REQUIRE( adopted.status == render::render_material_bridge_status_t::OK );
    CHECK( resource::Res_GetStats( &resources ).cLiveResources == 0u );
    REQUIRE( resource::Res_Shutdown( &resources ) == resource::resource_error_t::OK );
    std::memset( materialBytes.data(), 0, materialBytes.size() );
    std::memset( shaderBytes.data(), 0, shaderBytes.size() );
    std::memset( textureBytes.data(), 0, textureBytes.size() );

    constexpr float vertices[9]{ -1.0f, -1.0f, 0.0f, 3.0f, -1.0f, 0.0f,
                                 -1.0f, 3.0f, 0.0f };
    constexpr common::u16 indices[3]{ 0u, 1u, 2u };
    render::render_static_mesh_source_t meshSource{};
    meshSource.vertexLayout = layout;
    meshSource.vertexBytes = vertices;
    meshSource.vertexByteSize = sizeof( vertices );
    meshSource.vertexCount = 3u;
    meshSource.indexBytes = indices;
    meshSource.indexByteSize = sizeof( indices );
    meshSource.indexCount = 3u;
    render::render_static_mesh_bundle_t mesh{};
    REQUIRE( render::R_CreateStaticMeshBundle( meshSource, &mesh ).status ==
        render::render_static_mesh_bridge_status_t::OK );

    render::render_draw_indexed_desc_t draw{};
    REQUIRE( render::R_ApplyCookedMaterialBundleToDraw( bundle, &draw ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_ApplyStaticMeshBundleToDraw( mesh, &draw ) ==
        render::render_error_t::OK );
    render::render_frame_info_t frame{};
    frame.drawableExtent = extent;
    frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    frame.clearColor = { 0.0f, 0.0f, 1.0f, 1.0f };
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    common::byte center[4]{};
    glReadPixels( static_cast<GLint>( extent.width / 2u ),
        static_cast<GLint>( extent.height / 2u ), 1, 1,
        GL_RGBA, GL_UNSIGNED_BYTE, center );
    CHECK( glGetError() == GL_NO_ERROR );
    CHECK( center[0] > 240u );
    CHECK( center[1] < 16u );
    CHECK( center[2] < 16u );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );

    const auto shader = bundle.shader;
    const auto pipeline = bundle.pipeline;
    const auto texture = bundle.textures[0];
    REQUIRE( render::R_DestroyCookedMaterialBundle( &bundle ) == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsShaderValid( shader ) );
    CHECK_FALSE( render::R_IsGraphicsPipelineValid( pipeline ) );
    CHECK_FALSE( render::R_IsTextureValid( texture ) );
    REQUIRE( render::R_DestroyStaticMeshBundle( &mesh ) == render::render_error_t::OK );
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
    CheckHDRTargetToneMap( info->drawableExtent );
    CheckHDRTranslucentCompositing( info->drawableExtent );
    CheckAlphaMaskCutout( info->drawableExtent );
    CheckShadowPassLighting( info->drawableExtent );
    CheckCookedMaterialBundleDrawing( info->drawableExtent );

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

TEST_CASE( "Standalone OpenGL keeps sRGB conversion off when not requested",
           "[CypherRender][OpenGL][Smoke]" )
{
    const std::filesystem::path userPath = std::filesystem::temp_directory_path() /
        ( "cypher_render_opengl_srgb_" + std::to_string( sys::Sys_GetCurrentProcessId() ) );
    const std::string userPathString = userPath.string();
    const char *arguments[] = {
        "cypher_render_opengl_srgb_smoke_tests", "-basedir", ".",
        "-userpath", userPathString.c_str()
    };
    const sys::init_info_t initInfo{ 5, arguments,
        "CypherRenderOpenGLSRGBSmokeTests", "CypherTests" };
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
    windowDescription.title = "CypherRender OpenGL sRGB Policy Smoke Test";
    windowDescription.width = 64u;
    windowDescription.height = 64u;
    windowDescription.flags = sys::SYS_WINDOW_HIDDEN;
    REQUIRE( render::R_ConfigureWindow( config, windowDescription ) ==
        render::render_error_t::OK );
    // Request a capable visual independently of the renderer policy. Some
    // drivers provide one even when not requested, but this makes the original
    // capability-versus-policy regression observable where supported.
    windowDescription.openGL.sRGBFramebuffer = true;

    sys::window_t window{};
    if ( sys::Sys_CreateWindow( windowDescription, window ) != sys::sys_error_t::OK ) {
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host has no sRGB-capable OpenGL window system" );
    }
    if ( render::R_Init( window, config ) != render::render_error_t::OK ) {
        REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host cannot initialize an sRGB-capable OpenGL 4.1 context" );
    }

    const render::render_info_t *info = render::R_GetInfo();
    REQUIRE( info != nullptr );
    if ( ( info->capabilities & render::R_CAPABILITY_SRGB_FRAMEBUFFER ) == 0u ) {
        REQUIRE( render::R_Shutdown() == render::render_error_t::OK );
        REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host did not report an sRGB-capable default framebuffer" );
    }

    CHECK( glIsEnabled( GL_FRAMEBUFFER_SRGB ) == GL_FALSE );
    glEnable( GL_FRAMEBUFFER_SRGB );
    REQUIRE( glGetError() == GL_NO_ERROR );
    REQUIRE( glIsEnabled( GL_FRAMEBUFFER_SRGB ) == GL_TRUE );
    render::render_frame_info_t frame{};
    frame.drawableExtent = info->drawableExtent;
    frame.clearFlags = render::R_CLEAR_COLOR;
    frame.clearColor = { 0.25f, 0.25f, 0.25f, 1.0f };
    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    CHECK( glIsEnabled( GL_FRAMEBUFFER_SRGB ) == GL_FALSE );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    CHECK( glGetError() == GL_NO_ERROR );

    REQUIRE( render::R_Shutdown() == render::render_error_t::OK );
    REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
    std::error_code cleanupError{};
    std::filesystem::remove_all( userPath, cleanupError );
}
