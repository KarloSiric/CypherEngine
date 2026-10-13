//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Exercises bounded draw recording against a deterministic backend.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_DrawQueue.h"
#include "CypherRender/CypherRender_Local.h"

#include <catch2/catch_test_macros.hpp>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;
using error_t = render::render_error_t;

namespace
{

struct draw_queue_fixture_t {
    const common::allocator_t *systemAllocator{ common::Allocator_GetSystem() };
    common::allocator_t allocator{};
    render::backend_api_t backend{};
    render::render_draw_queue_t *queue{ nullptr };
    render::render_shader_handle_t shader{};
    render::render_buffer_handle_t vertices{};
    render::render_buffer_handle_t indices{};
    render::render_vertex_input_handle_t input{};
    render::render_pipeline_handle_t pipeline{};
    common::u64 nextToken{ 1u };
    common::u32 allocationAttempts{ 0u };
    common::u32 allocations{ 0u };
    common::u32 frees{ 0u };
    common::u32 failAllocationAt{ 0u };
    common::u32 drawCalls{ 0u };
    common::u32 failDrawAt{ 0u };
    common::u64 drawOffsets[8]{};
    bool rendererInitialized{ false };

    void InitAllocator()
    {
        allocator.pfnAllocate = []( void *state, common::usize size,
            common::usize alignment ) noexcept -> void * {
            auto &fixture = *static_cast<draw_queue_fixture_t *>( state );
            ++fixture.allocationAttempts;
            if ( fixture.allocationAttempts == fixture.failAllocationAt ) return nullptr;
            void *memory = fixture.systemAllocator->pfnAllocate(
                fixture.systemAllocator->pUserData, size, alignment );
            if ( memory != nullptr ) ++fixture.allocations;
            return memory;
        };
        allocator.pfnFree = []( void *state, void *memory,
            common::usize size, common::usize alignment ) noexcept {
            auto &fixture = *static_cast<draw_queue_fixture_t *>( state );
            ++fixture.frees;
            fixture.systemAllocator->pfnFree(
                fixture.systemAllocator->pUserData, memory, size, alignment );
        };
        allocator.pUserData = this;
    }

