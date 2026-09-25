//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CurveSampling.h
//  Purpose: Declares arc-length sampling of CurveNetwork paths with
//           rotation-minimizing frames (RMF).
//  Details: Sweeps, lofts, rails, and path followers need a frame at each
//           sample that twists as little as possible. The Frenet frame is
//           unusable (undefined on straight runs, flips at inflections), so
//           frames are propagated with the double-reflection method of Wang,
//           Jüttler, Zheng & Liu, "Computation of Rotation Minimizing
//           Frames", ACM TOG 27(1), 2008: each frame is mapped to the next by
//           two reflections, which is exact for circular arcs and
//           fourth-order accurate in general.
//
//           Closed paths: an RMF around a closed curve generally does not
//           return to its start frame (holonomy). The residual angle is
//           distributed linearly along arc length so the last frame meets
//           the first seamlessly — without that, a swept closed pipe would
//           show a visible twist seam.
//
//           Sampling: samples are placed at uniform arc-length spacing (at
//           most fMaxSpacing apart) *within each path step*, and every node
//           is sampled exactly, so polyline corners and authored nodes are
//           never cut. Frames at a sharp corner are propagated through it
//           like any other sample; consumers that want mitred corners should
//           split the path there.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CURVE_SAMPLING_H
#define CYPHER_EDITOR_GEOMETRY_CURVE_SAMPLING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CurveNetwork.h"

namespace cypher::editor::geometry
{

inline constexpr common::usize kCurveSamplesMax = 65536u;

struct curve_sample_t {
    math::vec3d_t position{};
    math::vec3d_t tangent{};   // unit
    math::vec3d_t normal{};    // unit, perpendicular to tangent
    math::vec3d_t binormal{};  // tangent x normal
    common::f64 distance{ 0.0 }; // arc length from path start
    common::u32 iStep{ 0u };     // path step the sample lies on
    common::f64 t{ 0.0 };        // local parameter on that step
};

struct curve_sampling_options_t {
    common::f64 fMaxSpacing{ 1.0 };           // world units between samples
    math::vec3d_t initialUp{ 0.0, 0.0, 1.0 }; // seeds the first normal
    bool bCorrectClosedTwist{ true };
};

// Samples path iPath into pOut (initialized; cleared). For a closed path the
// start point is emitted once (the consumer wraps). Returns
// NOT_INITIALIZED, INVALID_ARGUMENT (spacing <= 0, bad path), DEGENERATE
// (zero-length path or tangent), LIMIT_EXCEEDED, or ALLOCATION_FAILED.
CYPHER_NODISCARD geometry_status_t CurveSampling_TrySamplePath(
    const curve_network_t *pNet,
    common::u32 iPath,
    const curve_sampling_options_t &options,
    common::vector_t<curve_sample_t> *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CURVE_SAMPLING_H
