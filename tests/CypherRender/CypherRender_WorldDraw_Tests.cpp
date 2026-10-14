//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests bounded World-to-draw preparation without a graphics context.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_WorldDraw.h"
#include "CypherRender/CypherRender_Local.h"
#include "CypherCommon/Mathlib/CypherMath_Intersection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstring>
#include <limits>

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
    render::render_material_bundle_t materials[4]{};
    render::render_shader_handle_t shader{};
    render::render_pipeline_handle_t pipelines[4]{};
    render::render_pipeline_handle_t mirroredPipelines[4]{};
    render::render_pipeline_handle_t shadowPipeline{};
    render::render_pipeline_handle_t mirroredShadowPipeline{};
    render::render_pipeline_handle_t maskShadowPipeline{};
    render::render_pipeline_handle_t mirroredMaskShadowPipeline{};
    render::render_pipeline_handle_t litPipeline{};
    render::render_pipeline_handle_t mirroredLitPipeline{};
    render::render_pipeline_handle_t cutoutPipelines[2]{};
    render::render_pipeline_handle_t mirroredCutoutPipelines[2]{};
    render::render_texture_handle_t alphaTexture{};
    render::render_texture_handle_t passDepthTexture{};
    render::render_sampler_handle_t passCompareSampler{};
    render::render_sampler_handle_t passRawSampler{};
    render::render_buffer_handle_t viewBuffer{};
    render::render_buffer_handle_t materialBuffer{};
    render::render_uniform_batch_t batch{};
    world::world_t *pWorld{ nullptr };
    world::world_submission_t *pSubmission{ nullptr };
    world::world_submission_token_t token{};

    void Init( const bool withTwoTriangles = false )
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
        backend.CreateTexture2D = []( const render::render_texture_desc_t &,
            const render::render_texture_data_t &, render::backend_texture_t &out,
            void *state ) noexcept {
            out.value = static_cast<world_draw_fixture_t *>( state )->nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyTexture = []( render::backend_texture_t, void * ) noexcept {
            return render::render_error_t::OK;
        };
        backend.CreateSampler = []( const render::render_sampler_desc_t &,
            render::backend_sampler_t &out, void *state ) noexcept {
            out.value = static_cast<world_draw_fixture_t *>( state )->nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroySampler = []( render::backend_sampler_t, void * ) noexcept {
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
        render::tr.info.limits.maxTexture2DSize = 16u;
        render::tr.info.limits.maxCombinedTextureUnits = 2u;
        REQUIRE( render::R_BufferSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_TextureSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_SamplerSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == render::render_error_t::OK );

        const common::f32 vertices[9]{
            0.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f
        };
        const common::u16 indices[6]{ 0u, 1u, 2u, 2u, 1u, 0u };
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
        source.indexCount = withTwoTriangles ? 6u : 3u;
        source.indexByteSize = source.indexCount * sizeof( common::u16 );
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
            pipeline.frontCounterClockwise = false;
            REQUIRE( render::R_CreateGraphicsPipeline(
                pipeline, &mirroredPipelines[i] ) == render::render_error_t::OK );
            materials[i].shader = shader;
            materials[i].pipeline = pipelines[i];
            materials[i].mirroredPipeline = mirroredPipelines[i];
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

    world::world_object_id_t AddAtZ(
        common::u32 materialSlot, common::f32 x, common::f32 z )
    {
        const auto id = Add( 0u, materialSlot, x );
        world::world_object_t object{};
        REQUIRE( world::World_GetObject( pWorld, id, &object ) ==
            world::world_error_t::OK );
        object.localToWorld = math::Affine3_FromTranslation( { x, 0.0f, z } );
        object.worldBounds = math::Aabb_Make(
            { x - 1.0f, -1.0f, z - 1.0f },
            { x + 1.0f, 1.0f, z + 1.0f } );
        REQUIRE( world::World_UpdateObject( pWorld, id, &object ) ==
            world::world_error_t::OK );
        return id;
    }

    void Build()
    {
        const auto query = MakeView();
        REQUIRE( world::WorldSubmission_Build(
            pSubmission, pWorld, &query, &token ) ==
            world::world_submission_error_t::OK );
    }

    void Build( const world::world_view_query_t &query )
    {
        REQUIRE( world::WorldSubmission_Build(
            pSubmission, pWorld, &query, &token ) ==
            world::world_submission_error_t::OK );
    }

    void InitShadowPipelines()
    {
        render::render_vertex_input_info_t input{};
        REQUIRE( render::R_GetVertexInputInfo( mesh.vertexInput, &input ) ==
            render::render_error_t::OK );
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = input.layout;
        pipeline.uniformBlockCount = 2u;
        pipeline.uniformBlocks[0] = { "Light", 64u };
        pipeline.uniformBlocks[1] = { "Model", 64u };
        pipeline.colorWrite = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &shadowPipeline ) == render::render_error_t::OK );
        pipeline.frontCounterClockwise = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &mirroredShadowPipeline ) == render::render_error_t::OK );
    }

    void InitMaskShadowPipelines()
    {
        render::render_vertex_input_info_t input{};
        REQUIRE( render::R_GetVertexInputInfo( mesh.vertexInput, &input ) ==
            render::render_error_t::OK );
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = input.layout;
        pipeline.uniformBlockCount = 2u;
        pipeline.uniformBlocks[0] = { "Light", 64u };
        pipeline.uniformBlocks[1] = { "Model", 80u };
        pipeline.sampledTextureCount = 1u;
        pipeline.sampledTextures[0].name = "AlphaMap";
        pipeline.colorWrite = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &maskShadowPipeline ) == render::render_error_t::OK );
        pipeline.frontCounterClockwise = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &mirroredMaskShadowPipeline ) ==
            render::render_error_t::OK );
    }

    void EnableLitMaterial( common::u32 slot )
    {
        REQUIRE( slot < 2u );
        REQUIRE( litPipeline.value == 0u );
        render::render_vertex_input_info_t input{};
        REQUIRE( render::R_GetVertexInputInfo( mesh.vertexInput, &input ) ==
            render::render_error_t::OK );
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = input.layout;
        pipeline.uniformBlockCount = 2u;
        pipeline.uniformBlocks[0] = { "View", 64u };
        pipeline.uniformBlocks[1] = { "Model", 112u };
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &litPipeline ) == render::render_error_t::OK );
        pipeline.frontCounterClockwise = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &mirroredLitPipeline ) == render::render_error_t::OK );
        materials[slot].pipeline = litPipeline;
        materials[slot].mirroredPipeline = mirroredLitPipeline;
    }

    void EnableCutoutMaterial( common::u32 slot, bool withNormals, bool masked )
    {
        REQUIRE( slot < 2u );
        REQUIRE( cutoutPipelines[slot].value == 0u );
        render::render_vertex_input_info_t input{};
        REQUIRE( render::R_GetVertexInputInfo( mesh.vertexInput, &input ) ==
            render::render_error_t::OK );
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = input.layout;
        pipeline.uniformBlockCount = 2u;
        pipeline.uniformBlocks[0] = { "View", 64u };
        pipeline.uniformBlocks[1] = { "Model", withNormals ? 128u : 80u };
        if ( masked ) {
            if ( alphaTexture.value == 0u ) {
                const common::byte pixel[4]{ 255u, 255u, 255u, 128u };
                render::render_texture_desc_t textureDesc{};
                textureDesc.width = 1u;
                textureDesc.height = 1u;
                textureDesc.generateMips = false;
                REQUIRE( render::R_CreateTexture2D(
                    textureDesc, { pixel, sizeof( pixel ) }, &alphaTexture ) ==
                    render::render_error_t::OK );
            }
            pipeline.sampledTextureCount = 1u;
            pipeline.sampledTextures[0].name = "AlphaMap";
            materials[slot].alphaMode = common::render_material_alpha_mode_t::MASK;
            materials[slot].textureCount = 1u;
            materials[slot].textures[0] = alphaTexture;
        }
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &cutoutPipelines[slot] ) == render::render_error_t::OK );
        pipeline.frontCounterClockwise = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &mirroredCutoutPipelines[slot] ) ==
            render::render_error_t::OK );
        materials[slot].pipeline = cutoutPipelines[slot];
        materials[slot].mirroredPipeline = mirroredCutoutPipelines[slot];
    }

    void EnablePassShadowMaterial( const common::u32 slot )
    {
        REQUIRE( slot < 2u );
        REQUIRE( litPipeline.value == 0u );
        EnableCutoutMaterial( slot, false, true );
        render::render_vertex_input_info_t input{};
        REQUIRE( render::R_GetVertexInputInfo( mesh.vertexInput, &input ) ==
            render::render_error_t::OK );
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = input.layout;
        pipeline.uniformBlockCount = 2u;
        pipeline.uniformBlocks[0] = { "View", 64u };
        pipeline.uniformBlocks[1] = { "Model", 80u };
        pipeline.sampledTextureCount = 2u;
        pipeline.sampledTextures[0].name = "AlphaMap";
        pipeline.sampledTextures[1] = {
            "ShadowMap", render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D };
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &litPipeline ) == render::render_error_t::OK );
        pipeline.frontCounterClockwise = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &mirroredLitPipeline ) == render::render_error_t::OK );
        materials[slot].pipeline = litPipeline;
        materials[slot].mirroredPipeline = mirroredLitPipeline;
        materials[slot].passShadowSlot = 1u;
        materials[slot].alphaCutoff = 0.375f;
    }

    void CreatePassShadowBindings()
    {
        render::render_texture_desc_t depth{};
        depth.width = depth.height = 4u;
        depth.sRGB = false;
        depth.generateMips = false;
        depth.repeat = false;
        depth.renderTarget = true;
        depth.format = render::render_texture_format_t::D24;
        REQUIRE( render::R_CreateTexture2D(
            depth, {}, &passDepthTexture ) == render::render_error_t::OK );
        render::render_sampler_desc_t compare{};
        compare.addressU = compare.addressV =
            render::render_sampler_address_t::CLAMP_TO_EDGE;
        compare.compare = render::render_sampler_compare_t::LESS_EQUAL;
        REQUIRE( render::R_CreateSampler(
            compare, &passCompareSampler ) == render::render_error_t::OK );
    }

    render::render_world_shadow_prepare_desc_t ShadowDescription()
    {
        render::render_world_shadow_prepare_desc_t desc{};
        desc.submission = pSubmission;
        desc.token = token;
        desc.meshes = &mesh;
        desc.meshCount = 1u;
        desc.materials = materials;
        desc.materialCount = 4u;
        desc.modelUniforms = &batch;
        desc.modelUniformSlot = 1u;
        desc.shadowPipeline = shadowPipeline;
        desc.mirroredShadowPipeline = mirroredShadowPipeline;
        desc.maskShadowPipeline = maskShadowPipeline;
        desc.mirroredMaskShadowPipeline = mirroredMaskShadowPipeline;
        desc.drawTemplate.uniformBuffers[0] = viewBuffer;
        return desc;
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

    void AddCameraMaterial( common::u32 slot,
        common::render_material_alpha_mode_t alphaMode )
    {
        REQUIRE( slot >= 2u );
        REQUIRE( slot < 4u );
        render::render_vertex_input_info_t input{};
        REQUIRE( render::R_GetVertexInputInfo( mesh.vertexInput, &input ) ==
            render::render_error_t::OK );
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = input.layout;
        pipeline.uniformBlockCount = 2u;
        pipeline.uniformBlocks[0] = { "View", 64u };
        pipeline.uniformBlocks[1] = { "Model", 64u };
        pipeline.depthWrite = false;
        pipeline.blendMode = alphaMode == common::render_material_alpha_mode_t::BLEND
            ? render::render_blend_mode_t::STRAIGHT_ALPHA
            : render::render_blend_mode_t::ADDITIVE;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &pipelines[slot] ) == render::render_error_t::OK );
        pipeline.frontCounterClockwise = false;
        REQUIRE( render::R_CreateGraphicsPipeline(
            pipeline, &mirroredPipelines[slot] ) == render::render_error_t::OK );
        materials[slot].shader = shader;
        materials[slot].pipeline = pipelines[slot];
        materials[slot].mirroredPipeline = mirroredPipelines[slot];
        materials[slot].alphaMode = alphaMode;
    }

    render::render_world_camera_prepare_desc_t CameraDescription()
    {
        const auto base = Description();
        render::render_world_camera_prepare_desc_t desc{};
        desc.submission = base.submission;
        desc.token = base.token;
        desc.meshes = base.meshes;
        desc.meshCount = base.meshCount;
        desc.materials = base.materials;
        desc.materialCount = 4u;
        desc.modelUniforms = base.modelUniforms;
        desc.modelUniformSlot = base.modelUniformSlot;
        desc.drawTemplate = base.drawTemplate;
        return desc;
    }

    ~world_draw_fixture_t()
    {
        world::WorldSubmission_Shutdown( &pSubmission );
        world::World_Shutdown( &pWorld );
        (void)render::R_DestroyUniformBatch( &batch );
        if ( passRawSampler.value != 0u ) (void)render::R_DestroySampler( passRawSampler );
        if ( passCompareSampler.value != 0u ) (void)render::R_DestroySampler( passCompareSampler );
        if ( passDepthTexture.value != 0u ) (void)render::R_DestroyTexture( passDepthTexture );
        if ( materialBuffer.value != 0u ) (void)render::R_DestroyBuffer( materialBuffer );
        if ( viewBuffer.value != 0u ) (void)render::R_DestroyBuffer( viewBuffer );
        for ( auto pipeline : pipelines ) {
            if ( pipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( pipeline );
        }
        for ( auto pipeline : mirroredPipelines ) {
            if ( pipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( pipeline );
        }
        if ( shadowPipeline.value != 0u )
            (void)render::R_DestroyGraphicsPipeline( shadowPipeline );
        if ( mirroredShadowPipeline.value != 0u )
            (void)render::R_DestroyGraphicsPipeline( mirroredShadowPipeline );
        if ( maskShadowPipeline.value != 0u )
            (void)render::R_DestroyGraphicsPipeline( maskShadowPipeline );
        if ( mirroredMaskShadowPipeline.value != 0u )
            (void)render::R_DestroyGraphicsPipeline( mirroredMaskShadowPipeline );
        if ( litPipeline.value != 0u )
            (void)render::R_DestroyGraphicsPipeline( litPipeline );
        if ( mirroredLitPipeline.value != 0u )
            (void)render::R_DestroyGraphicsPipeline( mirroredLitPipeline );
        for ( auto pipeline : cutoutPipelines ) {
            if ( pipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( pipeline );
        }
        for ( auto pipeline : mirroredCutoutPipelines ) {
            if ( pipeline.value != 0u ) (void)render::R_DestroyGraphicsPipeline( pipeline );
        }
        if ( alphaTexture.value != 0u ) (void)render::R_DestroyTexture( alphaTexture );
        if ( shader.value != 0u ) (void)render::R_DestroyShader( shader );
        (void)render::R_DestroyStaticMeshBundle( &mesh );
        (void)render::R_PipelineSystemShutdown();
        (void)render::R_SamplerSystemShutdown();
        (void)render::R_TextureSystemShutdown();
        (void)render::R_ShaderSystemShutdown();
        (void)render::R_VertexInputSystemShutdown();
        (void)render::R_BufferSystemShutdown();
        render::tr = savedRenderer;
    }
};

enum class range_adapter_t { OPAQUE, CAMERA, SHADOW };

render::render_error_t PrepareMeshRanges( world_draw_fixture_t &fixture,
    const range_adapter_t adapter,
    const render::render_static_mesh_bundle_t *meshes,
    const common::u32 meshCount,
    const render::render_world_mesh_range_t *ranges,
    const common::u32 rangeCount,
    render::render_world_opaque_draw_t *output, const common::u32 capacity,
    common::u32 *outCount,
    render::render_world_camera_pass_counts_t *outCameraCounts,
    const render::render_material_bundle_t *materialOverride = nullptr )
{
    if ( adapter == range_adapter_t::CAMERA ) {
        auto desc = fixture.CameraDescription();
        render::render_scene_item_t items[8]{};
        common::u32 indices[8]{};
        desc.meshes = meshes;
        desc.meshCount = meshCount;
        desc.meshRanges = ranges;
        desc.meshRangeCount = rangeCount;
        if ( materialOverride != nullptr ) {
            desc.materials = materialOverride;
            desc.materialCount = 1u;
        }
        desc.modelLayout = render::render_world_model_layout_t::MODEL_CUTOUT;
        desc.sceneItemsScratch = items;
        desc.sceneItemCapacity = 8u;
        desc.passIndicesScratch = indices;
        desc.passIndexCapacity = 8u;
        return render::R_PrepareWorldCameraDraws(
            desc, output, capacity, outCameraCounts );
    }
    if ( adapter == range_adapter_t::SHADOW ) {
        auto desc = fixture.ShadowDescription();
        desc.meshes = meshes;
        desc.meshCount = meshCount;
        desc.meshRanges = ranges;
        desc.meshRangeCount = rangeCount;
        if ( materialOverride != nullptr ) {
            desc.materials = materialOverride;
            desc.materialCount = 1u;
        }
        return render::R_PrepareWorldShadowDraws(
            desc, output, capacity, outCount );
    }
    auto desc = fixture.Description();
    desc.meshes = meshes;
    desc.meshCount = meshCount;
    desc.meshRanges = ranges;
    desc.meshRangeCount = rangeCount;
    if ( materialOverride != nullptr ) {
        desc.materials = materialOverride;
        desc.materialCount = 1u;
    }
    desc.modelLayout = render::render_world_model_layout_t::MODEL_CUTOUT;
    return render::R_PrepareOpaqueWorldDraws(
        desc, output, capacity, outCount );
}

} // namespace

TEST_CASE( "World lighting View record packs camera and caller-supplied lights",
    "[CypherRender][WorldDraw][Lighting]" )
{
    using record_t = render::render_world_view_lighting_record_t;
    CHECK( sizeof( record_t ) == 256u );
    CHECK( alignof( record_t ) == 16u );
    CHECK( offsetof( record_t, view ) == 0u );
    CHECK( offsetof( record_t, projection ) == 64u );
    CHECK( offsetof( record_t, ambientLight ) == 128u );
    CHECK( offsetof( record_t, lightDirection ) == 144u );
    CHECK( offsetof( record_t, lightColor ) == 160u );
    CHECK( offsetof( record_t, pointLightPositionRange ) == 176u );
    CHECK( offsetof( record_t, pointLightColorIntensity ) == 208u );
    CHECK( offsetof( record_t, pointLightControl ) == 240u );

    render::render_world_view_lighting_desc_t description{};
    description.viewFromWorld.m[12] = -3.0f;
    description.clipFromView.m[0] = 2.0f;
    description.ambientLight = { 2.5f, 0.25f, 0.0f }; // HDR is allowed.
    description.directionToLight = { 0.0f, 0.0f, 2.0f };
    description.directionalRadiance = { 4.0f, 1.5f, 0.0f };
    // The inactive slot is ignored, even if an authoring tool left bad data.
    description.pointLights[1].range =
        std::numeric_limits<common::f32>::quiet_NaN();

    record_t record{};
    REQUIRE( render::R_PackWorldViewLighting( description, &record ) ==
        render::render_error_t::OK );
    CHECK( record.view.m[12] == -3.0f );
    CHECK( record.projection.m[0] == 2.0f );
    CHECK( record.ambientLight[0] == 2.5f );
    CHECK( record.ambientLight[1] == 0.25f );
    CHECK( record.ambientLight[3] == 0.0f );
    CHECK( record.lightDirection[2] == 1.0f );
    CHECK( record.lightDirection[3] == 0.0f );
    CHECK( record.lightColor[0] == 4.0f );
    CHECK( record.lightColor[1] == 1.5f );
    CHECK( record.lightColor[3] == 0.0f );
    for ( common::u32 i = 0u; i < 2u; ++i ) {
        for ( common::u32 lane = 0u; lane < 4u; ++lane ) {
            CHECK( record.pointLightPositionRange[i][lane] == 0.0f );
            CHECK( record.pointLightColorIntensity[i][lane] == 0.0f );
        }
    }
    for ( const auto lane : record.pointLightControl ) CHECK( lane == 0 );

    description.pointLightCount = 1u;
    description.pointLights[0] = {
        { 1.0f, -2.0f, 3.0f }, 5.0f,
        { 2.0f, 0.5f, 0.0f }, 1.25f
    };
    REQUIRE( render::R_PackWorldViewLighting( description, &record ) ==
        render::render_error_t::OK );
    CHECK( record.pointLightControl[0] == 1 );
    CHECK( record.pointLightPositionRange[0][0] == 1.0f );
    CHECK( record.pointLightPositionRange[0][1] == -2.0f );
    CHECK( record.pointLightPositionRange[0][2] == 3.0f );
    CHECK( record.pointLightPositionRange[0][3] == 5.0f );
    CHECK( record.pointLightColorIntensity[0][0] == 2.0f );
    CHECK( record.pointLightColorIntensity[0][1] == 0.5f );
    CHECK( record.pointLightColorIntensity[0][3] == 1.25f );
    for ( common::u32 lane = 0u; lane < 4u; ++lane ) {
        CHECK( record.pointLightPositionRange[1][lane] == 0.0f );
        CHECK( record.pointLightColorIntensity[1][lane] == 0.0f );
    }

    description.pointLightCount = 2u;
    description.pointLights[1] = {
        { -4.0f, 0.0f, 8.0f }, 16.0f,
        { 0.0f, 0.0f, 3.0f }, 2.0f
    };
    REQUIRE( render::R_PackWorldViewLighting( description, &record ) ==
        render::render_error_t::OK );
    CHECK( record.pointLightControl[0] == 2 );
    CHECK( record.pointLightControl[1] == 0 );
    CHECK( record.pointLightPositionRange[1][0] == -4.0f );
    CHECK( record.pointLightPositionRange[1][2] == 8.0f );
    CHECK( record.pointLightPositionRange[1][3] == 16.0f );
    CHECK( record.pointLightColorIntensity[1][2] == 3.0f );
    CHECK( record.pointLightColorIntensity[1][3] == 2.0f );
}

TEST_CASE( "World lighting View validation leaves output unchanged",
    "[CypherRender][WorldDraw][Lighting]" )
{
    render::render_world_view_lighting_desc_t valid{};
    valid.pointLightCount = 1u;
    valid.pointLights[0] = {
        { 0.0f, 0.0f, 2.0f }, 4.0f,
        { 1.0f, 0.5f, 0.0f }, 2.0f
    };
    render::render_world_view_lighting_record_t record{};
    std::memset( &record, 0x5a, sizeof( record ) );
    const auto original = record;
    const auto reject = [&]( const render::render_world_view_lighting_desc_t &input ) {
        CHECK( render::R_PackWorldViewLighting( input, &record ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CHECK( std::memcmp( &record, &original, sizeof( record ) ) == 0 );
    };
    const auto nan = std::numeric_limits<common::f32>::quiet_NaN();
    const auto inf = std::numeric_limits<common::f32>::infinity();
    auto invalid = valid;
    invalid.pointLightCount = 3u;
    reject( invalid );
    invalid = valid;
    invalid.viewFromWorld.m[0] = nan;
    reject( invalid );
    invalid = valid;
    invalid.viewFromWorld.m[3] = 1.0f; // Not affine.
    reject( invalid );
    invalid = valid;
    invalid.clipFromView.m[5] = inf;
    reject( invalid );
    invalid = valid;
    invalid.ambientLight.y = -1.0f;
    reject( invalid );
    invalid = valid;
    invalid.directionToLight = math::CY_VEC3_ZERO;
    reject( invalid );
    invalid = valid;
    invalid.directionToLight.x = nan;
    reject( invalid );
    invalid = valid;
    invalid.directionalRadiance.z = -0.1f;
    reject( invalid );
    invalid = valid;
    invalid.pointLights[0].position.x = inf;
    reject( invalid );
    invalid = valid;
    invalid.pointLights[0].range = 0.0f;
    reject( invalid );
    invalid = valid;
    invalid.pointLights[0].range = inf;
    reject( invalid );
    invalid = valid;
    invalid.pointLights[0].color.y = nan;
    reject( invalid );
    invalid = valid;
    invalid.pointLights[0].color.y = -0.1f;
    reject( invalid );
    invalid = valid;
    invalid.pointLights[0].intensity = -1.0f;
    reject( invalid );
    invalid = valid;
    invalid.pointLights[0].intensity = nan;
    reject( invalid );
    CHECK( render::R_PackWorldViewLighting( valid, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
}

TEST_CASE( "shadowed World View extends the existing lighting record",
    "[CypherRender][WorldDraw][Lighting]" )
{
    using record_t = render::render_world_shadowed_view_lighting_record_t;
    CHECK( sizeof( record_t ) == 320u );
    CHECK( alignof( record_t ) == 16u );
    CHECK( offsetof( record_t, lighting ) == 0u );
    CHECK( offsetof( record_t, lightClipFromWorld ) == 256u );

    render::render_world_view_lighting_desc_t description{};
    description.viewFromWorld.m[12] = -4.0f;
    description.clipFromView.m[0] = 1.75f;
    description.ambientLight = { 0.25f, 0.5f, 1.0f };
    description.directionToLight = { 0.0f, 2.0f, 0.0f };
    description.directionalRadiance = { 3.0f, 2.0f, 1.0f };
    description.pointLightCount = 1u;
    description.pointLights[0] = {
        { 3.0f, -2.0f, 6.0f }, 8.0f,
        { 1.0f, 0.75f, 0.25f }, 2.0f
    };
    math::mat4_t lightClipFromWorld = math::CY_MAT4_IDENTITY;
    lightClipFromWorld.m[3] = 0.125f; // A finite projective matrix is valid.
    lightClipFromWorld.m[11] = -0.5f;
    lightClipFromWorld.m[12] = 7.0f;

    render::render_world_view_lighting_record_t existing{};
    REQUIRE( render::R_PackWorldViewLighting( description, &existing ) ==
        render::render_error_t::OK );
    record_t extended{};
    REQUIRE( render::R_PackWorldShadowedViewLighting(
                 description, lightClipFromWorld, &extended ) ==
        render::render_error_t::OK );
    CHECK( std::memcmp( &extended.lighting, &existing, sizeof( existing ) ) == 0 );
    CHECK( std::memcmp( &extended.lightClipFromWorld, &lightClipFromWorld,
               sizeof( lightClipFromWorld ) ) == 0 );
}

TEST_CASE( "shadowed World View rejects invalid inputs without changing output",
    "[CypherRender][WorldDraw][Lighting]" )
{
    render::render_world_view_lighting_desc_t valid{};
    math::mat4_t lightClipFromWorld = math::CY_MAT4_IDENTITY;
    render::render_world_shadowed_view_lighting_record_t output{};
    std::memset( &output, 0x5a, sizeof( output ) );
    const auto original = output;
    const auto reject = [&]( const render::render_world_view_lighting_desc_t &lighting,
                            const math::mat4_t &lightMatrix ) {
        CHECK( render::R_PackWorldShadowedViewLighting(
                   lighting, lightMatrix, &output ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CHECK( std::memcmp( &output, &original, sizeof( output ) ) == 0 );
    };

    lightClipFromWorld.m[5] = std::numeric_limits<common::f32>::quiet_NaN();
    reject( valid, lightClipFromWorld );
    lightClipFromWorld = math::CY_MAT4_IDENTITY;
    lightClipFromWorld.m[15] = std::numeric_limits<common::f32>::infinity();
    reject( valid, lightClipFromWorld );
    lightClipFromWorld = math::CY_MAT4_IDENTITY;
    valid.directionalRadiance.x = -0.25f;
    reject( valid, lightClipFromWorld );
    valid.directionalRadiance.x = 0.0f;
    valid.pointLightCount = 1u;
    valid.pointLights[0].range = 0.0f;
    reject( valid, lightClipFromWorld );
    CHECK( render::R_PackWorldShadowedViewLighting(
               valid, lightClipFromWorld, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( std::memcmp( &output, &original, sizeof( output ) ) == 0 );
}

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
    const auto description = fixture.Description();
    CHECK( description.modelLayout == render::render_world_model_layout_t::MODEL_ONLY );
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        description, output, 3u, &count ) == render::render_error_t::OK );
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

TEST_CASE( "World lit Model records pack inverse-transpose columns after the model matrix",
    "[CypherRender][WorldDraw][LitModel]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnableLitMaterial( 0u );
    const auto sheared = fixture.Add( 0u, 0u, 0.0f );
    const auto mirrored = fixture.Add( 0u, 0u, 4.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject( fixture.pWorld, sheared, &object ) ==
        world::world_error_t::OK );
    object.localToWorld = math::Affine3_FromColumns(
        { 2.0f, 0.0f, 0.0f }, { 1.0f, 3.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f }, { 0.0f, 0.0f, 0.0f } );
    REQUIRE( world::World_UpdateObject( fixture.pWorld, sheared, &object ) ==
        world::world_error_t::OK );
    REQUIRE( world::World_GetObject( fixture.pWorld, mirrored, &object ) ==
        world::world_error_t::OK );
    object.localToWorld = math::Affine3_FromScale( { -2.0f, 3.0f, 4.0f } );
    REQUIRE( world::World_UpdateObject( fixture.pWorld, mirrored, &object ) ==
        world::world_error_t::OK );
    fixture.Build();

    auto description = fixture.Description();
    description.modelLayout = render::render_world_model_layout_t::MODEL_NORMAL;
    render::render_world_opaque_draw_t output[2]{};
    common::u32 count = 0u;
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        description, output, 2u, &count ) == render::render_error_t::OK );
    REQUIRE( count == 2u );
    CHECK( output[0].objectId.slot.value == sheared.slot.value );
    CHECK( output[1].objectId.slot.value == mirrored.slot.value );
    CHECK( output[0].draw.pipeline.value == fixture.litPipeline.value );
    CHECK( output[1].draw.pipeline.value == fixture.mirroredLitPipeline.value );
    CHECK( output[0].draw.uniformRanges[1].offset == 0u );
    CHECK( output[1].draw.uniformRanges[1].offset == 256u );
    CHECK( output[0].draw.uniformRanges[1].byteSize == 112u );
    CHECK( output[1].draw.uniformRanges[1].byteSize == 112u );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( fixture.uploadedSize == 368u );

    // std140 mat3 columns have a fourth padding float; this record is exactly
    // a 64-byte column-major mat4 followed by three 16-byte normal columns.
    common::f32 shearRecord[28]{};
    common::f32 mirroredRecord[28]{};
    std::memcpy( shearRecord, fixture.uploaded, sizeof( shearRecord ) );
    std::memcpy( mirroredRecord, fixture.uploaded + 256u,
        sizeof( mirroredRecord ) );
    CHECK( shearRecord[0] == 2.0f );
    CHECK( shearRecord[4] == 1.0f );
    CHECK( shearRecord[5] == 3.0f );
    CHECK( shearRecord[10] == 4.0f );
    CHECK( shearRecord[15] == 1.0f );
    CHECK( shearRecord[16] == 0.5f );
    CHECK( std::abs( shearRecord[17] + 1.0f / 6.0f ) < 1.0e-6f );
    CHECK( shearRecord[18] == 0.0f );
    CHECK( shearRecord[20] == 0.0f );
    CHECK( std::abs( shearRecord[21] - 1.0f / 3.0f ) < 1.0e-6f );
    CHECK( shearRecord[22] == 0.0f );
    CHECK( shearRecord[24] == 0.0f );
    CHECK( shearRecord[25] == 0.0f );
    CHECK( shearRecord[26] == 0.25f );
    CHECK( shearRecord[19] == 0.0f );
    CHECK( shearRecord[23] == 0.0f );
    CHECK( shearRecord[27] == 0.0f );

    CHECK( mirroredRecord[0] == -2.0f );
    CHECK( mirroredRecord[5] == 3.0f );
    CHECK( mirroredRecord[10] == 4.0f );
    CHECK( mirroredRecord[16] == -0.5f );
    CHECK( std::abs( mirroredRecord[21] - 1.0f / 3.0f ) < 1.0e-6f );
    CHECK( mirroredRecord[26] == 0.25f );
    CHECK( mirroredRecord[19] == 0.0f );
    CHECK( mirroredRecord[23] == 0.0f );
    CHECK( mirroredRecord[27] == 0.0f );
}

TEST_CASE( "World camera draws opt into the lit Model record",
    "[CypherRender][WorldDraw][LitModel][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnableLitMaterial( 0u );
    const auto id = fixture.AddAtZ( 0u, 0.0f, -4.0f );
    fixture.Build();
    render::render_scene_item_t sceneItem{};
    common::u32 passIndex{};
    auto description = fixture.CameraDescription();
    description.modelLayout = render::render_world_model_layout_t::MODEL_NORMAL;
    description.sceneItemsScratch = &sceneItem;
    description.sceneItemCapacity = 1u;
    description.passIndicesScratch = &passIndex;
    description.passIndexCapacity = 1u;
    render::render_world_camera_draw_t output{};
    render::render_world_camera_pass_counts_t counts{};
    REQUIRE( render::R_PrepareWorldCameraDraws(
        description, &output, 1u, &counts ) == render::render_error_t::OK );
    CHECK( counts.opaqueCount == 1u );
    CHECK( counts.blendCount == 0u );
    CHECK( counts.additiveCount == 0u );
    CHECK( output.objectId.slot.value == id.slot.value );
    CHECK( output.draw.uniformRanges[1].byteSize == 112u );
    CHECK( fixture.uploadedSize == 112u );
    common::f32 normalColumns[12]{};
    std::memcpy( normalColumns, fixture.uploaded + 64u,
        sizeof( normalColumns ) );
    CHECK( normalColumns[0] == 1.0f );
    CHECK( normalColumns[5] == 1.0f );
    CHECK( normalColumns[10] == 1.0f );
    CHECK( normalColumns[3] == 0.0f );
    CHECK( normalColumns[7] == 0.0f );
    CHECK( normalColumns[11] == 0.0f );
}

TEST_CASE( "World lit preflight rejects invalid normals and block sizes without staging",
    "[CypherRender][WorldDraw][LitModel]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    (void)fixture.Add( 0u, 0u, 0.0f ); // A valid object precedes the bad one.
    const auto bad = fixture.Add( 0u, 0u, 4.0f );
    SECTION( "zero determinant" ) {
        fixture.EnableLitMaterial( 0u );
        world::world_object_t object{};
        REQUIRE( world::World_GetObject( fixture.pWorld, bad, &object ) ==
            world::world_error_t::OK );
        object.localToWorld = math::Affine3_FromScale( { 0.0f, 1.0f, 1.0f } );
        REQUIRE( world::World_UpdateObject( fixture.pWorld, bad, &object ) ==
            world::world_error_t::OK );
    }
    SECTION( "inverse cannot fit a finite float normal column" ) {
        fixture.EnableLitMaterial( 0u );
        world::world_object_t object{};
        REQUIRE( world::World_GetObject( fixture.pWorld, bad, &object ) ==
            world::world_error_t::OK );
        object.localToWorld = math::Affine3_FromScale( { 1.0e-40f, 1.0f, 1.0f } );
        REQUIRE( world::World_UpdateObject( fixture.pWorld, bad, &object ) ==
            world::world_error_t::OK );
    }
    SECTION( "pipeline still declares only a 64-byte Model block" ) {
        // No lit pipeline: the requested record must match the actual block.
    }
    fixture.Build();
    auto description = fixture.Description();
    description.modelLayout = render::render_world_model_layout_t::MODEL_NORMAL;

    render::render_world_opaque_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    const auto error = render::R_PrepareOpaqueWorldDraws(
        description, output, 2u, &count );
    CHECK( error == ( fixture.litPipeline.value != 0u
        ? render::render_error_t::ERR_UNSUPPORTED
        : render::render_error_t::ERR_BINDING_MISMATCH ) );
    CHECK( count == 77u );
    CHECK( output[0].draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World lit capacity and upload failures keep public draws unpublished",
    "[CypherRender][WorldDraw][LitModel]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnableLitMaterial( 0u );
    (void)fixture.Add( 0u, 0u, 0.0f );
    (void)fixture.Add( 0u, 0u, 4.0f );
    fixture.Build();
    auto description = fixture.Description();
    description.modelLayout = render::render_world_model_layout_t::MODEL_NORMAL;
    render::render_world_opaque_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    common::u32 count = 77u;

    SECTION( "the aligned second record exceeds capacity" ) {
        REQUIRE( render::R_DestroyUniformBatch( &fixture.batch ) ==
            render::render_error_t::OK );
        REQUIRE( render::R_CreateUniformBatch(
            320u, common::Allocator_GetSystem(), &fixture.batch ) ==
            render::render_error_t::OK );
        CHECK( render::R_PrepareOpaqueWorldDraws(
            description, output, 2u, &count ) ==
            render::render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED );
        CHECK( fixture.batch.usedBytes == 0u );
        CHECK_FALSE( fixture.batch.sealed );
        CHECK( fixture.uploadCalls == 0u );
    }
    SECTION( "backend upload fails after staging both records" ) {
        fixture.failNextUpload = true;
        CHECK( render::R_PrepareOpaqueWorldDraws(
            description, output, 2u, &count ) ==
            render::render_error_t::ERR_RESOURCE_TRANSFER_FAILED );
        CHECK( fixture.batch.usedBytes == 368u );
        CHECK( fixture.batch.sealed );
        CHECK( fixture.uploadCalls == 1u );
        REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
            render::render_error_t::OK );
        CHECK( fixture.uploadCalls == 2u );
    }
    CHECK( count == 77u );
    CHECK( output[0].draw.pipeline.value == 12345u );
}

TEST_CASE( "World camera keeps MASK in the opaque span with per-object cutoffs",
    "[CypherRender][WorldDraw][Cutout][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnableCutoutMaterial( 0u, false, false );
    fixture.EnableCutoutMaterial( 1u, false, true );
    REQUIRE( render::R_DestroyUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateUniformBatch(
        640u, common::Allocator_GetSystem(), &fixture.batch ) ==
        render::render_error_t::OK );
    fixture.materials[1].alphaCutoff = 0.375f;
    const auto opaqueFirst = fixture.AddAtZ( 0u, 0.0f, -2.0f );
    const auto masked = fixture.AddAtZ( 1u, 4.0f, -4.0f );
    const auto opaqueLast = fixture.AddAtZ( 0u, 8.0f, -6.0f );
    fixture.Build();

    render::render_scene_item_t sceneItems[3]{};
    common::u32 passIndices[3]{};
    auto description = fixture.CameraDescription();
    description.modelLayout = render::render_world_model_layout_t::MODEL_CUTOUT;
    description.sceneItemsScratch = sceneItems;
    description.sceneItemCapacity = 3u;
    description.passIndicesScratch = passIndices;
    description.passIndexCapacity = 3u;
    render::render_world_camera_draw_t output[3]{};
    render::render_world_camera_pass_counts_t counts{};
    REQUIRE( render::R_PrepareWorldCameraDraws(
        description, output, 3u, &counts ) == render::render_error_t::OK );
    CHECK( counts.opaqueCount == 3u );
    CHECK( counts.blendCount == 0u );
    CHECK( counts.additiveCount == 0u );
    CHECK( output[0].objectId.slot.value == opaqueFirst.slot.value );
    CHECK( output[1].objectId.slot.value == masked.slot.value );
    CHECK( output[2].objectId.slot.value == opaqueLast.slot.value );
    CHECK( output[1].draw.sampledTextures[0].value == fixture.alphaTexture.value );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( fixture.uploadedSize == 592u );
    const common::f32 expectedCutoffs[3]{ -1.0f, 0.375f, -1.0f };
    for ( common::u32 i = 0u; i < 3u; ++i ) {
        CHECK( output[i].draw.uniformRanges[1].offset == i * 256u );
        CHECK( output[i].draw.uniformRanges[1].byteSize == 80u );
        common::f32 cutout[4]{};
        std::memcpy( cutout, fixture.uploaded + i * 256u + 64u,
            sizeof( cutout ) );
        CHECK( cutout[0] == expectedCutoffs[i] );
        CHECK( cutout[1] == 0.0f );
        CHECK( cutout[2] == 0.0f );
        CHECK( cutout[3] == 0.0f );
    }
}

TEST_CASE( "World camera preserves a pass-owned shadow pair only for its material",
    "[CypherRender][WorldDraw][CameraPasses][ShadowSampler]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnablePassShadowMaterial( 0u );
    fixture.EnableCutoutMaterial( 1u, false, false );
    fixture.CreatePassShadowBindings();
    const auto ordinary = fixture.Add( 0u, 1u, 0.0f );
    const auto shadowed = fixture.Add( 0u, 0u, 4.0f );
    fixture.Build();

    render::render_scene_item_t sceneItems[2]{};
    common::u32 passIndices[2]{};
    auto description = fixture.CameraDescription();
    description.modelLayout = render::render_world_model_layout_t::MODEL_CUTOUT;
    description.sceneItemsScratch = sceneItems;
    description.sceneItemCapacity = 2u;
    description.passIndicesScratch = passIndices;
    description.passIndexCapacity = 2u;
    description.drawTemplate.sampledTextures[1] = fixture.passDepthTexture;
    description.drawTemplate.samplers[1] = fixture.passCompareSampler;

    render::render_world_camera_draw_t output[2]{};
    render::render_world_camera_pass_counts_t counts{};
    REQUIRE( render::R_PrepareWorldCameraDraws(
        description, output, 2u, &counts ) == render::render_error_t::OK );
    CHECK( counts.opaqueCount == 2u );
    CHECK( output[0].objectId.slot.value == ordinary.slot.value );
    CHECK( output[1].objectId.slot.value == shadowed.slot.value );
    CHECK( output[0].draw.sampledTextures[1].value == 0u );
    CHECK( output[0].draw.samplers[1].value == 0u );
    CHECK( output[1].draw.sampledTextures[0].value == fixture.alphaTexture.value );
    CHECK( output[1].draw.sampledTextures[1].value == fixture.passDepthTexture.value );
    CHECK( output[1].draw.samplers[1].value == fixture.passCompareSampler.value );
    CHECK( fixture.uploadCalls == 1u );
}

TEST_CASE( "World shadow sampler preflight rejects invalid pass bindings atomically",
    "[CypherRender][WorldDraw][CameraPasses][ShadowSampler]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnablePassShadowMaterial( 0u );
    fixture.EnableCutoutMaterial( 1u, false, false );
    fixture.CreatePassShadowBindings();
    (void)fixture.Add( 0u, 1u, 0.0f ); // Valid object before invalid binding.
    (void)fixture.Add( 0u, 0u, 4.0f );
    fixture.Build();

    render::render_scene_item_t sceneItems[2]{};
    common::u32 passIndices[2]{};
    auto description = fixture.CameraDescription();
    description.modelLayout = render::render_world_model_layout_t::MODEL_CUTOUT;
    description.sceneItemsScratch = sceneItems;
    description.sceneItemCapacity = 2u;
    description.passIndicesScratch = passIndices;
    description.passIndexCapacity = 2u;
    description.drawTemplate.sampledTextures[1] = fixture.passDepthTexture;
    description.drawTemplate.samplers[1] = fixture.passCompareSampler;

    auto expected = render::render_error_t::ERR_INVALID_HANDLE;
    SECTION( "missing depth texture" ) {
        description.drawTemplate.sampledTextures[1] = {};
    }
    SECTION( "stale depth texture" ) {
        const auto stale = fixture.passDepthTexture;
        REQUIRE( render::R_DestroyTexture( fixture.passDepthTexture ) ==
            render::render_error_t::OK );
        fixture.passDepthTexture = {};
        description.drawTemplate.sampledTextures[1] = stale;
        expected = render::render_error_t::ERR_STALE_HANDLE;
    }
    SECTION( "color texture in shadow slot" ) {
        description.drawTemplate.sampledTextures[1] = fixture.alphaTexture;
        expected = render::render_error_t::ERR_BINDING_MISMATCH;
    }
    SECTION( "missing comparison sampler" ) {
        description.drawTemplate.samplers[1] = {};
    }
    SECTION( "stale comparison sampler" ) {
        const auto stale = fixture.passCompareSampler;
        REQUIRE( render::R_DestroySampler( fixture.passCompareSampler ) ==
            render::render_error_t::OK );
        fixture.passCompareSampler = {};
        description.drawTemplate.samplers[1] = stale;
        expected = render::render_error_t::ERR_STALE_HANDLE;
    }
    SECTION( "sampler without depth comparison" ) {
        REQUIRE( render::R_CreateSampler( {}, &fixture.passRawSampler ) ==
            render::render_error_t::OK );
        description.drawTemplate.samplers[1] = fixture.passRawSampler;
        expected = render::render_error_t::ERR_BINDING_MISMATCH;
    }

    render::render_world_camera_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    output[1].draw.pipeline.value = 54321u;
    render::render_world_camera_pass_counts_t counts{ 77u, 88u, 99u };
    const auto result = render::R_PrepareWorldCameraDraws(
        description, output, 2u, &counts );
    CHECK( result == expected );
    CHECK( counts.opaqueCount == 77u );
    CHECK( counts.blendCount == 88u );
    CHECK( counts.additiveCount == 99u );
    CHECK( output[0].draw.pipeline.value == 12345u );
    CHECK( output[1].draw.pipeline.value == 54321u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World normal-and-cutout Model record packs normals before alpha cutoff",
    "[CypherRender][WorldDraw][Cutout][LitModel]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnableCutoutMaterial( 0u, true, true );
    fixture.materials[0].alphaCutoff = 0.625f;
    const auto id = fixture.Add( 0u, 0u, 0.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject( fixture.pWorld, id, &object ) ==
        world::world_error_t::OK );
    object.localToWorld = math::Affine3_FromColumns(
        { 2.0f, 0.0f, 0.0f }, { 1.0f, 3.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f }, { 0.0f, 0.0f, 0.0f } );
    REQUIRE( world::World_UpdateObject( fixture.pWorld, id, &object ) ==
        world::world_error_t::OK );
    fixture.Build();

    auto description = fixture.Description();
    description.modelLayout = render::render_world_model_layout_t::MODEL_NORMAL_CUTOUT;
    render::render_world_opaque_draw_t output{};
    common::u32 count = 0u;
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        description, &output, 1u, &count ) == render::render_error_t::OK );
    CHECK( count == 1u );
    CHECK( output.draw.uniformRanges[1].byteSize == 128u );
    CHECK( output.draw.sampledTextures[0].value == fixture.alphaTexture.value );
    CHECK( fixture.uploadedSize == 128u );
    common::f32 normalColumns[12]{};
    std::memcpy( normalColumns, fixture.uploaded + 64u,
        sizeof( normalColumns ) );
    CHECK( normalColumns[0] == 0.5f );
    CHECK( std::abs( normalColumns[1] + 1.0f / 6.0f ) < 1.0e-6f );
    CHECK( std::abs( normalColumns[5] - 1.0f / 3.0f ) < 1.0e-6f );
    CHECK( normalColumns[10] == 0.25f );
    CHECK( normalColumns[3] == 0.0f );
    CHECK( normalColumns[7] == 0.0f );
    CHECK( normalColumns[11] == 0.0f );
    common::f32 cutout[4]{};
    std::memcpy( cutout, fixture.uploaded + 112u, sizeof( cutout ) );
    CHECK( cutout[0] == 0.625f );
    CHECK( cutout[1] == 0.0f );
    CHECK( cutout[2] == 0.0f );
    CHECK( cutout[3] == 0.0f );
}

TEST_CASE( "World MASK requires an opt-in cutout Model layout before staging",
    "[CypherRender][WorldDraw][Cutout]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnableCutoutMaterial( 1u, false, true );
    (void)fixture.Add( 0u, 0u, 0.0f ); // Valid opaque object precedes MASK.
    (void)fixture.Add( 0u, 1u, 4.0f );
    fixture.Build();
    auto description = fixture.Description();
    SECTION( "MODEL_ONLY" ) {
        description.modelLayout = render::render_world_model_layout_t::MODEL_ONLY;
    }
    SECTION( "MODEL_NORMAL" ) {
        fixture.EnableLitMaterial( 0u );
        description.modelLayout = render::render_world_model_layout_t::MODEL_NORMAL;
    }
    render::render_world_opaque_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        description, output, 2u, &count ) == render::render_error_t::ERR_UNSUPPORTED );
    CHECK( count == 77u );
    CHECK( output[0].draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World MASK preflight rejects invalid cutoff or alpha source atomically",
    "[CypherRender][WorldDraw][Cutout]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.EnableCutoutMaterial( 0u, false, true );
    fixture.EnableCutoutMaterial( 1u, false, true );
    (void)fixture.Add( 0u, 0u, 0.0f ); // Valid MASK object precedes bad one.
    (void)fixture.Add( 0u, 1u, 4.0f );
    fixture.Build();
    auto description = fixture.Description();
    description.modelLayout = render::render_world_model_layout_t::MODEL_CUTOUT;
    render::render_error_t expected = render::render_error_t::ERR_UNSUPPORTED;
    SECTION( "cutoff below zero" ) {
        fixture.materials[1].alphaCutoff = -0.01f;
    }
    SECTION( "cutoff above one" ) {
        fixture.materials[1].alphaCutoff = 1.01f;
    }
    SECTION( "nonfinite cutoff" ) {
        fixture.materials[1].alphaCutoff =
            std::numeric_limits<common::f32>::quiet_NaN();
    }
    SECTION( "missing alpha source" ) {
        fixture.materials[1].textureCount = 0u;
    }
    SECTION( "stale alpha source" ) {
        fixture.materials[1].textures[0] = {};
        expected = render::render_error_t::ERR_INVALID_HANDLE;
    }
    render::render_world_opaque_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        description, output, 2u, &count ) == expected );
    CHECK( count == 77u );
    CHECK( output[0].draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );

    render::render_scene_item_t sceneItems[2]{};
    common::u32 passIndices[2]{};
    auto cameraDescription = fixture.CameraDescription();
    cameraDescription.modelLayout = render::render_world_model_layout_t::MODEL_CUTOUT;
    cameraDescription.sceneItemsScratch = sceneItems;
    cameraDescription.sceneItemCapacity = 2u;
    cameraDescription.passIndicesScratch = passIndices;
    cameraDescription.passIndexCapacity = 2u;
    render::render_world_camera_draw_t cameraOutput[2]{};
    cameraOutput[0].draw.pipeline.value = 54321u;
    render::render_world_camera_pass_counts_t counts{ 77u, 88u, 99u };
    CHECK( render::R_PrepareWorldCameraDraws(
        cameraDescription, cameraOutput, 2u, &counts ) == expected );
    CHECK( counts.opaqueCount == 77u );
    CHECK( counts.blendCount == 88u );
    CHECK( counts.additiveCount == 99u );
    CHECK( cameraOutput[0].draw.pipeline.value == 54321u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World draw preparation carries an owned material uniform binding",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_DestroyGraphicsPipeline( fixture.pipelines[1] ) ==
        render::render_error_t::OK );
    fixture.pipelines[1] = {};

    render::render_vertex_input_info_t input{};
    REQUIRE( render::R_GetVertexInputInfo( fixture.mesh.vertexInput, &input ) ==
        render::render_error_t::OK );
    render::render_pipeline_desc_t pipeline{};
    pipeline.shader = fixture.shader;
    pipeline.vertexLayout = input.layout;
    pipeline.uniformBlockCount = 3u;
    pipeline.uniformBlocks[0] = { "View", 64u };
    pipeline.uniformBlocks[1] = { "Model", 64u };
    pipeline.uniformBlocks[2] = { "Material", 16u };
    REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &fixture.pipelines[1] ) ==
        render::render_error_t::OK );
    fixture.materials[1].pipeline = fixture.pipelines[1];

    const common::f32 tint[4]{ 0.25f, 0.5f, 0.75f, 1.0f };
    render::render_buffer_desc_t materialDesc{};
    materialDesc.byteSize = sizeof( tint );
    materialDesc.usage = render::R_BUFFER_USAGE_UNIFORM;
    materialDesc.memory = render::render_buffer_memory_t::UPLOAD;
    materialDesc.updatePolicy = render::render_buffer_update_t::DYNAMIC;
    const render::render_buffer_data_t materialData{ tint, sizeof( tint ) };
    REQUIRE( render::R_CreateBuffer( materialDesc,
        &materialData,
        &fixture.materialBuffer ) == render::render_error_t::OK );
    fixture.materials[1].materialUniformBuffer = fixture.materialBuffer;
    fixture.materials[1].materialUniformSlot = 2u;

    const auto id = fixture.Add( 0u, 1u, 0.0f );
    fixture.Build();
    render::render_world_opaque_draw_t output{};
    common::u32 count = 0u;
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), &output, 1u, &count ) ==
        render::render_error_t::OK );
    CHECK( count == 1u );
    CHECK( output.objectId.slot.value == id.slot.value );
    CHECK( output.draw.uniformBuffers[0].value == fixture.viewBuffer.value );
    CHECK( output.draw.uniformBuffers[1].value == fixture.batch.buffer.value );
    CHECK( output.draw.uniformBuffers[2].value == fixture.materialBuffer.value );
    CHECK( output.draw.uniformRanges[2].offset == 0u );
    CHECK( output.draw.uniformRanges[2].byteSize == 0u );
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

