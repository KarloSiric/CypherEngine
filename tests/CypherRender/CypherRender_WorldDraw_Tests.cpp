//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests bounded World-to-draw preparation without a graphics context.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_WorldDraw.h"
#include "CypherRender/CypherRender_Local.h"
#include "CypherCommon/Mathlib/CypherMath_Intersection.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace render = ::cypher::engine::render;
namespace world = ::cypher::engine::world;

namespace
{

world::world_view_query_t MakeView() noexcept
{
    world::world_view_query_t view{};
    constexpr common::f32 extent = 20.0f;
    view.frustum.planes[0] = math::Plane_Make( { 1.0f, 0.0f, 0.0f }, extent );
    view.frustum.planes[1] = math::Plane_Make( { -1.0f, 0.0f, 0.0f }, extent );
    view.frustum.planes[2] = math::Plane_Make( { 0.0f, 1.0f, 0.0f }, extent );
    view.frustum.planes[3] = math::Plane_Make( { 0.0f, -1.0f, 0.0f }, extent );
    view.frustum.planes[4] = math::Plane_Make( { 0.0f, 0.0f, 1.0f }, extent );
    view.frustum.planes[5] = math::Plane_Make( { 0.0f, 0.0f, -1.0f }, extent );
    view.cameraPosition = { 0.0f, 0.0f, 0.0f };
    view.maxDistance = 100.0f;
    view.layerMask = 1u;
    return view;
}

struct world_draw_fixture_t {
    render::renderer_state_t savedRenderer{ render::tr };
    render::backend_api_t backend{};
    common::u64 nextToken{ 1u };
    common::u32 uploadCalls{ 0u };
    common::u32 drawCalls{ 0u };
    bool failNextUpload{ false };
    common::byte uploaded[1024]{};
    common::u64 uploadedSize{ 0u };
    render::render_static_mesh_bundle_t mesh{};
    render::render_material_bundle_t materials[2]{};
    render::render_shader_handle_t shader{};
    render::render_pipeline_handle_t pipelines[2]{};
    render::render_buffer_handle_t viewBuffer{};
    render::render_uniform_batch_t batch{};
    world::world_t *pWorld{ nullptr };
    world::world_submission_t *pSubmission{ nullptr };
    world::world_submission_token_t token{};

