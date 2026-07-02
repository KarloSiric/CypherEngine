//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Plane.cpp
//  Purpose: Implements the CypherMath Math Plane module.
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

#include "CypherMath_Plane.h"
#include "CypherMath_Vec.h"

namespace cypher::engine::math
{

plane_t CypherMath_PlaneFromPointNormal( const vec3_t &point, const vec3_t &normal )
{
    /*
     * Forming a plane out of normal vector and a point vector somewhere in space.
     */

    plane_t result{};

    const vec3_t normalizedNormal = CypherMath_Vec3Normalize( normal );

    if ( CypherMath_Vec3LengthSquared( normalizedNormal ) <= MATH_EPSILON_F ) {
        return result;
    }

    result = { normalizedNormal, CypherMath_Vec3Dot( normalizedNormal, point ) };
    return result;
}

plane_t CypherMath_PlaneFromPoints( const vec3_t &p0, const vec3_t &p1, const vec3_t &p2 )
{
    /*
     * Forming a plane out of triangles essentially, so three vectors( vertices ).
     * Might be useful later for creating brushes, brush cpollisions, BSP faces, traingle planes etc.
     */

    plane_t result{};

    const vec3_t edge1 = CypherMath_Vec3Sub( p1, p0 );
    const vec3_t edge2 = CypherMath_Vec3Sub( p2, p0 );
    const vec3_t normal = CypherMath_Vec3Normalize( CypherMath_Vec3Cross( edge1, edge2 ) );

    if ( CypherMath_Vec3LengthSquared( normal ) <= MATH_EPSILON_F ) {
        return plane_t{};
    }

    result = { normal, CypherMath_Vec3Dot( normal, p0 ) };

    return result;
}


common::f32 CypherMath_PlaneDistance( const plane_t &plane, const vec3_t &v )
{
    /*
     * Corresponds to either giving a positive or negative value.
     * Positive value corresponds to a point being in front of the plane.
     * Negative value corresponds to a point being behind the plane.
     */
    return CypherMath_Vec3Dot( plane.normal, v ) - plane.dist;
}

bool CypherMath_PlanePointFront( const plane_t &plane, const vec3_t &v )
{
    /*
    const common::f32 vec_dot = CypherMath_Vec3Dot( plane.normal , v );
    if ( ( vec_dot - plane.dist ) < 0.0f ) {
        return false;
    }
    return true;
    */
    return CypherMath_PlaneDistance( plane, v ) > MATH_EPSILON_F;
}

bool CypherMath_PlanePointBack( const plane_t &plane, const vec3_t &v )
{
    /*
    const common::f32 vec_dot = CypherMath_Vec3Dot( plane.normal , v );
    if ( ( vec_dot - plane.dist ) > 0.0f ) {
        return false;
    }
    return true;
    */
    return CypherMath_PlaneDistance( plane, v ) < -MATH_EPSILON_F;
}

bool CypherMath_PlanePointOn( const plane_t &plane, const vec3_t &v, common::f32 epsilon )
{
    const common::f32 distance = CypherMath_PlaneDistance( plane, v );

    return distance >= -epsilon && distance <= epsilon;
}

}       // namespace cypher::engine::math
