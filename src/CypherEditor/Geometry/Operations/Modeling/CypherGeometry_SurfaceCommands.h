//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SurfaceCommands.h
//  Purpose: Declares transaction commands that edit brush-side surfacing:
//           material assignment and texture alignment.
//  Details: A command targets BRUSH_SIDE references and/or BRUSH references
//           (meaning every side of that brush) and applies one operation
//           to each targeted side. Pivot-relative operations pivot about
//           each face's centroid unless an explicit pivot is given. All
//           affected brushes are replaced in one atomic preview step;
//           geometry and identities are untouched.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SURFACE_COMMANDS_H
#define CYPHER_EDITOR_GEOMETRY_SURFACE_COMMANDS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Selection.h"
#include "CypherGeometry_SurfaceUv.h"
#include "CypherGeometry_Transaction.h"

namespace cypher::editor::geometry
{

enum class geometry_surface_op_kind_t : common::u8 {
    SET_MATERIAL = 0u,   // material
    SET_PROJECTION,      // projection (re-based onto each face)
    SHIFT,               // vector = UV delta
    ROTATE,              // radians
    SCALE,               // vector = factors
    FLIP,                // bFlipU / bFlipV
    FIT,                 // vector = repeats (u, v)
    ALIGN_LONGEST_EDGE,  // U along the face's longest edge
    RESET,               // default world-aligned projection
    COUNT
};

struct geometry_surface_op_t {
    geometry_surface_op_kind_t kind{ geometry_surface_op_kind_t::SHIFT };
    geometry_material_ref_t material{};
    math::planar_uv_mappingd_t projection{};
    math::vec2d_t vector{ 0.0, 0.0 };
    math::f64 radians{ 0.0 };
    common::bool_t bFlipU{ false };
    common::bool_t bFlipV{ false };
    // Rotate/scale/flip pivot: the face centroid unless bUsePivot.
    common::bool_t bUsePivot{ false };
    math::vec3d_t pivot{};
};

struct geometry_surface_result_t {
    common::u32 cBrushes{ 0u };
    common::u32 cSides{ 0u };
};

CYPHER_NODISCARD geometry_status_t SurfaceCommand_TryApply(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_component_ref_t> targets,
    const geometry_surface_op_t &op,
    geometry_surface_result_t *pResultOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SURFACE_COMMANDS_H
