//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherRender/CypherRender_Camera.cpp
//  Purpose: Implements the CypherRender Render Camera module.
//  Details: This file participates in the renderer bootstrap and draw path. Keep API
//           boundaries clear so the renderer can grow from simple OpenGL startup into
//           a fuller rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Camera.h"
#include "CypherLog.h"

namespace cypher::engine::render
{

namespace cmath = ::cypher::math;

namespace
{

constexpr common::f32 CAMERA_MINIMUM_DIRECTION_LENGTH = 1.0e-6f;
constexpr common::f32 CAMERA_MINIMUM_PLANE_NORMAL_LENGTH = 1.0e-6f;

} // namespace

void CypherRender_CameraInit( camera_t &camera, const camera_desc_t &cameraDesc )
{
    camera = camera_t{};

    camera.cameraDesc = cameraDesc;
    camera.position = cmath::CY_VEC3_ZERO;
    camera.orientation = cmath::CY_QUAT_IDENTITY;

    CypherRender_CameraUpdateMatrices( camera );

    LOG_INFO( log::channel_t::RENDER, "camera initialized: fov_y=%f, aspect=%f, near=%f, far=%f.", camera.cameraDesc.fovYRadians, camera.cameraDesc.aspectRatio, camera.cameraDesc.nearZ, camera.cameraDesc.farZ );
}

void CypherRender_CameraUpdateMatrices( camera_t &camera )
{
    cmath::quat_t normalizedOrientation{};
    if ( !cmath::Quat_TryNormalize(
             camera.orientation,
             CAMERA_MINIMUM_DIRECTION_LENGTH,
             &normalizedOrientation,
             nullptr ) ) {
        normalizedOrientation = cmath::CY_QUAT_IDENTITY;
        LOG_ERROR(
            log::channel_t::RENDER,
            "camera orientation was invalid; identity orientation selected." );
    }
    camera.orientation = normalizedOrientation;

    const cmath::vec3_t forward = cmath::Quat_Forward( camera.orientation );
    const cmath::vec3_t up = cmath::Quat_Up( camera.orientation );
    const cmath::vec3_t target = cmath::Vec3_Add( camera.position, forward );

    if ( !cmath::Mat4_TryLookAtRH(
             camera.position,
             target,
             up,
             CAMERA_MINIMUM_DIRECTION_LENGTH,
             &camera.view ) ) {
        camera.view = cmath::CY_MAT4_IDENTITY;
        LOG_ERROR( log::channel_t::RENDER, "camera view matrix construction failed." );
    }

    if ( !cmath::Mat4_TryPerspectiveRH(
             cmath::Angle_FromRadians( camera.cameraDesc.fovYRadians ),
             camera.cameraDesc.aspectRatio,
             camera.cameraDesc.nearZ,
             camera.cameraDesc.farZ,
             cmath::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
             &camera.projection ) ) {
        camera.projection = cmath::CY_MAT4_IDENTITY;
        LOG_ERROR( log::channel_t::RENDER, "camera projection matrix construction failed." );
    }

    camera.projectionView = cmath::Mat4_Multiply( camera.projection, camera.view );
    if ( !cmath::Frustum_TryFromViewProjection(
             camera.projectionView,
             cmath::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
             CAMERA_MINIMUM_PLANE_NORMAL_LENGTH,
             &camera.frustum ) ) {
        camera.frustum = {};
        LOG_ERROR( log::channel_t::RENDER, "camera frustum construction failed." );
    }
}

void CypherRender_CameraSetPerspective( camera_t &camera, common::f32 fovYRadians, common::f32 aspectRatio, common::f32 nearZ, common::f32 farZ )
{
    camera.cameraDesc.fovYRadians = fovYRadians;
    camera.cameraDesc.aspectRatio = aspectRatio;
    camera.cameraDesc.nearZ = nearZ;
    camera.cameraDesc.farZ = farZ;

    CypherRender_CameraUpdateMatrices( camera );

    LOG_INFO( log::channel_t::RENDER, "camera perspective changed: fov_y=%f, aspect=%f, near=%f, far=%f.", fovYRadians, aspectRatio, nearZ, farZ );
}

void CypherRender_CameraSetTransform(
    camera_t &camera,
    const cmath::vec3_t &position,
    const cmath::quat_t &orientation )
{
    camera.position = position;
    camera.orientation = orientation;

    CypherRender_CameraUpdateMatrices( camera );
}

void CypherRender_CameraSetPosition(
    camera_t &camera,
    const cmath::vec3_t &position )
{
    camera.position = position;

    CypherRender_CameraUpdateMatrices( camera );
}

void CypherRender_CameraSetOrientation(
    camera_t &camera,
    const cmath::quat_t &orientation )
{
    camera.orientation = orientation;

    CypherRender_CameraUpdateMatrices( camera );
}

void CypherRender_CameraSetPerspectiveMode( camera_t &camera, camera_projection_mode_t &mode )
{
    camera.cameraDesc.cameraProjectionMode = mode;

    CypherRender_CameraUpdateMatrices( camera );

    LOG_INFO( log::channel_t::RENDER, "camera projection mode changed: mode=%u.", static_cast<common::u32>( mode ) );
}

}       // namespace cypher::engine::render
