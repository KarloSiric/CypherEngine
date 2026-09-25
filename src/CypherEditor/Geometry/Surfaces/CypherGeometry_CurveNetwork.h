//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CurveNetwork.h
//  Purpose: Declares the CurveNetwork source representation: nodes,
//           curve segments between them, and paths chaining segments.
//  Details: A network is a graph, not a single spline, so one node can be
//           shared by several segments (branching roads, rail junctions).
//           Paths are ordered, direction-aware walks over segments that
//           consumers sweep, sample, or follow.
//
//           Canonical segment data is LINEAR or CUBIC_BEZIER with absolute
//           handle positions. Other authoring bases (Catmull-Rom) are
//           converted to Bézier handles on input, so evaluation, splitting,
//           and serialization only ever deal with two exact forms.
//
//           Identity: nodes, segments, and paths carry source IDs. Splitting
//           a segment keeps the original segment's ID on the first half and
//           takes caller-supplied IDs for the new node and second half, so
//           references to the original remain meaningful.
//
//           Bounds: kCurveNetwork* limits; every mutation is failure-atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CURVE_NETWORK_H
#define CYPHER_EDITOR_GEOMETRY_CURVE_NETWORK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_Span.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

inline constexpr common::usize kCurveNetworkNodesMax = 65536u;
inline constexpr common::usize kCurveNetworkSegmentsMax = 65536u;
inline constexpr common::usize kCurveNetworkPathsMax = 4096u;
inline constexpr common::usize kCurveNetworkPathStepsMax = 65536u;

enum class curve_basis_t : common::u8 {
    LINEAR       = 0u,
    CUBIC_BEZIER = 1u
};

struct curve_node_t {
    math::vec3d_t position{};
    geometry_source_id_t sourceId{};
};

struct curve_segment_t {
    common::u32 iNode0{ 0u };
    common::u32 iNode1{ 0u };
    curve_basis_t basis{ curve_basis_t::LINEAR };
    math::vec3d_t handle0{};   // Bézier control point near node 0 (absolute)
    math::vec3d_t handle1{};   // Bézier control point near node 1 (absolute)
    geometry_source_id_t sourceId{};
};

// One step of a path: a segment traversed forward (node0 -> node1) or
// reversed.
struct curve_path_step_t {
    common::u32 iSegment{ 0u };
    bool bReversed{ false };
};

struct curve_path_t {
    common::u32 iFirstStep{ 0u };
    common::u32 cSteps{ 0u };
    bool bClosed{ false };
    geometry_source_id_t sourceId{};
};

struct curve_network_t {
    common::vector_t<curve_node_t> nodes{};
    common::vector_t<curve_segment_t> segments{};
    common::vector_t<curve_path_step_t> steps{};
    common::vector_t<curve_path_t> paths{};
    geometry_source_id_t sourceId{};
};

// ---------------------------------------------------------------------------
// Lifecycle and building
// ---------------------------------------------------------------------------

CYPHER_NODISCARD geometry_status_t CurveNetwork_Init(
    curve_network_t *pNet,
    const common::allocator_t *pAllocator,
    geometry_source_id_t networkId ) noexcept;

void CurveNetwork_Shutdown( curve_network_t *pNet ) noexcept;

CYPHER_NODISCARD bool CurveNetwork_IsInitialized( const curve_network_t *pNet ) noexcept;

CYPHER_NODISCARD geometry_status_t CurveNetwork_TryAddNode(
    curve_network_t *pNet,
    math::vec3d_t position,
    geometry_source_id_t nodeId,
    common::u32 *pIndexOut ) noexcept;

// Adds a segment. Node indices must exist and differ; for CUBIC_BEZIER the
// handles must be finite (LINEAR ignores them and stores the endpoints).
CYPHER_NODISCARD geometry_status_t CurveNetwork_TryAddSegment(
    curve_network_t *pNet,
    common::u32 iNode0,
    common::u32 iNode1,
    curve_basis_t basis,
    math::vec3d_t handle0,
    math::vec3d_t handle1,
    geometry_source_id_t segmentId,
    common::u32 *pIndexOut ) noexcept;

