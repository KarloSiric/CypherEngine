//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Checks ordered scene passes and failure cleanup without a GPU.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_SceneView.h"
#include "CypherRender/CypherRender_Local.h"
#include "CypherRender/CypherRender_Public.h"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <limits>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;
using scene_error_t = render::render_error_t;
using scene_stage_t = render::render_scene_view_stage_t;

namespace
{
struct scene_view_fixture_t {
    struct callback_t { char operation; common::u64 value; };

    render::backend_api_t backend{};
    common::u64 nextToken{ 1u };
    common::u32 beginCalls{ 0u }, drawCalls{ 0u }, endCalls{ 0u }, frameEnds{ 0u };
    common::u32 failBeginAt{ 0u }, failDrawAt{ 0u }, failEndAt{ 0u };
    std::array<callback_t, 24u> callbacks{};
    common::u32 callbackCount{ 0u };
    common::u64 lastTargetToken{ 0u };
    render::backend_target_pass_desc_t firstBeginPass{};
    common::u64 shadowToken{ 0u }, cameraToken{ 0u }, outputToken{ 0u };
    render::render_shader_handle_t shader{};
    render::render_buffer_handle_t vertices{}, indices{};
    render::render_vertex_input_handle_t input{};
    render::render_pipeline_handle_t scenePipeline{}, shadowPipeline{}, outputPipeline{};
    render::render_texture_handle_t shadowDepth{}, cameraColor{}, cameraDepth{}, outputColor{}, outputDepth{};
    render::render_target_handle_t shadow{}, camera{}, output{};
    std::array<render::render_draw_indexed_desc_t, 8u> draws{};
    bool initialized{ false };

    void Record( char operation, common::u64 value = 0u ) noexcept
    {
        if ( callbackCount < callbacks.size() ) callbacks[callbackCount] = { operation, value };
        ++callbackCount;
    }

    render::render_texture_handle_t Texture( render::render_texture_format_t format )
    {
        render::render_texture_handle_t texture{};
        REQUIRE( render::R_CreateTexture2D(
            { 32u, 16u, false, false, false, "Scene view attachment", true, format },
            {}, &texture ) == scene_error_t::OK );
        return texture;
    }

    render::render_target_handle_t Target( render::render_texture_handle_t color,
        render::render_texture_handle_t depth )
    {
        render::render_target_handle_t target{};
        REQUIRE( render::R_CreateRenderTarget( { color, depth }, &target ) == scene_error_t::OK );
        return target;
    }

