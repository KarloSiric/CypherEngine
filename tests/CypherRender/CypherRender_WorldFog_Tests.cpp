//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests the opt-in World fog View record without a graphics context.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_WorldDraw.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace render = ::cypher::engine::render;

TEST_CASE( "fogged World View preserves the complete shadowed lighting prefix",
    "[CypherRender][WorldDraw][Lighting][Fog]" )
{
    using record_t = render::render_world_fogged_view_lighting_record_t;
    CHECK( sizeof( record_t ) == 352u );
    CHECK( alignof( record_t ) == 16u );
    CHECK( offsetof( record_t, shadowed ) == 0u );
    CHECK( offsetof( record_t, fogColor ) == 320u );
    CHECK( offsetof( record_t, fogDepths ) == 336u );

    render::render_world_view_lighting_desc_t lighting{};
    lighting.viewFromWorld.m[12] = -4.0f;
    lighting.viewFromWorld.m[14] = -9.0f;
    lighting.clipFromView.m[0] = 1.75f;
    lighting.clipFromView.m[11] = -1.0f;
    lighting.ambientLight = { 0.25f, 0.5f, 1.0f };
    lighting.directionToLight = { 0.0f, 3.0f, 0.0f };
    lighting.directionalRadiance = { 3.0f, 2.0f, 1.0f };
    lighting.pointLightCount = 2u;
    lighting.pointLights[0] = {
        { 3.0f, -2.0f, 6.0f }, 8.0f, { 1.0f, 0.75f, 0.25f }, 2.0f
    };
    lighting.pointLights[1] = {
        { -1.0f, 5.0f, 2.0f }, 16.0f, { 0.0f, 0.5f, 4.0f }, 0.75f
    };
    auto lightClip = math::CY_MAT4_IDENTITY;
    lightClip.m[3] = 0.125f; // A finite projective light matrix stays valid.
    lightClip.m[11] = -0.5f;
    lightClip.m[12] = 7.0f;
    render::render_world_shadowed_view_lighting_record_t legacy{};
    REQUIRE( render::R_PackWorldShadowedViewLighting( lighting, lightClip, &legacy ) ==
        render::render_error_t::OK );

    const render::render_world_depth_fog_desc_t fog{
        { 2.5f, 0.25f, 0.0f }, 12.0f, 80.0f, true
    };
    record_t output{};
    REQUIRE( render::R_PackWorldFoggedViewLighting( lighting, lightClip, fog, &output ) ==
        render::render_error_t::OK );
    CHECK( std::memcmp( &output, &legacy, sizeof( legacy ) ) == 0 );
    CHECK( output.shadowed.lighting.pointLightControl[0] == 2 );
    CHECK( output.shadowed.lighting.lightDirection[1] == 1.0f );
    CHECK( output.fogColor[0] == 2.5f ); // Linear HDR fog is not clamped to one.
    CHECK( output.fogColor[1] == 0.25f );
    CHECK( output.fogColor[2] == 0.0f );
    CHECK( output.fogColor[3] == 0.0f );
    CHECK( output.fogDepths[0] == 12.0f );
    CHECK( output.fogDepths[1] == 80.0f );
    CHECK( output.fogDepths[2] == 1.0f );
    CHECK( output.fogDepths[3] == 0.0f );
}

TEST_CASE( "disabled World fog ignores inactive fields and clears earlier fog data",
    "[CypherRender][WorldDraw][Lighting][Fog]" )
{
    const render::render_world_view_lighting_desc_t lighting{};
    const auto lightClip = math::CY_MAT4_IDENTITY;
    render::render_world_fogged_view_lighting_record_t output{};
    render::render_world_depth_fog_desc_t fog{
        { 1.0f, 2.0f, 3.0f }, 5.0f, 20.0f, true
    };
    REQUIRE( render::R_PackWorldFoggedViewLighting( lighting, lightClip, fog, &output ) ==
        render::render_error_t::OK );
    REQUIRE( output.fogDepths[2] == 1.0f );

    const auto nan = std::numeric_limits<common::f32>::quiet_NaN();
    const auto inf = std::numeric_limits<common::f32>::infinity();
    fog = { { nan, inf, -1.0f }, inf, nan, false };
    REQUIRE( render::R_PackWorldFoggedViewLighting( lighting, lightClip, fog, &output ) ==
        render::render_error_t::OK );
    for ( const auto lane : output.fogColor ) CHECK( lane == 0.0f );
    for ( const auto lane : output.fogDepths ) CHECK( lane == 0.0f );
    render::render_world_shadowed_view_lighting_record_t legacy{};
    REQUIRE( render::R_PackWorldShadowedViewLighting( lighting, lightClip, &legacy ) ==
        render::render_error_t::OK );
    CHECK( std::memcmp( &output, &legacy, sizeof( legacy ) ) == 0 );
}