TEST_CASE( "World draw preparation selects mirrored winding for negative scales",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    const auto mirrored = fixture.Add( 0u, 0u, 0.0f );
    const auto ordinary = fixture.Add( 0u, 1u, 4.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject(
        fixture.pWorld, mirrored, &object ) == world::world_error_t::OK );
    object.localToWorld = math::Affine3_FromScale(
        { -0.001f, 0.001f, 0.001f } );
    REQUIRE( world::World_UpdateObject(
        fixture.pWorld, mirrored, &object ) == world::world_error_t::OK );
    fixture.Build();

    render::render_world_opaque_draw_t output[2]{};
    common::u32 count = 0u;
    REQUIRE( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), output, 2u, &count ) == render::render_error_t::OK );
    REQUIRE( count == 2u );
    CHECK( output[0].objectId.slot.value == mirrored.slot.value );
    CHECK( output[1].objectId.slot.value == ordinary.slot.value );
    CHECK( output[0].draw.pipeline.value == fixture.mirroredPipelines[0].value );
    CHECK( output[1].draw.pipeline.value == fixture.pipelines[1].value );
    render::render_pipeline_info_t mirroredInfo{};
    REQUIRE( render::R_GetGraphicsPipelineInfo(
        output[0].draw.pipeline, &mirroredInfo ) == render::render_error_t::OK );
    CHECK_FALSE( mirroredInfo.frontCounterClockwise );
    CHECK( mirroredInfo.cullBackFaces );
    math::mat4_t model{};
    std::memcpy( &model, fixture.uploaded, sizeof( model ) );
    CHECK( model.m[0] == -0.001f );
    CHECK( model.m[5] == 0.001f );
    CHECK( model.m[10] == 0.001f );
}

