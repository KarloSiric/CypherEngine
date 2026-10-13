//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Checks frontend draw validation and pipeline dependency lifetimes
//          with a deterministic backend, without a native graphics context.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Draw.h"
#include "CypherRender/CypherRender_Local.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <utility>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;
using draw_error_t = render::render_error_t;

namespace
{
struct draw_fixture_t {
    render::backend_api_t backend{};
    common::u64 nextToken{ 1u };
    common::u32 drawCalls{ 0u };
    common::u32 pipelineCreates{ 0u };
    common::u32 pipelineDestroys{ 0u };
    common::u32 shaderDestroys{ 0u };
    draw_error_t pipelineResult{ draw_error_t::OK };
    draw_error_t pipelineDestroyResult{ draw_error_t::OK };
    draw_error_t drawResult{ draw_error_t::OK };
    render::render_primitive_topology_t lastPipelineTopology{ render::render_primitive_topology_t::TRIANGLE_LIST };
    render::backend_draw_indexed_desc_t lastDraw{};
    common::byte mappedBytes[256]{};
    render::render_shader_handle_t shader{};
    render::render_buffer_handle_t vertices{};
    render::render_buffer_handle_t instances{};
    render::render_buffer_handle_t indices{};
    render::render_vertex_input_handle_t input{};
    render::render_pipeline_handle_t pipeline{};
    render::render_vertex_layout_t layout{};

    void Init()
    {
        backend.CreateBuffer = []( const render::render_buffer_desc_t &,
            const render::render_buffer_data_t *, render::backend_buffer_t &out, void *state ) noexcept {
            out.value = static_cast<draw_fixture_t *>( state )->nextToken++;
            return draw_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void * ) noexcept { return draw_error_t::OK; };
        backend.MapBuffer = []( render::backend_buffer_t, const render::render_buffer_range_t &range,
            render::render_buffer_map_flags_t, render::render_buffer_mapping_t &out, void *state ) noexcept {
            out = { static_cast<draw_fixture_t *>( state )->mappedBytes, range.byteSize };
            return draw_error_t::OK;
        };
        backend.UnmapBuffer = []( render::backend_buffer_t, void * ) noexcept { return draw_error_t::OK; };
        backend.CreateVertexInput = []( const render::backend_vertex_input_desc_t &,
            render::backend_vertex_input_t &out, void *state ) noexcept {
            out.value = static_cast<draw_fixture_t *>( state )->nextToken++;
            return draw_error_t::OK;
        };
        backend.DestroyVertexInput = []( render::backend_vertex_input_t, void * ) noexcept { return draw_error_t::OK; };
        backend.CreateShader = []( const render::render_shader_desc_t &,
            render::backend_shader_t &out, void *state ) noexcept {
            out.value = static_cast<draw_fixture_t *>( state )->nextToken++;
            return draw_error_t::OK;
        };
        backend.DestroyShader = []( render::backend_shader_t, void *state ) noexcept {
            ++static_cast<draw_fixture_t *>( state )->shaderDestroys;
            return draw_error_t::OK;
        };
        backend.CreateGraphicsPipeline = []( const render::backend_pipeline_desc_t &description,
            render::backend_pipeline_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<draw_fixture_t *>( state );
            ++fixture.pipelineCreates;
            fixture.lastPipelineTopology = description.topology;
            out = {};
            if ( fixture.pipelineResult == draw_error_t::OK ) out.value = fixture.nextToken++;
            return fixture.pipelineResult;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t, void *state ) noexcept {
            auto &fixture = *static_cast<draw_fixture_t *>( state );
            ++fixture.pipelineDestroys;
            return fixture.pipelineDestroyResult;
        };
        backend.DrawIndexed = []( const render::backend_draw_indexed_desc_t &description, void *state ) noexcept {
            auto &fixture = *static_cast<draw_fixture_t *>( state );
            ++fixture.drawCalls;
            fixture.lastDraw = description;
            return fixture.drawResult;
        };
        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.capabilities = render::R_CAPABILITY_INSTANCING;
        render::tr.info.limits.maxUniformBlockBytes = 16384u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        REQUIRE( render::R_BufferSystemInit() == draw_error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == draw_error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == draw_error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == draw_error_t::OK );

        constexpr char source[] = "#version 410 core\nvoid main() {}\n";
        common::cooked_shader_view_t view{};
        view.nLanguageVersion = 410u;
        view.nStages = 2u;
        view.stages[0].stage = common::render_shader_stage_t::VERTEX;
        view.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        for ( auto &stage : view.stages ) {
            stage.code = { reinterpret_cast<const common::byte *>( source ), sizeof( source ) };
        }
        REQUIRE( render::R_CreateShader( { &view, "Mock draw shader" }, &shader ) == draw_error_t::OK );
        layout.bindingCount = 1u;
        layout.bindings[0].stride = 16u;
        layout.attributeCount = 2u;
        layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
        layout.attributes[1] = { render::render_format_t::RGBA8_UNORM, 12u, 1u, 0u };
    }

