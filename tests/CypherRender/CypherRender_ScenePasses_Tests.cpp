//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Checks deterministic, bounded scene pass classification.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_ScenePasses.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <limits>

namespace common = ::cypher::common;
namespace render = ::cypher::engine::render;

namespace
{
constexpr common::u32 kSentinel = 0xdeadbeefu;

struct pass_output_fixture_t {
    common::u32 shadow[8]{};
    common::u32 opaque[8]{};
    common::u32 blend[8]{};
    render::render_scene_pass_indices_t output{};

    pass_output_fixture_t()
    {
        for ( common::u32 i = 0u; i < 8u; ++i ) {
            shadow[i] = kSentinel;
            opaque[i] = kSentinel;
            blend[i] = kSentinel;
        }
        output.shadow = shadow;
        output.shadowCapacity = 8u;
        output.shadowCount = 41u;
        output.opaque = opaque;
        output.opaqueCapacity = 8u;
        output.opaqueCount = 42u;
        output.blend = blend;
        output.blendCapacity = 8u;
        output.blendCount = 43u;
    }
};

void CheckUntouched( const pass_output_fixture_t &fixture )
{
    CHECK( fixture.output.shadowCount == 41u );
    CHECK( fixture.output.opaqueCount == 42u );
    CHECK( fixture.output.blendCount == 43u );
    for ( common::u32 i = 0u; i < 8u; ++i ) {
        CHECK( fixture.shadow[i] == kSentinel );
        CHECK( fixture.opaque[i] == kSentinel );
        CHECK( fixture.blend[i] == kSentinel );
    }
}
} // namespace

TEST_CASE( "scene pass indices retain opaque order and sort blend far to near",
    "[CypherRender][ScenePasses]" )
{
    render::render_scene_item_t items[7]{};
    items[0].cameraVisible = true;
    items[0].castsShadow = true;
    items[1].cameraVisible = true;
    items[1].alpha = render::render_scene_alpha_t::BLEND;
    items[1].cameraDistance = 3.0f;
    items[2].castsShadow = true; // Hidden from this camera, still in the light pass.
    items[3].cameraVisible = true;
    items[3].alpha = render::render_scene_alpha_t::BLEND;
    items[3].cameraDistance = 9.0f;
    items[4].cameraVisible = true;
    items[4].castsShadow = true;
    items[5].cameraVisible = true;
    items[5].alpha = render::render_scene_alpha_t::BLEND;
    items[5].cameraDistance = 9.0f;
    items[6].alpha = render::render_scene_alpha_t::BLEND;
    items[6].castsShadow = true; // Caller decides shadow policy independently.

    pass_output_fixture_t fixture{};
    REQUIRE( render::R_ClassifySceneItems( items, 7u, &fixture.output ) ==
        render::render_error_t::OK );
    REQUIRE( fixture.output.shadowCount == 4u );
    REQUIRE( fixture.output.opaqueCount == 2u );
    REQUIRE( fixture.output.blendCount == 3u );
    CHECK( fixture.shadow[0] == 0u );
    CHECK( fixture.shadow[1] == 2u );
    CHECK( fixture.shadow[2] == 4u );
    CHECK( fixture.shadow[3] == 6u );
    CHECK( fixture.opaque[0] == 0u );
    CHECK( fixture.opaque[1] == 4u );
    CHECK( fixture.blend[0] == 3u );
    CHECK( fixture.blend[1] == 5u ); // Equal depth preserves input order.
    CHECK( fixture.blend[2] == 1u );
    CHECK( fixture.shadow[4] == kSentinel );
    CHECK( fixture.opaque[2] == kSentinel );
    CHECK( fixture.blend[3] == kSentinel );
}

TEST_CASE( "scene pass classification permits an empty span without storage",
    "[CypherRender][ScenePasses]" )
{
    render::render_scene_pass_indices_t output{};
    output.shadowCount = 5u;
    output.opaqueCount = 6u;
    output.blendCount = 7u;
    REQUIRE( render::R_ClassifySceneItems( nullptr, 0u, &output ) ==
        render::render_error_t::OK );
    CHECK( output.shadowCount == 0u );
    CHECK( output.opaqueCount == 0u );
    CHECK( output.blendCount == 0u );
}

