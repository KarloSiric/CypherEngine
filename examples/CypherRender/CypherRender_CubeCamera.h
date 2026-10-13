//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_CubeCamera.h
//  Purpose: Declares the cube sample's small, platform-independent fly camera.
//  Details: The sample maps System events to this value-only input. Camera math
//           never owns a window, an input queue, or a renderer resource.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_RENDER_CUBE_CAMERA_H
#define CYPHER_RENDER_CUBE_CAMERA_H

#include "CypherCommon/Mathlib/CypherMath_Matrix4.h"

namespace cypher::samples::render
{

struct cube_camera_t {
    math::vec3_t position{};
    float yawRadians{ 0.0f };   // Zero looks along world +X; positive turns toward +Y.
    float pitchRadians{ 0.0f }; // Positive looks toward world +Z.
    float moveSpeed{ 5.0f };    // World units per second before the fast modifier.
};

struct cube_camera_input_t {
    float forward{ 0.0f }; // -1, 0, or +1 after held-key reduction.
    float right{ 0.0f };
    float up{ 0.0f };
    float lookDeltaX{ 0.0f }; // Logical mouse pixels accumulated for this frame.
    float lookDeltaY{ 0.0f };
    bool fast{ false };
};

// Leaves output unchanged on invalid input. Initial orientation faces target.
[[nodiscard]] bool CubeCamera_FromLookAt(
    math::vec3_t position, math::vec3_t target, float moveSpeed,
    cube_camera_t *cameraOut ) noexcept;

// Applies one frame of normalized free-flight and bounded mouse look.
// Invalid values leave camera unchanged; elapsed time is limited to 0.1 s.
[[nodiscard]] bool CubeCamera_Advance(
    cube_camera_t *camera, const cube_camera_input_t &input,
    float deltaSeconds ) noexcept;

[[nodiscard]] bool CubeCamera_TryView(
    const cube_camera_t &camera, math::mat4_t *viewOut ) noexcept;

} // namespace cypher::samples::render

#endif // CYPHER_RENDER_CUBE_CAMERA_H
