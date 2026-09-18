//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileCamera_Tests.cpp
//  Purpose: Verifies navigation and framing shared by Qt and runtime previews.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCamera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>

namespace tile = ::cypher::tools::tile_editor;
namespace math = ::cypher::math;

namespace
{
math::vec3_t OrbitPivot( const tile::tile_camera_t &camera )
{
    return math::Vec3_Add( camera.position,
        math::Vec3_Scale( tile::CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
}

math::vec3_t CameraSpacePoint(
    const tile::tile_camera_t &camera, math::vec3_t point )
{
    const auto offset = math::Vec3_Subtract( point, camera.position );
    return { math::Vec3_Dot( offset, tile::CypherTileCamera_Right( camera ) ),
             math::Vec3_Dot( offset, tile::CypherTileCamera_Up( camera ) ),
             math::Vec3_Dot( offset, tile::CypherTileCamera_Forward( camera ) ) };
}

void RequireSamePose( const tile::tile_camera_t &actual, const tile::tile_camera_t &expected )
{
    REQUIRE( math::Vec3_EqualsExact( actual.position, expected.position ) );
    REQUIRE( actual.yawRadians == expected.yawRadians );
    REQUIRE( actual.pitchRadians == expected.pitchRadians );
    REQUIRE( actual.orbitDistance == expected.orbitDistance );
}
} // namespace

TEST_CASE( "Switching fly and orbit retains the exact inspection pose", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    tile::CypherTileCamera_FrameBounds( camera, { -8, -3, 0 }, { 12, 7, 5 }, 1.6f );
    const auto original = camera;
    math::mat4_t originalView{}, originalProjection{};
    REQUIRE( tile::CypherTileCamera_BuildMatrices( camera, 1.6f, originalView, originalProjection ) );
    for ( int transition = 0; transition < 20; ++transition ) {
        tile::CypherTileCamera_SetMode( camera, transition % 2 == 0
            ? tile::tile_camera_mode_t::ORBIT : tile::tile_camera_mode_t::FLY );
        RequireSamePose( camera, original );
        math::mat4_t view{}, projection{};
        REQUIRE( tile::CypherTileCamera_BuildMatrices( camera, 1.6f, view, projection ) );
        REQUIRE( math::Mat4_NearlyEquals( view, originalView, 0.0f, 0.0f ) );
    }
}

TEST_CASE( "Fly movement uses camera axes and caps combined movement speed", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    camera.position = math::CY_VEC3_ZERO;
    camera.yawRadians = std::numbers::pi_v<float> * 0.5f;
    camera.pitchRadians = 0.0f;
    REQUIRE( tile::CypherTileCamera_Move( camera, { 1, 0, 0 }, 0.1f ) );
    REQUIRE( camera.position.y == Catch::Approx( 0.8f ) );
    REQUIRE( std::abs( camera.position.x ) < 0.00001f );
    camera.position = math::CY_VEC3_ZERO;
    REQUIRE( tile::CypherTileCamera_Move( camera, { 0, 1, 0 }, 0.1f ) );
    REQUIRE( camera.position.x == Catch::Approx( 0.8f ) );

    camera.pitchRadians = -0.9f;
    camera.position = math::CY_VEC3_ZERO;
    REQUIRE( tile::CypherTileCamera_Move( camera, { 0, 0, 1 }, 0.1f ) );
    REQUIRE( camera.position.z == Catch::Approx( 0.8f ) );
    REQUIRE( camera.position.x == 0.0f );
    REQUIRE( camera.position.y == 0.0f );

    camera.position = math::CY_VEC3_ZERO;
    REQUIRE( tile::CypherTileCamera_Move( camera, { 1, 1, -1 }, 0.1f ) );
    REQUIRE( math::Vec3_Length( camera.position ) == Catch::Approx( 0.8f ) );
}

TEST_CASE( "Fly speed modifiers are predictable and motion is frame-rate independent", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    camera.position = math::CY_VEC3_ZERO;
    tile::tile_camera_t subdivided = camera;
    for ( int i = 0; i < 10; ++i ) {
        REQUIRE( tile::CypherTileCamera_Move( subdivided, { 1, 0, 0 }, 0.01f ) );
    }
    REQUIRE( tile::CypherTileCamera_Move( camera, { 1, 0, 0 }, 0.1f ) );
    REQUIRE( math::Vec3_NearlyEquals( camera.position, subdivided.position, 0.00001f, 0.00001f ) );
    camera.position = math::CY_VEC3_ZERO;
    REQUIRE( tile::CypherTileCamera_Move( camera, { 1, 0, 0, true, false }, 0.1f ) );
    REQUIRE( math::Vec3_Length( camera.position ) == Catch::Approx( 3.2f ) );
    camera.position = math::CY_VEC3_ZERO;
    REQUIRE( tile::CypherTileCamera_Move( camera, { 1, 0, 0, true, true }, 0.1f ) );
    REQUIRE( math::Vec3_Length( camera.position ) == Catch::Approx( 0.2f ) );
}

TEST_CASE( "Invalid timing and input cannot move or poison the camera", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    const auto original = camera;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for ( float dt : { -1.0f, 0.0f, nan, std::numeric_limits<float>::infinity() } ) {
        REQUIRE_FALSE( tile::CypherTileCamera_Move( camera, { 1, 0, 0 }, dt ) );
        RequireSamePose( camera, original );
    }
    REQUIRE_FALSE( tile::CypherTileCamera_Move( camera, { nan, 0, 0 }, 0.1f ) );
    tile::CypherTileCamera_ApplyLook( camera, nan, 0 );
    tile::CypherTileCamera_Wheel( camera, nan );
    tile::CypherTileCamera_Pan( camera, 100, 100, 0 );
    RequireSamePose( camera, original );

    REQUIRE( tile::CypherTileCamera_Move( camera, { 1, 0, 0 }, 10.0f ) );
    REQUIRE( math::Vec3_Distance( camera.position, original.position ) == Catch::Approx( 0.8f ) );
    tile::CypherTileCamera_SetMode( camera, tile::tile_camera_mode_t::ORBIT );
    REQUIRE_FALSE( tile::CypherTileCamera_Move( camera, { 1, 0, 0 }, 0.1f ) );
}

TEST_CASE( "Framing fits every map corner in portrait and landscape viewports", "[tile][camera]" )
{
    const math::vec3_t minimum{ -18, -4, -1 };
    const math::vec3_t maximum{ 26, 12, 9 };
    for ( float aspect : { 0.25f, 0.5f, 1.0f, 1.6f, 3.0f } ) {
        tile::tile_camera_t camera{};
        tile::CypherTileCamera_FrameBounds( camera, minimum, maximum, aspect );
        math::mat4_t view{}, projection{};
        REQUIRE( tile::CypherTileCamera_BuildMatrices( camera, aspect, view, projection ) );
        const math::mat4_t worldToClip = math::Mat4_Multiply( projection, view );
        for ( int i = 0; i < 8; ++i ) {
            const math::vec4_t point{ i & 1 ? maximum.x : minimum.x,
                i & 2 ? maximum.y : minimum.y, i & 4 ? maximum.z : minimum.z, 1.0f };
            const auto clip = math::Mat4_TransformVector4( worldToClip, point );
            REQUIRE( clip.w > 0.0f );
            REQUIRE( std::abs( clip.x / clip.w ) < 1.0f );
            REQUIRE( std::abs( clip.y / clip.w ) < 1.0f );
            REQUIRE( clip.z / clip.w > -1.0f );
            REQUIRE( clip.z / clip.w < 1.0f );
        }
    }
}

TEST_CASE( "Map reload bounds preserve the camera instead of following the new center", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    tile::CypherTileCamera_FrameBounds( camera, { 0, 0, 0 }, { 10, 10, 3 }, 1.6f );
    tile::CypherTileCamera_ApplyLook( camera, 75.0f, -20.0f );
    REQUIRE( tile::CypherTileCamera_Move( camera, { 1, 1, 0 }, 0.1f ) );
    const auto beforeReload = camera;
    tile::CypherTileCamera_UpdateBounds( camera, { 100, -80, 0 }, { 150, 60, 20 } );
    RequireSamePose( camera, beforeReload );
    REQUIRE( camera.boundsCenter.x == 125.0f );
    REQUIRE( camera.boundsRadius > beforeReload.boundsRadius );
    math::mat4_t view{}, projection{};
    REQUIRE( tile::CypherTileCamera_BuildMatrices( camera, 1.6f, view, projection ) );
}

TEST_CASE( "Mouse look clamps pitch and orbit manipulation holds its pivot", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    const auto eye = camera.position;
    tile::CypherTileCamera_ApplyLook( camera, 40, 100000 );
    REQUIRE( math::Vec3_EqualsExact( camera.position, eye ) );
    REQUIRE( camera.pitchRadians > -std::numbers::pi_v<float> * 0.5f );
    REQUIRE( camera.pitchRadians < -1.5f );
    camera.settings.invertMouseY = true;
    tile::CypherTileCamera_ApplyLook( camera, 0, 100000 );
    REQUIRE( camera.pitchRadians > 1.5f );
    REQUIRE( camera.pitchRadians < std::numbers::pi_v<float> * 0.5f );

    tile::CypherTileCamera_FrameBounds( camera, { -2, -2, -2 }, { 2, 2, 2 }, 1.5f, true );
    tile::CypherTileCamera_SetMode( camera, tile::tile_camera_mode_t::ORBIT );
    const auto pivot = OrbitPivot( camera );
    tile::CypherTileCamera_ApplyLook( camera, 20, 30 );
    REQUIRE( math::Vec3_NearlyEquals( OrbitPivot( camera ), pivot, 0.00001f, 0.00001f ) );
    const float previousDistance = camera.orbitDistance;
    tile::CypherTileCamera_Wheel( camera, 2 );
    REQUIRE( camera.orbitDistance < previousDistance );
    REQUIRE( math::Vec3_NearlyEquals( OrbitPivot( camera ), pivot, 0.00001f, 0.00001f ) );
}

TEST_CASE( "Explicit surface orbit preserves the picked point in camera space", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    camera.position = { -3.0f, -4.0f, 2.0f };
    camera.yawRadians = 0.35f;
    camera.pitchRadians = -0.25f;
    camera.mode = tile::tile_camera_mode_t::ORBIT;
    const math::vec3_t pivot{ 4.0f, 1.5f, 1.0f };
    const auto cameraSpaceBefore = CameraSpacePoint( camera, pivot );
    const float radiusBefore = math::Vec3_Distance( camera.position, pivot );

    REQUIRE( tile::CypherTileCamera_OrbitAround( camera, pivot, 45.0f, -18.0f ) );
    CHECK( math::Vec3_NearlyEquals(
        CameraSpacePoint( camera, pivot ), cameraSpaceBefore, 0.00001f, 0.00001f ) );
    CHECK( math::Vec3_Distance( camera.position, pivot ) == Catch::Approx( radiusBefore ) );
    CHECK( camera.orbitDistance == Catch::Approx( radiusBefore ) );

    const auto afterOrbit = camera;
    REQUIRE( tile::CypherTileCamera_OrbitWheel( camera, pivot, 2.0f ) );
    CHECK( math::Vec3_Distance( camera.position, pivot ) < radiusBefore );
    CHECK( math::Vec3_NearlyEquals(
        CameraSpacePoint( camera, pivot ),
        math::Vec3_Scale( CameraSpacePoint( afterOrbit, pivot ),
            camera.orbitDistance / afterOrbit.orbitDistance ),
        0.00001f, 0.00001f ) );

    const auto valid = camera;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE( tile::CypherTileCamera_OrbitAround( camera, { nan, 0, 0 }, 1, 1 ) );
    CHECK_FALSE( tile::CypherTileCamera_OrbitWheel( camera, pivot, nan ) );
    RequireSamePose( camera, valid );
}

TEST_CASE( "Explicit orbit rejects unsupported near pivots without corrupting pose",
    "[tile][camera][robustness]" )
{
    tile::tile_camera_t camera{};
    camera.position = { 0.0f, 0.0f, 0.0f };
    camera.yawRadians = 0.0f;
    camera.pitchRadians = 0.0f;
    camera.mode = tile::tile_camera_mode_t::ORBIT;
    const auto before = camera;
    const math::vec3_t tooClose{ 0.025f, 0.0f, 0.0f };

    CHECK_FALSE( tile::CypherTileCamera_OrbitAround(
        camera, tooClose, 15.0f, 8.0f ) );
    RequireSamePose( camera, before );
    CHECK_FALSE( tile::CypherTileCamera_OrbitWheel(
        camera, tooClose, 1.0f ) );
    RequireSamePose( camera, before );
}

TEST_CASE( "Implicit orbit wheel is transactional for invalid camera state",
    "[tile][camera][robustness]" )
{
    tile::tile_camera_t camera{};
    camera.mode = tile::tile_camera_mode_t::ORBIT;
    const auto position = camera.position;
    const auto yaw = camera.yawRadians;
    const auto pitch = camera.pitchRadians;

    camera.orbitDistance = std::numeric_limits<float>::quiet_NaN();
    tile::CypherTileCamera_Wheel( camera, 1.0f );
    CHECK( math::Vec3_EqualsExact( camera.position, position ) );
    CHECK( camera.yawRadians == yaw );
    CHECK( camera.pitchRadians == pitch );
    CHECK( std::isnan( camera.orbitDistance ) );

    camera.orbitDistance = 10.0f;
    camera.position.x = std::numeric_limits<float>::max();
    camera.yawRadians = 0.0f;
    camera.pitchRadians = 0.0f;
    const auto extreme = camera.position;
    tile::CypherTileCamera_Wheel( camera, 40.0f );
    CHECK( math::Vec3_EqualsExact( camera.position, extreme ) );
    CHECK( camera.orbitDistance == 10.0f );
}

TEST_CASE( "Fly wheel dollies while RMB wheel adjusts speed without moving the eye", "[tile][camera]" )
{
    tile::tile_camera_t camera{};
    const auto original = camera;
    tile::CypherTileCamera_Wheel( camera, 3 );
    REQUIRE_FALSE( math::Vec3_EqualsExact( camera.position, original.position ) );
    REQUIRE( camera.settings.moveSpeed == original.settings.moveSpeed );
    const auto afterDolly = camera;
    REQUIRE( tile::CypherTileCamera_AdjustMoveSpeed( camera, 3 ) );
    RequireSamePose( camera, afterDolly );
    REQUIRE( camera.settings.moveSpeed > afterDolly.settings.moveSpeed );
    tile::tile_camera_settings_t invalid{};
    invalid.moveSpeed = std::numeric_limits<float>::quiet_NaN();
    invalid.lookSensitivity = std::numeric_limits<float>::infinity();
    invalid.verticalFovDegrees = -10.0f;
    tile::CypherTileCamera_SetSettings( camera, invalid );
    REQUIRE( camera.settings.moveSpeed == 8.0f );
    REQUIRE( std::isfinite( camera.settings.lookSensitivity ) );
    REQUIRE( camera.settings.verticalFovDegrees == 20.0f );
    math::mat4_t view{}, projection{};
    REQUIRE_FALSE( tile::CypherTileCamera_BuildMatrices( camera, 0, view, projection ) );
}
