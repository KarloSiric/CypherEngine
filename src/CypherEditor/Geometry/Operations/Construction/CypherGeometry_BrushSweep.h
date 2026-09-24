//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSweep.h
//  Purpose: Declares the sweep tool: fill the gap between selected brush
//           faces and a copy of them moved along a path (straight, arc, or
//           S-bend), with a number of segments and repeated iterations.
//  Details: Each face and segment becomes one convex brush: the convex hull
//           of the face at the segment's two path positions. The face moves
//           rigidly (translation for straight and S-bend, rotation about an
//           axis for arcs), so the segments of a face meet exactly and a
//           swept strip is watertight.
//
//           Surfaces: a segment's end caps keep the face's surface; the
//           far cap's projection is moved with the face (texture lock), so
//           the texture continues where the next segment starts. The walls
//           take the face's material with a world-aligned projection for
//           their own orientation - the face's projection cannot be used
//           there, because a wall runs along the face's projection
//           direction.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_SWEEP_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_SWEEP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

enum class brush_sweep_path_t : common::u8 {
    STRAIGHT = 0u, // translate by `offset`
    ARC,           // rotate by `angleRadians` about the axis
    S_BEND         // reach `offset`: its component along the face normal is
                   // covered linearly, the sideways part along a smooth S
                   // (smoothstep), so the face stays parallel to itself
};

struct brush_sweep_params_t {
    brush_sweep_path_t path{ brush_sweep_path_t::STRAIGHT };
    math::vec3d_t offset{};                // STRAIGHT, S_BEND
    math::vec3d_t axisOrigin{};            // ARC
    math::vec3d_t axisDirection{ 0, 0, 1 };// ARC (need not be unit)
    common::f64 angleRadians{ 0.0 };       // ARC, in (-2 pi, 2 pi)
    common::u32 cSegments{ 1u };           // brushes per face per iteration
    common::u32 cIterations{ 1u };         // repeat from the previous cap
};

// A face to sweep: side iSide of an authored brush.
struct brush_sweep_face_t {
    const brush_source_t *pSource{ nullptr };
    common::u32 iSide{ 0u };
};

// Builds faces x cSegments x cIterations brushes into pOut (canonical-empty
// brush sources), face by face in path order; *pCountOut receives the
// count (or the needed count with INSUFFICIENT_CAPACITY). A segment with no
// volume (zero offset or angle, an arc axis in the face's plane that turns
// it edge-on) is DEGENERATE. Failure-atomic.
CYPHER_NODISCARD geometry_status_t BrushSweep_TryBuild(
    common::span_t<const brush_sweep_face_t> faces,
    const brush_sweep_params_t &params,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    brush_source_t *pOut,
    common::u32 cCapacity,
    common::u32 *pCountOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_SWEEP_H
