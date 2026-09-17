//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileCamera.h
//  Purpose: Shares inspection camera behavior between Qt and runtime previews.
//  Details: Input adapters own buttons, focus and pointer capture. This module
//           owns only Z-up camera math, with no Qt or platform dependency.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_CAMERA_H
#define CYPHER_TOOLS_TILEEDITOR_CAMERA_H
#pragma once

#include "CypherCommon/Mathlib/CypherMath_Matrix4.h"

namespace cypher::tools::tile_editor
{

enum class tile_camera_mode_t { ORBIT, FLY };

// Deterministic editor viewpoints. Perspective is the familiar oblique
// three-quarter view; the remaining presets align the camera to a world axis.
enum class tile_camera_view_preset_t {
    PERSPECTIVE,
    TOP,
    BOTTOM,
    FRONT,
    BACK,
    LEFT,
    RIGHT
};

struct tile_camera_settings_t {
    float moveSpeed{ 8.0f };              // World units per second.
    float lookSensitivity{ 0.0034906585f }; // Radians per logical pixel (0.2 degrees).
    float verticalFovDegrees{ 60.0f };
    bool invertMouseY{ false };
    float panSensitivity{ 1.0f };
    float zoomSensitivity{ 1.0f };
    float fastMultiplier{ 4.0f };
    float slowMultiplier{ 0.25f };
    bool invertWheel{ false };

    bool operator==( const tile_camera_settings_t & ) const = default;
};

struct tile_camera_t {
    tile_camera_mode_t mode{ tile_camera_mode_t::FLY };
    ::cypher::math::vec3_t position{ 7.3425f, -6.4400f, 6.9724f };
    float yawRadians{ 2.42159265f };
    float pitchRadians{ -0.62f };
    float orbitDistance{ 12.0f };
    ::cypher::math::vec3_t boundsCenter{ ::cypher::math::CY_VEC3_ZERO };
    float boundsRadius{ 4.0f };
    tile_camera_settings_t settings{};
};

// The adapter emits axes in [-1, 1] only while navigation is captured. Positive
// right is camera-right; positive up is world +Z, independent of camera pitch.
struct tile_camera_input_t {
    float forward{ 0.0f };
    float right{ 0.0f };
    float up{ 0.0f };
    bool fast{ false };
    bool slow{ false };
};

void CypherTileCamera_SetSettings(
    tile_camera_t &camera, const tile_camera_settings_t &settings ) noexcept;
void CypherTileCamera_SetMode(
    tile_camera_t &camera, tile_camera_mode_t mode ) noexcept;

// Updating scene bounds never modifies eye, orientation, mode or orbit pivot.
// Use after a map reload so authoring does not interrupt inspection.
void CypherTileCamera_UpdateBounds( tile_camera_t &camera,
    ::cypher::math::vec3_t minimum, ::cypher::math::vec3_t maximum ) noexcept;
void CypherTileCamera_FrameBounds( tile_camera_t &camera,
    ::cypher::math::vec3_t minimum, ::cypher::math::vec3_t maximum,
    float aspect, bool resetOrientation = false ) noexcept;

::cypher::math::vec3_t CypherTileCamera_Forward(
    const tile_camera_t &camera ) noexcept;
::cypher::math::vec3_t CypherTileCamera_Right(
    const tile_camera_t &camera ) noexcept;
::cypher::math::vec3_t CypherTileCamera_Up(
    const tile_camera_t &camera ) noexcept;
// Presets rotate around the current implicit focus point
// (eye + forward * orbitDistance), preserving distance, settings and bounds.
void CypherTileCamera_ApplyViewPreset(
    tile_camera_t &camera, tile_camera_view_preset_t preset ) noexcept;
// Fly mode holds the eye in place; orbit mode holds the implicit focus point.
void CypherTileCamera_Level( tile_camera_t &camera ) noexcept;
bool CypherTileCamera_TranslateWorld(
    tile_camera_t &camera, ::cypher::math::vec3_t offset ) noexcept;
void CypherTileCamera_ApplyLook(
    tile_camera_t &camera, float deltaX, float deltaY ) noexcept;
void CypherTileCamera_Pan( tile_camera_t &camera,
    float deltaX, float deltaY, float viewportHeight ) noexcept;
void CypherTileCamera_Wheel( tile_camera_t &camera, float ticks ) noexcept;

// Returns whether position changed. Invalid/negative dt produces no movement;
// long stalls are capped at 100 ms. Combined directions cannot increase speed.
bool CypherTileCamera_Move( tile_camera_t &camera,
    const tile_camera_input_t &input, float deltaSeconds ) noexcept;
bool CypherTileCamera_BuildMatrices( const tile_camera_t &camera,
    float aspect, ::cypher::math::mat4_t &viewOut,
    ::cypher::math::mat4_t &projectionOut ) noexcept;

} // namespace cypher::tools::tile_editor
#endif
