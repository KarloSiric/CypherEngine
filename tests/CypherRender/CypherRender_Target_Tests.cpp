//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Checks offscreen target ownership and pass state without a GPU.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Target.h"
#include "CypherRender/CypherRender_Local.h"
#include "CypherRender/CypherRender_Public.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;
using target_error_t = render::render_error_t;

namespace
{
struct target_fixture_t {
    render::backend_api_t backend{};
    common::u64 nextToken{ 1u };
    common::u32 nativeCreates{ 0u };
    common::u32 nativeDestroys{ 0u };
    common::u32 passBegins{ 0u };
    common::u32 passEnds{ 0u };
    common::u32 frameEnds{ 0u };
    target_error_t createResult{ target_error_t::OK };
    target_error_t destroyResult{ target_error_t::OK };
    target_error_t beginResult{ target_error_t::OK };
    bool tokenOnCreateFailure{ false };
    render::backend_render_target_desc_t lastTarget{};
    render::backend_target_pass_desc_t lastPass{};

    void Init()
    {
        backend.CreateTexture2D = []( const render::render_texture_desc_t &,
            const render::render_texture_data_t &, render::backend_texture_t &out,
            void *state ) noexcept {
            out.value = static_cast<target_fixture_t *>( state )->nextToken++;
            return target_error_t::OK;
        };
        backend.DestroyTexture = []( render::backend_texture_t, void * ) noexcept {
            return target_error_t::OK;
        };
        backend.CreateRenderTarget = []( const render::backend_render_target_desc_t &desc,
            render::backend_render_target_t &out, void *state ) noexcept {
            auto &self = *static_cast<target_fixture_t *>( state );
            ++self.nativeCreates;
            self.lastTarget = desc;
            if ( self.createResult == target_error_t::OK || self.tokenOnCreateFailure ) {
                out.value = self.nextToken++;
            }
            return self.createResult;
        };
        backend.DestroyRenderTarget = []( render::backend_render_target_t, void *state ) noexcept {
            auto &self = *static_cast<target_fixture_t *>( state );
            ++self.nativeDestroys;
            return self.destroyResult;
        };
        backend.BeginTargetPass = []( const render::backend_target_pass_desc_t &desc,
            void *state ) noexcept {
            auto &self = *static_cast<target_fixture_t *>( state );
            ++self.passBegins;
            self.lastPass = desc;
            return self.beginResult;
        };
        backend.EndTargetPass = []( void *state ) noexcept {
            ++static_cast<target_fixture_t *>( state )->passEnds;
            return target_error_t::OK;
        };
        backend.EndFrame = []( void *state ) noexcept {
            ++static_cast<target_fixture_t *>( state )->frameEnds;
            return target_error_t::OK;
        };
        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.limits.maxTexture2DSize = 4096u;
        render::tr.info.limits.maxViewportWidth = 4096u;
        render::tr.info.limits.maxViewportHeight = 4096u;
        REQUIRE( render::R_TextureSystemInit() == target_error_t::OK );
        REQUIRE( render::R_TargetSystemInit() == target_error_t::OK );
    }

    render::render_texture_handle_t Color()
    {
        const render::render_texture_desc_t desc{
            32u, 16u, false, false, false, "Target color", true };
        render::render_texture_handle_t color{};
        REQUIRE( render::R_CreateTexture2D( desc, {}, &color ) == target_error_t::OK );
        return color;
    }

    render::render_texture_handle_t Depth( common::u32 width = 32u,
                                          common::u32 height = 16u )
    {
        const render::render_texture_desc_t desc{ width, height, false, false,
            false, "Target depth", true, render::render_texture_format_t::D24 };
        render::render_texture_handle_t depth{};
        REQUIRE( render::R_CreateTexture2D( desc, {}, &depth ) == target_error_t::OK );
        return depth;
    }

    ~target_fixture_t()
    {
        render::tr.frameActive = false;
        render::tr.activeTarget = {};
        render::tr.activeTargetColor = {};
        render::tr.activeTargetDepth = {};
        destroyResult = target_error_t::OK;
        (void)render::R_TargetSystemShutdown();
        (void)render::R_TextureSystemShutdown();
        render::tr = {};
    }
};
} // namespace

