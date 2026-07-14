//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Ray.h
//  Purpose: Declares the CypherMath Math Ray module.
//  Details: This file participates in math primitives used by rendering, physics,
//           world queries, and tools. Keep operations deterministic and benchmark
//           important hot paths before adding clever optimizations.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MATH_RAY_H
#define CYPHER_ENGINE_MATH_RAY_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Types.h"

namespace cypher::engine::math
{

vec3_t CypherMath_RayPointAt( const ray_t &ray, common::f32 tUnits );

bool CypherMath_RayIntersectsPlane( const ray_t &ray, const plane_t &plane, common::f32 &tUnitsOut );

bool CypherMath_RayIntersectsBounds( const ray_t &ray, const bounds_t &bounds, common::f32 &tminOut, common::f32 &tmaxOut );

}       // namespace cypher::engine::math

#endif // CYPHER_ENGINE_MATH_RAY_H