TEST_CASE( "World draw preflight rejects a missing mirrored pipeline without staging",
    "[CypherRender][WorldDraw]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    const auto id = fixture.Add( 0u, 0u, 0.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject(
        fixture.pWorld, id, &object ) == world::world_error_t::OK );
    object.localToWorld = math::Affine3_FromScale( { -1.0f, 1.0f, 1.0f } );
    REQUIRE( world::World_UpdateObject(
        fixture.pWorld, id, &object ) == world::world_error_t::OK );
    fixture.Build();
    fixture.materials[0].mirroredPipeline = {};

    render::render_world_opaque_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), &output, 1u, &count ) != render::render_error_t::OK );
    CHECK( count == 77u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World camera draws place opaque, far-to-near blend, then additive",
    "[CypherRender][WorldDraw][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.AddCameraMaterial( 2u, common::render_material_alpha_mode_t::BLEND );
    fixture.AddCameraMaterial( 3u, common::render_material_alpha_mode_t::ADDITIVE );
    REQUIRE( render::R_DestroyUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateUniformBatch(
        1024u, common::Allocator_GetSystem(), &fixture.batch ) ==
        render::render_error_t::OK );

    const auto opaque = fixture.AddAtZ( 0u, 0.0f, -4.0f );
    const auto nearBlend = fixture.AddAtZ( 2u, 4.0f, -3.0f );
    const auto farBlend = fixture.AddAtZ( 2u, 8.0f, -9.0f );
    const auto additive = fixture.AddAtZ( 3u, 12.0f, -5.0f );
    world::world_object_t mirroredObject{};
    REQUIRE( world::World_GetObject(
        fixture.pWorld, farBlend, &mirroredObject ) == world::world_error_t::OK );
    mirroredObject.localToWorld = math::Affine3_Multiply(
        math::Affine3_FromTranslation( { 8.0f, 0.0f, -9.0f } ),
        math::Affine3_FromScale( { -1.0f, 1.0f, 1.0f } ) );
    REQUIRE( world::World_UpdateObject(
        fixture.pWorld, farBlend, &mirroredObject ) == world::world_error_t::OK );
    fixture.Build();

    render::render_scene_item_t sceneItems[4]{};
    common::u32 passIndices[4]{};
    auto desc = fixture.CameraDescription();
    desc.sceneItemsScratch = sceneItems;
    desc.sceneItemCapacity = 4u;
    desc.passIndicesScratch = passIndices;
    desc.passIndexCapacity = 4u;
    render::render_world_camera_draw_t output[4]{};
    render::render_world_camera_pass_counts_t counts{};
    REQUIRE( render::R_PrepareWorldCameraDraws(
        desc, output, 4u, &counts ) == render::render_error_t::OK );
    CHECK( counts.opaqueCount == 1u );
    CHECK( counts.blendCount == 2u );
    CHECK( counts.additiveCount == 1u );
    CHECK( output[0].objectId.slot.value == opaque.slot.value );
    CHECK( output[1].objectId.slot.value == farBlend.slot.value );
    CHECK( output[2].objectId.slot.value == nearBlend.slot.value );
    CHECK( output[3].objectId.slot.value == additive.slot.value );
    CHECK( output[0].draw.pipeline.value == fixture.pipelines[0].value );
    CHECK( output[1].draw.pipeline.value == fixture.mirroredPipelines[2].value );
    CHECK( output[2].draw.pipeline.value == fixture.pipelines[2].value );
    CHECK( output[3].draw.pipeline.value == fixture.pipelines[3].value );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( fixture.uploadedSize == 832u );
    const common::f32 expectedZ[4]{ -4.0f, -9.0f, -3.0f, -5.0f };
    for ( common::u32 i = 0u; i < 4u; ++i ) {
        CHECK( output[i].draw.uniformRanges[1].offset == i * 256u );
        math::mat4_t model{};
        std::memcpy( &model, fixture.uploaded + i * 256u, sizeof( model ) );
        CHECK( model.m[14] == expectedZ[i] );
    }
}