TEST_CASE( "scene pass blend sorting handles multiple heap levels and ties",
    "[CypherRender][ScenePasses]" )
{
    constexpr float distances[] = { 2.0f, 9.0f, 5.0f, 9.0f,
        1.0f, 7.0f, 7.0f, 3.0f };
    render::render_scene_item_t items[8]{};
    for ( common::u32 i = 0u; i < 8u; ++i ) {
        items[i].cameraVisible = true;
        items[i].alpha = render::render_scene_alpha_t::BLEND;
        items[i].cameraDistance = distances[i];
    }
    pass_output_fixture_t fixture{};
    REQUIRE( render::R_ClassifySceneItems( items, 8u, &fixture.output ) ==
        render::render_error_t::OK );
    REQUIRE( fixture.output.blendCount == 8u );
    constexpr common::u32 expected[] = { 1u, 3u, 5u, 6u, 2u, 7u, 0u, 4u };
    for ( common::u32 i = 0u; i < 8u; ++i ) {
        CHECK( fixture.blend[i] == expected[i] );
    }
}

TEST_CASE( "scene pass classification preflights all input before writing output",
    "[CypherRender][ScenePasses]" )
{
    render::render_scene_item_t items[2]{};
    items[0].cameraVisible = true;
    items[0].castsShadow = true;
    items[1].cameraVisible = true;
    items[1].alpha = render::render_scene_alpha_t::BLEND;
    items[1].cameraDistance = 1.0f;

    SECTION( "missing input" ) {
        pass_output_fixture_t fixture{};
        CHECK( render::R_ClassifySceneItems( nullptr, 2u, &fixture.output ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CheckUntouched( fixture );
    }
    SECTION( "missing output" ) {
        CHECK( render::R_ClassifySceneItems( items, 2u, nullptr ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
    }
    SECTION( "invalid late alpha class" ) {
        pass_output_fixture_t fixture{};
        items[1].alpha = render::render_scene_alpha_t::COUNT;
        CHECK( render::R_ClassifySceneItems( items, 2u, &fixture.output ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CheckUntouched( fixture );
    }
    SECTION( "visible blend needs positive finite distance" ) {
        for ( const float distance : { 0.0f, -1.0f,
                  std::numeric_limits<float>::infinity(),
                  std::numeric_limits<float>::quiet_NaN() } ) {
            pass_output_fixture_t fixture{};
            items[1].cameraDistance = distance;
            CHECK( render::R_ClassifySceneItems( items, 2u, &fixture.output ) ==
                render::render_error_t::ERR_INVALID_ARGUMENT );
            CheckUntouched( fixture );
        }
    }
    SECTION( "missing required output array" ) {
        pass_output_fixture_t fixture{};
        fixture.output.blend = nullptr;
        CHECK( render::R_ClassifySceneItems( items, 2u, &fixture.output ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CheckUntouched( fixture );
    }
    SECTION( "any pass capacity can reject the entire preparation" ) {
        for ( common::u32 pass = 0u; pass < 3u; ++pass ) {
            pass_output_fixture_t fixture{};
            if ( pass == 0u ) fixture.output.shadowCapacity = 0u;
            if ( pass == 1u ) fixture.output.opaqueCapacity = 0u;
            if ( pass == 2u ) fixture.output.blendCapacity = 0u;
            CHECK( render::R_ClassifySceneItems( items, 2u, &fixture.output ) ==
                render::render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED );
            CheckUntouched( fixture );
        }
    }
    SECTION( "used output regions cannot overlap" ) {
        pass_output_fixture_t fixture{};
        fixture.output.blend = fixture.shadow;
        CHECK( render::R_ClassifySceneItems( items, 2u, &fixture.output ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CheckUntouched( fixture );
    }
    SECTION( "output cannot overwrite its input items" ) {
        pass_output_fixture_t fixture{};
        fixture.output.opaque = reinterpret_cast<common::u32 *>( items );
        CHECK( render::R_ClassifySceneItems( items, 2u, &fixture.output ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CheckUntouched( fixture );
        CHECK( items[0].cameraVisible );
    }
}
