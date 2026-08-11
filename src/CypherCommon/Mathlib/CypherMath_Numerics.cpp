//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Numerics.cpp
//  Purpose: Implements robust numerical helpers shared by tools and physics.
//  Details: Quadratic roots avoid catastrophic cancellation, closest-segment
//           queries handle degeneracy, and integration rejects invalid state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Numerics.h"

#include "CypherCommon_Assert.h"

#include <algorithm>
#include <cmath>

namespace cypher::math
{

namespace
{

struct vec3d_t {
    f64 x;
    f64 y;
    f64 z;
};

vec3d_t SubtractDouble( vec3_t a, vec3_t b ) noexcept
{
    return {
        static_cast<f64>( a.x ) - b.x,
        static_cast<f64>( a.y ) - b.y,
        static_cast<f64>( a.z ) - b.z
    };
}

f64 DotDouble( vec3d_t a, vec3d_t b ) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t SegmentPointDouble( segment_t segment, f64 parameter ) noexcept
{
    return Vec3_Make(
        static_cast<f32>( static_cast<f64>( segment.start.x ) +
            ( static_cast<f64>( segment.end.x ) - segment.start.x ) * parameter ),
        static_cast<f32>( static_cast<f64>( segment.start.y ) +
            ( static_cast<f64>( segment.end.y ) - segment.start.y ) * parameter ),
        static_cast<f32>( static_cast<f64>( segment.start.z ) +
            ( static_cast<f64>( segment.end.z ) - segment.start.z ) * parameter ) );
}

} // namespace

bool_t Numerics_TrySolveQuadratic(
    f64 a,
    f64 b,
    f64 c,
    f64 coefficientTolerance,
    f64 discriminantTolerance,
    quadratic_solution_t *pSolution ) noexcept
{
    const bool_t bValidOutput = pSolution != nullptr;
    CY_ASSERT_MSG( bValidOutput, "Numerics_TrySolveQuadratic requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pSolution = {};
    pSolution->count = polynomial_solution_count_t::ZERO;
    if ( !Scalar_IsFinite( a ) || !Scalar_IsFinite( b ) ||
         !Scalar_IsFinite( c ) || coefficientTolerance < 0.0 ||
         discriminantTolerance < 0.0 ) {
        return false;
    }

    if ( std::abs( a ) <= coefficientTolerance ) {
        if ( std::abs( b ) <= coefficientTolerance ) {
            pSolution->count = std::abs( c ) <= coefficientTolerance
                ? polynomial_solution_count_t::INFINITE
                : polynomial_solution_count_t::ZERO;
            return true;
        }
        const f64 root = -c / b;
        if ( !Scalar_IsFinite( root ) ) {
            return false;
        }
        pSolution->count = polynomial_solution_count_t::ONE;
        pSolution->root0 = root;
        pSolution->root1 = root;
        return true;
    }

    const f64 discriminant = std::fma( b, b, -4.0 * a * c );
    if ( !Scalar_IsFinite( discriminant ) ) {
        return false;
    }
    if ( discriminant < -discriminantTolerance ) {
        return true;
    }
    if ( std::abs( discriminant ) <= discriminantTolerance ) {
        const f64 root = -0.5 * b / a;
        if ( !Scalar_IsFinite( root ) ) {
            return false;
        }
        pSolution->count = polynomial_solution_count_t::ONE;
        pSolution->root0 = root;
        pSolution->root1 = root;
        return true;
    }

    const f64 squareRoot = std::sqrt( discriminant );
    const f64 q = -0.5 * ( b + std::copysign( squareRoot, b ) );
    f64 root0 = 0.0;
    f64 root1 = 0.0;
    if ( std::abs( q ) <= coefficientTolerance ) {
        root0 = ( -b - squareRoot ) / ( 2.0 * a );
        root1 = ( -b + squareRoot ) / ( 2.0 * a );
    } else {
        root0 = q / a;
        root1 = c / q;
    }
    if ( !Scalar_IsFinite( root0 ) || !Scalar_IsFinite( root1 ) ) {
        return false;
    }
    if ( root1 < root0 ) {
        std::swap( root0, root1 );
    }
    pSolution->count = polynomial_solution_count_t::TWO;
    pSolution->root0 = root0;
    pSolution->root1 = root1;
    return true;
}

bool_t Numerics_TryClosestSegmentPoints(
    segment_t a,
    segment_t b,
    f64 relativeParallelTolerance,
    segment_closest_points_t *pClosest ) noexcept
{
    const bool_t bValidOutput = pClosest != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Numerics_TryClosestSegmentPoints requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pClosest = {};
    if ( !Segment_IsFinite( a ) || !Segment_IsFinite( b ) ||
         relativeParallelTolerance < 0.0 ||
         !Scalar_IsFinite( relativeParallelTolerance ) ) {
        return false;
    }

    const vec3d_t directionA = SubtractDouble( a.end, a.start );
    const vec3d_t directionB = SubtractDouble( b.end, b.start );
    const vec3d_t relativeStart = SubtractDouble( a.start, b.start );
    const f64 lengthSquaredA = DotDouble( directionA, directionA );
    const f64 lengthSquaredB = DotDouble( directionB, directionB );
    const f64 projectionB = DotDouble( directionB, relativeStart );

    f64 parameterA = 0.0;
    f64 parameterB = 0.0;
    if ( lengthSquaredA == 0.0 && lengthSquaredB == 0.0 ) {
        parameterA = 0.0;
        parameterB = 0.0;
    } else if ( lengthSquaredA == 0.0 ) {
        parameterB = std::clamp( projectionB / lengthSquaredB, 0.0, 1.0 );
    } else {
        const f64 projectionA = DotDouble( directionA, relativeStart );
        if ( lengthSquaredB == 0.0 ) {
            parameterA = std::clamp( -projectionA / lengthSquaredA, 0.0, 1.0 );
        } else {
            const f64 directionDot = DotDouble( directionA, directionB );
            const f64 denominator =
                lengthSquaredA * lengthSquaredB - directionDot * directionDot;
            const f64 parallelThreshold = relativeParallelTolerance *
                lengthSquaredA * lengthSquaredB;
            if ( denominator > parallelThreshold ) {
                parameterA = std::clamp(
                    ( directionDot * projectionB -
                      projectionA * lengthSquaredB ) / denominator,
                    0.0, 1.0 );
            }
            parameterB =
                ( directionDot * parameterA + projectionB ) / lengthSquaredB;
            if ( parameterB < 0.0 ) {
                parameterB = 0.0;
                parameterA = std::clamp(
                    -projectionA / lengthSquaredA, 0.0, 1.0 );
            } else if ( parameterB > 1.0 ) {
                parameterB = 1.0;
                parameterA = std::clamp(
                    ( directionDot - projectionA ) / lengthSquaredA,
                    0.0, 1.0 );
            }
        }
    }

    const vec3_t pointA = SegmentPointDouble( a, parameterA );
    const vec3_t pointB = SegmentPointDouble( b, parameterB );
    const f32 distanceSquared = Vec3_DistanceSquared( pointA, pointB );
    if ( !Vec3_IsFinite( pointA ) || !Vec3_IsFinite( pointB ) ||
         !Scalar_IsFinite( distanceSquared ) ) {
        return false;
    }
    *pClosest = {
        pointA,
        pointB,
        static_cast<f32>( parameterA ),
        static_cast<f32>( parameterB ),
        distanceSquared
    };
    return true;
}

bool_t Numerics_TryIntegrateLinearSemiImplicit(
    vec3_t position,
    vec3_t velocity,
    vec3_t acceleration,
    f32 deltaSeconds,
    vec3_t *pNextPosition,
    vec3_t *pNextVelocity ) noexcept
{
    const bool_t bValidOutputs = pNextPosition != nullptr && pNextVelocity != nullptr;
    CY_ASSERT_MSG( bValidOutputs,
        "Numerics_TryIntegrateLinearSemiImplicit requires two outputs." );
    if ( !bValidOutputs ) {
        return false;
    }
    *pNextPosition = CY_VEC3_ZERO;
    *pNextVelocity = CY_VEC3_ZERO;
    if ( !Vec3_IsFinite( position ) || !Vec3_IsFinite( velocity ) ||
         !Vec3_IsFinite( acceleration ) || !Scalar_IsFinite( deltaSeconds ) ||
         deltaSeconds < 0.0f ) {
        return false;
    }

    const vec3_t nextVelocity = Vec3_MulAdd(
        velocity, acceleration, deltaSeconds );
    const vec3_t nextPosition = Vec3_MulAdd(
        position, nextVelocity, deltaSeconds );
    if ( !Vec3_IsFinite( nextPosition ) || !Vec3_IsFinite( nextVelocity ) ) {
        return false;
    }
    *pNextPosition = nextPosition;
    *pNextVelocity = nextVelocity;
    return true;
}

bool_t Numerics_TryIntegrateAngularVelocity(
    quat_t orientation,
    vec3_t angularVelocity,
    angular_velocity_space_t velocitySpace,
    f32 deltaSeconds,
    f32 minimumLength,
    quat_t *pNextOrientation ) noexcept
{
    const bool_t bValidOutput = pNextOrientation != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Numerics_TryIntegrateAngularVelocity requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pNextOrientation = CY_QUAT_IDENTITY;
    if ( !Quat_IsFinite( orientation ) || !Vec3_IsFinite( angularVelocity ) ||
         !Scalar_IsFinite( deltaSeconds ) || deltaSeconds < 0.0f ||
         !Scalar_IsFinite( minimumLength ) || minimumLength < 0.0f ||
         velocitySpace >= angular_velocity_space_t::COUNT ) {
        return false;
    }

    quat_t unitOrientation{};
    if ( !Quat_TryNormalize(
             orientation, minimumLength, &unitOrientation, nullptr ) ) {
        return false;
    }
    const f32 speed = Vec3_Length( angularVelocity );
    if ( !Scalar_IsFinite( speed ) ) {
        return false;
    }
    if ( speed <= minimumLength || deltaSeconds == 0.0f ) {
        *pNextOrientation = unitOrientation;
        return true;
    }

    const vec3_t axis = Vec3_Scale( angularVelocity, 1.0f / speed );
    const quat_t delta = Quat_FromUnitAxisAngle(
        axis, Angle_FromRadians( speed * deltaSeconds ) );
    const quat_t integrated = velocitySpace == angular_velocity_space_t::WORLD
        ? Quat_Multiply( delta, unitOrientation )
        : Quat_Multiply( unitOrientation, delta );
    return Quat_TryNormalize(
        integrated, minimumLength, pNextOrientation, nullptr );
}

} // namespace cypher::math