    void Init()
    {
        backend.CreateBuffer = []( const render::render_buffer_desc_t &,
            const render::render_buffer_data_t *, render::backend_buffer_t &out,
            void *state ) noexcept {
            out.value = static_cast<scene_view_fixture_t *>( state )->nextToken++;
            return scene_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void * ) noexcept {
            return scene_error_t::OK;
        };
        backend.CreateVertexInput = []( const render::backend_vertex_input_desc_t &,
            render::backend_vertex_input_t &out, void *state ) noexcept {
            out.value = static_cast<scene_view_fixture_t *>( state )->nextToken++;
            return scene_error_t::OK;
        };
        backend.DestroyVertexInput = []( render::backend_vertex_input_t, void * ) noexcept {
            return scene_error_t::OK;
        };
        backend.CreateShader = []( const render::render_shader_desc_t &,
            render::backend_shader_t &out, void *state ) noexcept {
            out.value = static_cast<scene_view_fixture_t *>( state )->nextToken++;
            return scene_error_t::OK;
        };
        backend.DestroyShader = []( render::backend_shader_t, void * ) noexcept {
            return scene_error_t::OK;
        };
        backend.CreateGraphicsPipeline = []( const render::backend_pipeline_desc_t &,
            render::backend_pipeline_t &out, void *state ) noexcept {
            out.value = static_cast<scene_view_fixture_t *>( state )->nextToken++;
            return scene_error_t::OK;
        };
        backend.DestroyGraphicsPipeline = []( render::backend_pipeline_t, void * ) noexcept {
            return scene_error_t::OK;
        };
        backend.CreateTexture2D = []( const render::render_texture_desc_t &,
            const render::render_texture_data_t &, render::backend_texture_t &out,
            void *state ) noexcept {
            out.value = static_cast<scene_view_fixture_t *>( state )->nextToken++;
            return scene_error_t::OK;
        };
        backend.DestroyTexture = []( render::backend_texture_t, void * ) noexcept {
            return scene_error_t::OK;
        };
        backend.CreateRenderTarget = []( const render::backend_render_target_desc_t &,
            render::backend_render_target_t &out, void *state ) noexcept {
            auto &self = *static_cast<scene_view_fixture_t *>( state );
            out.value = self.nextToken++;
            self.lastTargetToken = out.value;
            return scene_error_t::OK;
        };
        backend.DestroyRenderTarget = []( render::backend_render_target_t, void * ) noexcept {
            return scene_error_t::OK;
        };
        backend.BeginTargetPass = []( const render::backend_target_pass_desc_t &pass,
            void *state ) noexcept {
            auto &self = *static_cast<scene_view_fixture_t *>( state );
            self.Record( 'B', pass.target.value );
            if ( self.beginCalls == 0u ) self.firstBeginPass = pass;
            return ++self.beginCalls == self.failBeginAt
                ? scene_error_t::ERR_DEVICE_LOST : scene_error_t::OK;
        };
        backend.DrawIndexed = []( const render::backend_draw_indexed_desc_t &draw,
            void *state ) noexcept {
            auto &self = *static_cast<scene_view_fixture_t *>( state );
            self.Record( 'D', draw.indexByteOffset );
            return ++self.drawCalls == self.failDrawAt
                ? scene_error_t::ERR_SUBMISSION_FAILED : scene_error_t::OK;
        };
        backend.EndTargetPass = []( void *state ) noexcept {
            auto &self = *static_cast<scene_view_fixture_t *>( state );
            self.Record( 'E' );
            return ++self.endCalls == self.failEndAt
                ? scene_error_t::ERR_DEVICE_LOST : scene_error_t::OK;
        };
        backend.EndFrame = []( void *state ) noexcept {
            auto &self = *static_cast<scene_view_fixture_t *>( state );
            self.Record( 'F' );
            ++self.frameEnds;
            return scene_error_t::OK;
        };

        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.drawableExtent = { 32u, 16u };
        render::tr.info.limits.maxTexture2DSize = 4096u;
        render::tr.info.limits.maxViewportWidth = 4096u;
        render::tr.info.limits.maxViewportHeight = 4096u;
        render::tr.info.limits.maxUniformBlockBytes = 16384u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        render::tr.info.limits.maxCombinedTextureUnits = 16u;
        render::tr.info.limits.uniformBufferOffsetAlignment = 256u;
        initialized = true;
        REQUIRE( render::R_BufferSystemInit() == scene_error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == scene_error_t::OK );
        REQUIRE( render::R_ShaderSystemInit() == scene_error_t::OK );
        REQUIRE( render::R_PipelineSystemInit() == scene_error_t::OK );
        REQUIRE( render::R_TextureSystemInit() == scene_error_t::OK );
        REQUIRE( render::R_TargetSystemInit() == scene_error_t::OK );

        constexpr char source[] = "#version 410 core\nvoid main() {}\n";
        common::cooked_shader_view_t shaderView{};
        shaderView.nLanguageVersion = 410u;
        shaderView.nStages = 2u;
        shaderView.stages[0].stage = common::render_shader_stage_t::VERTEX;
        shaderView.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
        for ( auto &stage : shaderView.stages ) {
            stage.code = { reinterpret_cast<const common::byte *>( source ), sizeof( source ) };
        }
        REQUIRE( render::R_CreateShader( { &shaderView, "Scene view test shader" }, &shader ) ==
            scene_error_t::OK );
        render::render_buffer_desc_t buffer{};
        buffer.memory = render::render_buffer_memory_t::UPLOAD;
        buffer.updatePolicy = render::render_buffer_update_t::DYNAMIC;
        buffer.byteSize = 64u;
        buffer.usage = render::R_BUFFER_USAGE_VERTEX;
        REQUIRE( render::R_CreateBuffer( buffer, nullptr, &vertices ) == scene_error_t::OK );
        buffer.usage = render::R_BUFFER_USAGE_INDEX;
        REQUIRE( render::R_CreateBuffer( buffer, nullptr, &indices ) == scene_error_t::OK );
        render::render_vertex_layout_t layout{};
        layout.bindingCount = 1u;
        layout.bindings[0].stride = 12u;
        layout.attributeCount = 1u;
        layout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
        render::render_vertex_input_desc_t inputDesc{};
        inputDesc.layout = layout;
        inputDesc.vertexBufferCount = 1u;
        inputDesc.vertexBuffers[0] = { vertices, 12u, 0u };
        inputDesc.indexBuffer = { indices, 0u, render::render_index_type_t::UINT16 };
        REQUIRE( render::R_CreateVertexInput( inputDesc, &input ) == scene_error_t::OK );
        render::render_pipeline_desc_t pipeline{};
        pipeline.shader = shader;
        pipeline.vertexLayout = layout;
        REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &scenePipeline ) == scene_error_t::OK );
        pipeline.colorWrite = false;
        REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &shadowPipeline ) == scene_error_t::OK );
        pipeline.colorWrite = true;
        pipeline.depthTest = false;
        pipeline.depthWrite = false;
        pipeline.sampledTextureCount = 1u;
        pipeline.sampledTextures[0] = { "sceneColor" };
        REQUIRE( render::R_CreateGraphicsPipeline( pipeline, &outputPipeline ) == scene_error_t::OK );

        shadowDepth = Texture( render::render_texture_format_t::D24 );
        cameraColor = Texture( render::render_texture_format_t::RGBA8 );
        cameraDepth = Texture( render::render_texture_format_t::D24 );
        outputColor = Texture( render::render_texture_format_t::RGBA8 );
        outputDepth = Texture( render::render_texture_format_t::D24 );
        shadow = Target( {}, shadowDepth );
        shadowToken = lastTargetToken;
        camera = Target( cameraColor, cameraDepth );
        cameraToken = lastTargetToken;
        output = Target( outputColor, outputDepth );
        outputToken = lastTargetToken;
        for ( common::u32 i = 0u; i < draws.size(); ++i ) {
            draws[i].pipeline = i < 2u ? shadowPipeline : scenePipeline;
            draws[i].vertexInput = input;
            draws[i].firstIndex = i;
            draws[i].indexCount = 3u;
            draws[i].vertexCount = 3u;
        }
        draws[7].pipeline = outputPipeline;
        draws[7].sampledTextures[0] = cameraColor;
        render::tr.frameActive = true;
    }

    render::render_scene_view_desc_t View( bool offscreenOutput = true ) const
    {
        render::render_scene_view_desc_t view{};
        view.shadowPass.target = shadow;
        view.shadowDraws = { draws.data(), 2u };
        view.cameraPass.target = camera;
        view.opaqueDraws = { draws.data() + 2u, 2u };
        view.blendDraws = { draws.data() + 4u, 2u };
        view.additiveDraws = { draws.data() + 6u, 1u };
        view.outputDraw = draws[7];
        if ( offscreenOutput ) view.outputPass.target = output;
        return view;
    }

    void CheckHostFrame() const
    {
        CHECK( render::R_IsFrameActive() );
        CHECK_FALSE( render::R_IsTargetPassActive() );
        CHECK( frameEnds == 0u );
    }

    void FinishHostFrame()
    {
        CheckHostFrame();
        REQUIRE( render::R_EndFrame() == scene_error_t::OK );
        CHECK( frameEnds == 1u );
        CHECK( callbacks[callbackCount - 1u].operation == 'F' );
        CHECK_FALSE( render::R_IsFrameActive() );
        CHECK_FALSE( render::R_IsTargetPassActive() );
        CHECK( render::R_EndFrame() == scene_error_t::ERR_FRAME_NOT_ACTIVE );
        CHECK( frameEnds == 1u );
    }

    void CheckPreflightRejected( const render::render_scene_view_desc_t &view,
        scene_error_t expected ) const
    {
        const auto result = render::R_ExecuteSceneView( view );
        CHECK( result.error == expected );
        CHECK( result.cleanupError == scene_error_t::OK );
        CHECK( result.stage == scene_stage_t::VALIDATION );
        CHECK( result.shadowDraws == 0u );
        CHECK( result.opaqueDraws == 0u );
        CHECK( result.blendDraws == 0u );
        CHECK( result.additiveDraws == 0u );
        CHECK_FALSE( result.outputDrawn );
        CHECK( callbackCount == 0u );
        CheckHostFrame();
    }

    ~scene_view_fixture_t()
    {
        if ( !initialized ) return;
        render::tr.frameActive = false;
        render::tr.activeTarget = {};
        render::tr.activeTargetColor = {};
        render::tr.activeTargetDepth = {};
        CHECK( render::R_TargetSystemShutdown() == scene_error_t::OK );
        CHECK( render::R_TextureSystemShutdown() == scene_error_t::OK );
        CHECK( render::R_PipelineSystemShutdown() == scene_error_t::OK );
        CHECK( render::R_VertexInputSystemShutdown() == scene_error_t::OK );
        CHECK( render::R_ShaderSystemShutdown() == scene_error_t::OK );
        CHECK( render::R_BufferSystemShutdown() == scene_error_t::OK );
        render::tr = {};
    }
};
} // namespace