    void Init()
    {
        backend.CreateBuffer = []( const render::render_buffer_desc_t &,
            const render::render_buffer_data_t *, render::backend_buffer_t &out,
            void *state ) noexcept {
            out.value = static_cast<world_draw_fixture_t *>( state )->nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void * ) noexcept {
            return render::render_error_t::OK;
        };
        backend.UpdateBuffer = []( render::backend_buffer_t,
            common::u64 offset, const render::render_buffer_data_t &data,
            void *state ) noexcept {
            auto &self = *static_cast<world_draw_fixture_t *>( state );
            ++self.uploadCalls;
            if ( self.failNextUpload ) {
                self.failNextUpload = false;
                return render::render_error_t::ERR_RESOURCE_TRANSFER_FAILED;
            }
            if ( offset != 0u || data.bytes == nullptr ||
                 data.byteSize > sizeof( self.uploaded ) ) {
                return render::render_error_t::ERR_INVALID_ARGUMENT;
            }
            self.uploadedSize = data.byteSize;
            std::memcpy( self.uploaded, data.bytes,
                static_cast<common::usize>( data.byteSize ) );
            return render::render_error_t::OK;
        };
        backend.CreateVertexInput = []( const render::backend_vertex_input_desc_t &,
            render::backend_vertex_input_t &out, void *state ) noexcept {
            out.value = static_cast<world_draw_fixture_t *>( state )->nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyVertexInput = []( render::backend_vertex_input_t,
            void * ) noexcept { return render::render_error_t::OK; };
        backend.CreateShader = []( const render::render_shader_desc_t &,
            render::backend_shader_t &out, void *state ) noexcept {
            out.value = static_cast<world_draw_fixture_t *>( state )->nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyShader = []( render::backend_shader_t,
            void * ) noexcept { return render::render_error_t::OK; };
        backend.CreateGraphicsPipeline = [](
            const render::backend_pipeline_desc_t &,
            render::backend_pipeline_t &out, void *state ) noexcept {
            out.value = static_cast<world_draw_fixture_t *>( state )->nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t,
            void * ) noexcept { return render::render_error_t::OK; };
        backend.DrawIndexed = [](
            const render::backend_draw_indexed_desc_t &, void *state ) noexcept {
            ++static_cast<world_draw_fixture_t *>( state )->drawCalls;
            return render::render_error_t::OK;
        };

        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.limits.maxUniformBlockBytes = 256u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        render::tr.info.limits.uniformBufferOffsetAlignment = 256u;
        REQUIRE( render::R_BufferSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == render::render_error_t::OK );

        const common::f32 vertices[9]{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f
        };
        const common::u16 indices[3]{ 0u, 1u, 2u };
        render::render_static_mesh_source_t source{};
        source.vertexLayout.bindingCount = 1u;
        source.vertexLayout.bindings[0].stride = 3u * sizeof( common::f32 );
        source.vertexLayout.attributeCount = 1u;
        source.vertexLayout.attributes[0] = {
            render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u
        };
        source.vertexBytes = vertices;
        source.vertexByteSize = sizeof( vertices );
        source.vertexCount = 3u;
        source.indexBytes = indices;
        source.indexByteSize = sizeof( indices );
        source.indexCount = 3u;
        REQUIRE( render::R_CreateStaticMeshBundle(
            source, &mesh ).status == render::render_static_mesh_bridge_status_t::OK );

        constexpr char shaderSource[] = "#version 410 core\nvoid main() {}\n";
        common::cooked_shader_view_t shaderView{};
        shaderView.nLanguageVersion = 410u;
        shaderView.nStages = 2u;
        shaderView.stages[0].stage = common::render_shader_stage_t::VERTEX;
        shaderView.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        for ( auto &stage : shaderView.stages ) {
            stage.code = { reinterpret_cast<const common::byte *>( shaderSource ),
                sizeof( shaderSource ) };
        }
        REQUIRE( render::R_CreateShader(
            { &shaderView, "World draw test shader" }, &shader ) ==
            render::render_error_t::OK );
        for ( common::u32 i = 0u; i < 2u; ++i ) {
            render::render_pipeline_desc_t pipeline{};
            pipeline.shader = shader;
            pipeline.vertexLayout = source.vertexLayout;
            pipeline.uniformBlockCount = 2u;
            pipeline.uniformBlocks[0] = { "View", 64u };
            pipeline.uniformBlocks[1] = { "Model", 64u };
            REQUIRE( render::R_CreateGraphicsPipeline(
                pipeline, &pipelines[i] ) == render::render_error_t::OK );
            materials[i].shader = shader;
            materials[i].pipeline = pipelines[i];
        }
        render::render_buffer_desc_t viewDesc{};
        viewDesc.byteSize = 64u;
        viewDesc.usage = render::R_BUFFER_USAGE_UNIFORM;
        viewDesc.memory = render::render_buffer_memory_t::UPLOAD;
        viewDesc.updatePolicy = render::render_buffer_update_t::DYNAMIC;
        REQUIRE( render::R_CreateBuffer(
            viewDesc, nullptr, &viewBuffer ) == render::render_error_t::OK );
        REQUIRE( render::R_CreateUniformBatch(
            576u, common::Allocator_GetSystem(), &batch ) ==
            render::render_error_t::OK );
        REQUIRE( world::World_Init(
            &pWorld, common::Allocator_GetSystem(), 8u ) == world::world_error_t::OK );
        REQUIRE( world::WorldSubmission_Init(
            &pSubmission, common::Allocator_GetSystem(), 8u ) ==
            world::world_submission_error_t::OK );
    }

    world::world_object_id_t Add(
        common::u32 meshSlot, common::u32 materialSlot,
        common::f32 x )
    {
        world::world_object_t object{};
        object.localToWorld = math::Affine3_FromTranslation( { x, 0.0f, 0.0f } );
        object.worldBounds = math::Aabb_Make(
            { x - 1.0f, -1.0f, -1.0f },
            { x + 1.0f, 1.0f, 1.0f } );
        object.layerMask = 1u;
        object.meshSlot = meshSlot;
        object.materialSlot = materialSlot;
        world::world_object_id_t id{};
        REQUIRE( world::World_AddObject(
            pWorld, &object, &id ) == world::world_error_t::OK );
        return id;
    }

    void Build()
    {
        const auto query = MakeView();
        REQUIRE( world::WorldSubmission_Build(
            pSubmission, pWorld, &query, &token ) ==
            world::world_submission_error_t::OK );
    }

    render::render_world_opaque_prepare_desc_t Description()
    {
        render::render_world_opaque_prepare_desc_t desc{};
        desc.submission = pSubmission;
        desc.token = token;
        desc.meshes = &mesh;
        desc.meshCount = 1u;
        desc.materials = materials;
        desc.materialCount = 2u;
        desc.modelUniforms = &batch;
        desc.modelUniformSlot = 1u;
        desc.drawTemplate.uniformBuffers[0] = viewBuffer;
        return desc;
    }

    ~world_draw_fixture_t()
    {
        world::WorldSubmission_Shutdown( &pSubmission );
        world::World_Shutdown( &pWorld );
        (void)render::R_DestroyUniformBatch( &batch );
        if ( viewBuffer.value != 0u ) (void)render::R_DestroyBuffer( viewBuffer );
        for ( auto pipeline : pipelines ) {
            if ( pipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( pipeline );
        }
        if ( shader.value != 0u ) (void)render::R_DestroyShader( shader );
        (void)render::R_DestroyStaticMeshBundle( &mesh );
        (void)render::R_PipelineSystemShutdown();
        (void)render::R_ShaderSystemShutdown();
        (void)render::R_VertexInputSystemShutdown();
        (void)render::R_BufferSystemShutdown();
        render::tr = savedRenderer;
    }
};

} // namespace

TEST_CASE( "visible opaque World objects resolve ordered draws and aligned models",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    const auto first = fixture.Add( 0u, 0u, 0.0f );
    const auto second = fixture.Add( 0u, 1u, 4.0f );
    (void)fixture.Add( 0u, 0u, 200.0f ); // Culled by the World view.
    fixture.Build();

    render::render_world_opaque_draw_t output[3]{};
    common::u32 count = 99u;
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), output, 3u, &count ) == render::render_error_t::OK );
    CHECK( count == 2u );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( fixture.drawCalls == 0u );
    CHECK( fixture.uploadedSize == 320u );
    CHECK( output[0].objectId.slot.value == first.slot.value );
    CHECK( output[1].objectId.slot.value == second.slot.value );
    CHECK( output[0].draw.vertexInput.value == fixture.mesh.vertexInput.value );
    CHECK( output[1].draw.vertexInput.value == fixture.mesh.vertexInput.value );
    CHECK( output[0].draw.pipeline.value == fixture.pipelines[0].value );
    CHECK( output[1].draw.pipeline.value == fixture.pipelines[1].value );
    CHECK( output[0].draw.uniformBuffers[0].value == fixture.viewBuffer.value );
    CHECK( output[0].draw.uniformBuffers[1].value == fixture.batch.buffer.value );
    CHECK( output[0].draw.uniformRanges[1].offset == 0u );
    CHECK( output[1].draw.uniformRanges[1].offset == 256u );
    CHECK( output[0].draw.uniformRanges[1].byteSize == 64u );

