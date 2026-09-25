//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshPaint.h
//  Purpose: Declares the blend-painting brush for editable meshes (Hammer's
//           Paint tool): painting per-corner color channels, which blend
//           materials read as blend weights.
//  Details: Channel layout: colorRgba is read as the hex value 0xRRGGBBAA,
//           so R is the top byte and A the bottom one. Nothing in the
//           geometry layer decodes the value (the render cook passes it
//           through unchanged), so the renderer's vertex format must agree
//           with this reading.
//
//           One dab moves each painted channel of every corner within
//           `radius` of `center` toward `value` by strength x falloff:
//           full at the centre, fading to nothing at the radius. Corners
//           are painted per face, so every face around a vertex gets the
//           same weight and the blend stays smooth; restricting the dab to
//           selected faces leaves the others' corners alone (a deliberate
//           seam). Dabs are computed and written in one pass after the
//           whole face list is validated, so a rejected dab paints nothing.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_PAINT_H
#define CYPHER_EDITOR_GEOMETRY_MESH_PAINT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_MeshStore.h"
#include "CypherGeometry_EditableMesh.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

enum mesh_paint_channel_bits_t : common::u8 {
    MESH_PAINT_CHANNEL_R = 1u << 0,
    MESH_PAINT_CHANNEL_G = 1u << 1,
    MESH_PAINT_CHANNEL_B = 1u << 2,
    MESH_PAINT_CHANNEL_A = 1u << 3
};

// Weight across the brush, for t = distance / radius in [0, 1].
enum class mesh_paint_falloff_t : common::u8 {
    CONSTANT = 0u, // 1
    LINEAR,        // 1 - t
    SMOOTH         // 1 - smoothstep(t): flat centre, soft edge
};

struct mesh_paint_brush_t {
    math::vec3d_t center{};
    common::f64 radius{ 1.0 };
    common::f64 strength{ 1.0 }; // in [0, 1]
    mesh_paint_falloff_t falloff{ mesh_paint_falloff_t::SMOOTH };
    common::u8 channels{ MESH_PAINT_CHANNEL_R };
    common::u8 value{ 255u }; // painted toward (0 erases)
};

// Applies one dab to the corners of `faces` (every face for an empty
// span). *pCornersChangedOut (optional) receives how many corners changed.
// Rejected (nothing painted): a non-finite centre, radius, or strength ->
// NUMERIC_FAILURE; radius <= 0, strength outside [0, 1], no or unknown
// channels -> INVALID_ARGUMENT; a face listed twice -> INVALID_ARGUMENT; a
// stale face -> STALE_HANDLE.
CYPHER_NODISCARD geometry_status_t MeshPaint_TryBrush(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    common::span_t<const geometry_mesh_face_handle_t> faces,
    const mesh_paint_brush_t &brush,
    common::u32 *pCornersChangedOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_PAINT_H