TEST_CASE( "scene view preserves shadow and camera span order before output",
    "[CypherRender][SceneView]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    auto view = fixture.View();
    SECTION( "offscreen output has its own balanced target pass" ) {}
    SECTION( "presentation output uses the framebuffer already prepared by the host" )
    {
        view.outputPass.target = {};
        // Disabled passes ignore their clear descriptions.
        view.outputPass.clearFlags = std::numeric_limits<render::render_clear_flags_t>::max();
        view.outputPass.clearDepth = std::numeric_limits<float>::quiet_NaN();
    }
    const auto result = render::R_ExecuteSceneView( view );
    REQUIRE( result.error == scene_error_t::OK );
    CHECK( result.cleanupError == scene_error_t::OK );
    CHECK( result.stage == scene_stage_t::COMPLETE );
    CHECK( result.shadowDraws == 2u );
    CHECK( result.opaqueDraws == 2u );
    CHECK( result.blendDraws == 2u );
    CHECK( result.additiveDraws == 1u );
    CHECK( result.outputDrawn );
    const bool offscreen = view.outputPass.target.value != 0u;
    CHECK( fixture.beginCalls == ( offscreen ? 3u : 2u ) );
    CHECK( fixture.endCalls == fixture.beginCalls );
    CHECK( fixture.drawCalls == 8u );
    const scene_view_fixture_t::callback_t expected[]{
        { 'B', fixture.shadowToken }, { 'D', 0u }, { 'D', 2u }, { 'E', 0u },
        { 'B', fixture.cameraToken }, { 'D', 4u }, { 'D', 6u }, { 'D', 8u },
        { 'D', 10u }, { 'D', 12u }, { 'E', 0u }, { 'B', fixture.outputToken },
        { 'D', 14u }, { 'E', 0u } };
    common::u32 cursor = 0u;
    for ( common::u32 i = 0u; i < std::size( expected ); ++i ) {
        if ( !offscreen && ( i == 11u || i == 13u ) ) continue;
        REQUIRE( cursor < fixture.callbackCount );
        CHECK( fixture.callbacks[cursor].operation == expected[i].operation );
        CHECK( fixture.callbacks[cursor].value == expected[i].value );
        ++cursor;
    }
    CHECK( fixture.callbackCount == cursor );
    fixture.CheckHostFrame();
}

