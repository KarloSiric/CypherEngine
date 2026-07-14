//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Frustum.h
//  Purpose: Declares the CypherMath Math Frustum module.
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

#ifndef CYPHER_ENGINE_MATH_FRUSTUM_H
#define CYPHER_ENGINE_MATH_FRUSTUM_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Types.h"

namespace cypher::engine::math
{

frustum_t CypherMath_FrustumFromProjectionView( const mat4_t &projectionView );

bool CypherMath_FrustumContainsPoint( const frustum_t &frustum, const vec3_t &point );

bool CypherMath_FrustumIntersectsBounds( const frustum_t &frustum, const bounds_t &bounds );

}       // namespace cypher::engine::math

#endif // CYPHER_ENGINE_MATH_FRUSTUM_H