TEST_CASE( "World fog validates enabled inputs atomically with its lighting prefix",
    "[CypherRender][WorldDraw][Lighting][Fog]" )
{
    render::render_world_view_lighting_desc_t validLighting{};
    validLighting.pointLightCount = 1u;
    validLighting.pointLights[0] = {
        { 0.0f, 0.0f, 2.0f }, 4.0f, { 1.0f, 0.5f, 0.0f }, 2.0f
    };
    const auto lightClip = math::CY_MAT4_IDENTITY;
    const render::render_world_depth_fog_desc_t validFog{
        { 0.25f, 0.5f, 1.0f }, 5.0f, 30.0f, true
    };
    render::render_world_fogged_view_lighting_record_t output{};
    std::memset( &output, 0x5a, sizeof( output ) );
    const auto original = output;
    const auto reject = [&]( const render::render_world_view_lighting_desc_t &lighting,
                            const math::mat4_t &matrix,
                            const render::render_world_depth_fog_desc_t &fog ) {
        CHECK( render::R_PackWorldFoggedViewLighting( lighting, matrix, fog, &output ) ==
            render::render_error_t::ERR_INVALID_ARGUMENT );
        CHECK( std::memcmp( &output, &original, sizeof( output ) ) == 0 );
    };
    const auto nan = std::numeric_limits<common::f32>::quiet_NaN();
    const auto inf = std::numeric_limits<common::f32>::infinity();
    for ( const auto bad : { -1.0f, nan, inf, -inf } ) {
        for ( common::u32 channel = 0u; channel < 3u; ++channel ) {
            auto invalid = validFog;
            if ( channel == 0u ) invalid.color.x = bad;
            if ( channel == 1u ) invalid.color.y = bad;
            if ( channel == 2u ) invalid.color.z = bad;
            reject( validLighting, lightClip, invalid );
        }
        auto invalid = validFog;
        invalid.startDepth = bad;
        reject( validLighting, lightClip, invalid );
        invalid = validFog;
        invalid.endDepth = bad;
        reject( validLighting, lightClip, invalid );
    }
    auto invalidFog = validFog;
    invalidFog.endDepth = invalidFog.startDepth;
    reject( validLighting, lightClip, invalidFog );
    invalidFog.endDepth = invalidFog.startDepth - 1.0f;
    reject( validLighting, lightClip, invalidFog );

    // Extending View320 must retain its validation, even with fog disabled.
    auto disabled = validFog;
    disabled.enabled = false;
    auto invalidLighting = validLighting;
    invalidLighting.viewFromWorld.m[12] = nan;
    reject( invalidLighting, lightClip, disabled );
    invalidLighting = validLighting;
    invalidLighting.viewFromWorld.m[3] = 1.0f;
    reject( invalidLighting, lightClip, validFog );
    invalidLighting = validLighting;
    invalidLighting.clipFromView.m[5] = inf;
    reject( invalidLighting, lightClip, disabled );
    invalidLighting = validLighting;
    invalidLighting.ambientLight.y = -1.0f;
    reject( invalidLighting, lightClip, validFog );
    invalidLighting = validLighting;
    invalidLighting.directionToLight = math::CY_VEC3_ZERO;
    reject( invalidLighting, lightClip, disabled );
    invalidLighting = validLighting;
    invalidLighting.directionalRadiance.z = nan;
    reject( invalidLighting, lightClip, validFog );
    invalidLighting = validLighting;
    invalidLighting.pointLightCount = 3u;
    reject( invalidLighting, lightClip, disabled );
    invalidLighting = validLighting;
    invalidLighting.pointLights[0].range = 0.0f;
    reject( invalidLighting, lightClip, validFog );
    auto invalidMatrix = lightClip;
    invalidMatrix.m[11] = nan;
    reject( validLighting, invalidMatrix, validFog );
    invalidMatrix = lightClip;
    invalidMatrix.m[15] = inf;
    reject( validLighting, invalidMatrix, disabled );
    CHECK( render::R_PackWorldFoggedViewLighting(
               validLighting, lightClip, validFog, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( std::memcmp( &output, &original, sizeof( output ) ) == 0 );
}

TEST_CASE( "World fog stores representable depth bounds without a reciprocal width",
    "[CypherRender][WorldDraw][Lighting][Fog]" )
{
    const render::render_world_view_lighting_desc_t lighting{};
    const auto lightClip = math::CY_MAT4_IDENTITY;
    render::render_world_fogged_view_lighting_record_t output{};
    const auto tiny = std::numeric_limits<common::f32>::denorm_min();
    REQUIRE( tiny > 0.0f );
    REQUIRE( !std::isfinite( 1.0f / tiny ) );
    render::render_world_depth_fog_desc_t fog{ { 0.0f, 0.0f, 0.0f }, 0.0f, tiny, true };
    REQUIRE( render::R_PackWorldFoggedViewLighting( lighting, lightClip, fog, &output ) ==
        render::render_error_t::OK );
    CHECK( output.fogDepths[0] == 0.0f );
    CHECK( output.fogDepths[1] == tiny );
    CHECK( output.fogDepths[2] == 1.0f );

    const auto maximum = std::numeric_limits<common::f32>::max();
    fog = { { maximum, 2.0f, 0.0f }, maximum * 0.5f, maximum, true };
    REQUIRE( render::R_PackWorldFoggedViewLighting( lighting, lightClip, fog, &output ) ==
        render::render_error_t::OK );
    CHECK( output.fogColor[0] == maximum );
    CHECK( output.fogDepths[0] == maximum * 0.5f );
    CHECK( output.fogDepths[1] == maximum );
    CHECK( output.fogColor[3] == 0.0f );
    CHECK( output.fogDepths[3] == 0.0f );
}