TEST_CASE( "scene view supports a camera-only view and empty camera spans",
    "[CypherRender][SceneView]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    auto view = fixture.View( false );
    view.shadowPass.target = {};
    view.shadowPass.clearDepth = std::numeric_limits<float>::quiet_NaN();
    view.shadowDraws = {};
    SECTION( "camera draws still execute" ) {}
    SECTION( "cleared camera target may be presented without geometry" )
    {
        view.opaqueDraws = {};
        view.blendDraws = {};
        view.additiveDraws = {};
    }
    const auto result = render::R_ExecuteSceneView( view );
    REQUIRE( result.error == scene_error_t::OK );
    CHECK( result.stage == scene_stage_t::COMPLETE );
    CHECK( result.shadowDraws == 0u );
    CHECK( result.opaqueDraws == view.opaqueDraws.count );
    CHECK( result.blendDraws == view.blendDraws.count );
    CHECK( result.additiveDraws == view.additiveDraws.count );
    CHECK( result.outputDrawn );
    CHECK( fixture.beginCalls == 1u );
    CHECK( fixture.endCalls == 1u );
    CHECK( fixture.drawCalls == view.opaqueDraws.count + view.blendDraws.count +
        view.additiveDraws.count + 1u );
    CHECK( fixture.callbacks[0].value == fixture.cameraToken );
    fixture.CheckHostFrame();
}