TEST_CASE( "World camera pass preflight rejects mismatched alpha pipelines atomically",
    "[CypherRender][WorldDraw][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.AddCameraMaterial( 2u, common::render_material_alpha_mode_t::BLEND );
    (void)fixture.AddAtZ( 2u, 0.0f, -4.0f );
    fixture.Build();
    // A BLEND material must not borrow an opaque depth-writing pipeline.
    fixture.materials[2].pipeline = fixture.pipelines[0];
    render::render_scene_item_t sceneItems[1]{};
    common::u32 passIndices[1]{};
    auto desc = fixture.CameraDescription();
    desc.sceneItemsScratch = sceneItems;
    desc.sceneItemCapacity = 1u;
    desc.passIndicesScratch = passIndices;
    desc.passIndexCapacity = 1u;
    render::render_world_camera_draw_t output{};
    output.draw.pipeline.value = 12345u;
    render::render_world_camera_pass_counts_t counts{ 77u, 88u, 99u };
    CHECK( render::R_PrepareWorldCameraDraws(
        desc, &output, 1u, &counts ) == render::render_error_t::ERR_BINDING_MISMATCH );
    CHECK( counts.opaqueCount == 77u );
    CHECK( counts.blendCount == 88u );
    CHECK( counts.additiveCount == 99u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World camera and opaque preflight reject invalid color and winding pipelines",
    "[CypherRender][WorldDraw][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    (void)fixture.Add( 0u, 0u, 0.0f ); // Valid object precedes the bad one.
    (void)fixture.Add( 0u, 1u, 4.0f );
    fixture.Build();

    SECTION( "depth-only pipeline cannot serve as a camera material" ) {
        fixture.InitShadowPipelines();
        fixture.materials[1].pipeline = fixture.shadowPipeline;
    }
    SECTION( "ordinary transform cannot use reversed front-face winding" ) {
        fixture.materials[1].pipeline = fixture.mirroredPipelines[1];
    }

    render::render_world_opaque_draw_t opaqueOutput[2]{};
    opaqueOutput[0].draw.pipeline.value = 12345u;
    common::u32 opaqueCount = 77u;
    CHECK( render::R_PrepareOpaqueWorldDraws(
        fixture.Description(), opaqueOutput, 2u, &opaqueCount ) ==
        render::render_error_t::ERR_BINDING_MISMATCH );
    CHECK( opaqueCount == 77u );
    CHECK( opaqueOutput[0].draw.pipeline.value == 12345u );

    render::render_scene_item_t sceneItems[2]{};
    common::u32 passIndices[2]{};
    auto cameraDesc = fixture.CameraDescription();
    cameraDesc.sceneItemsScratch = sceneItems;
    cameraDesc.sceneItemCapacity = 2u;
    cameraDesc.passIndicesScratch = passIndices;
    cameraDesc.passIndexCapacity = 2u;
    render::render_world_camera_draw_t cameraOutput[2]{};
    cameraOutput[0].draw.pipeline.value = 54321u;
    render::render_world_camera_pass_counts_t counts{ 77u, 88u, 99u };
    CHECK( render::R_PrepareWorldCameraDraws(
        cameraDesc, cameraOutput, 2u, &counts ) ==
        render::render_error_t::ERR_BINDING_MISMATCH );
    CHECK( counts.opaqueCount == 77u );
    CHECK( counts.blendCount == 88u );
    CHECK( counts.additiveCount == 99u );
    CHECK( cameraOutput[0].draw.pipeline.value == 54321u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World camera draws retain visible blend boxes crossing the eye",
    "[CypherRender][WorldDraw][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.AddCameraMaterial( 2u, common::render_material_alpha_mode_t::BLEND );
    const auto crossing = fixture.AddAtZ( 2u, 0.0f, 0.2f );
    const auto far = fixture.AddAtZ( 2u, 4.0f, -4.0f );
    fixture.Build();
    render::render_scene_item_t sceneItems[2]{};
    common::u32 passIndices[2]{};
    auto desc = fixture.CameraDescription();
    desc.sceneItemsScratch = sceneItems;
    desc.sceneItemCapacity = 2u;
    desc.passIndicesScratch = passIndices;
    desc.passIndexCapacity = 2u;
    render::render_world_camera_draw_t output[2]{};
    render::render_world_camera_pass_counts_t counts{};
    REQUIRE( render::R_PrepareWorldCameraDraws(
        desc, output, 2u, &counts ) == render::render_error_t::OK );
    CHECK( counts.opaqueCount == 0u );
    CHECK( counts.blendCount == 2u );
    CHECK( counts.additiveCount == 0u );
    CHECK( output[0].objectId.slot.value == far.slot.value );
    CHECK( output[1].objectId.slot.value == crossing.slot.value );
    CHECK( sceneItems[0].cameraDistance > 0.0f );
    CHECK( sceneItems[0].cameraDistance < sceneItems[1].cameraDistance );
}

TEST_CASE( "World camera pass rejects behind-eye blend and preserves public state",
    "[CypherRender][WorldDraw][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.AddCameraMaterial( 2u, common::render_material_alpha_mode_t::BLEND );
    (void)fixture.AddAtZ( 2u, 0.0f, 4.0f );
    fixture.Build();
    render::render_scene_item_t sceneItem{};
    common::u32 passIndex{};
    auto desc = fixture.CameraDescription();
    desc.sceneItemsScratch = &sceneItem;
    desc.sceneItemCapacity = 1u;
    desc.passIndicesScratch = &passIndex;
    desc.passIndexCapacity = 1u;
    render::render_world_camera_draw_t output{};
    output.draw.pipeline.value = 12345u;
    render::render_world_camera_pass_counts_t counts{ 77u, 88u, 99u };
    CHECK( render::R_PrepareWorldCameraDraws(
        desc, &output, 1u, &counts ) == render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( counts.blendCount == 88u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World camera upload failure leaves output unpublished and batch retryable",
    "[CypherRender][WorldDraw][CameraPasses]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.AddCameraMaterial( 2u, common::render_material_alpha_mode_t::BLEND );
    (void)fixture.AddAtZ( 2u, 0.0f, -4.0f );
    fixture.Build();
    render::render_scene_item_t sceneItem{};
    common::u32 passIndex{};
    auto desc = fixture.CameraDescription();
    desc.sceneItemsScratch = &sceneItem;
    desc.sceneItemCapacity = 1u;
    desc.passIndicesScratch = &passIndex;
    desc.passIndexCapacity = 1u;
    render::render_world_camera_draw_t output{};
    output.draw.pipeline.value = 12345u;
    render::render_world_camera_pass_counts_t counts{ 77u, 88u, 99u };
    fixture.failNextUpload = true;
    CHECK( render::R_PrepareWorldCameraDraws(
        desc, &output, 1u, &counts ) ==
        render::render_error_t::ERR_RESOURCE_TRANSFER_FAILED );
    CHECK( counts.blendCount == 88u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == sizeof( math::mat4_t ) );
    CHECK( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 1u );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( fixture.uploadCalls == 2u );
}

TEST_CASE( "World light submission keeps off-camera opaque casters and mirrored winding",
    "[CypherRender][WorldDraw][Shadow]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.InitShadowPipelines();
    fixture.materials[0].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    const auto offCameraCaster = fixture.Add( 0u, 0u, 30.0f );
    (void)fixture.Add( 0u, 1u, 0.0f ); // Visible, but does not cast.
    const auto mirroredCaster = fixture.Add( 0u, 0u, 4.0f );
    world::world_object_t mirroredObject{};
    REQUIRE( world::World_GetObject(
        fixture.pWorld, mirroredCaster, &mirroredObject ) == world::world_error_t::OK );
    mirroredObject.localToWorld = math::Affine3_Multiply(
        math::Affine3_FromTranslation( { 4.0f, 0.0f, 0.0f } ),
        math::Affine3_FromScale( { -1.0f, 1.0f, 1.0f } ) );
    REQUIRE( world::World_UpdateObject(
        fixture.pWorld, mirroredCaster, &mirroredObject ) == world::world_error_t::OK );

    fixture.Build();
    common::u32 cameraCount = 0u;
    REQUIRE( world::WorldSubmission_Count(
        fixture.pSubmission, fixture.token, &cameraCount ) ==
        world::world_submission_error_t::OK );
    CHECK( cameraCount == 2u );
    world::WorldSubmission_End( fixture.pSubmission );
    auto lightView = MakeView();
    lightView.frustum.planes[0] = math::Plane_Make(
        { 1.0f, 0.0f, 0.0f }, 40.0f );
    lightView.frustum.planes[1] = math::Plane_Make(
        { -1.0f, 0.0f, 0.0f }, 40.0f );
    fixture.Build( lightView );

    render::render_world_shadow_draw_t output[2]{};
    common::u32 count = 99u;
    REQUIRE( render::R_PrepareWorldShadowDraws(
        fixture.ShadowDescription(), output, 2u, &count ) ==
        render::render_error_t::OK );
    REQUIRE( count == 2u );
    CHECK( output[0].objectId.slot.value == offCameraCaster.slot.value );
    CHECK( output[1].objectId.slot.value == mirroredCaster.slot.value );
    CHECK( output[0].draw.pipeline.value == fixture.shadowPipeline.value );
    CHECK( output[1].draw.pipeline.value ==
        fixture.mirroredShadowPipeline.value );
    CHECK( output[0].draw.uniformRanges[1].offset == 0u );
    CHECK( output[1].draw.uniformRanges[1].offset == 256u );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( fixture.uploadedSize == 320u );
}

TEST_CASE( "World shadow packs mixed opaque and MASK caster records",
    "[CypherRender][WorldDraw][Shadow][Cutout]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.InitShadowPipelines();
    fixture.InitMaskShadowPipelines();
    fixture.EnableCutoutMaterial( 1u, false, true );
    REQUIRE( render::R_DestroyUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    REQUIRE( render::R_CreateUniformBatch(
        640u, common::Allocator_GetSystem(), &fixture.batch ) ==
        render::render_error_t::OK );
    fixture.materials[0].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    fixture.materials[1].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    fixture.materials[1].alphaCutoff = 0.375f;
    const auto opaque = fixture.Add( 0u, 0u, 0.0f );
    const auto masked = fixture.Add( 0u, 1u, 4.0f );
    const auto mirroredMasked = fixture.Add( 0u, 1u, 8.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject(
        fixture.pWorld, mirroredMasked, &object ) == world::world_error_t::OK );
    object.localToWorld = math::Affine3_Multiply(
        math::Affine3_FromTranslation( { 8.0f, 0.0f, 0.0f } ),
        math::Affine3_FromScale( { -1.0f, 1.0f, 1.0f } ) );
    REQUIRE( world::World_UpdateObject(
        fixture.pWorld, mirroredMasked, &object ) == world::world_error_t::OK );
    fixture.Build();

    render::render_world_shadow_draw_t output[3]{};
    common::u32 count = 77u;
    REQUIRE( render::R_PrepareWorldShadowDraws(
        fixture.ShadowDescription(), output, 3u, &count ) ==
        render::render_error_t::OK );
    CHECK( count == 3u );
    CHECK( output[0].objectId.slot.value == opaque.slot.value );
    CHECK( output[1].objectId.slot.value == masked.slot.value );
    CHECK( output[2].objectId.slot.value == mirroredMasked.slot.value );
    CHECK( output[0].draw.pipeline.value == fixture.shadowPipeline.value );
    CHECK( output[1].draw.pipeline.value == fixture.maskShadowPipeline.value );
    CHECK( output[2].draw.pipeline.value ==
        fixture.mirroredMaskShadowPipeline.value );
    CHECK( output[0].draw.uniformRanges[1].offset == 0u );
    CHECK( output[1].draw.uniformRanges[1].offset == 256u );
    CHECK( output[2].draw.uniformRanges[1].offset == 512u );
    CHECK( output[0].draw.uniformRanges[1].byteSize == 64u );
    CHECK( output[1].draw.uniformRanges[1].byteSize == 80u );
    CHECK( output[2].draw.uniformRanges[1].byteSize == 80u );
    CHECK( output[1].draw.sampledTextures[0].value ==
        fixture.alphaTexture.value );
    CHECK( output[2].draw.sampledTextures[0].value ==
        fixture.alphaTexture.value );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( fixture.uploadedSize == 592u );
    const common::u32 maskedOffsets[2]{ 256u, 512u };
    for ( const common::u32 offset : maskedOffsets ) {
        common::f32 cutoff[4]{};
        std::memcpy( cutoff, fixture.uploaded + offset + 64u,
            sizeof( cutoff ) );
        CHECK( cutoff[0] == 0.375f );
        CHECK( cutoff[1] == 0.0f );
        CHECK( cutoff[2] == 0.0f );
        CHECK( cutoff[3] == 0.0f );
    }
}

TEST_CASE( "World MASK shadow preflight rejects bad cutoff and alpha source atomically",
    "[CypherRender][WorldDraw][Shadow][Cutout]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.InitShadowPipelines();
    fixture.InitMaskShadowPipelines();
    fixture.EnableCutoutMaterial( 1u, false, true );
    fixture.materials[0].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    fixture.materials[1].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 0u, 0.0f ); // Valid caster before invalid MASK.
    (void)fixture.Add( 0u, 1u, 4.0f );
    fixture.Build();
    render::render_error_t expected = render::render_error_t::ERR_UNSUPPORTED;
    SECTION( "cutoff below zero" ) {
        fixture.materials[1].alphaCutoff = -0.01f;
    }
    SECTION( "cutoff above one" ) {
        fixture.materials[1].alphaCutoff = 1.01f;
    }
    SECTION( "nonfinite cutoff" ) {
        fixture.materials[1].alphaCutoff =
            std::numeric_limits<common::f32>::quiet_NaN();
    }
    SECTION( "missing alpha source" ) {
        fixture.materials[1].textureCount = 0u;
    }
    SECTION( "stale alpha source" ) {
        fixture.materials[1].textures[0] = {};
        expected = render::render_error_t::ERR_INVALID_HANDLE;
    }
    render::render_world_shadow_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareWorldShadowDraws(
        fixture.ShadowDescription(), output, 2u, &count ) == expected );
    CHECK( count == 77u );
    CHECK( output[0].draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World MASK shadow requires a depth-only textured 80-byte pipeline",
    "[CypherRender][WorldDraw][Shadow][Cutout]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.InitShadowPipelines();
    fixture.EnableCutoutMaterial( 1u, false, true );
    fixture.materials[1].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 1u, 0.0f );
    fixture.Build();
    auto description = fixture.ShadowDescription();
    description.maskShadowPipeline = fixture.shadowPipeline;
    render::render_world_shadow_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareWorldShadowDraws(
        description, &output, 1u, &count ) ==
        render::render_error_t::ERR_BINDING_MISMATCH );
    CHECK( count == 77u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World shadow preflight refuses casting translucent geometry atomically",
    "[CypherRender][WorldDraw][Shadow]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.InitShadowPipelines();
    common::render_material_alpha_mode_t alphaMode{};
    SECTION( "BLEND" ) {
        alphaMode = common::render_material_alpha_mode_t::BLEND;
    }
    SECTION( "ADDITIVE" ) {
        alphaMode = common::render_material_alpha_mode_t::ADDITIVE;
    }
    fixture.AddCameraMaterial( 2u, alphaMode );
    fixture.materials[2].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 2u, 0.0f );
    fixture.Build();
    render::render_world_shadow_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareWorldShadowDraws(
        fixture.ShadowDescription(), &output, 1u, &count ) ==
        render::render_error_t::ERR_UNSUPPORTED );
    CHECK( count == 77u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World shadow preflight rejects a color-writing pipeline atomically",
    "[CypherRender][WorldDraw][Shadow]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.InitShadowPipelines();
    fixture.materials[0].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 0u, 0.0f );
    fixture.Build();
    auto desc = fixture.ShadowDescription();
    desc.shadowPipeline = fixture.pipelines[0]; // Color writes remain enabled.
    render::render_world_shadow_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    CHECK( render::R_PrepareWorldShadowDraws(
        desc, &output, 1u, &count ) ==
        render::render_error_t::ERR_BINDING_MISMATCH );
    CHECK( count == 77u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World shadow upload failure keeps public draws unpublished",
    "[CypherRender][WorldDraw][Shadow]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init();
    fixture.InitShadowPipelines();
    fixture.materials[0].materialFlags =
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 0u, 0.0f );
    fixture.Build();
    render::render_world_shadow_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 count = 77u;
    fixture.failNextUpload = true;
    CHECK( render::R_PrepareWorldShadowDraws(
        fixture.ShadowDescription(), &output, 1u, &count ) ==
        render::render_error_t::ERR_RESOURCE_TRANSFER_FAILED );
    CHECK( count == 77u );
    CHECK( output.draw.pipeline.value == 12345u );
    CHECK( fixture.batch.usedBytes == sizeof( math::mat4_t ) );
    CHECK( fixture.batch.sealed );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( fixture.uploadCalls == 2u );
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

TEST_CASE( "World mesh slots select shared-buffer ranges in camera and shadow draws",
    "[CypherRender][WorldDraw][MeshRange]" )
{
    const auto adapter = GENERATE( range_adapter_t::OPAQUE,
        range_adapter_t::CAMERA, range_adapter_t::SHADOW );
    INFO( "adapter " << static_cast<int>( adapter ) );
    world_draw_fixture_t fixture{};
    fixture.Init( true );
    fixture.EnableCutoutMaterial( 0u, false, false );
    fixture.EnableCutoutMaterial( 1u, false, true );
    fixture.InitShadowPipelines();
    fixture.InitMaskShadowPipelines();
    fixture.materials[0].materialFlags = common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    fixture.materials[1].materialFlags = common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    const auto first = fixture.Add( 0u, 0u, 0.0f );
    const auto second = fixture.Add( 1u, 1u, 4.0f );
    fixture.Build();
    const render::render_static_mesh_bundle_t meshes[2]{ fixture.mesh, fixture.mesh };
    const render::render_world_mesh_range_t ranges[2]{ { 0u, 3u }, { 3u, 3u } };
    const render::render_world_mesh_range_t *selectedRanges = nullptr;
    common::u32 rangeCount = 0u;
    SECTION( "omitted ranges preserve the complete mesh" ) {}
    SECTION( "each borrowed mesh alias selects one triangle" ) {
        selectedRanges = ranges;
        rangeCount = 2u;
    }
    render::render_world_opaque_draw_t output[2]{};
    common::u32 count = 99u;
    render::render_world_camera_pass_counts_t cameraCounts{ 77u, 88u, 99u };
    REQUIRE( PrepareMeshRanges( fixture, adapter, meshes, 2u, selectedRanges,
        rangeCount, output, 2u, &count, &cameraCounts ) == render::render_error_t::OK );
    if ( adapter == range_adapter_t::CAMERA ) {
        CHECK( cameraCounts.opaqueCount == 2u );
        CHECK( cameraCounts.blendCount == 0u );
        CHECK( cameraCounts.additiveCount == 0u );
    } else {
        CHECK( count == 2u );
    }
    CHECK( output[0].objectId.slot.value == first.slot.value );
    CHECK( output[1].objectId.slot.value == second.slot.value );
    CHECK( output[0].draw.vertexInput.value == fixture.mesh.vertexInput.value );
    CHECK( output[1].draw.vertexInput.value == fixture.mesh.vertexInput.value );
    CHECK( output[0].draw.vertexCount == fixture.mesh.vertexCount );
    CHECK( output[1].draw.vertexCount == fixture.mesh.vertexCount );
    CHECK( output[0].draw.firstIndex == 0u );
    CHECK( output[1].draw.firstIndex == ( selectedRanges == nullptr ? 0u : 3u ) );
    CHECK( output[0].draw.indexCount == ( selectedRanges == nullptr ? 6u : 3u ) );
    CHECK( output[1].draw.indexCount == output[0].draw.indexCount );
    CHECK( output[1].draw.sampledTextures[0].value == fixture.alphaTexture.value );
    CHECK( fixture.uploadCalls == 1u );
    CHECK( fixture.drawCalls == 0u );
}

TEST_CASE( "World range validation rejects a later selected slot before staging",
    "[CypherRender][WorldDraw][MeshRange]" )
{
    const auto adapter = GENERATE( range_adapter_t::OPAQUE,
        range_adapter_t::CAMERA, range_adapter_t::SHADOW );
    INFO( "adapter " << static_cast<int>( adapter ) );
    world_draw_fixture_t fixture{};
    fixture.Init( true );
    fixture.EnableCutoutMaterial( 0u, false, false );
    fixture.EnableCutoutMaterial( 1u, false, true );
    fixture.InitShadowPipelines();
    fixture.InitMaskShadowPipelines();
    fixture.materials[0].materialFlags = common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    fixture.materials[1].materialFlags = common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 0u, 0.0f );
    (void)fixture.Add( 1u, 1u, 4.0f );
    fixture.Build();
    const common::byte priorRecord[16]{ 1u, 2u, 3u, 4u };
    render::render_uniform_record_t record{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, priorRecord,
        sizeof( priorRecord ), &record ) == render::render_error_t::OK );
    const render::render_static_mesh_bundle_t meshes[2]{ fixture.mesh, fixture.mesh };
    render::render_world_mesh_range_t ranges[2]{ { 0u, 3u }, { 3u, 3u } };
    const render::render_world_mesh_range_t *selectedRanges = ranges;
    common::u32 rangeCount = 2u;
    auto expected = render::render_error_t::ERR_INVALID_ARGUMENT;
    SECTION( "zero-length range" ) { ranges[1].indexCount = 0u; }
    SECTION( "range starts inside a triangle" ) { ranges[1].firstIndex = 1u; }
    SECTION( "range ends inside a triangle" ) { ranges[1].indexCount = 4u; }
    SECTION( "range starts at the end of the mesh" ) {
        ranges[1].firstIndex = 6u;
        expected = render::render_error_t::ERR_INDEX_DATA_INVALID;
    }
    SECTION( "range extends beyond the end" ) {
        ranges[1].indexCount = 6u;
        expected = render::render_error_t::ERR_INDEX_DATA_INVALID;
    }
    SECTION( "first plus count would wrap u32" ) {
        ranges[1].firstIndex = std::numeric_limits<common::u32>::max();
        expected = render::render_error_t::ERR_INDEX_DATA_INVALID;
    }
    SECTION( "count plus first would wrap u32" ) {
        ranges[1].indexCount = std::numeric_limits<common::u32>::max();
        expected = render::render_error_t::ERR_INDEX_DATA_INVALID;
    }
    SECTION( "short range table" ) { rangeCount = 1u; }
    SECTION( "nonnull table with zero count" ) { rangeCount = 0u; }
    SECTION( "null table with nonzero count" ) { selectedRanges = nullptr; }
    render::render_world_opaque_draw_t output[2]{};
    output[0].draw.pipeline.value = 12345u;
    output[1].draw.firstIndex = 54321u;
    common::byte beforeOutput[sizeof( output )]{};
    std::memcpy( beforeOutput, output, sizeof( output ) );
    common::u32 count = 42u;
    render::render_world_camera_pass_counts_t cameraCounts{ 77u, 88u, 99u };
    CHECK( PrepareMeshRanges( fixture, adapter, meshes, 2u, selectedRanges,
        rangeCount, output, 2u, &count, &cameraCounts ) == expected );
    CHECK( std::memcmp( beforeOutput, output, sizeof( output ) ) == 0 );
    CHECK( count == 42u );
    CHECK( cameraCounts.opaqueCount == 77u );
    CHECK( cameraCounts.blendCount == 88u );
    CHECK( cameraCounts.additiveCount == 99u );
    CHECK( fixture.batch.usedBytes == sizeof( priorRecord ) );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK_FALSE( fixture.batch.committed );
    CHECK( std::memcmp( fixture.batch.staging, priorRecord, sizeof( priorRecord ) ) == 0 );
    CHECK( fixture.uploadCalls == 0u );
    CHECK( fixture.drawCalls == 0u );
}