// Adds a path over existing segments. Consecutive steps must connect (end
// node of one = start node of the next); a closed path must also connect
// last to first. Returns INVALID_TOPOLOGY for a broken chain.
CYPHER_NODISCARD geometry_status_t CurveNetwork_TryAddPath(
    curve_network_t *pNet,
    common::span_t<const curve_path_step_t> steps,
    bool bClosed,
    geometry_source_id_t pathId,
    common::u32 *pIndexOut ) noexcept;

// Convenience: builds nodes + uniform Catmull-Rom segments (converted to
// Bézier) + one path through `points`. IDs are taken from *pIdAllocator in
// the order: nodes, segments, path. End tangents of an open path use the
// clamped (duplicated end point) rule. Failure-atomic, including the ID
// allocator.
CYPHER_NODISCARD geometry_status_t CurveNetwork_TryAddCatmullRomPath(
    curve_network_t *pNet,
    common::span_t<const math::vec3d_t> points,
    bool bClosed,
    geometry_source_id_allocator_t *pIdAllocator,
    common::u32 *pPathIndexOut ) noexcept;

// Splits segment iSegment at parameter t in (0, 1) without changing the
// curve (de Casteljau for Bézier, lerp for linear). Every path step that
// references the segment gains a step for the new second half, in the
// correct traversal order. Returns the new node and segment indices.
CYPHER_NODISCARD geometry_status_t CurveNetwork_TrySplitSegment(
    curve_network_t *pNet,
    common::u32 iSegment,
    common::f64 t,
    geometry_source_id_t newNodeId,
    geometry_source_id_t newSegmentId,
    common::u32 *pNewNodeOut,
    common::u32 *pNewSegmentOut ) noexcept;

// ---------------------------------------------------------------------------
// Evaluation and queries
// ---------------------------------------------------------------------------

// Position / first derivative of a segment at t in [0, 1] (clamped). An
// out-of-range segment returns the zero vector.
CYPHER_NODISCARD math::vec3d_t CurveNetwork_EvaluateSegment(
    const curve_network_t *pNet,
    common::u32 iSegment,
    common::f64 t ) noexcept;

CYPHER_NODISCARD math::vec3d_t CurveNetwork_EvaluateSegmentDerivative(
    const curve_network_t *pNet,
    common::u32 iSegment,
    common::f64 t ) noexcept;

// Arc length of a segment by adaptive 5-point Gauss-Legendre quadrature of
// |B'(t)|, refined until two levels agree within fTolerance (relative) or a
// fixed depth is reached. Exact for linear segments.
CYPHER_NODISCARD common::f64 CurveNetwork_SegmentLength(
    const curve_network_t *pNet,
    common::u32 iSegment,
    common::f64 fTolerance ) noexcept;

// Sum of step lengths along a path.
CYPHER_NODISCARD common::f64 CurveNetwork_PathLength(
    const curve_network_t *pNet,
    common::u32 iPath,
    common::f64 fTolerance ) noexcept;

// Start/end node of a path step (respecting reversal).
CYPHER_NODISCARD common::u32 CurveNetwork_StepStartNode(
    const curve_network_t *pNet,
    curve_path_step_t step ) noexcept;
CYPHER_NODISCARD common::u32 CurveNetwork_StepEndNode(
    const curve_network_t *pNet,
    curve_path_step_t step ) noexcept;

// Position on a path step at local t (0 = step start, respecting reversal).
CYPHER_NODISCARD math::vec3d_t CurveNetwork_EvaluateStep(
    const curve_network_t *pNet,
    curve_path_step_t step,
    common::f64 t ) noexcept;

// Tangent (derivative w.r.t. local step parameter) on a path step.
CYPHER_NODISCARD math::vec3d_t CurveNetwork_EvaluateStepDerivative(
    const curve_network_t *pNet,
    curve_path_step_t step,
    common::f64 t ) noexcept;

// Validates finite data, index ranges, distinct segment endpoints, and path
// connectivity. OK, NUMERIC_FAILURE, INVALID_HANDLE, or INVALID_TOPOLOGY.
CYPHER_NODISCARD geometry_status_t CurveNetwork_Validate(
    const curve_network_t *pNet ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CURVE_NETWORK_H