    render::render_buffer_handle_t Buffer( common::u64 bytes, render::render_buffer_usage_flags_t usage )
    {
        render::render_buffer_desc_t description{};
        description.byteSize = bytes;
        description.usage = usage;
        description.memory = render::render_buffer_memory_t::UPLOAD;
        description.updatePolicy = render::render_buffer_update_t::DYNAMIC;
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( description, nullptr, &result ) == draw_error_t::OK );
        return result;
    }

    render::render_pipeline_desc_t PipelineDescription( common::u32 uniformBytes = 0u ) const
    {
        render::render_pipeline_desc_t description{};
        description.shader = shader;
        description.vertexLayout = layout;
        if ( uniformBytes != 0u ) {
            description.uniformBlockCount = 1u;
            description.uniformBlocks[0] = { "Transform", uniformBytes };
        }
        return description;
    }

    void Geometry( common::u32 uniformBytes = 0u )
    {
        vertices = Buffer( 64u, render::R_BUFFER_USAGE_VERTEX ); // 16-byte prefix, three vertices.
        indices = Buffer( 12u, render::R_BUFFER_USAGE_INDEX ); // 4-byte prefix, four indices.
        render::render_vertex_input_desc_t description{};
        description.layout = layout;
        description.vertexBufferCount = 1u;
        description.vertexBuffers[0] = { vertices, 16u, 0u };
        description.indexBuffer = { indices, 4u, render::render_index_type_t::UINT16 };
        REQUIRE( render::R_CreateVertexInput( description, &input ) == draw_error_t::OK );
        REQUIRE( render::R_CreateGraphicsPipeline( PipelineDescription( uniformBytes ), &pipeline ) == draw_error_t::OK );
    }

    render::render_draw_indexed_desc_t Draw() const
    {
        render::render_draw_indexed_desc_t draw{};
        draw.pipeline = pipeline;
        draw.vertexInput = input;
        draw.firstIndex = 1u;
        draw.indexCount = 3u;
        draw.vertexCount = 3u;
        return draw;
    }

    ~draw_fixture_t()
    {
        // Destructors still run if a fatal assertion unwinds a test body.
        render::tr.frameActive = false;
        (void)render::R_PipelineSystemShutdown();
        (void)render::R_VertexInputSystemShutdown();
        (void)render::R_ShaderSystemShutdown();
        (void)render::R_BufferSystemShutdown();
        render::tr = {};
    }
};
} // namespace

TEST_CASE( "pipelines retain shaders, roll back failed creation, and invalidate stale handles", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    fixture.pipelineResult = draw_error_t::ERR_SHADER_INTERFACE_MISMATCH;
    render::render_pipeline_handle_t failed{ 99u };
    CHECK( render::R_CreateGraphicsPipeline( fixture.PipelineDescription(), &failed ) == draw_error_t::ERR_SHADER_INTERFACE_MISMATCH );
    CHECK( failed.value == 0u );
    // A failed pipeline must not leave a shader reference behind.
    CHECK( render::R_DestroyShader( fixture.shader ) == draw_error_t::OK );
    CHECK( fixture.shaderDestroys == 1u );
}

