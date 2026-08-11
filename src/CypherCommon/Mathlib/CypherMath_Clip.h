//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Clip.h
//  Purpose: Declares plane clipping used by mesh and block authoring tools.
//  Details: Clipping retains the nonpositive half-space of outward planes and
//           writes into caller-owned storage without hidden allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_CLIP_H
#define CYPHER_COMMON_MATH_CLIP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Plane.h"
#include "CypherMath_Ray.h"

namespace cypher::math
{

using common::usize;

enum class polygon_clip_status_t : common::u8 {
    OK = 0u,
    FULLY_CLIPPED,
    INVALID_ARGUMENT,
    INSUFFICIENT_CAPACITY,
    COUNT
};

struct polygon_clip_result_t {
    polygon_clip_status_t status;
    usize cVerticesWritten;
};

struct segment_clip_result_t {
    segment_t segment;
    f32 parameterEnter;
    f32 parameterExit;
};

// Input and output arrays must not overlap; maximum output is cVertices + 1.
CYPHER_NODISCARD CYPHER_MATH_API polygon_clip_result_t
Clip_PolygonAgainstPlane(
    CY_IN_READS( cVertices ) const vec3_t *pVertices,
    usize cVertices,
    plane_t outwardPlane,
    f32 insideTolerance,
    CY_OUT_WRITES( cOutputVertices ) vec3_t *pOutputVertices,
    usize cOutputVertices ) noexcept;

CYPHER_NODISCARD CYPHER_MATH_API bool_t Clip_TrySegmentAgainstConvexPlanes(
    segment_t segment,
    CY_IN_READS( cPlanes ) const plane_t *pPlanes,
    usize cPlanes,
    f32 insideTolerance,
    f32 minimumAbsDenominator,
    CY_OUT segment_clip_result_t *pResult ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_CLIP_H
