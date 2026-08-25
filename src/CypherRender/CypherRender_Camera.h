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

/*
================
Camera Contract

Camera state is renderer-front-end data. View and projection derivation must use the engine
coordinate conventions and must not depend on a particular graphics API.
================
*/

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
    PERSPECTIVE, // Perspective divide provides depth foreshortening.
    ORTOGRAPHIC  // Parallel projection preserves object scale with distance.
};

struct camera_desc_t {
    camera_projection_mode_t cameraProjectionMode{ camera_projection_mode_t::PERSPECTIVE }; // Projection family.
    common::f32 fovYRadians{ CYPHER_RENDER_DEFAULT_FOV_Y_RADIANS }; // Vertical perspective field of view in radians.
    common::f32 aspectRatio{ CYPHER_RENDER_DEFAULT_ASPECT_RATIO };   // Drawable width divided by height.
    common::f32 nearZ{ CYPHER_RENDER_DEFAULT_NEAR_Z };               // Positive near clipping distance.
    common::f32 farZ{ CYPHER_RENDER_DEFAULT_FAR_Z };                 // Far clipping distance greater than nearZ.
};

struct camera_t {
    ::cypher::math::vec3_t position{};                      // Camera origin in world space.
    ::cypher::math::quat_t orientation{ ::cypher::math::CY_QUAT_IDENTITY }; // World-space camera rotation.

    camera_desc_t cameraDesc{};                             // Parameters from which projection is derived.

    ::cypher::math::mat4_t view{ ::cypher::math::CY_MAT4_IDENTITY }; // World-to-camera transform.
    ::cypher::math::mat4_t projection{ ::cypher::math::CY_MAT4_IDENTITY }; // Camera-to-clip transform.
    ::cypher::math::mat4_t projectionView{ ::cypher::math::CY_MAT4_IDENTITY }; // Cached projection * view.
    ::cypher::math::frustum_t frustum{};                    // World-space planes derived from projectionView.
};

void R_CameraInit( camera_t &camera, const camera_desc_t &cameraDesc );

void R_CameraUpdateMatrices( camera_t &camera );

void R_CameraSetPerspective( camera_t &camera, common::f32 fovYRadians, common::f32 aspectRation, common::f32 nearZ, common::f32 farZ );

void R_CameraSetTransform(
    camera_t &camera,
    const ::cypher::math::vec3_t &position,
    const ::cypher::math::quat_t &orientation );

void R_CameraSetPosition(
    camera_t &camera,
    const ::cypher::math::vec3_t &position );

void R_CameraSetOrientation(
    camera_t &camera,
    const ::cypher::math::quat_t &orientation );

void R_CameraSetPerspectiveMode( camera_t &camera, camera_projection_mode_t &mode );

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_CAMERA_H
