//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests texture ownership, bounds and sampled draw validation.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender/CypherRender_Texture.h"
#include "CypherRender/CypherRender_Sampler.h"
#include "CypherRender/CypherRender_Local.h"
#include "CypherRender/CypherRender_Draw.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;
namespace
{
using texture_error_t = render::render_error_t;
struct texture_fixture_t {
    render::backend_api_t backend{};
    common::u64 next{ 1u };
    unsigned creates{ 0u }, destroys{ 0u }, draws{ 0u };
    unsigned samplerCreates{ 0u }, samplerDestroys{ 0u };
    texture_error_t createResult{ texture_error_t::OK }, destroyResult{ texture_error_t::OK };
    texture_error_t samplerCreateResult{ texture_error_t::OK }, samplerDestroyResult{ texture_error_t::OK };
    texture_error_t drawResult{ texture_error_t::OK };
    bool tokenOnCreateFailure{ false };
    bool samplerTokenOnCreateFailure{ false };
    render::render_texture_desc_t captured{};
    render::render_sampler_desc_t capturedSampler{};
    render::backend_draw_indexed_desc_t capturedDraw{};
    common::byte pixels[24]{};
    common::byte mappedBytes[256]{};

    void Init()
    {
        backend.CreateTexture2D = []( const render::render_texture_desc_t &desc,
            const render::render_texture_data_t &, render::backend_texture_t &out, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.creates;
            self.captured = desc;
            out = {};
            if ( self.createResult == texture_error_t::OK || self.tokenOnCreateFailure ) {
                out.value = self.next++;
            }
            return self.createResult;
        };
        backend.DestroyTexture = []( render::backend_texture_t, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.destroys;
            return self.destroyResult;
        };
        backend.CreateSampler = []( const render::render_sampler_desc_t &desc,
            render::backend_sampler_t &out, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.samplerCreates;
            self.capturedSampler = desc;
            out = {};
            if ( self.samplerCreateResult == texture_error_t::OK ||
                 self.samplerTokenOnCreateFailure ) {
                out.value = self.next++;
            }
            return self.samplerCreateResult;
        };
        backend.DestroySampler = []( render::backend_sampler_t, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.samplerDestroys;
            return self.samplerDestroyResult;
        };
        backend.CreateBuffer = []( const render::render_buffer_desc_t &,
            const render::render_buffer_data_t *, render::backend_buffer_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void * ) noexcept { return texture_error_t::OK; };
        backend.MapBuffer = []( render::backend_buffer_t,
            const render::render_buffer_range_t &range,
            render::render_buffer_map_flags_t,
            render::render_buffer_mapping_t &out, void *state ) noexcept {
            out = { static_cast<texture_fixture_t *>( state )->mappedBytes, range.byteSize };
            return texture_error_t::OK;
        };
        backend.UnmapBuffer = []( render::backend_buffer_t, void * ) noexcept { return texture_error_t::OK; };
        backend.CreateVertexInput = []( const render::backend_vertex_input_desc_t &,
            render::backend_vertex_input_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyVertexInput = []( render::backend_vertex_input_t, void * ) noexcept { return texture_error_t::OK; };
        backend.CreateShader = []( const render::render_shader_desc_t &,
            render::backend_shader_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyShader = []( render::backend_shader_t, void * ) noexcept { return texture_error_t::OK; };
        backend.CreateGraphicsPipeline = []( const render::backend_pipeline_desc_t &,
            render::backend_pipeline_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t, void * ) noexcept { return texture_error_t::OK; };
        backend.DrawIndexed = []( const render::backend_draw_indexed_desc_t &desc, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.draws;
            self.capturedDraw = desc;
            return self.drawResult;
        };
        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.limits.maxTexture2DSize = 4096u;
        render::tr.info.limits.maxCombinedTextureUnits = 16u;
        render::tr.info.limits.maxUniformBlockBytes = 16384u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        REQUIRE( render::R_TextureSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_SamplerSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_BufferSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == texture_error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == texture_error_t::OK );
    }
    render::render_texture_desc_t Description() const { return { 3u, 2u, true, true, true, "Test texture" }; }
    render::render_texture_data_t Data() const { return { pixels, sizeof( pixels ) }; }
    render::render_texture_handle_t Create()
    {
        render::render_texture_handle_t texture{};
        REQUIRE( render::R_CreateTexture2D( Description(), Data(), &texture ) == texture_error_t::OK );
        return texture;
    }
    render::render_sampler_handle_t Sampler( render::render_sampler_mip_filter_t mip =
                                                render::render_sampler_mip_filter_t::NONE )
    {
        render::render_sampler_desc_t desc{};
        desc.minFilter = render::render_sampler_filter_t::NEAREST;
        desc.magFilter = render::render_sampler_filter_t::LINEAR;
        desc.mipFilter = mip;
        desc.addressU = render::render_sampler_address_t::CLAMP_TO_EDGE;
        desc.addressV = render::render_sampler_address_t::REPEAT;
        desc.debugName = "Fixture sampler";
        render::render_sampler_handle_t sampler{};
        REQUIRE( render::R_CreateSampler( desc, &sampler ) == texture_error_t::OK );
        return sampler;
    }
    render::render_buffer_handle_t Uniform( common::u64 bytes )
    {
        render::render_buffer_desc_t desc{};
        desc.byteSize = bytes;
        desc.usage = render::R_BUFFER_USAGE_UNIFORM;
        desc.memory = render::render_buffer_memory_t::UPLOAD;
        desc.updatePolicy = render::render_buffer_update_t::DYNAMIC;
        render::render_buffer_handle_t result{};
        REQUIRE( render::R_CreateBuffer( desc, nullptr, &result ) == texture_error_t::OK );
        return result;
    }
    render::render_draw_indexed_desc_t MakeDraw( bool textured, bool twoBindings = false )
    {
        constexpr char source[] = "#version 410 core\nvoid main() {}\n";
        common::cooked_shader_view_t cooked{};
        cooked.nLanguageVersion = 410u;
        cooked.nStages = 2u;
        cooked.stages[0].stage = common::render_shader_stage_t::VERTEX;
        cooked.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        for ( auto &stage : cooked.stages ) stage.code = { reinterpret_cast<const common::byte *>( source ), sizeof( source ) };
        render::render_shader_handle_t shader{};
        REQUIRE( render::R_CreateShader( { &cooked, nullptr }, &shader ) == texture_error_t::OK );
        render::render_vertex_layout_t layout{};
        layout.bindingCount = 1u;
        layout.bindings[0].stride = 12u;
        layout.attributeCount = 1u;
        layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = layout;
        pipeline.sampledTextureCount = textured ? ( twoBindings ? 2u : 1u ) : 0u;
        if ( textured ) pipeline.sampledTextures[0].name = "base_color";
        if ( twoBindings ) {
            pipeline.sampledTextures[1].name = "detail";
            pipeline.uniformBlockCount = 2u;
            pipeline.uniformBlocks[0] = { "Transform", 64u };
            pipeline.uniformBlocks[1] = { "Material", 32u };
        }
        render::render_draw_indexed_desc_t draw{};
        REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &draw.pipeline ) == texture_error_t::OK );
        render::render_pipeline_info_t pipelineInfo{};
        REQUIRE( render::R_GetGraphicsPipelineInfo( draw.pipeline, &pipelineInfo ) == texture_error_t::OK );
        CHECK( pipelineInfo.sampledTextureCount == pipeline.sampledTextureCount );
        CHECK( pipelineInfo.uniformBlockCount == pipeline.uniformBlockCount );
        auto makeBuffer = []( common::u64 bytes, render::render_buffer_usage_flags_t usage ) {
            render::render_buffer_desc_t desc{};
            desc.byteSize = bytes;
            desc.usage = usage;
            desc.memory = render::render_buffer_memory_t::UPLOAD;
            desc.updatePolicy = render::render_buffer_update_t::DYNAMIC;
            render::render_buffer_handle_t result{};
            REQUIRE( render::R_CreateBuffer( desc, nullptr, &result ) == texture_error_t::OK );
            return result;
        };
        render::render_vertex_input_desc_t input{};
        input.layout = layout;
        input.vertexBufferCount = 1u;
        input.vertexBuffers[0] = { makeBuffer( 36u, render::R_BUFFER_USAGE_VERTEX ), 0u, 0u };
        input.indexBuffer = { makeBuffer( 6u, render::R_BUFFER_USAGE_INDEX ), 0u, render::render_index_type_t::UINT16 };
        REQUIRE( render::R_CreateVertexInput( input, &draw.vertexInput ) == texture_error_t::OK );
        draw.indexCount = draw.vertexCount = 3u;
        render::tr.frameActive = true;
        return draw;
    }
    ~texture_fixture_t()
    {
        render::tr.frameActive = false;
        destroyResult = texture_error_t::OK;
        (void)render::R_PipelineSystemShutdown();
        samplerDestroyResult = texture_error_t::OK;
        (void)render::R_SamplerSystemShutdown();
        (void)render::R_TextureSystemShutdown();
        (void)render::R_VertexInputSystemShutdown();
        (void)render::R_ShaderSystemShutdown();
        (void)render::R_BufferSystemShutdown();
        render::tr = {};
    }
};
}

