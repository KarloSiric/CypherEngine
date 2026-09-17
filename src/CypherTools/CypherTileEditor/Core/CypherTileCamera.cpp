//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileCamera.cpp
//  Purpose: Implements shared Z-up orbit and free-flight preview navigation.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileCamera.h"

#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <numbers>

namespace cypher::tools::tile_editor
{
namespace math = ::cypher::math;
namespace
{
constexpr float RADIANS_PER_DEGREE = std::numbers::pi_v<float> / 180.0f;
constexpr float PITCH_LIMIT = 89.0f * RADIANS_PER_DEGREE;

float FiniteClamped( float value, float minimum,
    float maximum, float fallback ) noexcept
{
    return std::isfinite( value ) ? std::clamp( value, minimum, maximum ) : fallback;
}

bool ValidBounds( math::vec3_t minimum, math::vec3_t maximum ) noexcept
{
    return math::Vec3_IsFinite( minimum ) && math::Vec3_IsFinite( maximum ) &&
        minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
}
} // namespace

void CypherTileCamera_SetSettings(
    tile_camera_t &camera, const tile_camera_settings_t &settings ) noexcept
{
    camera.settings.moveSpeed = FiniteClamped( settings.moveSpeed, 0.05f, 100000.0f, 8.0f );
    camera.settings.lookSensitivity = FiniteClamped(
        settings.lookSensitivity, 0.00001f, 0.1f, 0.2f * RADIANS_PER_DEGREE );
    camera.settings.verticalFovDegrees = FiniteClamped(
        settings.verticalFovDegrees, 20.0f, 120.0f, 60.0f );
    camera.settings.invertMouseY = settings.invertMouseY;
    camera.settings.panSensitivity = FiniteClamped( settings.panSensitivity, 0.1f, 5.0f, 1.0f );
    camera.settings.zoomSensitivity = FiniteClamped( settings.zoomSensitivity, 0.1f, 5.0f, 1.0f );
    camera.settings.fastMultiplier = FiniteClamped( settings.fastMultiplier, 1.0f, 20.0f, 4.0f );
    camera.settings.slowMultiplier = FiniteClamped( settings.slowMultiplier, 0.01f, 1.0f, 0.25f );
    camera.settings.invertWheel = settings.invertWheel;
}

void CypherTileCamera_SetMode( tile_camera_t &camera, tile_camera_mode_t mode ) noexcept
{
    // Eye and orientation are canonical in both modes. The orbit pivot is
    // always eye + forward * distance, so switching modes cannot jump the view.
    camera.mode = mode;
}

math::vec3_t CypherTileCamera_Forward( const tile_camera_t &camera ) noexcept
{
    const float horizontal = std::cos( camera.pitchRadians );
    return { std::cos( camera.yawRadians ) * horizontal,
             std::sin( camera.yawRadians ) * horizontal,
             std::sin( camera.pitchRadians ) };
}

math::vec3_t CypherTileCamera_Right( const tile_camera_t &camera ) noexcept
{
    return { std::sin( camera.yawRadians ), -std::cos( camera.yawRadians ), 0.0f };
}

math::vec3_t CypherTileCamera_Up( const tile_camera_t &camera ) noexcept
{
    // Deriving up from yaw keeps exact top/bottom views non-collinear with
    // forward while retaining the normal Z-up horizon for perspective views.
    return math::Vec3_Cross(
        CypherTileCamera_Right( camera ), CypherTileCamera_Forward( camera ) );
}

void CypherTileCamera_ApplyViewPreset(
    tile_camera_t &camera, tile_camera_view_preset_t preset ) noexcept
{
    if ( !math::Vec3_IsFinite( camera.position ) ||
         !std::isfinite( camera.yawRadians ) ||
         !std::isfinite( camera.pitchRadians ) ||
         !std::isfinite( camera.orbitDistance ) || camera.orbitDistance <= 0.0f ) return;
    const math::vec3_t focus = math::Vec3_Add( camera.position,
        math::Vec3_Scale( CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
    if ( !math::Vec3_IsFinite( focus ) ) return;
    tile_camera_t candidate = camera;
    switch ( preset ) {
        case tile_camera_view_preset_t::PERSPECTIVE:
            candidate.yawRadians = 2.42159265f;
            candidate.pitchRadians = -0.62f;
            break;
        case tile_camera_view_preset_t::TOP:
            candidate.yawRadians = std::numbers::pi_v<float> * 0.5f;
            candidate.pitchRadians = -std::numbers::pi_v<float> * 0.5f;
            break;
        case tile_camera_view_preset_t::BOTTOM:
            candidate.yawRadians = std::numbers::pi_v<float> * 0.5f;
            candidate.pitchRadians = std::numbers::pi_v<float> * 0.5f;
            break;
        case tile_camera_view_preset_t::FRONT:
            candidate.yawRadians = std::numbers::pi_v<float> * 0.5f;
            candidate.pitchRadians = 0.0f;
            break;
        case tile_camera_view_preset_t::BACK:
            candidate.yawRadians = -std::numbers::pi_v<float> * 0.5f;
            candidate.pitchRadians = 0.0f;
            break;
        case tile_camera_view_preset_t::LEFT:
            candidate.yawRadians = 0.0f;
            candidate.pitchRadians = 0.0f;
            break;
        case tile_camera_view_preset_t::RIGHT:
            candidate.yawRadians = std::numbers::pi_v<float>;
            candidate.pitchRadians = 0.0f;
            break;
        default:
            return;
    }
    candidate.position = math::Vec3_Subtract( focus,
        math::Vec3_Scale( CypherTileCamera_Forward( candidate ), candidate.orbitDistance ) );
    if ( !math::Vec3_IsFinite( candidate.position ) ) return;
    camera.position = candidate.position;
    camera.yawRadians = candidate.yawRadians;
    camera.pitchRadians = candidate.pitchRadians;
}

void CypherTileCamera_Level( tile_camera_t &camera ) noexcept
{
    if ( !math::Vec3_IsFinite( camera.position ) ||
         !std::isfinite( camera.yawRadians ) ||
         !std::isfinite( camera.pitchRadians ) ) return;
    // A fly camera has no user-visible orbit target. Leveling it should feel
    // like straightening the pilot's head, so the eye must remain stationary.
    if ( camera.mode == tile_camera_mode_t::FLY ) {
        camera.pitchRadians = 0.0f;
        return;
    }
    if ( !std::isfinite( camera.orbitDistance ) ||
         camera.orbitDistance <= 0.0f ) return;
    // In orbit mode the target is the subject being inspected. Preserve that
    // target while moving the eye onto the target's horizontal plane.
    const math::vec3_t focus = math::Vec3_Add( camera.position,
        math::Vec3_Scale( CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
    if ( !math::Vec3_IsFinite( focus ) ) return;
    tile_camera_t candidate = camera;
    candidate.pitchRadians = 0.0f;
    candidate.position = math::Vec3_Subtract( focus,
        math::Vec3_Scale( CypherTileCamera_Forward( candidate ), candidate.orbitDistance ) );
    if ( !math::Vec3_IsFinite( candidate.position ) ) return;
    camera.position = candidate.position;
    camera.pitchRadians = candidate.pitchRadians;
}

bool CypherTileCamera_TranslateWorld(
    tile_camera_t &camera, math::vec3_t offset ) noexcept
{
    if ( !math::Vec3_IsFinite( camera.position ) ||
         !math::Vec3_IsFinite( offset ) ) return false;
    constexpr float maximum = std::numeric_limits<float>::max();
    const auto canAdd = [maximum]( float position, float delta ) noexcept {
        return !( delta > 0.0f && position > maximum - delta ) &&
               !( delta < 0.0f && position < -maximum - delta );
    };
    if ( !canAdd( camera.position.x, offset.x ) ||
         !canAdd( camera.position.y, offset.y ) ||
         !canAdd( camera.position.z, offset.z ) ) return false;
    const math::vec3_t translated = math::Vec3_Add( camera.position, offset );
    if ( !math::Vec3_IsFinite( translated ) ||
         math::Vec3_NearlyEquals( translated, camera.position, 0.0f, 0.0f ) ) return false;
    camera.position = translated;
    return true;
}

void CypherTileCamera_UpdateBounds( tile_camera_t &camera,
    math::vec3_t minimum, math::vec3_t maximum ) noexcept
{
    if ( !ValidBounds( minimum, maximum ) ) return;
    const math::vec3_t center = math::Vec3_Add(
        math::Vec3_Scale( minimum, 0.5f ), math::Vec3_Scale( maximum, 0.5f ) );
    const float radius = math::Vec3_Distance( center, maximum );
    if ( !std::isfinite( radius ) ) return;
    camera.boundsCenter = center;
    camera.boundsRadius = std::max( radius, 0.5f );
}

void CypherTileCamera_FrameBounds( tile_camera_t &camera,
    math::vec3_t minimum, math::vec3_t maximum,
    float aspect, bool resetOrientation ) noexcept
{
    if ( !ValidBounds( minimum, maximum ) || !std::isfinite( aspect ) || aspect <= 0.0f ) return;
    CypherTileCamera_UpdateBounds( camera, minimum, maximum );
    if ( resetOrientation ) {
        camera.yawRadians = 2.42159265f;
        camera.pitchRadians = -0.62f;
    }
    constexpr double padding = 1.10;
    const double halfVertical = camera.settings.verticalFovDegrees * RADIANS_PER_DEGREE * 0.5;
    const std::array<double, 2> slopes{
        std::tan( halfVertical ) * aspect / padding, std::tan( halfVertical ) / padding };
    const math::vec3_t forward = CypherTileCamera_Forward( camera );
    const math::vec3_t right = CypherTileCamera_Right( camera );
    const math::vec3_t up = CypherTileCamera_Up( camera );
    std::array<std::array<double, 3>, 8> corners{};
    std::array<double, 2> maximumLower{
        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
    std::array<double, 2> minimumUpper{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() };
    double distance = 0.5;
    for ( int corner = 0; corner < 8; ++corner ) {
        const math::vec3_t offset = math::Vec3_Subtract( {
            corner & 1 ? maximum.x : minimum.x,
            corner & 2 ? maximum.y : minimum.y,
            corner & 4 ? maximum.z : minimum.z }, camera.boundsCenter );
        auto &point = corners[corner];
        point = { math::Vec3_Dot( offset, right ), math::Vec3_Dot( offset, up ),
                  math::Vec3_Dot( offset, forward ) };
        distance = std::max( distance, 0.05 - point[2] );
        for ( int axis = 0; axis < 2; ++axis ) {
            maximumLower[axis] = std::max( maximumLower[axis], point[axis] - slopes[axis] * point[2] );
            minimumUpper[axis] = std::min( minimumUpper[axis], point[axis] + slopes[axis] * point[2] );
        }
    }
    // A corner at (x, z) constrains lateral camera pan to
    // [x - slope * (distance + z), x + slope * (distance + z)]. The
    // intervals must overlap for both axes. Solving their overlap gives the
    // closest safe eye distance without forcing the AABB center onto screen
    // center, which wastes space above obliquely viewed flat maps.
    for ( int axis = 0; axis < 2; ++axis )
        distance = std::max( distance, ( maximumLower[axis] - minimumUpper[axis] ) / ( 2.0 * slopes[axis] ) );
    if ( !std::isfinite( distance ) ) return;

    std::array<double, 2> pan{};
    for ( int axis = 0; axis < 2; ++axis ) {
        // Center the projected silhouette even when this axis has spare room.
        // The smallest symmetric image slope must contain every corner pair:
        // |xi - xj| <= slope * (depthi + depthj). Its pan interval then
        // collapses at the centered position. Eight corners keep this exact
        // calculation small and avoid iterative camera fitting.
        double centeredSlope = 0.0;
        for ( std::size_t i = 0; i < corners.size(); ++i ) {
            for ( std::size_t j = i + 1; j < corners.size(); ++j ) {
                centeredSlope = std::max( centeredSlope,
                    std::abs( corners[i][axis] - corners[j][axis] ) /
                    ( 2.0 * distance + corners[i][2] + corners[j][2] ) );
            }
        }
        double lower = -std::numeric_limits<double>::infinity();
        double upper = std::numeric_limits<double>::infinity();
        for ( const auto &point : corners ) {
            lower = std::max( lower, point[axis] - centeredSlope * ( distance + point[2] ) );
            upper = std::min( upper, point[axis] + centeredSlope * ( distance + point[2] ) );
        }
        pan[axis] = ( lower + upper ) * 0.5;
    }
    const math::vec3_t position = math::Vec3_Add(
        math::Vec3_Subtract( camera.boundsCenter, math::Vec3_Scale( forward, static_cast<float>( distance ) ) ),
        math::Vec3_Add( math::Vec3_Scale( right, static_cast<float>( pan[0] ) ),
                       math::Vec3_Scale( up, static_cast<float>( pan[1] ) ) ) );
    if ( !math::Vec3_IsFinite( position ) ) return;
    camera.orbitDistance = static_cast<float>( distance );
    camera.position = position;

}

void CypherTileCamera_ApplyLook( tile_camera_t &camera, float deltaX, float deltaY ) noexcept
{
    if ( !std::isfinite( deltaX ) || !std::isfinite( deltaY ) ) return;
    const math::vec3_t pivot = math::Vec3_Add( camera.position,
        math::Vec3_Scale( CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
    // Reduce huge synthetic inputs before wrapping to avoid overflow poisoning.
    camera.yawRadians = std::remainder( camera.yawRadians -
        std::clamp( deltaX, -100000.0f, 100000.0f ) * camera.settings.lookSensitivity,
        2.0f * std::numbers::pi_v<float> );
    camera.pitchRadians = std::clamp( camera.pitchRadians +
        std::clamp( deltaY, -100000.0f, 100000.0f ) * camera.settings.lookSensitivity *
            ( camera.settings.invertMouseY ? 1.0f : -1.0f ), -PITCH_LIMIT, PITCH_LIMIT );
    if ( camera.mode == tile_camera_mode_t::ORBIT ) {
        camera.position = math::Vec3_Subtract( pivot,
            math::Vec3_Scale( CypherTileCamera_Forward( camera ), camera.orbitDistance ) );
    }
}

void CypherTileCamera_Pan( tile_camera_t &camera,
    float deltaX, float deltaY, float viewportHeight ) noexcept
{
    if ( !std::isfinite( deltaX ) || !std::isfinite( deltaY ) ||
         !std::isfinite( viewportHeight ) || viewportHeight <= 0.0f ) return;
    const float worldPerPixel = 2.0f * camera.orbitDistance * std::tan(
        camera.settings.verticalFovDegrees * RADIANS_PER_DEGREE * 0.5f ) /
        viewportHeight * camera.settings.panSensitivity;
    const math::vec3_t right = CypherTileCamera_Right( camera );
    const math::vec3_t up = math::Vec3_Cross( right, CypherTileCamera_Forward( camera ) );
    camera.position = math::Vec3_Add( camera.position, math::Vec3_Scale(
        math::Vec3_Add( math::Vec3_Scale( right, -deltaX ),
                       math::Vec3_Scale( up, deltaY ) ), worldPerPixel ) );
}

void CypherTileCamera_Wheel( tile_camera_t &camera, float ticks ) noexcept
{
    if ( !std::isfinite( ticks ) ) return;
    const float exponent = std::clamp( ticks, -40.0f, 40.0f ) * 0.15f *
        camera.settings.zoomSensitivity * ( camera.settings.invertWheel ? -1.0f : 1.0f );
    if ( camera.mode == tile_camera_mode_t::FLY ) {
        camera.settings.moveSpeed = std::clamp(
            camera.settings.moveSpeed * std::exp( exponent ), 0.05f, 100000.0f );
        return;
    }
    const float previousDistance = camera.orbitDistance;
    camera.orbitDistance = std::clamp(
        previousDistance * std::exp( -exponent ), 0.05f, 100000000.0f );
    camera.position = math::Vec3_Add( camera.position,
        math::Vec3_Scale( CypherTileCamera_Forward( camera ),
            previousDistance - camera.orbitDistance ) );
}

bool CypherTileCamera_Move( tile_camera_t &camera,
    const tile_camera_input_t &input, float deltaSeconds ) noexcept
{
    if ( camera.mode != tile_camera_mode_t::FLY || !std::isfinite( deltaSeconds ) ||
         deltaSeconds <= 0.0f || !std::isfinite( input.forward ) ||
         !std::isfinite( input.right ) || !std::isfinite( input.up ) ) return false;
    math::vec3_t direction = math::Vec3_Add(
        math::Vec3_Scale( CypherTileCamera_Forward( camera ), std::clamp( input.forward, -1.0f, 1.0f ) ),
        math::Vec3_Scale( CypherTileCamera_Right( camera ), std::clamp( input.right, -1.0f, 1.0f ) ) );
    direction.z += std::clamp( input.up, -1.0f, 1.0f );
    const float length = math::Vec3_Length( direction );
    if ( length < 0.00001f ) return false;
    if ( length > 1.0f ) direction = math::Vec3_Scale( direction, 1.0f / length );
    // Slow wins when both modifiers are held, making precision movement safe.
    const float multiplier = input.slow ? camera.settings.slowMultiplier
        : ( input.fast ? camera.settings.fastMultiplier : 1.0f );
    camera.position = math::Vec3_Add( camera.position, math::Vec3_Scale( direction,
        camera.settings.moveSpeed * multiplier * std::min( deltaSeconds, 0.1f ) ) );
    return true;
}

bool CypherTileCamera_BuildMatrices( const tile_camera_t &camera, float aspect,
    math::mat4_t &viewOut, math::mat4_t &projectionOut ) noexcept
{
    if ( !std::isfinite( aspect ) || aspect <= 0.0f || !math::Vec3_IsFinite( camera.position ) ) return false;
    const float farDistance = std::max( 100.0f,
        math::Vec3_Distance( camera.position, camera.boundsCenter ) + camera.boundsRadius * 2.0f );
    return math::Mat4_TryLookAtRH( camera.position,
               math::Vec3_Add( camera.position, CypherTileCamera_Forward( camera ) ),
               CypherTileCamera_Up( camera ), 0.0001f, &viewOut ) &&
           math::Mat4_TryPerspectiveRH(
               math::Angle_FromDegrees( camera.settings.verticalFovDegrees ), aspect,
               0.025f, farDistance, math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &projectionOut );
}

} // namespace cypher::tools::tile_editor
