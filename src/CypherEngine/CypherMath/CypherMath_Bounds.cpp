//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Bounds.cpp
//  Purpose: Implements the CypherMath Math Bounds module.
//  Details: This file participates in math primitives used by rendering, physics,
//           world queries, and tools. Keep operations deterministic and benchmark
//           important hot paths before adding clever optimizations.
//
//  History:
//  - Created by Karlo Siric on 2026-05-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Bounds.h"
#include "CypherMath_Vec.h"

#include <limits>

namespace cypher::engine::math
{

bounds_t CypherMath_BoundsClear()
{
    const common::f32 nMaxValue = std::numeric_limits<common::f32>::max();

    return bounds_t{
        vec3_t{ nMaxValue, nMaxValue, nMaxValue },
        vec3_t{ -nMaxValue, -nMaxValue, -nMaxValue }
    };
}

bounds_t CypherMath_BoundsFromPoint( const vec3_t &v )
{
    return bounds_t{ v, v };
}

void CypherMath_BoundsAddPoint( bounds_t &bounds, const vec3_t &point )
{
    bounds.mins = CypherMath_Vec3Min( bounds.mins , point );
    bounds.maxs = CypherMath_Vec3Max( bounds.maxs, point );
}

vec3_t CypherMath_BoundsCenter( const bounds_t &bounds )
{
    return CypherMath_Vec3Scale( CypherMath_Vec3Add( bounds.mins, bounds.maxs ), 0.5f );
}

vec3_t CypherMath_BoundsSize( const bounds_t &bounds )
{
    return CypherMath_Vec3Sub( bounds.maxs, bounds.mins );
}

bool CypherMath_BoundsContainsPoint( const bounds_t &bounds, const vec3_t &point )
{
    return point.x >= bounds.mins.x && point.x <= bounds.maxs.x &&
           point.y >= bounds.mins.y && point.y <= bounds.maxs.y &&
           point.z >= bounds.mins.z && point.z <= bounds.maxs.z;
}

bool CypherMath_BoundsIntersects( const bounds_t &b1, const bounds_t &b2 )
{
    if ( b1.maxs.x < b2.mins.x || b1.mins.x > b2.maxs.x ) {
        return false;
    }
    if ( b1.maxs.y < b2.mins.y || b1.mins.y > b2.maxs.y ) {
        return false;
    }
    if ( b1.maxs.z < b2.mins.z || b1.mins.z > b2.maxs.z ) {
        return false;
    }
    return true;
}

}       // namespace cypher::engine::math