TEST_CASE( "live pipeline ownership is released only after native destruction", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    const auto description = fixture.PipelineDescription( 64u );
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::OK );
    CHECK( render::R_IsGraphicsPipelineValid( pipeline ) );
    CHECK( render::R_DestroyShader( fixture.shader ) == draw_error_t::ERR_RESOURCE_BUSY );
    render::render_pipeline_info_t info{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( pipeline, &info ) == draw_error_t::OK );
    CHECK( info.shader.value == fixture.shader.value );
    CHECK( info.uniformBlockCount == 1u );
    CHECK( info.uniformBlockBytes[0] == 64u );
    fixture.pipelineDestroyResult = draw_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyGraphicsPipeline( pipeline ) == draw_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsGraphicsPipelineValid( pipeline ) );
    CHECK( render::R_DestroyShader( fixture.shader ) == draw_error_t::ERR_RESOURCE_BUSY );
    fixture.pipelineDestroyResult = draw_error_t::OK;
    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == draw_error_t::OK );
    CHECK_FALSE( render::R_IsGraphicsPipelineValid( pipeline ) );
    CHECK( render::R_GetGraphicsPipelineInfo( pipeline, &info ) == draw_error_t::ERR_STALE_HANDLE );
    CHECK( render::R_DestroyGraphicsPipeline( pipeline ) == draw_error_t::ERR_STALE_HANDLE );
    CHECK( fixture.pipelineDestroys == 2u );
    CHECK( render::R_DestroyShader( fixture.shader ) == draw_error_t::OK );
}

TEST_CASE( "pipeline validation checks block shape and instancing capability", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    auto description = fixture.PipelineDescription();
    render::render_pipeline_handle_t pipeline{};
    description.uniformBlockCount = 1u;
    description.uniformBlocks[0].name = "Transform";
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description.uniformBlocks[0].name = nullptr;
    description.uniformBlocks[0].byteSize = 64u;
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description = fixture.PipelineDescription();
    description.uniformBlockCount = render::R_MAX_UNIFORM_BLOCK_BINDINGS + 1u;
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description = fixture.PipelineDescription( 64u );
    description.uniformBlockCount = 2u;
    description.uniformBlocks[1] = { "Transform", 32u };
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description.uniformBlocks[1] = { "Material", 0u };
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description = fixture.PipelineDescription();
    description.uniformBlocks[1] = { "Undeclared", 16u };
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description = fixture.PipelineDescription();
    description.sampledTextureCount = render::R_MAX_SAMPLED_TEXTURE_BINDINGS + 1u;
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description = fixture.PipelineDescription();
    description.sampledTextureCount = 2u;
    description.sampledTextures[0].name = "base_color";
    description.sampledTextures[1].name = "base_color";
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    description = fixture.PipelineDescription( 16385u );
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED );
    description = fixture.PipelineDescription();
    description.vertexLayout.bindings[0].inputRate = render::render_vertex_input_rate_t::PER_INSTANCE;
    description.vertexLayout.bindings[0].instanceDivisor = 1u;
    render::tr.info.capabilities = render::R_CAPABILITY_NONE;
    CHECK( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::ERR_CAPABILITY_MISSING );
    CHECK( fixture.pipelineCreates == 0u );
    render::tr.info.capabilities = render::R_CAPABILITY_INSTANCING;
    REQUIRE( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::OK );
    CHECK( fixture.pipelineCreates == 1u );
    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == draw_error_t::OK );
    CHECK( render::R_DestroyShader( fixture.shader ) == draw_error_t::OK );
}

TEST_CASE( "pipeline info preserves two declared uniform block byte sizes",
           "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    auto description = fixture.PipelineDescription( 64u );
    description.uniformBlockCount = 2u;
    description.uniformBlocks[1] = { "Material", 32u };
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( description, &pipeline ) == draw_error_t::OK );
    render::render_pipeline_info_t info{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( pipeline, &info ) == draw_error_t::OK );
    CHECK( info.uniformBlockCount == 2u );
    CHECK( info.uniformBlockBytes[0] == 64u );
    CHECK( info.uniformBlockBytes[1] == 32u );
    CHECK( info.sampledTextureCount == 0u );
}

TEST_CASE( "draw validates full offset ranges and dispatches only during an active frame", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    fixture.Geometry();
    auto draw = fixture.Draw();
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_FRAME_NOT_ACTIVE );
    render::tr.frameActive = true;
    REQUIRE( render::R_DrawIndexed( draw ) == draw_error_t::OK );
    CHECK( fixture.drawCalls == 1u );
    CHECK( fixture.lastDraw.indexByteOffset == 6u );
    CHECK( fixture.lastDraw.indexCount == 3u );
    CHECK( fixture.lastDraw.vertexCount == 3u );
    CHECK( fixture.lastDraw.instanceCount == 1u );
    CHECK( fixture.lastDraw.indexType == render::render_index_type_t::UINT16 );
    draw.firstIndex = 2u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_INDEX_DATA_INVALID );
    draw.firstIndex = std::numeric_limits<common::u32>::max();
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_INDEX_DATA_INVALID );
    draw = fixture.Draw();
    draw.vertexCount = 4u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_VERTEX_LAYOUT_INVALID );
    draw = fixture.Draw();
    draw.indexCount = 2u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    draw.indexCount = 0u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    draw = fixture.Draw();
    draw.vertexCount = 0u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    CHECK( fixture.drawCalls == 1u );
}

