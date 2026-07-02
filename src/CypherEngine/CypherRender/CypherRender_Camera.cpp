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
#include "CypherMath_Frustum.h"
#include "CypherMath_Mat.h"
#include "CypherMath_Quat.h"

namespace cypher::engine::render
{

void CypherRender_CameraInit( camera_t &camera, const camera_desc_t &cameraDesc )
{
    camera = camera_t{};

    camera.cameraDesc = cameraDesc;
    camera.position = math::vec3_t{};
    camera.orientation = math::CypherMath_QuatIdentity();

    CypherRender_CameraUpdateMatrices( camera );

    LOG_INFO( log::channel_t::RENDER, "camera initialized: fov_y=%f, aspect=%f, near=%f, far=%f.", camera.cameraDesc.fovYRadians, camera.cameraDesc.aspectRatio, camera.cameraDesc.nearZ, camera.cameraDesc.farZ );
}

void CypherRender_CameraUpdateMatrices( camera_t &camera )
{
    camera.orientation = math::CypherMath_QuatNormalize( camera.orientation );

    const math::vec3_t forward  = math::CypherMath_QuatForwardVec3( camera.orientation );
    const math::vec3_t up       = math::CypherMath_QuatUpVec3( camera.orientation );

    // we do not need right basis vector because the LookAt matrix func builds it already internally

    const math::vec3_t target   = math::CypherMath_Vec3Add( camera.position, forward );

    camera.view = math::CypherMath_Mat4LookAt( camera.position, target, up );
    camera.projection = math::CypherMath_Mat4Perspective( camera.cameraDesc.fovYRadians,
                                                    camera.cameraDesc.aspectRatio,
                                                    camera.cameraDesc.nearZ,
                                                    camera.cameraDesc.farZ );
    camera.projectionView = math::CypherMath_Mat4Multiply( camera.projection, camera.view );
    camera.frustum = math::CypherMath_FrustumFromProjectionView( camera.projectionView );
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

void CypherRender_CameraSetTransform( camera_t &camera, const math::vec3_t &position, const math::quat_t &orientation )
{
    camera.position = position;
    camera.orientation = math::CypherMath_QuatNormalize( orientation );

    CypherRender_CameraUpdateMatrices( camera );
}

void CypherRender_CameraSetPosition( camera_t &camera, const math::vec3_t &position )
{
    camera.position = position;

    CypherRender_CameraUpdateMatrices( camera );
}

void CypherRender_CameraSetOrientation( camera_t &camera, const math::quat_t &orientation )
{
    camera.orientation = math::CypherMath_QuatNormalize( orientation );

    CypherRender_CameraUpdateMatrices( camera );
}

void CypherRender_CameraSetPerspectiveMode( camera_t &camera, camera_projection_mode_t &mode )
{
    camera.cameraDesc.cameraProjectionMode = mode;

    CypherRender_CameraUpdateMatrices( camera );

    LOG_INFO( log::channel_t::RENDER, "camera projection mode changed: mode=%u.", static_cast<common::u32>( mode ) );
}

}       // namespace cypher::engine::render