TEST_CASE( "scene view clears and closes a supplied shadow target with no casters",
    "[CypherRender][SceneView]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    auto view = fixture.View();
    view.shadowDraws = {};
    view.shadowPass.clearDepth = 0.375f;
    const auto result = render::R_ExecuteSceneView( view );
    REQUIRE( result.error == scene_error_t::OK );
    CHECK( result.stage == scene_stage_t::COMPLETE );
    CHECK( result.shadowDraws == 0u );
    CHECK( result.opaqueDraws == 2u );
    CHECK( result.blendDraws == 2u );
    CHECK( result.additiveDraws == 1u );
    CHECK( result.outputDrawn );
    CHECK( fixture.beginCalls == 3u );
    CHECK( fixture.endCalls == 3u );
    CHECK( fixture.drawCalls == 6u );
    CHECK( fixture.callbacks[0].operation == 'B' );
    CHECK( fixture.callbacks[0].value == fixture.shadowToken );
    CHECK( fixture.callbacks[1].operation == 'E' );
    CHECK( fixture.firstBeginPass.clearFlags == render::R_CLEAR_DEPTH );
    CHECK( fixture.firstBeginPass.clearDepth == 0.375f );
    fixture.FinishHostFrame();
}

TEST_CASE( "scene view stops after a failed target begin without ending an unopened pass",
    "[CypherRender][SceneView][Failure]" )
{
    constexpr scene_stage_t stages[]{ scene_stage_t::SHADOW, scene_stage_t::CAMERA, scene_stage_t::OUTPUT };
    constexpr common::u32 precedingDraws[]{ 0u, 2u, 7u };
    for ( common::u32 pass = 1u; pass <= 3u; ++pass ) {
        INFO( "failed begin " << pass );
        scene_view_fixture_t fixture;
        fixture.Init();
        fixture.failBeginAt = pass;
        const auto result = render::R_ExecuteSceneView( fixture.View() );
        CHECK( result.error == scene_error_t::ERR_DEVICE_LOST );
        CHECK( result.cleanupError == scene_error_t::OK );
        CHECK( result.stage == stages[pass - 1u] );
        CHECK( result.shadowDraws == ( pass > 1u ? 2u : 0u ) );
        CHECK( result.opaqueDraws == ( pass > 2u ? 2u : 0u ) );
        CHECK( result.blendDraws == ( pass > 2u ? 2u : 0u ) );
        CHECK( result.additiveDraws == ( pass > 2u ? 1u : 0u ) );
        CHECK_FALSE( result.outputDrawn );
        CHECK( fixture.drawCalls == precedingDraws[pass - 1u] );
        CHECK( fixture.beginCalls == pass );
        CHECK( fixture.endCalls == pass - 1u );
        CHECK( fixture.callbacks[fixture.callbackCount - 1u].operation == 'B' );
        fixture.FinishHostFrame();
    }
}

TEST_CASE( "scene view reports only completed draws and closes the failing draw's pass",
    "[CypherRender][SceneView][Failure]" )
{
    struct expected_t { scene_stage_t stage; common::u32 shadow, opaque, blend, additive, passes; };
    constexpr expected_t expected[]{
        { scene_stage_t::SHADOW, 0u, 0u, 0u, 0u, 1u },
        { scene_stage_t::SHADOW, 1u, 0u, 0u, 0u, 1u },
        { scene_stage_t::CAMERA, 2u, 0u, 0u, 0u, 2u },
        { scene_stage_t::CAMERA, 2u, 1u, 0u, 0u, 2u },
        { scene_stage_t::CAMERA, 2u, 2u, 0u, 0u, 2u },
        { scene_stage_t::CAMERA, 2u, 2u, 1u, 0u, 2u },
        { scene_stage_t::CAMERA, 2u, 2u, 2u, 0u, 2u },
        { scene_stage_t::OUTPUT, 2u, 2u, 2u, 1u, 3u } };
    for ( common::u32 failure = 1u; failure <= std::size( expected ); ++failure ) {
        INFO( "failed draw " << failure );
        scene_view_fixture_t fixture;
        fixture.Init();
        fixture.failDrawAt = failure;
        const auto result = render::R_ExecuteSceneView( fixture.View() );
        const auto &counts = expected[failure - 1u];
        CHECK( result.error == scene_error_t::ERR_SUBMISSION_FAILED );
        CHECK( result.cleanupError == scene_error_t::OK );
        CHECK( result.stage == counts.stage );
        CHECK( result.shadowDraws == counts.shadow );
        CHECK( result.opaqueDraws == counts.opaque );
        CHECK( result.blendDraws == counts.blend );
        CHECK( result.additiveDraws == counts.additive );
        CHECK_FALSE( result.outputDrawn );
        CHECK( fixture.drawCalls == failure );
        CHECK( fixture.beginCalls == counts.passes );
        CHECK( fixture.endCalls == counts.passes );
        CHECK( fixture.callbacks[fixture.callbackCount - 1u].operation == 'E' );
        fixture.FinishHostFrame();
    }
}