TEST_CASE( "triangle and line pipelines validate their own indexed primitive counts", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    fixture.Geometry();
    CHECK( fixture.PipelineDescription().topology == render::render_primitive_topology_t::TRIANGLE_LIST );
    CHECK( fixture.lastPipelineTopology == render::render_primitive_topology_t::TRIANGLE_LIST );

    render::render_pipeline_info_t triangleInfo{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( fixture.pipeline, &triangleInfo ) == draw_error_t::OK );
    CHECK( triangleInfo.topology == render::render_primitive_topology_t::TRIANGLE_LIST );

    auto lineDescription = fixture.PipelineDescription();
    lineDescription.topology = static_cast<render::render_primitive_topology_t>( 255u );
    render::render_pipeline_handle_t invalidPipeline{ 99u };
    CHECK( render::R_CreateGraphicsPipeline( lineDescription, &invalidPipeline ) == draw_error_t::ERR_PIPELINE_DESC_INVALID );
    CHECK( invalidPipeline.value == 0u );
    CHECK( fixture.pipelineCreates == 1u ); // Invalid enum never reaches the backend.

    lineDescription.topology = render::render_primitive_topology_t::LINE_LIST;
    render::render_pipeline_handle_t linePipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( lineDescription, &linePipeline ) == draw_error_t::OK );
    CHECK( fixture.lastPipelineTopology == render::render_primitive_topology_t::LINE_LIST );
    render::render_pipeline_info_t lineInfo{};
    REQUIRE( render::R_GetGraphicsPipelineInfo( linePipeline, &lineInfo ) == draw_error_t::OK );
    CHECK( lineInfo.topology == render::render_primitive_topology_t::LINE_LIST );

    render::tr.frameActive = true;
    auto draw = fixture.Draw();
    draw.indexCount = 2u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    draw.indexCount = 3u;
    REQUIRE( render::R_DrawIndexed( draw ) == draw_error_t::OK );
    CHECK( fixture.drawCalls == 1u );

    draw.pipeline = linePipeline;
    draw.indexCount = 1u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    draw.indexCount = 3u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    draw.indexCount = 2u;
    REQUIRE( render::R_DrawIndexed( draw ) == draw_error_t::OK );
    CHECK( fixture.lastDraw.indexCount == 2u );
    draw.firstIndex = 0u;
    draw.indexCount = 4u;
    REQUIRE( render::R_DrawIndexed( draw ) == draw_error_t::OK );
    CHECK( fixture.lastDraw.indexCount == 4u );
    CHECK( fixture.drawCalls == 3u );

    render::tr.frameActive = false;
    REQUIRE( render::R_DestroyGraphicsPipeline( linePipeline ) == draw_error_t::OK );
}

TEST_CASE( "instanced draws validate the final divisor element and forward count", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    fixture.layout.bindingCount = 2u;
    fixture.layout.bindings[1].binding = 1u;
    fixture.layout.bindings[1].stride = 12u;
    fixture.layout.bindings[1].inputRate = render::render_vertex_input_rate_t::PER_INSTANCE;
    fixture.layout.bindings[1].instanceDivisor = 2u;
    fixture.layout.attributeCount = 3u;
    fixture.layout.attributes[2] = { render::render_format_t::RGB32_FLOAT, 0u, 2u, 1u };
    fixture.vertices = fixture.Buffer( 64u, render::R_BUFFER_USAGE_VERTEX );
    fixture.instances = fixture.Buffer( 24u, render::R_BUFFER_USAGE_VERTEX );
    fixture.indices = fixture.Buffer( 12u, render::R_BUFFER_USAGE_INDEX );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout = fixture.layout;
    inputDesc.vertexBufferCount = 2u;
    inputDesc.vertexBuffers[0] = { fixture.vertices, 16u, 0u };
    inputDesc.vertexBuffers[1] = { fixture.instances, 0u, 1u };
    inputDesc.indexBuffer = { fixture.indices, 4u, render::render_index_type_t::UINT16 };
    REQUIRE( render::R_CreateVertexInput( inputDesc, &fixture.input ) == draw_error_t::OK );
    REQUIRE( render::R_CreateGraphicsPipeline( fixture.PipelineDescription(), &fixture.pipeline ) == draw_error_t::OK );
    render::tr.frameActive = true;

    auto draw = fixture.Draw();
    draw.instanceCount = 4u; // Divisor two consumes precisely two 12-byte records.
    REQUIRE( render::R_DrawIndexed( draw ) == draw_error_t::OK );
    CHECK( fixture.lastDraw.instanceCount == 4u );
    CHECK( fixture.drawCalls == 1u );

    draw.instanceCount = 5u; // A third record would exceed the instance buffer.
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_VERTEX_LAYOUT_INVALID );
    draw.instanceCount = 0u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    draw.instanceCount = static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) + 1u;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_DRAW_DESC_INVALID );
    draw.instanceCount = 2u;
    render::tr.info.capabilities = render::R_CAPABILITY_NONE;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_CAPABILITY_MISSING );
    render::tr.info.capabilities = render::R_CAPABILITY_INSTANCING;
    render::render_buffer_mapping_t mapping{};
    REQUIRE( render::R_MapBuffer( fixture.instances, { 0u, 12u }, render::R_BUFFER_MAP_WRITE, &mapping ) == draw_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_UnmapBuffer( fixture.instances ) == draw_error_t::OK );
    CHECK( fixture.drawCalls == 1u );
}