    math::mat4_t firstModel{};
    math::mat4_t secondModel{};
    std::memcpy( &firstModel, fixture.uploaded, sizeof( firstModel ) );
    std::memcpy( &secondModel, fixture.uploaded + 256u, sizeof( secondModel ) );
    CHECK( firstModel.m[12] == 0.0f );
    CHECK( secondModel.m[12] == 4.0f );
}

TEST_CASE( "World draw preflight preserves outputs and batch on bad slots or capacity",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    (void)fixture.Add( 0u, 0u, 0.0f );
    (void)fixture.Add( 0u, 7u, 4.0f );
    fixture.Build();
    render::render_world_opaque_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    const auto desc = fixture.Description();
    CHECK( render::R_PrepareOpaqueWorldDraws(
        desc, output, 1u, &count ) ==
        render::render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED );
    CHECK( render::R_PrepareOpaqueWorldDraws(
        desc, output, 2u, &count ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( count == 77u );
    CHECK( output[0].draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World draw preparation appends after existing uniform records",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    (void)fixture.Add( 0u, 0u, 0.0f );
    (void)fixture.Add( 0u, 1u, 4.0f );
    fixture.Build();
    const common::byte existing[16]{ 1u, 2u, 3u, 4u };
    render::render_uniform_record_t existingRecord{};
    REQUIRE( render::R_AppendUniformRecord(
        &fixture.batch, existing, sizeof( existing ), &existingRecord ) ==
        render::render_error_t::OK );
    REQUIRE( existingRecord.offset == 0u );

    render::render_world_opaque_draw_t output[2]{};
    common::u32 count = 0u;
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), output, 2u, &count ) == render::render_error_t::OK );
    CHECK( count == 2u );
    CHECK( fixture.uploadedSize == 576u );
    CHECK( std::memcmp( fixture.uploaded, existing, sizeof( existing ) ) == 0 );
    CHECK( output[0].draw.uniformRanges[1].offset == 256u );
    CHECK( output[1].draw.uniformRanges[1].offset == 512u );
}

