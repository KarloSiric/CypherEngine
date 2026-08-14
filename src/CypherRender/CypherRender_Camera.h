//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Camera.h
//  Purpose: Declares the CypherRender Render Camera module.
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

#ifndef CYPHER_ENGINE_RENDER_CAMERA_H
#define CYPHER_ENGINE_RENDER_CAMERA_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath.h"
#include "CypherRender_Config.h"

namespace cypher::engine::render
{

enum camera_projection_mode_t {
    PERSPECTIVE,
    ORTOGRAPHIC
};

struct camera_desc_t {
    camera_projection_mode_t cameraProjectionMode{ camera_projection_mode_t::PERSPECTIVE };
    common::f32 fovYRadians{ CYPHER_RENDER_DEFAULT_FOV_Y_RADIANS };
    common::f32 aspectRatio{ CYPHER_RENDER_DEFAULT_ASPECT_RATIO };
    common::f32 nearZ{ CYPHER_RENDER_DEFAULT_NEAR_Z };
    common::f32 farZ{ CYPHER_RENDER_DEFAULT_FAR_Z };
};

struct camera_t {
    ::cypher::math::vec3_t position{};
    ::cypher::math::quat_t orientation{ ::cypher::math::CY_QUAT_IDENTITY };

    camera_desc_t cameraDesc{};

    ::cypher::math::mat4_t view{ ::cypher::math::CY_MAT4_IDENTITY };
    ::cypher::math::mat4_t projection{ ::cypher::math::CY_MAT4_IDENTITY };
    ::cypher::math::mat4_t projectionView{ ::cypher::math::CY_MAT4_IDENTITY };
    ::cypher::math::frustum_t frustum{};
};

void CypherRender_CameraInit( camera_t &camera, const camera_desc_t &cameraDesc );

void CypherRender_CameraUpdateMatrices( camera_t &camera );

void CypherRender_CameraSetPerspective( camera_t &camera, common::f32 fovYRadians, common::f32 aspectRation, common::f32 nearZ, common::f32 farZ );

void CypherRender_CameraSetTransform(
    camera_t &camera,
    const ::cypher::math::vec3_t &position,
    const ::cypher::math::quat_t &orientation );

void CypherRender_CameraSetPosition(
    camera_t &camera,
    const ::cypher::math::vec3_t &position );

void CypherRender_CameraSetOrientation(
    camera_t &camera,
    const ::cypher::math::quat_t &orientation );

void CypherRender_CameraSetPerspectiveMode( camera_t &camera, camera_projection_mode_t &mode );

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_CAMERA_H