TEST_CASE( "scene view retains cleanup errors without losing the original submission failure",
    "[CypherRender][SceneView][Failure]" )
{
    constexpr scene_stage_t stages[]{ scene_stage_t::SHADOW, scene_stage_t::CAMERA, scene_stage_t::OUTPUT };
    constexpr common::u32 drawsBeforeEnd[]{ 2u, 7u, 8u };
    for ( common::u32 pass = 1u; pass <= 3u; ++pass ) {
        INFO( "failed end " << pass );
        scene_view_fixture_t fixture;
        fixture.Init();
        fixture.failEndAt = pass;
        const auto result = render::R_ExecuteSceneView( fixture.View() );
        CHECK( result.error == scene_error_t::ERR_DEVICE_LOST );
        CHECK( result.cleanupError == scene_error_t::ERR_DEVICE_LOST );
        CHECK( result.stage == stages[pass - 1u] );
        CHECK( result.shadowDraws == 2u );
        CHECK( result.opaqueDraws == ( pass >= 2u ? 2u : 0u ) );
        CHECK( result.blendDraws == ( pass >= 2u ? 2u : 0u ) );
        CHECK( result.additiveDraws == ( pass >= 2u ? 1u : 0u ) );
        CHECK( result.outputDrawn == ( pass == 3u ) );
        CHECK( fixture.drawCalls == drawsBeforeEnd[pass - 1u] );
        CHECK( fixture.beginCalls == pass );
        CHECK( fixture.endCalls == pass );
        fixture.FinishHostFrame();
    }

    scene_view_fixture_t fixture;
    fixture.Init();
    fixture.failDrawAt = 6u; // Second blended draw fails after one completed blended draw.
    fixture.failEndAt = 2u;
    const auto result = render::R_ExecuteSceneView( fixture.View() );
    CHECK( result.error == scene_error_t::ERR_SUBMISSION_FAILED );
    CHECK( result.cleanupError == scene_error_t::ERR_DEVICE_LOST );
    CHECK( result.stage == scene_stage_t::CAMERA );
    CHECK( result.shadowDraws == 2u );
    CHECK( result.opaqueDraws == 2u );
    CHECK( result.blendDraws == 1u );
    CHECK( result.additiveDraws == 0u );
    CHECK_FALSE( result.outputDrawn );
    CHECK( fixture.drawCalls == 6u );
    CHECK( fixture.beginCalls == 2u );
    CHECK( fixture.endCalls == 2u );
    fixture.FinishHostFrame();
}

TEST_CASE( "scene view presentation draw failure does not close a target or the caller frame",
    "[CypherRender][SceneView][Failure]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    fixture.failDrawAt = 8u;
    const auto result = render::R_ExecuteSceneView( fixture.View( false ) );
    CHECK( result.error == scene_error_t::ERR_SUBMISSION_FAILED );
    CHECK( result.cleanupError == scene_error_t::OK );
    CHECK( result.stage == scene_stage_t::OUTPUT );
    CHECK( result.shadowDraws == 2u );
    CHECK( result.opaqueDraws == 2u );
    CHECK( result.blendDraws == 2u );
    CHECK( result.additiveDraws == 1u );
    CHECK_FALSE( result.outputDrawn );
    CHECK( fixture.drawCalls == 8u );
    CHECK( fixture.beginCalls == 2u );
    CHECK( fixture.endCalls == 2u );
    CHECK( fixture.callbacks[fixture.callbackCount - 1u].operation == 'D' );
    fixture.FinishHostFrame();
}