    void InitRenderer()
    {
        backend.CreateBuffer = []( const render::render_buffer_desc_t &,
            const render::render_buffer_data_t *, render::backend_buffer_t &out,
            void *state ) noexcept {
            out.value = static_cast<draw_queue_fixture_t *>( state )->nextToken++;
            return error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void * ) noexcept {
            return error_t::OK;
        };
        backend.CreateVertexInput = []( const render::backend_vertex_input_desc_t &,
            render::backend_vertex_input_t &out, void *state ) noexcept {
            out.value = static_cast<draw_queue_fixture_t *>( state )->nextToken++;
            return error_t::OK;
        };
        backend.DestroyVertexInput = []( render::backend_vertex_input_t, void * ) noexcept {
            return error_t::OK;
        };
        backend.CreateShader = []( const render::render_shader_desc_t &,
            render::backend_shader_t &out, void *state ) noexcept {
            out.value = static_cast<draw_queue_fixture_t *>( state )->nextToken++;
            return error_t::OK;
        };
        backend.DestroyShader = []( render::backend_shader_t, void * ) noexcept {
            return error_t::OK;
        };
        backend.CreateGraphicsPipeline = []( const render::backend_pipeline_desc_t &,
            render::backend_pipeline_t &out, void *state ) noexcept {
            out.value = static_cast<draw_queue_fixture_t *>( state )->nextToken++;
            return error_t::OK;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t, void * ) noexcept {
            return error_t::OK;
        };
        backend.DrawIndexed = []( const render::backend_draw_indexed_desc_t &draw,
            void *state ) noexcept {
            auto &fixture = *static_cast<draw_queue_fixture_t *>( state );
            ++fixture.drawCalls;
            if ( fixture.drawCalls <= 8u ) {
                fixture.drawOffsets[fixture.drawCalls - 1u] = draw.indexByteOffset;
            }
            if ( fixture.drawCalls == fixture.failDrawAt ) {
                return error_t::ERR_SUBMISSION_FAILED;
            }
            return error_t::OK;
        };

        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.limits.maxUniformBlockBytes = 16384u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        render::tr.info.limits.uniformBufferOffsetAlignment = 256u;
        rendererInitialized = true;
        REQUIRE( render::R_BufferSystemInit() == error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == error_t::OK );

        constexpr char source[] = "#version 410 core\nvoid main() {}\n";
        common::cooked_shader_view_t view{};
        view.nLanguageVersion = 410u;
        view.nStages = 2u;
        view.stages[0].stage = common::render_shader_stage_t::VERTEX;
        view.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        for ( auto &stage : view.stages ) {
            stage.code = { reinterpret_cast<const common::byte *>( source ), sizeof( source ) };
        }
        REQUIRE( render::R_CreateShader( { &view, "Queue test shader" }, &shader ) == error_t::OK );

        render::render_buffer_desc_t buffer{};
        buffer.memory = render::render_buffer_memory_t::UPLOAD;
        buffer.updatePolicy = render::render_buffer_update_t::DYNAMIC;
        buffer.byteSize = 64u;
        buffer.usage = render::R_BUFFER_USAGE_VERTEX;
        REQUIRE( render::R_CreateBuffer( buffer, nullptr, &vertices ) == error_t::OK );
        buffer.byteSize = 12u;
        buffer.usage = render::R_BUFFER_USAGE_INDEX;
        REQUIRE( render::R_CreateBuffer( buffer, nullptr, &indices ) == error_t::OK );

        render::render_vertex_layout_t layout{};
        layout.bindingCount = 1u;
        layout.bindings[0].stride = 16u;
        layout.attributeCount = 2u;
        layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
        layout.attributes[1] = { render::render_format_t::RGBA8_UNORM, 12u, 1u, 0u };

        render::render_vertex_input_desc_t vertexInput{};
        vertexInput.layout = layout;
        vertexInput.vertexBufferCount = 1u;
        vertexInput.vertexBuffers[0] = { vertices, 16u, 0u };
        vertexInput.indexBuffer = { indices, 0u, render::render_index_type_t::UINT16 };
        REQUIRE( render::R_CreateVertexInput( vertexInput, &input ) == error_t::OK );

        render::render_pipeline_desc_t pipelineDesc{};
        pipelineDesc.shader = shader;
        pipelineDesc.vertexLayout = layout;
        REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &pipeline ) == error_t::OK );
    }

    render::render_draw_indexed_desc_t Draw( const common::u32 firstIndex ) const
    {
        render::render_draw_indexed_desc_t draw{};
        draw.pipeline = pipeline;
        draw.vertexInput = input;
        draw.firstIndex = firstIndex;
        draw.indexCount = 3u;
        draw.vertexCount = 3u;
        return draw;
    }

    ~draw_queue_fixture_t()
    {
        render::R_ShutdownDrawQueue( &queue );
        if ( rendererInitialized ) {
            render::tr.frameActive = false;
            if ( render::R_IsGraphicsPipelineValid( pipeline ) ) {
                CHECK( render::R_DestroyGraphicsPipeline( pipeline ) == error_t::OK );
            }
            if ( render::R_IsVertexInputValid( input ) ) {
                CHECK( render::R_DestroyVertexInput( input ) == error_t::OK );
            }
            if ( render::R_IsShaderValid( shader ) ) {
                CHECK( render::R_DestroyShader( shader ) == error_t::OK );
            }
            if ( render::R_IsBufferValid( indices ) ) {
                CHECK( render::R_DestroyBuffer( indices ) == error_t::OK );
            }
            if ( render::R_IsBufferValid( vertices ) ) {
                CHECK( render::R_DestroyBuffer( vertices ) == error_t::OK );
            }
            (void)render::R_PipelineSystemShutdown();
            (void)render::R_VertexInputSystemShutdown();
            (void)render::R_ShaderSystemShutdown();
            (void)render::R_BufferSystemShutdown();
            render::tr = {};
        }
    }
};

} // namespace

