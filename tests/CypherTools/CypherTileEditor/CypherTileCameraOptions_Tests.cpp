//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Verifies configurable navigation and discoverable camera controls.
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCamera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace cypher::tools::tile_editor;
namespace math = ::cypher::math;

TEST_CASE( "Camera option normalization rejects nonfinite sensitivities and bounds multipliers",
    "[TileEditor][Camera][Options]" )
{
    tile_camera_t camera;
    tile_camera_settings_t settings;
    settings.panSensitivity = std::numeric_limits<float>::quiet_NaN();
    settings.zoomSensitivity = std::numeric_limits<float>::infinity();
    settings.fastMultiplier = 100.0f;
    settings.slowMultiplier = -1.0f;
    settings.invertWheel = true;
    CypherTileCamera_SetSettings( camera, settings );
    CHECK( camera.settings.panSensitivity == 1.0f );
    CHECK( camera.settings.zoomSensitivity == 1.0f );
    CHECK( camera.settings.fastMultiplier == 20.0f );
    CHECK( camera.settings.slowMultiplier == 0.01f );
    CHECK( camera.settings.invertWheel );
}

TEST_CASE( "Pan sensitivity scales screen motion without changing orientation",
    "[TileEditor][Camera][Options]" )
{
    tile_camera_t normal;
    auto fast = normal;
    fast.settings.panSensitivity = 2.5f;
    const auto initial = normal;
    CypherTileCamera_Pan( normal, 60, -30, 480 );
    CypherTileCamera_Pan( fast, 60, -30, 480 );
    CHECK( math::Vec3_Distance( fast.position, initial.position ) ==
        Catch::Approx( math::Vec3_Distance( normal.position, initial.position ) * 2.5f ) );
    CHECK( fast.yawRadians == initial.yawRadians );
    CHECK( fast.pitchRadians == initial.pitchRadians );
}

TEST_CASE( "Wheel sensitivity and inversion apply consistently in fly and orbit modes",
    "[TileEditor][Camera][Options]" )
{
    for ( const auto mode : { tile_camera_mode_t::FLY, tile_camera_mode_t::ORBIT } ) {
        tile_camera_t normal;
        normal.mode = mode;
        auto sensitive = normal;
        sensitive.settings.zoomSensitivity = 2.0f;
        CypherTileCamera_Wheel( normal, 2 );
        CypherTileCamera_Wheel( sensitive, 1 );
        CHECK( math::Vec3_NearlyEquals(
            sensitive.position, normal.position, 0.00001f, 0.00001f ) );
        CHECK( sensitive.settings.moveSpeed == Catch::Approx( normal.settings.moveSpeed ) );
        CHECK( sensitive.orbitDistance == Catch::Approx( normal.orbitDistance ) );
        sensitive.settings.invertWheel = true;
        CypherTileCamera_Wheel( sensitive, 1 );
        CHECK( math::Vec3_NearlyEquals(
            sensitive.position, tile_camera_t{}.position, 0.00001f, 0.00001f ) );
        CHECK( sensitive.settings.moveSpeed == Catch::Approx( tile_camera_t{}.settings.moveSpeed ) );
        CHECK( sensitive.orbitDistance == Catch::Approx( tile_camera_t{}.orbitDistance ) );
    }
}

TEST_CASE( "Configured fast and slow movement multipliers preserve precision priority",
    "[TileEditor][Camera][Options]" )
{
    tile_camera_t camera;
    camera.settings.moveSpeed = 10.0f;
    camera.settings.fastMultiplier = 6.0f;
    camera.settings.slowMultiplier = 0.1f;
    camera.position = math::CY_VEC3_ZERO;
    REQUIRE( CypherTileCamera_Move( camera, { 1, 0, 0, true, false }, 0.1f ) );
    CHECK( math::Vec3_Length( camera.position ) == Catch::Approx( 6.0f ) );
    camera.position = math::CY_VEC3_ZERO;
    REQUIRE( CypherTileCamera_Move( camera, { 1, 0, 0, true, true }, 0.1f ) );
    CHECK( math::Vec3_Length( camera.position ) == Catch::Approx( 0.1f ) );
}