TEST_CASE( "offscreen targets retain only renderable colors and release them on destruction",
           "[CypherRender][Target]" )
{
    target_fixture_t fixture;
    fixture.Init();
    const common::byte pixels[4]{ 1u, 2u, 3u, 4u };
    render::render_texture_handle_t uploaded{};
    REQUIRE( render::R_CreateTexture2D( { 1u, 1u, false, false, false, nullptr },
        { pixels, sizeof( pixels ) }, &uploaded ) == target_error_t::OK );
    render::render_target_handle_t target{ 99u };
    CHECK( render::R_CreateRenderTarget( {}, &target ) == target_error_t::ERR_INVALID_HANDLE );
    CHECK( target.value == 0u );
    CHECK( render::R_CreateRenderTarget( { uploaded }, &target ) ==
        target_error_t::ERR_RENDER_TARGET_INVALID );
    CHECK( target.value == 0u );
    CHECK( fixture.nativeCreates == 0u );
    REQUIRE( render::R_DestroyTexture( uploaded ) == target_error_t::OK );

    const auto color = fixture.Color();
    render::render_texture_info_t colorInfo{};
    REQUIRE( render::R_GetTextureInfo( color, &colorInfo ) == target_error_t::OK );
    CHECK( colorInfo.renderTarget );
    CHECK( colorInfo.mipLevels == 1u );
    REQUIRE( render::R_CreateRenderTarget( { color }, &target ) == target_error_t::OK );
    CHECK( fixture.nativeCreates == 1u );
    CHECK( fixture.lastTarget.extent.width == 32u );
    CHECK( fixture.lastTarget.extent.height == 16u );
    render::render_target_info_t targetInfo{};
    REQUIRE( render::R_GetRenderTargetInfo( target, &targetInfo ) == target_error_t::OK );
    CHECK( targetInfo.color.value == color.value );
    CHECK( targetInfo.extent.width == 32u );
    CHECK( render::R_GetRenderTargetInfo( color, &targetInfo ) ==
        target_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    CHECK( render::R_DestroyTexture( color ) == target_error_t::ERR_RESOURCE_BUSY );

    REQUIRE( render::R_DestroyRenderTarget( target ) == target_error_t::OK );
    CHECK_FALSE( render::R_IsRenderTargetValid( target ) );
    CHECK( render::R_GetRenderTargetInfo( target, &targetInfo ) == target_error_t::ERR_STALE_HANDLE );
    CHECK( render::R_IsTextureValid( color ) );
    CHECK( render::R_DestroyTexture( color ) == target_error_t::OK );
}

TEST_CASE( "failed target creation and destruction preserve precise resource ownership",
           "[CypherRender][Target]" )
{
    target_fixture_t fixture;
    fixture.Init();
    auto color = fixture.Color();
    fixture.createResult = target_error_t::ERR_OUT_OF_MEMORY;
    render::render_target_handle_t target{ 99u };
    CHECK( render::R_CreateRenderTarget( { color }, &target ) == target_error_t::ERR_OUT_OF_MEMORY );
    CHECK( target.value == 0u );
    CHECK( render::R_DestroyTexture( color ) == target_error_t::OK );

    color = fixture.Color();
    fixture.createResult = target_error_t::OK;
    REQUIRE( render::R_CreateRenderTarget( { color }, &target ) == target_error_t::OK );
    fixture.destroyResult = target_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyRenderTarget( target ) == target_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsRenderTargetValid( target ) );
    CHECK( render::R_DestroyTexture( color ) == target_error_t::ERR_RESOURCE_BUSY );
    fixture.destroyResult = target_error_t::OK;
    REQUIRE( render::R_DestroyRenderTarget( target ) == target_error_t::OK );
    CHECK( render::R_DestroyTexture( color ) == target_error_t::OK );
    CHECK( fixture.nativeDestroys == 2u );
}

