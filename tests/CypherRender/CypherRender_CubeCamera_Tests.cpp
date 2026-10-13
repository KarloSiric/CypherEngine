#include "CypherRender_CubeCamera.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace camera = ::cypher::samples::render;
namespace math = ::cypher::math;

TEST_CASE( "sample camera starts at the existing graybox view", "[render][camera]" )
{
    camera::cube_camera_t camera{};
    const math::vec3_t eye{ 4.0f, -6.0f, 3.2f };
    REQUIRE( camera::CubeCamera_FromLookAt( eye, math::CY_VEC3_ZERO, 5.0f,
        &camera ) );
    REQUIRE( math::Vec3_NearlyEquals( camera.position, eye, 0.0f, 0.0f ) );

    math::mat4_t view{};
    REQUIRE( camera::CubeCamera_TryView( camera, &view ) );
    const math::vec3_t center = math::Mat4_TransformPointAffine(
        view, math::CY_VEC3_ZERO );
    REQUIRE( std::abs( center.x ) < 0.0001f );
    REQUIRE( std::abs( center.y ) < 0.0001f );
    REQUIRE( center.z < -7.0f );

    const camera::cube_camera_t before = camera;
    REQUIRE_FALSE( camera::CubeCamera_FromLookAt( eye, eye, 5.0f, &camera ) );
    REQUIRE( math::Vec3_EqualsExact( camera.position, before.position ) );
    REQUIRE( camera.yawRadians == before.yawRadians );
}

TEST_CASE( "sample camera moves on Cypher's forward right and up axes", "[render][camera]" )
{
    camera::cube_camera_t camera{};
    camera.position = math::CY_VEC3_ZERO;
    camera.moveSpeed = 5.0f;
    camera::cube_camera_input_t input{};
    input.forward = 1.0f;
    REQUIRE( camera::CubeCamera_Advance( &camera, input, 0.1f ) );
    REQUIRE( std::abs( camera.position.x - 0.5f ) < 0.0001f );

    input = {};
    input.right = 1.0f;
    REQUIRE( camera::CubeCamera_Advance( &camera, input, 0.1f ) );
    REQUIRE( std::abs( camera.position.y + 0.5f ) < 0.0001f );

    input = {};
    input.up = 1.0f;
    input.fast = true;
    REQUIRE( camera::CubeCamera_Advance( &camera, input, 0.1f ) );
    REQUIRE( std::abs( camera.position.z - 1.5f ) < 0.0001f );
}

TEST_CASE( "sample camera normalizes diagonal movement and clamps a stalled frame",
    "[render][camera]" )
{
    camera::cube_camera_t camera{};
    camera.position = math::CY_VEC3_ZERO;
    camera.moveSpeed = 5.0f;
    camera::cube_camera_input_t input{};
    input.forward = 1.0f;
    input.right = 1.0f;
    REQUIRE( camera::CubeCamera_Advance( &camera, input, 10.0f ) );
    REQUIRE( std::abs( math::Vec3_Length( camera.position ) - 0.5f ) < 0.0001f );
    REQUIRE( camera.position.x > 0.0f );
    REQUIRE( camera.position.y < 0.0f );
}

TEST_CASE( "sample camera mouse look follows screen motion and cannot pass vertical",
    "[render][camera]" )
{
    camera::cube_camera_t camera{};
    camera::cube_camera_input_t input{};
    input.lookDeltaX = 100.0f;
    input.lookDeltaY = -100.0f;
    REQUIRE( camera::CubeCamera_Advance( &camera, input, 0.0f ) );
    REQUIRE( std::abs( camera.yawRadians + 0.25f ) < 0.0001f );
    REQUIRE( std::abs( camera.pitchRadians - 0.25f ) < 0.0001f );

    input = {};
    input.lookDeltaY = -10000.0f;
    REQUIRE( camera::CubeCamera_Advance( &camera, input, 0.0f ) );
    REQUIRE( camera.pitchRadians < math::CY_HALF_PI_F );
    REQUIRE( camera.pitchRadians > 1.5f );
    math::mat4_t view{};
    REQUIRE( camera::CubeCamera_TryView( camera, &view ) );
}

TEST_CASE( "sample camera rejects bad input without changing the pose", "[render][camera]" )
{
    camera::cube_camera_t camera{};
    camera.position = { 1.0f, 2.0f, 3.0f };
    const camera::cube_camera_t original = camera;
    camera::cube_camera_input_t input{};
    input.forward = 1.0f;
    REQUIRE_FALSE( camera::CubeCamera_Advance( &camera, input, -0.1f ) );
    input.lookDeltaX = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE( camera::CubeCamera_Advance( &camera, input, 0.1f ) );
    REQUIRE( math::Vec3_EqualsExact( camera.position, original.position ) );
    REQUIRE( camera.yawRadians == original.yawRadians );
    REQUIRE( camera.pitchRadians == original.pitchRadians );
}
