//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMath/CypherMath_Frustum.cpp
//  Purpose: Implements the CypherMath Math Frustum module.
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

#include "CypherMath_Bounds.h"
#include "CypherMath_Plane.h"
#include "CypherMath_Types.h"
#include "CypherMath_Frustum.h"
#include "CypherMath_Mat.h"
#include "CypherMath_Vec.h"

#include <cmath>            // sqrt

namespace cypher::engine::math
{

static plane_t CypherMath_FrustumMakePlane( const common::f32 a, const common::f32 b, const common::f32 c, const common::f32 d )
{
    // a, b and c are part of the normal vector, d is just the offset, position of the plane.
    const common::f32 length = std::sqrt( a * a + b * b + c * c );

    if ( length <= MATH_EPSILON_F ) {
        return plane_t{};
    }

    // for normalization process
    const common::f32 nInvLength = 1.0f / length;
    // negative value is used because of the way the engine is written itself.
    plane_t result = { vec3_t { a * nInvLength, b * nInvLength, c * nInvLength }, -d * nInvLength };

    return result;
}

frustum_t CypherMath_FrustumFromProjectionView( const mat4_t &projectionView )
{
    frustum_t result{};
    // GRIBB-HARTMANM METHOD for extrating all the planes effectively out of the P-V matrix.

    result.planes[FRUSTUM_PLANE_LEFT] = CypherMath_FrustumMakePlane(
        CypherMath_Mat4Get( projectionView, 0u, 3u ) + CypherMath_Mat4Get( projectionView, 0u, 0u ),
        CypherMath_Mat4Get( projectionView, 1u, 3u ) + CypherMath_Mat4Get( projectionView, 1u, 0u ),
        CypherMath_Mat4Get( projectionView, 2u, 3u ) + CypherMath_Mat4Get( projectionView, 2u, 0u ),
        CypherMath_Mat4Get( projectionView, 3u, 3u ) + CypherMath_Mat4Get( projectionView, 3u, 0u )
    );

    result.planes[FRUSTUM_PLANE_RIGHT] = CypherMath_FrustumMakePlane(
        CypherMath_Mat4Get( projectionView, 0u, 3u ) - CypherMath_Mat4Get( projectionView, 0u, 0u ),
        CypherMath_Mat4Get( projectionView, 1u, 3u ) - CypherMath_Mat4Get( projectionView, 1u, 0u ),
        CypherMath_Mat4Get( projectionView, 2u, 3u ) - CypherMath_Mat4Get( projectionView, 2u, 0u ),
        CypherMath_Mat4Get( projectionView, 3u, 3u ) - CypherMath_Mat4Get( projectionView, 3u, 0u )
    );

    result.planes[FRUSTUM_PLANE_BOTTOM] = CypherMath_FrustumMakePlane(
        CypherMath_Mat4Get( projectionView, 0u, 3u ) + CypherMath_Mat4Get( projectionView, 0u, 1u ),
        CypherMath_Mat4Get( projectionView, 1u, 3u ) + CypherMath_Mat4Get( projectionView, 1u, 1u ),
        CypherMath_Mat4Get( projectionView, 2u, 3u ) + CypherMath_Mat4Get( projectionView, 2u, 1u ),
        CypherMath_Mat4Get( projectionView, 3u, 3u ) + CypherMath_Mat4Get( projectionView, 3u, 1u )
    );

    result.planes[FRUSTUM_PLANE_TOP] = CypherMath_FrustumMakePlane(
        CypherMath_Mat4Get( projectionView, 0u, 3u ) - CypherMath_Mat4Get( projectionView, 0u, 1u ),
        CypherMath_Mat4Get( projectionView, 1u, 3u ) - CypherMath_Mat4Get( projectionView, 1u, 1u ),
        CypherMath_Mat4Get( projectionView, 2u, 3u ) - CypherMath_Mat4Get( projectionView, 2u, 1u ),
        CypherMath_Mat4Get( projectionView, 3u, 3u ) - CypherMath_Mat4Get( projectionView, 3u, 1u )
    );

    result.planes[FRUSTUM_PLANE_NEAR] = CypherMath_FrustumMakePlane(
        CypherMath_Mat4Get( projectionView, 0u, 3u ) + CypherMath_Mat4Get( projectionView, 0u, 2u ),
        CypherMath_Mat4Get( projectionView, 1u, 3u ) + CypherMath_Mat4Get( projectionView, 1u, 2u ),
        CypherMath_Mat4Get( projectionView, 2u, 3u ) + CypherMath_Mat4Get( projectionView, 2u, 2u ),
        CypherMath_Mat4Get( projectionView, 3u, 3u ) + CypherMath_Mat4Get( projectionView, 3u, 2u )
    );

    result.planes[FRUSTUM_PLANE_FAR] = CypherMath_FrustumMakePlane(
        CypherMath_Mat4Get( projectionView, 0u, 3u ) - CypherMath_Mat4Get( projectionView, 0u, 2u ),
        CypherMath_Mat4Get( projectionView, 1u, 3u ) - CypherMath_Mat4Get( projectionView, 1u, 2u ),
        CypherMath_Mat4Get( projectionView, 2u, 3u ) - CypherMath_Mat4Get( projectionView, 2u, 2u ),
        CypherMath_Mat4Get( projectionView, 3u, 3u ) - CypherMath_Mat4Get( projectionView, 3u, 2u )
    );

    return result;
}

/*
 * ===============
 * HELPER FRUSTUM FUNCS
 * ================
 */
bool CypherMath_FrustumContainsPoint( const frustum_t &frustum, const vec3_t &point )
{
    for( common::u32 i = 0u; i < FRUSTUM_PLANE_COUNT; ++i ) {
        if ( CypherMath_PlaneDistance( frustum.planes[i], point ) < -MATH_EPSILON_F ) {
            return false;
        }
    }
    return true;
}

bool CypherMath_FrustumIntersectsBounds( const frustum_t &frustum, const bounds_t &bounds )
{
    // finding the center of the boundings
    const vec3_t center = CypherMath_BoundsCenter( bounds );
    const vec3_t size = CypherMath_BoundsSize( bounds );
    const vec3_t halfs = CypherMath_Vec3Scale( size, 0.5f );

    for( common::u32 i = 0; i < FRUSTUM_PLANE_COUNT; ++i ) {
        const plane_t &plane = frustum.planes[i];

        const common::f32 distance = CypherMath_PlaneDistance( plane, center );

        const common::f32 radius = halfs.x * std::fabs( plane.normal.x ) + halfs.y * std::fabs( plane.normal.y ) + halfs.z * std::fabs( plane.normal.z );

        if ( distance < -radius ) {
            return false;
        }
    }
    return true;
}

}       // namespace cypher::engine::math