TEST_CASE( "draw queue owns only bounded CPU records and cleans up both OOM sites",
    "[CypherRender][DrawQueue]" )
{
    draw_queue_fixture_t fixture{};
    fixture.InitAllocator();
    CHECK( render::R_InitDrawQueue( nullptr, &fixture.allocator, 2u ) ==
        error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_InitDrawQueue( &fixture.queue, nullptr, 2u ) ==
        error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 0u ) ==
        error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 65537u ) ==
        error_t::ERR_INVALID_ARGUMENT );

    fixture.failAllocationAt = 1u;
    CHECK( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 2u ) ==
        error_t::ERR_OUT_OF_MEMORY );
    CHECK( fixture.queue == nullptr );
    CHECK( fixture.allocations == fixture.frees );

    fixture.failAllocationAt = 3u; // First attempt was the previous failed owner allocation.
    CHECK( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 2u ) ==
        error_t::ERR_OUT_OF_MEMORY );
    CHECK( fixture.queue == nullptr );
    CHECK( fixture.allocations == fixture.frees );

    fixture.failAllocationAt = 0u;
    REQUIRE( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 2u ) ==
        error_t::OK );
    common::u32 completed = 99u;
    CHECK( render::R_FlushDrawQueue( fixture.queue, &completed ) ==
        error_t::ERR_NOT_INITIALIZED );
    CHECK( completed == 0u );
    auto *const original = fixture.queue;
    CHECK( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 2u ) ==
        error_t::ERR_INVALID_ARGUMENT );
    CHECK( fixture.queue == original );
    render::R_ShutdownDrawQueue( &fixture.queue );
    CHECK( fixture.queue == nullptr );
    CHECK( fixture.allocations == fixture.frees );
}

TEST_CASE( "draw queue copies input, preserves order, seals flush, and resets outside frame",
    "[CypherRender][DrawQueue]" )
{
    draw_queue_fixture_t fixture{};
    fixture.InitAllocator();
    fixture.InitRenderer();
    REQUIRE( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 2u ) == error_t::OK );

    auto borrowed = fixture.Draw( 0u );
    REQUIRE( render::R_AppendDrawQueue( fixture.queue, borrowed ) == error_t::OK );
    borrowed.firstIndex = 99u; // Mutating the source must not change the queued copy.
    common::u32 completed = 99u;
    CHECK( render::R_FlushDrawQueue( fixture.queue, &completed ) ==
        error_t::ERR_FRAME_NOT_ACTIVE );
    CHECK( completed == 0u );
    REQUIRE( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 1u ) ) == error_t::OK );
    CHECK( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 2u ) ) ==
        error_t::ERR_COMMAND_CAPACITY_EXCEEDED );
    render::tr.frameActive = true;
    REQUIRE( render::R_FlushDrawQueue( fixture.queue, &completed ) == error_t::OK );
    CHECK( completed == 2u );
    CHECK( fixture.drawCalls == 2u );
    CHECK( fixture.drawOffsets[0] == 0u );
    CHECK( fixture.drawOffsets[1] == 2u );
    CHECK( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 2u ) ) ==
        error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( render::R_ResetDrawQueue( fixture.queue ) ==
        error_t::ERR_FRAME_ALREADY_ACTIVE );
    CHECK( render::R_FlushDrawQueue( fixture.queue, &completed ) ==
        error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( completed == 0u );
    CHECK( fixture.drawCalls == 2u );

    render::tr.frameActive = false;
    REQUIRE( render::R_ResetDrawQueue( fixture.queue ) == error_t::OK );
    REQUIRE( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 2u ) ) == error_t::OK );
    render::tr.frameActive = true;
    REQUIRE( render::R_FlushDrawQueue( fixture.queue, &completed ) == error_t::OK );
    CHECK( completed == 1u );
    CHECK( fixture.drawOffsets[2] == 4u );
}

