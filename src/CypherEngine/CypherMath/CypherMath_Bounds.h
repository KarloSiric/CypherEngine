//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Bounds.h
//  Purpose: Declares the CypherMath Math Bounds module.
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

#ifndef CYPHER_ENGINE_MATH_BOUNDS_H
#define CYPHER_ENGINE_MATH_BOUNDS_H

#pragma once

#include "CypherMath_Types.h"

namespace cypher::engine::math
{

bounds_t CypherMath_BoundsClear();

bounds_t CypherMath_BoundsFromPoint( const vec3_t &v );

void CypherMath_BoundsAddPoint( bounds_t &bounds, const vec3_t &point );

vec3_t CypherMath_BoundsCenter( const bounds_t &bounds );

vec3_t CypherMath_BoundsSize( const bounds_t &bounds );

bool CypherMath_BoundsContainsPoint( const bounds_t &bounds, const vec3_t &point );

bool CypherMath_BoundsIntersects( const bounds_t &b1, const bounds_t &b2 );

}       // namespace cypher::engine::math

#endif // CYPHER_ENGINE_MATH_BOUNDS_H