TEST_CASE( "textures reject invalid sizes and incomplete pixels before backend upload", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto desc = fixture.Description();
    render::render_texture_handle_t out{ 99u };
    desc.width = 0u;
    CHECK( render::R_CreateTexture2D( desc, fixture.Data(), &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( out.value == 0u );
    desc.width = 4097u;
    CHECK( render::R_CreateTexture2D( desc, fixture.Data(), &out ) == texture_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED );
    desc.width = std::numeric_limits<common::u32>::max();
    desc.height = desc.width;
    CHECK( render::R_CreateTexture2D( desc, fixture.Data(), &out ) == texture_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED );
    desc = fixture.Description();
    CHECK( render::R_CreateTexture2D( desc, { fixture.pixels, 23u }, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { fixture.pixels, 25u }, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_CreateTexture2D( desc, { nullptr, 24u }, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( fixture.creates == 0u );
}
TEST_CASE( "textures retain immutable metadata and invalidate recycled generations", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto first = fixture.Create();
    render::render_texture_info_t info{};
    REQUIRE( render::R_GetTextureInfo( first, &info ) == texture_error_t::OK );
    CHECK( info.width == 3u );
    CHECK( info.height == 2u );
    CHECK( info.sRGB );
    CHECK( info.generateMips );
    CHECK( info.repeat );
    CHECK( info.mipLevels == 2u );
    REQUIRE( render::R_DestroyTexture( first ) == texture_error_t::OK );
    const auto second = fixture.Create();
    CHECK( second.value != first.value );
    CHECK_FALSE( render::R_IsTextureValid( first ) );
    CHECK( render::R_DestroyTexture( first ) == texture_error_t::ERR_STALE_HANDLE );
    CHECK( render::R_IsTextureValid( second ) );
    auto wrongKind = common::Cy_Handle64Make( common::Cy_Handle64Index( second ),
        common::Cy_Handle64Generation( second ), static_cast<common::u32>( render::render_object_type_t::BUFFER ) );
    CHECK( render::R_DestroyTexture( wrongKind ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    CHECK( fixture.destroys == 1u );
}
TEST_CASE( "texture failures publish no handle and failed destruction preserves ownership", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    fixture.createResult = texture_error_t::ERR_OUT_OF_MEMORY;
    render::render_texture_handle_t out{ 999u };
    CHECK( render::R_CreateTexture2D( fixture.Description(), fixture.Data(), &out ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( out.value == 0u );
    fixture.tokenOnCreateFailure = true;
    CHECK( render::R_CreateTexture2D( fixture.Description(), fixture.Data(), &out ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( out.value == 0u );
    CHECK( fixture.destroys == 1u );
    fixture.tokenOnCreateFailure = false;
    fixture.createResult = texture_error_t::OK;
    out = fixture.Create();
    fixture.destroyResult = texture_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyTexture( out ) == texture_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsTextureValid( out ) );
    fixture.destroyResult = texture_error_t::OK;
    CHECK( render::R_TextureSystemShutdown() == texture_error_t::OK );
    CHECK_FALSE( render::R_IsTextureValid( out ) );
    CHECK( fixture.destroys == 3u );
}
TEST_CASE( "texture-less backend reports unsupported without changing existing resources", "[CypherRender][Texture]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    fixture.backend.CreateTexture2D = nullptr;
    fixture.backend.DestroyTexture = nullptr;
    render::render_texture_handle_t texture{};
    CHECK( render::R_CreateTexture2D( fixture.Description(), fixture.Data(), &texture ) == texture_error_t::ERR_UNSUPPORTED );
    CHECK( texture.value == 0u );
    const auto draw = fixture.MakeDraw( false );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
}
TEST_CASE( "sampled draws require a live texture of the right type", "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_BINDING_MISMATCH );
    draw.sampledTextures[0] = draw.pipeline;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    draw.sampledTextures[0] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.capturedDraw.sampledTextures[0].value != 0u );
    REQUIRE( render::R_DestroyTexture( draw.sampledTextures[0] ) == texture_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_STALE_HANDLE );
    CHECK( fixture.draws == 1u );
}
TEST_CASE( "untextured pipelines reject an undeclared sampled binding", "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( false );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    draw.sampledTextures[0] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_BINDING_MISMATCH );
    CHECK( fixture.draws == 1u );
}

TEST_CASE( "two sampled textures and two uniform blocks bind in stable slots",
           "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true, true );
    const auto baseColor = fixture.Create();
    const auto detail = fixture.Create();
    const auto transform = fixture.Uniform( 64u );
    const auto material = fixture.Uniform( 32u );
    draw.sampledTextures[0] = baseColor;
    draw.sampledTextures[1] = detail;
    draw.uniformBuffers[0] = transform;
    draw.uniformBuffers[1] = material;

    REQUIRE( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.draws == 1u );
    CHECK( fixture.capturedDraw.sampledTextures[0].value != 0u );
    CHECK( fixture.capturedDraw.sampledTextures[1].value != 0u );
    CHECK( fixture.capturedDraw.sampledTextures[0].value !=
           fixture.capturedDraw.sampledTextures[1].value );
    CHECK( fixture.capturedDraw.uniformBuffers[0].value != 0u );
    CHECK( fixture.capturedDraw.uniformBuffers[1].value != 0u );
    CHECK( fixture.capturedDraw.uniformBuffers[0].value !=
           fixture.capturedDraw.uniformBuffers[1].value );

    auto invalid = draw;
    invalid.sampledTextures[1] = {};
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    invalid = draw;
    invalid.uniformBuffers[1] = {};
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    invalid = draw;
    invalid.sampledTextures[1] = draw.pipeline;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    invalid = draw;
    invalid.uniformBuffers[1] = baseColor;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );

    render::tr.activeTargetColor = detail;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_COMMAND_STATE_INVALID );
    render::tr.activeTargetColor = {};

    const auto tooShort = fixture.Uniform( 31u );
    invalid = draw;
    invalid.uniformBuffers[1] = tooShort;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    CHECK( render::R_DestroyBuffer( tooShort ) == texture_error_t::OK );

    render::render_buffer_mapping_t mapped{};
    REQUIRE( render::R_MapBuffer( material, { 0u, 32u },
        render::R_BUFFER_MAP_WRITE, &mapped ) == texture_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_UnmapBuffer( material ) == texture_error_t::OK );
    CHECK( fixture.draws == 1u ); // Validation never reached backend.

    fixture.drawResult = texture_error_t::ERR_SUBMISSION_FAILED;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_SUBMISSION_FAILED );
    CHECK( fixture.draws == 2u );
    // Temporary draw references, including those from earlier slots, must be
    // released even when a later binding or the backend submission fails.
    CHECK( render::R_DestroyBuffer( transform ) == texture_error_t::OK );
    CHECK( render::R_DestroyBuffer( material ) == texture_error_t::OK );
    CHECK( render::R_DestroyTexture( baseColor ) == texture_error_t::OK );
    CHECK( render::R_DestroyTexture( detail ) == texture_error_t::OK );
}

TEST_CASE( "one-slot pipelines reject a resource in an undeclared second slot",
           "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true );
    draw.sampledTextures[0] = fixture.Create();
    draw.sampledTextures[1] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_BINDING_MISMATCH );
    CHECK( fixture.draws == 0u );
}

TEST_CASE( "immutable samplers validate policy and retain inspected metadata",
           "[CypherRender][Sampler]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    render::render_sampler_desc_t desc{};
    CHECK( desc.minFilter == render::render_sampler_filter_t::LINEAR );
    CHECK( desc.magFilter == render::render_sampler_filter_t::LINEAR );
    CHECK( desc.mipFilter == render::render_sampler_mip_filter_t::NONE );

    render::render_sampler_handle_t out{ 99u };
    for ( const auto invalidFilter : {
              static_cast<render::render_sampler_filter_t>( 2u ),
              static_cast<render::render_sampler_filter_t>( 255u ) } ) {
        desc.minFilter = invalidFilter;
        CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
        CHECK( out.value == 0u );
    }
    desc.minFilter = render::render_sampler_filter_t::NEAREST;
    desc.magFilter = static_cast<render::render_sampler_filter_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    desc.magFilter = render::render_sampler_filter_t::LINEAR;
    desc.mipFilter = static_cast<render::render_sampler_mip_filter_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    desc.mipFilter = render::render_sampler_mip_filter_t::NEAREST;
    desc.addressU = static_cast<render::render_sampler_address_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    desc.addressU = render::render_sampler_address_t::CLAMP_TO_EDGE;
    desc.addressV = static_cast<render::render_sampler_address_t>( 255u );
    CHECK( render::R_CreateSampler( desc, &out ) == texture_error_t::ERR_INVALID_ARGUMENT );
    CHECK( fixture.samplerCreates == 0u );
    CHECK( render::R_CreateSampler( desc, nullptr ) == texture_error_t::ERR_INVALID_ARGUMENT );

    desc.addressV = render::render_sampler_address_t::REPEAT;
    desc.debugName = "Immutable sampler";
    REQUIRE( render::R_CreateSampler( desc, &out ) == texture_error_t::OK );
    CHECK( render::R_IsSamplerValid( out ) );
    CHECK( fixture.samplerCreates == 1u );
    CHECK( fixture.capturedSampler.minFilter == desc.minFilter );
    CHECK( fixture.capturedSampler.mipFilter == desc.mipFilter );
    render::render_sampler_info_t info{};
    REQUIRE( render::R_GetSamplerInfo( out, &info ) == texture_error_t::OK );
    CHECK( info.minFilter == desc.minFilter );
    CHECK( info.magFilter == desc.magFilter );
    CHECK( info.mipFilter == desc.mipFilter );
    CHECK( info.addressU == desc.addressU );
    CHECK( info.addressV == desc.addressV );
    CHECK( render::R_GetSamplerInfo( out, nullptr ) == texture_error_t::ERR_INVALID_ARGUMENT );

    const auto wrongKind = common::Cy_Handle64Make( common::Cy_Handle64Index( out ),
        common::Cy_Handle64Generation( out ), static_cast<common::u32>( render::render_object_type_t::TEXTURE ) );
    CHECK( render::R_DestroySampler( wrongKind ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    REQUIRE( render::R_DestroySampler( out ) == texture_error_t::OK );
    CHECK_FALSE( render::R_IsSamplerValid( out ) );
    CHECK( render::R_DestroySampler( out ) == texture_error_t::ERR_STALE_HANDLE );
    render::render_sampler_handle_t replacement{};
    REQUIRE( render::R_CreateSampler( desc, &replacement ) == texture_error_t::OK );
    CHECK( replacement.value != out.value );
    CHECK( render::R_GetSamplerInfo( out, &info ) == texture_error_t::ERR_STALE_HANDLE );
}

TEST_CASE( "sampler backend failure publishes no handle and failed destruction preserves ownership",
           "[CypherRender][Sampler]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    render::render_sampler_desc_t desc{};
    render::render_sampler_handle_t sampler{ 99u };
    fixture.samplerCreateResult = texture_error_t::ERR_OUT_OF_MEMORY;
    CHECK( render::R_CreateSampler( desc, &sampler ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( sampler.value == 0u );
    CHECK( fixture.samplerDestroys == 0u );
    fixture.samplerTokenOnCreateFailure = true;
    CHECK( render::R_CreateSampler( desc, &sampler ) == texture_error_t::ERR_OUT_OF_MEMORY );
    CHECK( sampler.value == 0u );
    CHECK( fixture.samplerDestroys == 1u );
    fixture.samplerTokenOnCreateFailure = false;
    fixture.samplerCreateResult = texture_error_t::OK;
    sampler = fixture.Sampler();
    fixture.samplerDestroyResult = texture_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroySampler( sampler ) == texture_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsSamplerValid( sampler ) );
    fixture.samplerDestroyResult = texture_error_t::OK;
    REQUIRE( render::R_DestroySampler( sampler ) == texture_error_t::OK );
    CHECK_FALSE( render::R_IsSamplerValid( sampler ) );
    CHECK( fixture.samplerDestroys == 3u );
}

TEST_CASE( "sampler-less backends report unsupported without affecting texture draws",
           "[CypherRender][Sampler]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    fixture.backend.CreateSampler = nullptr;
    fixture.backend.DestroySampler = nullptr;
    render::render_sampler_handle_t sampler{ 99u };
    CHECK( render::R_CreateSampler( {}, &sampler ) == texture_error_t::ERR_UNSUPPORTED );
    CHECK( sampler.value == 0u );
    auto draw = fixture.MakeDraw( true );
    draw.sampledTextures[0] = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
}

TEST_CASE( "draw samplers are optional but must match live textures and declared slots",
           "[CypherRender][Sampler][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( true, true );
    auto texture = fixture.Create();
    auto mipSampler = fixture.Sampler( render::render_sampler_mip_filter_t::LINEAR );
    auto nearestSampler = fixture.Sampler();
    draw.sampledTextures[0] = texture;
    draw.sampledTextures[1] = texture;
    draw.uniformBuffers[0] = fixture.Uniform( 64u );
    draw.uniformBuffers[1] = fixture.Uniform( 32u );
    draw.samplers[0] = nearestSampler;
    draw.samplers[1] = mipSampler;
    REQUIRE( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.capturedDraw.samplers[0].value != 0u );
    CHECK( fixture.capturedDraw.samplers[1].value != 0u );
    CHECK( fixture.capturedDraw.samplers[0].value != fixture.capturedDraw.samplers[1].value );

    auto invalid = draw;
    invalid.samplers[0] = draw.pipeline;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    REQUIRE( render::R_DestroySampler( nearestSampler ) == texture_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_STALE_HANDLE );
    draw.samplers[0] = {}; // Legacy texture policy remains usable without a sampler.
    REQUIRE( render::R_DrawIndexed( draw ) == texture_error_t::OK );

    auto noMips = fixture.Description();
    noMips.generateMips = false;
    render::render_texture_handle_t singleLevel{};
    REQUIRE( render::R_CreateTexture2D( noMips, fixture.Data(), &singleLevel ) == texture_error_t::OK );
    invalid = draw;
    invalid.sampledTextures[1] = singleLevel;
    CHECK( render::R_DrawIndexed( invalid ) == texture_error_t::ERR_BINDING_MISMATCH );
    invalid.samplers[1] = {};
    REQUIRE( render::R_DrawIndexed( invalid ) == texture_error_t::OK );
    CHECK( render::R_DestroySampler( mipSampler ) == texture_error_t::OK );

    auto oneSlot = fixture.MakeDraw( true );
    oneSlot.sampledTextures[0] = texture;
    oneSlot.samplers[1] = fixture.Sampler();
    CHECK( render::R_DrawIndexed( oneSlot ) == texture_error_t::ERR_BINDING_MISMATCH );
    auto noTexture = fixture.MakeDraw( false );
    noTexture.samplers[0] = oneSlot.samplers[1];
    CHECK( render::R_DrawIndexed( noTexture ) == texture_error_t::ERR_BINDING_MISMATCH );
}