TEST_CASE( "scene view reports per-draw frontend validation without submitting a stale pipeline",
    "[CypherRender][SceneView][Failure]" )
{
    constexpr scene_stage_t stages[]{ scene_stage_t::SHADOW, scene_stage_t::CAMERA, scene_stage_t::OUTPUT };
    constexpr common::u32 previousDraws[]{ 0u, 2u, 7u };
    for ( common::u32 pass = 1u; pass <= 3u; ++pass ) {
        INFO( "stale pipeline in pass " << pass );
        scene_view_fixture_t fixture;
        fixture.Init();
        render::tr.frameActive = false;
        const render::render_pipeline_handle_t pipelines[]{
            fixture.shadowPipeline, fixture.scenePipeline, fixture.outputPipeline };
        REQUIRE( render::R_DestroyGraphicsPipeline( pipelines[pass - 1u] ) == scene_error_t::OK );
        render::tr.frameActive = true;
        const auto result = render::R_ExecuteSceneView( fixture.View() );
        CHECK( result.error == scene_error_t::ERR_STALE_HANDLE );
        CHECK( result.cleanupError == scene_error_t::OK );
        CHECK( result.stage == stages[pass - 1u] );
        CHECK( result.shadowDraws == ( pass > 1u ? 2u : 0u ) );
        CHECK( result.opaqueDraws == ( pass > 2u ? 2u : 0u ) );
        CHECK( result.blendDraws == ( pass > 2u ? 2u : 0u ) );
        CHECK( result.additiveDraws == ( pass > 2u ? 1u : 0u ) );
        CHECK_FALSE( result.outputDrawn );
        CHECK( fixture.drawCalls == previousDraws[pass - 1u] );
        CHECK( fixture.beginCalls == pass );
        CHECK( fixture.endCalls == pass );
        fixture.FinishHostFrame();
    }
}

TEST_CASE( "scene view preflight rejects malformed spans and invalid clear descriptions",
    "[CypherRender][SceneView][Validation]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    auto view = fixture.View();
    SECTION( "nonempty spans require storage" )
    {
        render::render_scene_draw_span_t *spans[]{
            &view.shadowDraws, &view.opaqueDraws, &view.blendDraws, &view.additiveDraws };
        for ( auto *span : spans ) {
            const auto saved = *span;
            span->draws = nullptr;
            fixture.CheckPreflightRejected( view, scene_error_t::ERR_INVALID_ARGUMENT );
            *span = saved;
        }
    }
    SECTION( "shadow draws cannot be submitted without a shadow target" )
    {
        view.shadowPass.target = {};
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_INVALID_ARGUMENT );
    }
    SECTION( "all enabled pass clears are checked before the first pass" )
    {
        render::render_target_pass_desc_t *passes[]{ &view.shadowPass, &view.cameraPass, &view.outputPass };
        for ( auto *pass : passes ) {
            const auto saved = *pass;
            pass->clearFlags = std::numeric_limits<render::render_clear_flags_t>::max();
            fixture.CheckPreflightRejected( view, scene_error_t::ERR_INVALID_ARGUMENT );
            *pass = saved;
            pass->clearColor.blue = std::numeric_limits<float>::quiet_NaN();
            fixture.CheckPreflightRejected( view, scene_error_t::ERR_INVALID_ARGUMENT );
            *pass = saved;
            pass->clearDepth = -0.1f;
            fixture.CheckPreflightRejected( view, scene_error_t::ERR_INVALID_ARGUMENT );
            *pass = saved;
        }
    }
    SECTION( "depth-only shadow passes cannot clear color" )
    {
        view.shadowPass.clearFlags |= render::R_CLEAR_COLOR;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_INVALID_ARGUMENT );
    }
}

TEST_CASE( "scene view preflight rejects overlapping attachments and output texture feedback",
    "[CypherRender][SceneView][Validation]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    auto view = fixture.View();
    SECTION( "output and camera cannot be the same target" )
    {
        view.outputPass.target = fixture.camera;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RENDER_TARGET_INVALID );
    }
    SECTION( "different target handles cannot alias camera color" )
    {
        render::tr.frameActive = false;
        view.outputPass.target = fixture.Target( fixture.cameraColor, fixture.outputDepth );
        render::tr.frameActive = true;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RENDER_TARGET_INVALID );
    }
    SECTION( "shadow and camera cannot share a depth attachment" )
    {
        render::tr.frameActive = false;
        view.shadowPass.target = fixture.Target( {}, fixture.cameraDepth );
        render::tr.frameActive = true;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RENDER_TARGET_INVALID );
    }
    SECTION( "output and shadow cannot share a depth attachment" )
    {
        render::tr.frameActive = false;
        view.outputPass.target = fixture.Target( fixture.outputColor, fixture.shadowDepth );
        render::tr.frameActive = true;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RENDER_TARGET_INVALID );
    }
    SECTION( "any output sample slot is checked for color and depth feedback" )
    {
        view.outputDraw.sampledTextures[1] = fixture.outputColor;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RESOURCE_BUSY );
        view.outputDraw.sampledTextures[1] = fixture.outputDepth;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RESOURCE_BUSY );
    }
    SECTION( "output slot zero must sample camera color" )
    {
        view.outputDraw.sampledTextures[0] = fixture.shadowDepth;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_BINDING_MISMATCH );
    }
}

