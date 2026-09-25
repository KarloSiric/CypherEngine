//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Sweep.h
//  Purpose: Declares profile sweeps along CurveNetwork paths and lathe
//           (surface of revolution) generation into EditableMesh.
//  Details: Pure evaluators (Procedural README): they read immutable inputs
//           and publish a checked mesh through Sanitation; retaining the
//           parameters as a live recipe is a Modifier concern.
//
//           Sweep: the 2D profile (x along the sample normal, y along the
//           binormal) is placed at every rotation-minimizing frame from
//           CurveSampling and consecutive rings are joined with quads. A
//           counter-clockwise closed profile yields outward-facing walls;
//           an open profile produces a ribbon facing the right-hand side of
//           its direction. Closed paths join the last ring to the first;
//           open paths with a closed profile can be capped (profile ear-
//           clipped), giving a closed solid.
//
//           Lathe: the profile (x = radius >= 0, y = height along the axis)
//           is revolved by `angle` in `segments` steps. Profile points on
//           the axis (x == 0) become single pole vertices, so revolving an
//           arc that touches the axis yields a closed sphere-like solid with
//           triangle fans at the poles instead of degenerate quads. Closed
//           results are oriented outward by signed volume.
//
//           Face groups in the result soup: 0 = walls, 1 = start cap,
//           2 = end cap (queryable through Sanitation face maps).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SWEEP_H
#define CYPHER_EDITOR_GEOMETRY_SWEEP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CurveSampling.h"
#include "CypherGeometry_EditableMesh.h"

namespace cypher::editor::geometry
{

inline constexpr common::usize kSweepProfilePointsMax = 1024u;

struct sweep_options_t {
    curve_sampling_options_t sampling{};
    bool bProfileClosed{ true };
    bool bCapEnds{ true };            // open paths with closed profiles only
    common::f64 fScaleStart{ 1.0 };   // profile scale at the path start
    common::f64 fScaleEnd{ 1.0 };     // ... and end (linear in arc length)
};

struct sweep_report_t {
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
    common::u32 cRings{ 0u };
    common::u32 cWallFaces{ 0u };
    common::u32 cCapFaces{ 0u };
    bool bClosed{ false };
};

// Sweeps `profile` along path iPath into pMeshOut (zero-initialized).
CYPHER_NODISCARD sweep_report_t Sweep_TryAlongPath(
    const curve_network_t *pNet,
    common::u32 iPath,
    common::span_t<const math::vec2d_t> profile,
    const sweep_options_t &options,
    const common::allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept;

struct lathe_options_t {
    common::f64 angle{ 6.283185307179586 }; // radians; >= 2*pi - 1e-9 means full turn
    common::u32 segments{ 16u };
};

// Revolves `profile` around the axis (axisOrigin, axisDir) into pMeshOut.
// Profile x must be >= 0 for every point. At least two points.
CYPHER_NODISCARD sweep_report_t Sweep_TryLathe(
    common::span_t<const math::vec2d_t> profile,
    math::vec3d_t axisOrigin,
    math::vec3d_t axisDir,
    const lathe_options_t &options,
    const common::allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SWEEP_H