TEST_CASE( "draw accepts reordered layout arrays but rejects mismatches and mapped geometry", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    fixture.Geometry();
    auto description = fixture.PipelineDescription();
    std::swap( description.vertexLayout.attributes[0], description.vertexLayout.attributes[1] );
    render::render_pipeline_handle_t reordered{};
    REQUIRE( render::R_CreateGraphicsPipeline( description, &reordered ) == draw_error_t::OK );
    auto draw = fixture.Draw();
    draw.pipeline = reordered;
    render::tr.frameActive = true;
    REQUIRE( render::R_DrawIndexed( draw ) == draw_error_t::OK );
    description.vertexLayout.attributes[0].format = render::render_format_t::RGBA8_UINT;
    render::render_pipeline_handle_t mismatched{};
    REQUIRE( render::R_CreateGraphicsPipeline( description, &mismatched ) == draw_error_t::OK );
    draw.pipeline = mismatched;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_BINDING_MISMATCH );
    draw = fixture.Draw();
    for ( const auto buffer : { fixture.vertices, fixture.indices } ) {
        render::render_buffer_mapping_t mapping{};
        REQUIRE( render::R_MapBuffer( buffer, { 0u, 4u }, render::R_BUFFER_MAP_WRITE, &mapping ) == draw_error_t::OK );
        CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_RESOURCE_BUSY );
        REQUIRE( render::R_UnmapBuffer( buffer ) == draw_error_t::OK );
    }
    draw.pipeline = fixture.vertices; // Same representation, wrong public object type.
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    CHECK( fixture.drawCalls == 1u );
}

TEST_CASE( "uniform validation and failed draws release temporary buffer references", "[CypherRender][Draw]" )
{
    draw_fixture_t fixture;
    fixture.Init();
    fixture.Geometry( 64u );
    render::tr.frameActive = true;
    auto draw = fixture.Draw();
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_BINDING_MISMATCH );
    draw.uniformBuffers[0] = fixture.vertices;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_BINDING_MISMATCH );
    const auto shortBuffer = fixture.Buffer( 63u, render::R_BUFFER_USAGE_UNIFORM );
    draw.uniformBuffers[0] = shortBuffer;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_BINDING_MISMATCH );
    CHECK( render::R_DestroyBuffer( shortBuffer ) == draw_error_t::OK );
    const auto uniform = fixture.Buffer( 64u, render::R_BUFFER_USAGE_UNIFORM );
    draw.uniformBuffers[0] = uniform;
    render::render_buffer_mapping_t mapping{};
    REQUIRE( render::R_MapBuffer( uniform, { 0u, 64u }, render::R_BUFFER_MAP_WRITE, &mapping ) == draw_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_UnmapBuffer( uniform ) == draw_error_t::OK );
    fixture.drawResult = draw_error_t::ERR_SUBMISSION_FAILED;
    CHECK( render::R_DrawIndexed( draw ) == draw_error_t::ERR_SUBMISSION_FAILED );
    CHECK( fixture.drawCalls == 1u );
    CHECK( fixture.lastDraw.uniformBuffers[0].value != 0u );
    CHECK( render::R_DestroyBuffer( uniform ) == draw_error_t::OK );
}
