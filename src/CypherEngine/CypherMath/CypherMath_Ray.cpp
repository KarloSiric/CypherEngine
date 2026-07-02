//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Ray.cpp
//  Purpose: Implements the CypherMath Math Ray module.
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

#include "CypherMath_Ray.h"
#include "CypherMath_Vec.h"

#include <cmath>

namespace cypher::engine::math
{

vec3_t CypherMath_RayPointAt( const ray_t &ray, common::f32 tUnits )
{
    return CypherMath_Vec3Add( ray.origin, CypherMath_Vec3Scale( ray.direction, tUnits ) );
}

bool CypherMath_RayIntersectsPlane( const ray_t &ray, const plane_t &plane, common::f32 &tUnitsOut )
{
    common::f32 denom = CypherMath_Vec3Dot( plane.normal, ray.direction );
    if ( std::fabs( denom ) <= MATH_EPSILON_F ) {
        return false;
    }
    tUnitsOut = ( plane.dist - CypherMath_Vec3Dot( plane.normal, ray.origin ) ) / denom;
    return tUnitsOut >= 0.0f;
}
bool CypherMath_RayIntersectsBounds( const ray_t &ray, const bounds_t &bounds, common::f32 &tminOut, common::f32 &tmaxOut )
{

    /*
     * Using a slab metho for doing this calculation.
     *
     * AABB slab method, where we find the intersection of each and every single of
     * the X, Y and Z slabs.
     *
     * Each of those slabs has a min and max value, and we try to find the entry
     * start
     */

    common::f32 tUnitsMin = 0.0f;
    common::f32 tUnitsMax = 3.402823466e+38f;

    common::f32 origin[3]      = { ray.origin.x, ray.origin.y, ray.origin.z };
    common::f32 direction[3]   = { ray.direction.x, ray.direction.y, ray.direction.z };
    common::f32 mins[3]        = { bounds.mins.x, bounds.mins.y, bounds.mins.z };
    common::f32 maxs[3]        = { bounds.maxs.x, bounds.maxs.y, bounds.maxs.z };

    for ( common::u32 i = 0; i < 3u; ++i ) {
        if ( std::fabs( direction[i] ) <= MATH_EPSILON_F ) {
            if ( origin[i] < mins[i] || origin[i] > maxs[i] ) {
                return false;
            }
            continue;
        }
        // @NOTE: Same as dividing, but this is cleaner somehow.
        common::f32 invDirection = 1.0f / direction[i];
        common::f32 t1 = ( mins[i] - origin[i] ) * invDirection;
        common::f32 t2 = ( maxs[i] - origin[i] ) * invDirection;

        // If the ray is going in the opposite direction, so negative dir of the vector.
        if ( t1 > t2 ) {
            const common::f32 temp = t1;
            t1 = t2;
            t2 = temp;
        }

        if ( t1 > tUnitsMin ) {
            tUnitsMin = t1;
        }

        if ( t2 < tUnitsMax ) {
            tUnitsMax = t2;
        }
        /*
         * It happened if and only if the tmin < tmax essentially.
         * Because if tmin is larger than tmax then the ray exited one slab window
         * before it entered another.
         *
         * And the ray needs to be inside all three of those at the same time
         * to be intersecting.
         *
         * So we are checking the largest entry value and the smallest exit value and
         * we are comparing them to see.
         */
        if ( tUnitsMin > tUnitsMax ) {
            return false;
        }
    }

    tminOut = tUnitsMin;
    tmaxOut = tUnitsMax;

    return true;
}

}       // namespace cypher::engine::math