TEST_CASE( "World camera sorting retains the range belonging to each mesh slot",
    "[CypherRender][WorldDraw][MeshRange]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init( true );
    fixture.AddCameraMaterial( 2u, common::render_material_alpha_mode_t::BLEND );
    const auto opaque = fixture.AddAtZ( 0u, 0.0f, -4.0f );
    const auto near = fixture.AddAtZ( 2u, 4.0f, -3.0f );
    const auto far = fixture.AddAtZ( 2u, 8.0f, -9.0f );
    world::world_object_t object{};
    REQUIRE( world::World_GetObject( fixture.pWorld, near, &object ) ==
        world::world_error_t::OK );
    object.meshSlot = 1u;
    REQUIRE( world::World_UpdateObject( fixture.pWorld, near, &object ) ==
        world::world_error_t::OK );
    REQUIRE( world::World_GetObject( fixture.pWorld, far, &object ) ==
        world::world_error_t::OK );
    object.meshSlot = 2u;
    REQUIRE( world::World_UpdateObject( fixture.pWorld, far, &object ) ==
        world::world_error_t::OK );
    fixture.Build();
    const render::render_static_mesh_bundle_t meshes[3]{
        fixture.mesh, fixture.mesh, fixture.mesh };
    const render::render_world_mesh_range_t ranges[3]{
        { 0u, 3u }, { 3u, 3u }, { 0u, 6u } };
    render::render_scene_item_t items[3]{};
    common::u32 indices[3]{};
    auto desc = fixture.CameraDescription();
    desc.meshes = meshes;
    desc.meshCount = 3u;
    desc.meshRanges = ranges;
    desc.meshRangeCount = 3u;
    desc.sceneItemsScratch = items;
    desc.sceneItemCapacity = 3u;
    desc.passIndicesScratch = indices;
    desc.passIndexCapacity = 3u;
    render::render_world_camera_draw_t output[3]{};
    render::render_world_camera_pass_counts_t counts{};
    REQUIRE( render::R_PrepareWorldCameraDraws( desc, output, 3u, &counts ) ==
        render::render_error_t::OK );
    CHECK( counts.opaqueCount == 1u );
    CHECK( counts.blendCount == 2u );
    CHECK( counts.additiveCount == 0u );
    CHECK( output[0].objectId.slot.value == opaque.slot.value );
    CHECK( output[1].objectId.slot.value == far.slot.value );
    CHECK( output[2].objectId.slot.value == near.slot.value );
    CHECK( output[0].draw.firstIndex == 0u );
    CHECK( output[0].draw.indexCount == 3u );
    CHECK( output[1].draw.firstIndex == 0u );
    CHECK( output[1].draw.indexCount == 6u );
    CHECK( output[2].draw.firstIndex == 3u );
    CHECK( output[2].draw.indexCount == 3u );
    CHECK( fixture.uploadCalls == 1u );
}

