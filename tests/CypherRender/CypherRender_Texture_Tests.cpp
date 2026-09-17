//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests texture ownership, bounds and sampled draw validation.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender/CypherRender_Texture.h"
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
    texture_error_t createResult{ texture_error_t::OK }, destroyResult{ texture_error_t::OK };
    render::render_texture_desc_t captured{};
    render::backend_draw_indexed_desc_t capturedDraw{};
    common::byte pixels[24]{};

    void Init()
    {
        backend.state = this;
        backend.CreateTexture2D = []( const render::render_texture_desc_t &desc,
            const render::render_texture_data_t &, render::backend_texture_t &out, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.creates;
            self.captured = desc;
            out = {};
            if ( self.createResult == texture_error_t::OK ) out.value = self.next++;
            return self.createResult;
        };
        backend.DestroyTexture = []( render::backend_texture_t, void *state ) noexcept {
            auto &self = *static_cast<texture_fixture_t *>( state );
            ++self.destroys;
            return self.destroyResult;
        };
        backend.CreateBuffer = []( const render::render_buffer_desc_t &,
            const render::render_buffer_data_t *, render::backend_buffer_t &out, void *state ) noexcept {
            out.value = static_cast<texture_fixture_t *>( state )->next++;
            return texture_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void * ) noexcept { return texture_error_t::OK; };
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
            return texture_error_t::OK;
        };
        render::tr = {};
        render::tr.backend = &backend;
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.limits.maxTexture2DSize = 4096u;
        render::tr.info.limits.maxCombinedTextureUnits = 16u;
        REQUIRE( render::R_TextureSystemInit() == texture_error_t::OK );
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
    render::render_draw_indexed_desc_t MakeDraw( bool textured )
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
        pipeline.sampledTextureName = textured ? "base_color" : nullptr;
        render::render_draw_indexed_desc_t draw{};
        REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &draw.pipeline ) == texture_error_t::OK );
        render::render_pipeline_info_t pipelineInfo{};
        REQUIRE( render::R_GetGraphicsPipelineInfo( draw.pipeline, &pipelineInfo ) == texture_error_t::OK );
        CHECK( pipelineInfo.sampledTextureRequired == textured );
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
    fixture.createResult = texture_error_t::OK;
    out = fixture.Create();
    fixture.destroyResult = texture_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyTexture( out ) == texture_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsTextureValid( out ) );
    fixture.destroyResult = texture_error_t::OK;
    CHECK( render::R_TextureSystemShutdown() == texture_error_t::OK );
    CHECK_FALSE( render::R_IsTextureValid( out ) );
    CHECK( fixture.destroys == 2u );
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
    draw.sampledTexture = draw.pipeline;
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    draw.sampledTexture = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    CHECK( fixture.capturedDraw.sampledTexture.value != 0u );
    REQUIRE( render::R_DestroyTexture( draw.sampledTexture ) == texture_error_t::OK );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_STALE_HANDLE );
    CHECK( fixture.draws == 1u );
}
TEST_CASE( "untextured pipelines reject an undeclared sampled binding", "[CypherRender][Texture][Draw]" )
{
    texture_fixture_t fixture;
    fixture.Init();
    auto draw = fixture.MakeDraw( false );
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::OK );
    draw.sampledTexture = fixture.Create();
    CHECK( render::R_DrawIndexed( draw ) == texture_error_t::ERR_BINDING_MISMATCH );
    CHECK( fixture.draws == 1u );
}