TEST_CASE( "partial draw failure seals queue against unsafe retries in any frame",
    "[CypherRender][DrawQueue]" )
{
    draw_queue_fixture_t fixture{};
    fixture.InitAllocator();
    fixture.InitRenderer();
    REQUIRE( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 3u ) == error_t::OK );
    for ( common::u32 i = 0u; i < 3u; ++i ) {
        REQUIRE( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( i ) ) == error_t::OK );
    }
    fixture.failDrawAt = 2u;
    render::tr.frameActive = true;
    common::u32 completed = 99u;
    CHECK( render::R_FlushDrawQueue( fixture.queue, &completed ) ==
        error_t::ERR_SUBMISSION_FAILED );
    CHECK( completed == 1u );
    CHECK( fixture.drawCalls == 2u );
    CHECK( fixture.drawOffsets[0] == 0u );
    CHECK( fixture.drawOffsets[1] == 2u );
    CHECK( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 0u ) ) ==
        error_t::ERR_COMMAND_STATE_INVALID );
    fixture.failDrawAt = 0u;
    CHECK( render::R_FlushDrawQueue( fixture.queue, &completed ) ==
        error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( completed == 0u );
    CHECK( fixture.drawCalls == 2u );

    // Ending one frame and beginning another must not silently submit the
    // remainder: the caller needs to discard and rebuild intentionally.
    render::tr.frameActive = false;
    render::tr.frameActive = true;
    CHECK( render::R_FlushDrawQueue( fixture.queue, &completed ) ==
        error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( completed == 0u );
    CHECK( fixture.drawCalls == 2u );
    render::tr.frameActive = false;
    REQUIRE( render::R_ResetDrawQueue( fixture.queue ) == error_t::OK );
    REQUIRE( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 2u ) ) == error_t::OK );
    render::tr.frameActive = true;
    REQUIRE( render::R_FlushDrawQueue( fixture.queue, &completed ) == error_t::OK );
    CHECK( completed == 1u );
    CHECK( fixture.drawCalls == 3u );
    CHECK( fixture.drawOffsets[2] == 4u );
}

TEST_CASE( "empty in-frame flush seals the recording phase until reset",
    "[CypherRender][DrawQueue]" )
{
    draw_queue_fixture_t fixture{};
    fixture.InitAllocator();
    fixture.InitRenderer();
    REQUIRE( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 1u ) == error_t::OK );
    render::tr.frameActive = true;
    common::u32 completed = 99u;
    REQUIRE( render::R_FlushDrawQueue( fixture.queue, &completed ) == error_t::OK );
    CHECK( completed == 0u );
    CHECK( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 0u ) ) ==
        error_t::ERR_COMMAND_STATE_INVALID );
    render::tr.frameActive = false;
    REQUIRE( render::R_ResetDrawQueue( fixture.queue ) == error_t::OK );
    CHECK( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 0u ) ) == error_t::OK );
}

TEST_CASE( "queue does not retain GPU handles and reports stale handles at flush",
    "[CypherRender][DrawQueue]" )
{
    draw_queue_fixture_t fixture{};
    fixture.InitAllocator();
    fixture.InitRenderer();
    REQUIRE( render::R_InitDrawQueue( &fixture.queue, &fixture.allocator, 1u ) == error_t::OK );
    REQUIRE( render::R_AppendDrawQueue( fixture.queue, fixture.Draw( 0u ) ) == error_t::OK );
    REQUIRE( render::R_DestroyGraphicsPipeline( fixture.pipeline ) == error_t::OK );
    render::tr.frameActive = true;
    common::u32 completed = 99u;
    CHECK( render::R_FlushDrawQueue( fixture.queue, &completed ) ==
        error_t::ERR_STALE_HANDLE );
    CHECK( completed == 0u );
    CHECK( fixture.drawCalls == 0u );
}