TEST_CASE( "sampleable depth attachments match the color extent and retain both textures",
           "[CypherRender][Target][Depth]" )
{
    target_fixture_t fixture;
    fixture.Init();
    const auto color = fixture.Color();
    const auto depth = fixture.Depth();
    const auto wrongSize = fixture.Depth( 16u, 16u );
    render::render_target_handle_t target{ 99u };

    CHECK( render::R_CreateRenderTarget( { depth }, &target ) ==
           target_error_t::ERR_RENDER_TARGET_INVALID );
    CHECK( target.value == 0u );
    CHECK( render::R_CreateRenderTarget( { color, color }, &target ) ==
           target_error_t::ERR_RENDER_TARGET_INVALID );
    CHECK( target.value == 0u );
    CHECK( render::R_CreateRenderTarget( { color, wrongSize }, &target ) ==
           target_error_t::ERR_RENDER_TARGET_INVALID );
    CHECK( target.value == 0u );
    CHECK( fixture.nativeCreates == 0u );
    CHECK( render::R_DestroyTexture( wrongSize ) == target_error_t::OK );

    REQUIRE( render::R_CreateRenderTarget( { color, depth }, &target ) == target_error_t::OK );
    CHECK( fixture.lastTarget.color.value != 0u );
    CHECK( fixture.lastTarget.depth.value != 0u );
    render::render_target_info_t info{};
    REQUIRE( render::R_GetRenderTargetInfo( target, &info ) == target_error_t::OK );
    CHECK( info.color.value == color.value );
    CHECK( info.depth.value == depth.value );
    CHECK( info.extent.width == 32u );
    CHECK( info.extent.height == 16u );
    CHECK( render::R_DestroyTexture( color ) == target_error_t::ERR_RESOURCE_BUSY );
    CHECK( render::R_DestroyTexture( depth ) == target_error_t::ERR_RESOURCE_BUSY );
    render::tr.frameActive = true;
    render::render_target_pass_desc_t pass{};
    pass.target = target;
    REQUIRE( render::R_BeginTargetPass( pass ) == target_error_t::OK );
    CHECK( render::tr.activeTargetDepth.value == depth.value );
    REQUIRE( render::R_EndTargetPass() == target_error_t::OK );
    CHECK( render::tr.activeTargetDepth.value == 0u );
    render::tr.frameActive = false;
    REQUIRE( render::R_DestroyRenderTarget( target ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( color ) == target_error_t::OK );
}

TEST_CASE( "failed depth target creation and destruction release both references precisely",
           "[CypherRender][Target][Depth]" )
{
    target_fixture_t fixture;
    fixture.Init();
    auto color = fixture.Color();
    auto depth = fixture.Depth();
    render::render_target_handle_t target{ 99u };
    fixture.createResult = target_error_t::ERR_OUT_OF_MEMORY;
    fixture.tokenOnCreateFailure = true; // Native failure after partial allocation.
    CHECK( render::R_CreateRenderTarget( { color, depth }, &target ) ==
           target_error_t::ERR_OUT_OF_MEMORY );
    CHECK( target.value == 0u );
    CHECK( fixture.nativeDestroys == 1u );
    REQUIRE( render::R_DestroyTexture( color ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == target_error_t::OK );

    color = fixture.Color();
    depth = fixture.Depth();
    fixture.createResult = target_error_t::OK;
    fixture.tokenOnCreateFailure = false;
    REQUIRE( render::R_CreateRenderTarget( { color, depth }, &target ) == target_error_t::OK );
    fixture.destroyResult = target_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyRenderTarget( target ) == target_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsRenderTargetValid( target ) );
    CHECK( render::R_DestroyTexture( color ) == target_error_t::ERR_RESOURCE_BUSY );
    CHECK( render::R_DestroyTexture( depth ) == target_error_t::ERR_RESOURCE_BUSY );
    fixture.destroyResult = target_error_t::OK;
    REQUIRE( render::R_DestroyRenderTarget( target ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( color ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == target_error_t::OK );
}

TEST_CASE( "depth-only targets derive their extent and retain the depth attachment",
           "[CypherRender][Target][DepthOnly]" )
{
    target_fixture_t fixture;
    fixture.Init();
    const auto color = fixture.Color();
    const auto depth = fixture.Depth( 24u, 12u );
    render::render_target_handle_t target{ 99u };

    CHECK( render::R_CreateRenderTarget( { {}, color }, &target ) ==
           target_error_t::ERR_RENDER_TARGET_INVALID );
    CHECK( target.value == 0u );
    CHECK( fixture.nativeCreates == 0u );
    REQUIRE( render::R_CreateRenderTarget( { {}, depth }, &target ) == target_error_t::OK );
    CHECK( fixture.nativeCreates == 1u );
    CHECK( fixture.lastTarget.color.value == 0u );
    CHECK( fixture.lastTarget.depth.value != 0u );
    CHECK( fixture.lastTarget.extent.width == 24u );
    CHECK( fixture.lastTarget.extent.height == 12u );
    render::render_target_info_t info{};
    REQUIRE( render::R_GetRenderTargetInfo( target, &info ) == target_error_t::OK );
    CHECK( info.color.value == 0u );
    CHECK( info.depth.value == depth.value );
    CHECK( info.extent.width == 24u );
    CHECK( info.extent.height == 12u );
    CHECK( render::R_DestroyTexture( depth ) == target_error_t::ERR_RESOURCE_BUSY );

    render::tr.frameActive = true;
    render::render_target_pass_desc_t pass{};
    pass.target = target;
    CHECK( render::R_BeginTargetPass( pass ) == target_error_t::ERR_INVALID_ARGUMENT );
    CHECK( fixture.passBegins == 0u );
    pass.clearFlags = render::R_CLEAR_COLOR;
    CHECK( render::R_BeginTargetPass( pass ) == target_error_t::ERR_INVALID_ARGUMENT );
    pass.clearFlags = render::R_CLEAR_DEPTH;
    REQUIRE( render::R_BeginTargetPass( pass ) == target_error_t::OK );
    CHECK( fixture.lastPass.clearFlags == render::R_CLEAR_DEPTH );
    CHECK( fixture.lastPass.extent.width == 24u );
    CHECK( render::tr.activeTargetColor.value == 0u );
    CHECK( render::tr.activeTargetDepth.value == depth.value );
    REQUIRE( render::R_EndTargetPass() == target_error_t::OK );
    CHECK( render::tr.activeTargetDepth.value == 0u );
    render::tr.frameActive = false;

    REQUIRE( render::R_DestroyRenderTarget( target ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( color ) == target_error_t::OK );
}

TEST_CASE( "failed depth-only target creation and destruction release its reference",
           "[CypherRender][Target][DepthOnly]" )
{
    target_fixture_t fixture;
    fixture.Init();
    auto depth = fixture.Depth();
    render::render_target_handle_t target{ 99u };
    fixture.createResult = target_error_t::ERR_OUT_OF_MEMORY;
    fixture.tokenOnCreateFailure = true;
    CHECK( render::R_CreateRenderTarget( { {}, depth }, &target ) ==
           target_error_t::ERR_OUT_OF_MEMORY );
    CHECK( target.value == 0u );
    CHECK( fixture.nativeDestroys == 1u );
    REQUIRE( render::R_DestroyTexture( depth ) == target_error_t::OK );

    depth = fixture.Depth();
    fixture.createResult = target_error_t::OK;
    fixture.tokenOnCreateFailure = false;
    REQUIRE( render::R_CreateRenderTarget( { {}, depth }, &target ) == target_error_t::OK );
    fixture.destroyResult = target_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyRenderTarget( target ) == target_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsRenderTargetValid( target ) );
    CHECK( render::R_DestroyTexture( depth ) == target_error_t::ERR_RESOURCE_BUSY );
    fixture.destroyResult = target_error_t::OK;
    REQUIRE( render::R_DestroyRenderTarget( target ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( depth ) == target_error_t::OK );
}

TEST_CASE( "offscreen passes require a frame, valid clears, and an explicit end",
           "[CypherRender][Target]" )
{
    target_fixture_t fixture;
    fixture.Init();
    const auto color = fixture.Color();
    render::render_target_handle_t target{};
    REQUIRE( render::R_CreateRenderTarget( { color }, &target ) == target_error_t::OK );
    render::render_target_pass_desc_t pass{};
    pass.target = target;
    pass.clearColor = { 0.2f, 0.4f, 0.6f, 1.0f };
    CHECK( render::R_BeginTargetPass( pass ) == target_error_t::ERR_FRAME_NOT_ACTIVE );
    render::tr.frameActive = true;
    pass.clearFlags |= render::R_CLEAR_STENCIL;
    CHECK( render::R_BeginTargetPass( pass ) == target_error_t::ERR_INVALID_ARGUMENT );
    pass.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
    pass.clearDepth = std::numeric_limits<float>::quiet_NaN();
    CHECK( render::R_BeginTargetPass( pass ) == target_error_t::ERR_INVALID_ARGUMENT );
    pass.clearDepth = 1.0f;
    fixture.beginResult = target_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_BeginTargetPass( pass ) == target_error_t::ERR_DEVICE_LOST );
    CHECK( render::tr.activeTarget.value == 0u );
    fixture.beginResult = target_error_t::OK;
    REQUIRE( render::R_BeginTargetPass( pass ) == target_error_t::OK );
    CHECK( fixture.lastPass.clearColor.green == 0.4f );
    CHECK( fixture.lastPass.extent.height == 16u );
    CHECK( render::R_BeginTargetPass( pass ) == target_error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( render::R_EndFrame() == target_error_t::ERR_COMMAND_STATE_INVALID );
    CHECK( render::tr.frameActive );
    CHECK( fixture.frameEnds == 0u );
    CHECK( render::R_DestroyRenderTarget( target ) == target_error_t::ERR_COMMAND_STATE_INVALID );
    REQUIRE( render::R_EndTargetPass() == target_error_t::OK );
    CHECK( render::R_EndTargetPass() == target_error_t::ERR_COMMAND_STATE_INVALID );
    REQUIRE( render::R_EndFrame() == target_error_t::OK );
    CHECK( fixture.frameEnds == 1u );
    REQUIRE( render::R_DestroyRenderTarget( target ) == target_error_t::OK );
    REQUIRE( render::R_DestroyTexture( color ) == target_error_t::OK );
}