TEST_CASE( "scene view preflight resolves every required target before issuing callbacks",
    "[CypherRender][SceneView][Validation]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    auto view = fixture.View();
    SECTION( "camera must have a color attachment" )
    {
        view.shadowPass.target = {};
        view.shadowDraws = {};
        view.cameraPass.target = fixture.shadow;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RENDER_TARGET_INVALID );
    }
    SECTION( "shadow must be depth-only" )
    {
        view.shadowPass.target = fixture.camera;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RENDER_TARGET_INVALID );
    }
    SECTION( "offscreen output must have a color attachment" )
    {
        view.shadowPass.target = {};
        view.shadowDraws = {};
        view.outputPass.target = fixture.shadow;
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_RENDER_TARGET_INVALID );
    }
    SECTION( "missing camera handle is rejected" )
    {
        view.cameraPass.target = {};
        fixture.CheckPreflightRejected( view, scene_error_t::ERR_INVALID_HANDLE );
    }
    SECTION( "stale camera, shadow, and output handles are all rejected" )
    {
        render::render_target_pass_desc_t *passes[]{ &view.cameraPass, &view.shadowPass, &view.outputPass };
        for ( auto *pass : passes ) {
            render::tr.frameActive = false;
            REQUIRE( render::R_DestroyRenderTarget( pass->target ) == scene_error_t::OK );
            render::tr.frameActive = true;
            fixture.CheckPreflightRejected( view, scene_error_t::ERR_STALE_HANDLE );
            // Disable the stale optional pass or restore a new camera target so
            // the next iteration actually reaches the next stale handle.
            if ( pass == &view.cameraPass ) {
                render::tr.frameActive = false;
                view.cameraPass.target = fixture.Target( fixture.cameraColor, fixture.cameraDepth );
                render::tr.frameActive = true;
            } else {
                pass->target = {};
                if ( pass == &view.shadowPass ) view.shadowDraws = {};
            }
        }
    }
}

TEST_CASE( "scene view leaves an existing caller target and frame untouched",
    "[CypherRender][SceneView][Validation]" )
{
    scene_view_fixture_t fixture;
    fixture.Init();
    CHECK_FALSE( render::R_IsTargetPassActive() );
    REQUIRE( render::R_BeginTargetPass( fixture.View().cameraPass ) == scene_error_t::OK );
    REQUIRE( render::R_IsTargetPassActive() );
    const auto callbacksBefore = fixture.callbackCount;
    auto invalid = fixture.View();
    invalid.opaqueDraws = { nullptr, 5u }; // Existing-pass rejection takes priority.
    const auto result = render::R_ExecuteSceneView( invalid );
    CHECK( result.error == scene_error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( result.cleanupError == scene_error_t::OK );
    CHECK( result.stage == scene_stage_t::VALIDATION );
    CHECK( fixture.callbackCount == callbacksBefore );
    CHECK( fixture.endCalls == 0u );
    CHECK( fixture.frameEnds == 0u );
    CHECK( render::R_IsFrameActive() );
    CHECK( render::R_IsTargetPassActive() );
    CHECK( render::tr.activeTarget.value == fixture.camera.value );
    REQUIRE( render::R_EndTargetPass() == scene_error_t::OK );
    fixture.CheckHostFrame();
    render::tr.frameActive = false;
    CHECK_FALSE( render::R_IsTargetPassActive() );
    CHECK( render::R_ExecuteSceneView( fixture.View() ).error == scene_error_t::ERR_FRAME_NOT_ACTIVE );
    CHECK( fixture.callbackCount == callbacksBefore + 1u );
    render::tr.initialized = false;
    CHECK_FALSE( render::R_IsTargetPassActive() );
    CHECK( render::R_ExecuteSceneView( fixture.View() ).error == scene_error_t::ERR_NOT_INITIALIZED );
    CHECK( fixture.callbackCount == callbacksBefore + 1u );
    render::tr.initialized = true;
}
