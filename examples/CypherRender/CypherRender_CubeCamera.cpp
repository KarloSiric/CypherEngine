//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_CubeCamera.cpp
//  Purpose: Implements the cube sample's value-only fly camera.
//  Details: Z is up, +X is forward at zero yaw, and +Y is left. No System or
//           renderer state is accessed here, so movement can be tested alone.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_CubeCamera.h"

#include <algorithm>
#include <cmath>

namespace cypher::samples::render
{

namespace
{
constexpr float LOOK_RADIANS_PER_PIXEL = 0.0025f;
constexpr float FAST_MULTIPLIER = 3.0f;
constexpr float MAX_FRAME_SECONDS = 0.1f;
constexpr float PITCH_LIMIT_RADIANS = math::CY_HALF_PI_F - math::CY_DEGREES_TO_RADIANS_F;

bool Camera_IsValid( const cube_camera_t &camera ) noexcept
{
    return math::Vec3_IsFinite( camera.position ) &&
        std::isfinite( camera.yawRadians ) &&
        std::isfinite( camera.pitchRadians ) &&
        std::abs( camera.pitchRadians ) <= PITCH_LIMIT_RADIANS &&
        std::isfinite( camera.moveSpeed ) && camera.moveSpeed > 0.0f;
}

math::vec3_t Camera_Forward( const cube_camera_t &camera ) noexcept
{
    const float cosPitch = std::cos( camera.pitchRadians );
    return { cosPitch * std::cos( camera.yawRadians ),
             cosPitch * std::sin( camera.yawRadians ),
             std::sin( camera.pitchRadians ) };
}
} // namespace

bool CubeCamera_FromLookAt(
    const math::vec3_t position, const math::vec3_t target,
    const float moveSpeed, cube_camera_t *cameraOut ) noexcept
{
    if ( cameraOut == nullptr || !math::Vec3_IsFinite( position ) ||
         !math::Vec3_IsFinite( target ) || !std::isfinite( moveSpeed ) ||
         moveSpeed <= 0.0f ) return false;

    math::vec3_t direction{};
    if ( !math::Vec3_TryNormalize( math::Vec3_Subtract( target, position ),
             0.0001f, &direction, nullptr ) ) return false;

    cube_camera_t next{};
    next.position = position;
    next.yawRadians = std::atan2( direction.y, direction.x );
    next.pitchRadians = std::clamp( std::atan2( direction.z,
        std::sqrt( direction.x * direction.x + direction.y * direction.y ) ),
        -PITCH_LIMIT_RADIANS, PITCH_LIMIT_RADIANS );
    next.moveSpeed = moveSpeed;
    if ( !Camera_IsValid( next ) ) return false;
    *cameraOut = next;
    return true;
}

bool CubeCamera_Advance(
    cube_camera_t *camera, const cube_camera_input_t &input,
    const float deltaSeconds ) noexcept
{
    if ( camera == nullptr || !Camera_IsValid( *camera ) ||
         !std::isfinite( deltaSeconds ) || deltaSeconds < 0.0f ||
         !std::isfinite( input.forward ) || !std::isfinite( input.right ) ||
         !std::isfinite( input.up ) || !std::isfinite( input.lookDeltaX ) ||
         !std::isfinite( input.lookDeltaY ) ) return false;

    cube_camera_t next = *camera;
    const float mouseX = std::clamp( input.lookDeltaX, -4096.0f, 4096.0f );
    const float mouseY = std::clamp( input.lookDeltaY, -4096.0f, 4096.0f );
    next.yawRadians = std::remainder(
        next.yawRadians - mouseX * LOOK_RADIANS_PER_PIXEL, math::CY_TAU_F );
    next.pitchRadians = std::clamp(
        next.pitchRadians - mouseY * LOOK_RADIANS_PER_PIXEL,
        -PITCH_LIMIT_RADIANS, PITCH_LIMIT_RADIANS );

    const math::vec3_t forward = Camera_Forward( next );
    // The normalized forward x world-up axis is camera-right in Cypher's
    // forward/left/up convention. Its length stays one near vertical pitch.
    const math::vec3_t right{ std::sin( next.yawRadians ),
                              -std::cos( next.yawRadians ), 0.0f };
    const math::vec3_t requested = math::Vec3_Add(
        math::Vec3_Add( math::Vec3_Scale( forward, input.forward ),
                        math::Vec3_Scale( right, input.right ) ),
        math::Vec3_Scale( math::CY_VEC3_UP, input.up ) );
    const float requestedLengthSquared = math::Vec3_LengthSquared( requested );
    if ( !std::isfinite( requestedLengthSquared ) ) return false;
    if ( requestedLengthSquared > 0.0f ) {
        const float frameSeconds = std::min( deltaSeconds, MAX_FRAME_SECONDS );
        const float distance = next.moveSpeed *
            ( input.fast ? FAST_MULTIPLIER : 1.0f ) * frameSeconds;
        next.position = math::Vec3_MulAdd( next.position, requested,
            distance / std::sqrt( requestedLengthSquared ) );
    }
    if ( !Camera_IsValid( next ) ) return false;
    *camera = next;
    return true;
}

bool CubeCamera_TryView(
    const cube_camera_t &camera, math::mat4_t *viewOut ) noexcept
{
    if ( viewOut == nullptr || !Camera_IsValid( camera ) ) return false;
    const math::vec3_t target = math::Vec3_Add(
        camera.position, Camera_Forward( camera ) );
    if ( !math::Vec3_IsFinite( target ) ) return false;
    return math::Mat4_TryLookAtRH( camera.position, target,
        math::CY_VEC3_UP, 0.0001f, viewOut );
}

} // namespace cypher::samples::render