TEST_CASE( "World camera range tables must be disjoint from writable storage",
    "[CypherRender][WorldDraw][MeshRange]" )
{
    world_draw_fixture_t fixture{};
    fixture.Init( true );
    (void)fixture.Add( 0u, 0u, 0.0f );
    fixture.Build();
    render::render_world_camera_draw_t output{};
    output.draw.pipeline.value = 12345u;
    render::render_world_camera_pass_counts_t counts{ 77u, 88u, 99u };
    render::render_scene_item_t items[1]{};
    common::u32 indices[2]{};
    const render::render_world_mesh_range_t range{ 0u, 3u };
    auto desc = fixture.CameraDescription();
    desc.meshRanges = &range;
    desc.meshRangeCount = 1u;
    desc.sceneItemsScratch = items;
    desc.sceneItemCapacity = 1u;
    desc.passIndicesScratch = indices;
    desc.passIndexCapacity = 2u;
    SECTION( "range table overlaps output" ) {
        std::memcpy( &output, &range, sizeof( range ) );
        desc.meshRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( &output );
    }
    SECTION( "range table overlaps public counts" ) {
        std::memcpy( &counts, &range, sizeof( range ) );
        desc.meshRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( &counts );
    }
    SECTION( "range table overlaps scene scratch" ) {
        std::memcpy( items, &range, sizeof( range ) );
        desc.meshRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( items );
    }
    SECTION( "range table overlaps sort scratch" ) {
        std::memcpy( indices, &range, sizeof( range ) );
        desc.meshRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( indices );
    }
    SECTION( "range table overlaps uniform staging" ) {
        std::memcpy( fixture.batch.staging, &range, sizeof( range ) );
        desc.meshRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( fixture.batch.staging );
    }
    SECTION( "range table overlaps batch metadata" ) {
        // The generation field provides eight allocated, aligned bytes without
        // disturbing buffer ownership, the staging pointer, or capacity.
        std::memcpy( &fixture.batch.generation, &range, sizeof( range ) );
        desc.meshRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( &fixture.batch.generation );
    }
    common::byte beforeOutput[sizeof( output )]{};
    common::byte beforeCounts[sizeof( counts )]{};
    common::byte beforeRange[sizeof( range )]{};
    std::memcpy( beforeOutput, &output, sizeof( output ) );
    std::memcpy( beforeCounts, &counts, sizeof( counts ) );
    std::memcpy( beforeRange, desc.meshRanges, sizeof( range ) );
    CHECK( render::R_PrepareWorldCameraDraws( desc, &output, 1u, &counts ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( std::memcmp( beforeOutput, &output, sizeof( output ) ) == 0 );
    CHECK( std::memcmp( beforeCounts, &counts, sizeof( counts ) ) == 0 );
    CHECK( std::memcmp( beforeRange, desc.meshRanges, sizeof( range ) ) == 0 );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World opaque and shadow ranges cannot alias output count or staging",
    "[CypherRender][WorldDraw][MeshRange]" )
{
    const auto adapter = GENERATE( range_adapter_t::OPAQUE, range_adapter_t::SHADOW );
    INFO( "adapter " << static_cast<int>( adapter ) );
    world_draw_fixture_t fixture{};
    fixture.Init( true );
    fixture.EnableCutoutMaterial( 0u, false, false );
    fixture.InitShadowPipelines();
    fixture.materials[0].materialFlags = common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 0u, 0.0f );
    fixture.Build();
    render::render_world_opaque_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::u32 countStorage[2]{ 77u, 88u };
    render::render_world_camera_pass_counts_t unusedCounts{};
    const render::render_world_mesh_range_t range{ 0u, 3u };
    const render::render_world_mesh_range_t *selectedRanges = &range;
    SECTION( "range table overlaps output" ) {
        std::memcpy( &output, &range, sizeof( range ) );
        selectedRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( &output );
    }
    SECTION( "range table overlaps public count" ) {
        std::memcpy( countStorage, &range, sizeof( range ) );
        selectedRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( countStorage );
    }
    SECTION( "range table overlaps uniform staging" ) {
        std::memcpy( fixture.batch.staging, &range, sizeof( range ) );
        selectedRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( fixture.batch.staging );
    }
    SECTION( "range table overlaps batch metadata" ) {
        std::memcpy( &fixture.batch.generation, &range, sizeof( range ) );
        selectedRanges = reinterpret_cast<const render::render_world_mesh_range_t *>( &fixture.batch.generation );
    }
    common::byte beforeOutput[sizeof( output )]{};
    common::byte beforeCounts[sizeof( countStorage )]{};
    common::byte beforeRange[sizeof( range )]{};
    std::memcpy( beforeOutput, &output, sizeof( output ) );
    std::memcpy( beforeCounts, countStorage, sizeof( countStorage ) );
    std::memcpy( beforeRange, selectedRanges, sizeof( range ) );
    CHECK( PrepareMeshRanges( fixture, adapter, &fixture.mesh, 1u,
        selectedRanges, 1u, &output, 1u, countStorage, &unusedCounts ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( std::memcmp( beforeOutput, &output, sizeof( output ) ) == 0 );
    CHECK( std::memcmp( beforeCounts, countStorage, sizeof( countStorage ) ) == 0 );
    CHECK( std::memcmp( beforeRange, selectedRanges, sizeof( range ) ) == 0 );
    CHECK( fixture.batch.usedBytes == 0u );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World ranges validate the live index buffer rather than forged mesh extents",
    "[CypherRender][WorldDraw][MeshRange]" )
{
    const auto adapter = GENERATE( range_adapter_t::OPAQUE,
        range_adapter_t::CAMERA, range_adapter_t::SHADOW );
    INFO( "adapter " << static_cast<int>( adapter ) );
    world_draw_fixture_t fixture{};
    fixture.Init( true );
    fixture.EnableCutoutMaterial( 0u, false, false );
    fixture.InitShadowPipelines();
    fixture.materials[0].materialFlags = common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 0u, 0.0f );
    fixture.Build();
    auto mesh = fixture.mesh;
    render::render_world_mesh_range_t range{ 6u, 3u };
    SECTION( "modest forged metadata cannot expose absent indices" ) {
        mesh.indexCount = 12u;
    }
    SECTION( "large first index is checked without u32 byte multiplication" ) {
        mesh.indexCount = std::numeric_limits<common::u32>::max();
        range.firstIndex = 4294967292u;
    }
    std::memset( fixture.batch.staging, 0xA5,
        static_cast<common::usize>( fixture.batch.capacityBytes ) );
    const common::byte existing[16]{ 1u, 2u, 3u, 4u };
    render::render_uniform_record_t record{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, existing,
        sizeof( existing ), &record ) == render::render_error_t::OK );
    common::byte beforeStaging[1024]{};
    REQUIRE( fixture.batch.capacityBytes <= sizeof( beforeStaging ) );
    const auto stagingSize = static_cast<common::usize>( fixture.batch.capacityBytes );
    std::memcpy( beforeStaging, fixture.batch.staging, stagingSize );
    render::render_world_opaque_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::byte beforeOutput[sizeof( output )]{};
    std::memcpy( beforeOutput, &output, sizeof( output ) );
    common::u32 count = 42u;
    render::render_world_camera_pass_counts_t cameraCounts{ 77u, 88u, 99u };
    CHECK( PrepareMeshRanges( fixture, adapter, &mesh, 1u, &range, 1u,
        &output, 1u, &count, &cameraCounts ) == render::render_error_t::ERR_INDEX_DATA_INVALID );
    CHECK( std::memcmp( beforeOutput, &output, sizeof( output ) ) == 0 );
    CHECK( count == 42u );
    CHECK( cameraCounts.opaqueCount == 77u );
    CHECK( cameraCounts.blendCount == 88u );
    CHECK( cameraCounts.additiveCount == 99u );
    CHECK( std::memcmp( beforeStaging, fixture.batch.staging, stagingSize ) == 0 );
    CHECK( fixture.batch.usedBytes == sizeof( existing ) );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}

TEST_CASE( "World borrowed resource tables cannot reside in uniform staging",
    "[CypherRender][WorldDraw][MeshRange]" )
{
    const auto adapter = GENERATE( range_adapter_t::OPAQUE,
        range_adapter_t::CAMERA, range_adapter_t::SHADOW );
    INFO( "adapter " << static_cast<int>( adapter ) );
    world_draw_fixture_t fixture{};
    fixture.Init( true );
    fixture.EnableCutoutMaterial( 0u, false, false );
    fixture.InitShadowPipelines();
    fixture.materials[0].materialFlags = common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS;
    (void)fixture.Add( 0u, 0u, 0.0f );
    fixture.Build();
    REQUIRE( render::R_DestroyUniformBatch( &fixture.batch ) == render::render_error_t::OK );
    REQUIRE( render::R_CreateUniformBatch( 1024u, common::Allocator_GetSystem(),
        &fixture.batch ) == render::render_error_t::OK );
    std::memset( fixture.batch.staging, 0xA5, 1024u );
    const common::byte existing[16]{ 1u, 2u, 3u, 4u };
    render::render_uniform_record_t record{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, existing,
        sizeof( existing ), &record ) == render::render_error_t::OK );
    const render::render_static_mesh_bundle_t *mesh = &fixture.mesh;
    const render::render_material_bundle_t *material = nullptr;
    const render::render_world_mesh_range_t range{ 0u, 3u };
    constexpr common::usize tableOffset = 128u;
    SECTION( "valid borrowed mesh table overlaps future model records" ) {
        REQUIRE( tableOffset + sizeof( fixture.mesh ) <= fixture.batch.capacityBytes );
        std::memcpy( fixture.batch.staging + tableOffset,
            &fixture.mesh, sizeof( fixture.mesh ) );
        mesh = reinterpret_cast<const render::render_static_mesh_bundle_t *>(
            fixture.batch.staging + tableOffset );
    }
    SECTION( "valid borrowed material table overlaps future model records" ) {
        REQUIRE( tableOffset + sizeof( fixture.materials[0] ) <= fixture.batch.capacityBytes );
        std::memcpy( fixture.batch.staging + tableOffset,
            &fixture.materials[0], sizeof( fixture.materials[0] ) );
        material = reinterpret_cast<const render::render_material_bundle_t *>(
            fixture.batch.staging + tableOffset );
    }
    common::byte beforeStaging[1024]{};
    std::memcpy( beforeStaging, fixture.batch.staging, sizeof( beforeStaging ) );
    render::render_world_opaque_draw_t output{};
    output.draw.pipeline.value = 12345u;
    common::byte beforeOutput[sizeof( output )]{};
    std::memcpy( beforeOutput, &output, sizeof( output ) );
    common::u32 count = 42u;
    render::render_world_camera_pass_counts_t cameraCounts{ 77u, 88u, 99u };
    CHECK( PrepareMeshRanges( fixture, adapter, mesh, 1u, &range, 1u,
        &output, 1u, &count, &cameraCounts, material ) == render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( std::memcmp( beforeOutput, &output, sizeof( output ) ) == 0 );
    CHECK( count == 42u );
    CHECK( cameraCounts.opaqueCount == 77u );
    CHECK( cameraCounts.blendCount == 88u );
    CHECK( cameraCounts.additiveCount == 99u );
    CHECK( std::memcmp( beforeStaging, fixture.batch.staging, sizeof( beforeStaging ) ) == 0 );
    CHECK( fixture.batch.usedBytes == sizeof( existing ) );
    CHECK_FALSE( fixture.batch.sealed );
    CHECK( fixture.uploadCalls == 0u );
}