TEST_CASE( "World draw capacity preflight leaves earlier batch records intact",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    (void)fixture.Add( 0u, 0u, 0.0f );
    (void)fixture.Add( 0u, 1u, 4.0f );
    (void)fixture.Add( 0u, 0u, 8.0f );
    fixture.Build();
    const common::byte existing[16]{ 1u };
    render::render_uniform_record_t record{};
    REQUIRE( render::R_AppendUniformRecord(
        &fixture.batch, existing, sizeof( existing ), &record ) ==
        render::render_error_t::OK );
    render::render_world_opaque_draw_t output[3]{};
    output[0].draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), output, 3u, &count ) ==
        render::render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED );
    CHECK( count == 77u );
    CHECK( output[0].draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == sizeof( existing ) );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World draw preparation rejects stale tokens and leaves empty views appendable",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.Build();
    common::u32 count = 55u;
    auto desc = fixture.Description();
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        desc, nullptr, 0u, &count ) == render::render_error_t::OK );
    CHECK( count == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );

    world::WorldSubmission_End( fixture.pSubmission );
    count = 55u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        desc, nullptr, 0u, &count ) == render::render_error_t::ERR_INVALID_STATE );
    CHECK( count == 55u );
}

TEST_CASE( "World draw preparation rejects singular transforms before staging",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    const auto id = fixture.Add( 0u, 0u, 0.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject(
        fixture.pWorld, id, &object ) == world::world_error_t::OK );
    object.localToWorld = math::Affine3_FromScale( { 0.0f, 1.0f, 1.0f } );
    REQUIRE( world::World_UpdateObject(
        fixture.pWorld, id, &object ) == world::world_error_t::OK );
    fixture.Build();
    render::render_world_opaque_draw_t output{};
    common::u32 count = 42u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), &output, 1u, &count ) ==
        render::render_error_t::ERR_UNSUPPORTED );
    CHECK( count == 42u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World draw preparation preserves small positive object scales",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    const auto id = fixture.Add( 0u, 0u, 0.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject(
        fixture.pWorld, id, &object ) == world::world_error_t::OK );
    object.localToWorld = math::Affine3_FromScale(
        { 0.001f, 0.001f, 0.001f } );
    REQUIRE( world::World_UpdateObject(
        fixture.pWorld, id, &object ) == world::world_error_t::OK );
    fixture.Build();

    render::render_world_opaque_draw_t output{};
    common::u32 count = 0u;
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), &output, 1u, &count ) ==
        render::render_error_t::OK );
    CHECK( count == 1u );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( output.objectId.slot.value == id.slot.value );
    math::mat4_t model{};
    std::memcpy( &model, fixture.uploaded, sizeof( model ) );
    CHECK( model.m[0] == 0.001f );
    CHECK( model.m[5] == 0.001f );
    CHECK( model.m[10] == 0.001f );
}

TEST_CASE( "World draw preparation rejects nonopaque materials before staging",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    (void)fixture.Add( 0u, 1u, 0.0f );
    fixture.Build();
    fixture.materials[1].alphaMode = common::render_material_alpha_mode_t::BLEND;
    render::render_world_opaque_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 count = 42u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), &output, 1u, &count ) ==
        render::render_error_t::ERR_UNSUPPORTED );
    CHECK( count == 42u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World draw upload failure publishes nothing and keeps batch retryable",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    (void)fixture.Add( 0u, 0u, 0.0f );
    fixture.Build();
    fixture.failNextUpload = true;
    render::render_world_opaque_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), &output, 1u, &count ) ==
        render::render_error_t::ERR_RESOURCE_TRANSFER_FAILED );
    CHECK( count == 77u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == sizeof( math::mat4_t ) );
    CHECK( fixture.batch.sealed );
    CHECK_FALSE( fixture.batch.committed );
    CHECK( fixture.uploadCalls == 1u );
    REQUIRE( render::R_CommitUniformBatch(
        &fixture.batch ) == render::render_error_t::OK );
    CHECK( fixture.batch.committed );
    CHECK( fixture.uploadCalls == 2u );
}
