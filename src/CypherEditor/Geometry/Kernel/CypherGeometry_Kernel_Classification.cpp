/*======================================================================
   File: CypherGeometry_Kernel_Classification.cpp
   Project: CYPHER
   Author: ksiric <email@example.com>
   Created: 2026-09-20 21:17:25
   Last Modified by: ksiric
   Last Modified: 2026-09-21 00:29:20
   ---------------------------------------------------------------------
   Description:
       
   ---------------------------------------------------------------------
   License: 
   Company: 
   Version: 0.1.0
 ======================================================================
                                                                       */

#include "CypherGeometry_Kernel_Classification.h"

namespace cypher::editor::geometry
{
    
namespace
{
    
using cypher::common::bool_t;
using cypher::common::i32;
using cypher::math::f64;
using cypher::math::plane_side_t;
using cypher::math::planed_t;
using cypher::math::vec2d_t;
using cypher::math::vec3d_t;

// One name, three types. Kept as an overload set rather than separate names so
// a call site cannot silently pick the wrong check: passing a scalar where a
// vector was meant is then a compile error instead of a missing axis.
bool_t IsWithinMagnitudeLimit( f64 value, f64 limit ) noexcept
{
    return cypher::math::Scalar_Abs( value ) <= limit;
}

bool_t IsWithinMagnitudeLimit( vec2d_t value, f64 limit ) noexcept
{
    return IsWithinMagnitudeLimit( value.x, limit ) &&
           IsWithinMagnitudeLimit( value.y, limit );
}

bool_t IsWithinMagnitudeLimit( vec3d_t value, f64 limit ) noexcept
{
    return IsWithinMagnitudeLimit( value.x, limit ) &&
           IsWithinMagnitudeLimit( value.y, limit ) &&
           IsWithinMagnitudeLimit( value.z, limit );
}

geometry_orientation_t ToGeometryOrientation( plane_side_t side ) noexcept
{
    switch( side ) {
    case plane_side_t::POSITIVE:
        return geometry_orientation_t::POSITIVE;
    case plane_side_t::NEGATIVE:
        return geometry_orientation_t::NEGATIVE;
    case plane_side_t::ON_PLANE:
    case plane_side_t::COUNT:
        break;
    }
    return geometry_orientation_t::ON_PLANE;
}

geometry_orientation_t ToGeometryOrientation( i32 sign ) noexcept
{
    if ( sign > 0 ) {
        return geometry_orientation_t::POSITIVE;
    }
    if ( sign < 0 ) {
        return geometry_orientation_t::NEGATIVE;
    }
    return geometry_orientation_t::ON_PLANE;
}

}       // namespace

geometry_classify_result_t Kernel_ClassifyPoint( const geometry_numerical_policy_t &policy, planed_t plane, vec3d_t point ) noexcept
{
    geometry_classify_result_t result{};
    if ( !cypher::math::Vec3d_IsFinite( point ) || !cypher::math::Planed_IsFinite( plane ) ) {
        result.status = geometry_status_t::NUMERIC_FAILURE;
        return result;   
    }
    // Sourcing the tolerance from policy is only safe if the tolerance is
    // itself usable. Planed_ClassifyPoint returns ON_PLANE for a non-finite or
    // negative tolerance, and without this guard that would be reported with
    // status OK -- a confident answer derived from nothing. Checking the two
    // fields this function actually reads costs four comparisons; validating
    // the whole policy per call would not be affordable on a path that runs
    // once per vertex per plane.
    if ( !cypher::math::Scalar_IsFinite( policy.fCoplanarDistanceTolerance ) ||
         policy.fCoplanarDistanceTolerance < 0.0 ||
         !cypher::math::Scalar_IsFinite( policy.fUnitNormalTolerance ) ||
         policy.fUnitNormalTolerance < 0.0 ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !IsWithinMagnitudeLimit( point, policy.fCoordinateMagnitudeLimit ) || !IsWithinMagnitudeLimit( plane.d, policy.fCoordinateMagnitudeLimit ) ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }
    if ( !cypher::math::Planed_IsNormalized( plane, policy.fUnitNormalTolerance ) ) {
        result.status = geometry_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.status = geometry_status_t::OK;
    result.orientation = ToGeometryOrientation(
        cypher::math::Planed_ClassifyPoint(
            plane, point, policy.fCoplanarDistanceTolerance ) );
    return result;
}

geometry_classify_result_t Kernel_Orient2D(
    const geometry_numerical_policy_t &policy,
    cypher::math::vec2d_t a,
    cypher::math::vec2d_t b,
    cypher::math::vec2d_t c ) noexcept
{
    geometry_classify_result_t result{};

    if ( !cypher::math::Vec2d_IsFinite( a ) ||
         !cypher::math::Vec2d_IsFinite( b ) ||
         !cypher::math::Vec2d_IsFinite( c ) ) {
        result.status = geometry_status_t::NUMERIC_FAILURE;
        return result;
    }

    const f64 limit = policy.fCoordinateMagnitudeLimit;
    if ( !IsWithinMagnitudeLimit( a, limit ) ||
         !IsWithinMagnitudeLimit( b, limit ) ||
         !IsWithinMagnitudeLimit( c, limit ) ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }

    result.status = geometry_status_t::OK;
    result.orientation = ToGeometryOrientation( cypher::math::Orient2D( a, b, c ) );
    return result;
}

geometry_classify_result_t Kernel_Orient3D(
    const geometry_numerical_policy_t &policy,
    cypher::math::vec3d_t a,
    cypher::math::vec3d_t b,
    cypher::math::vec3d_t c,
    cypher::math::vec3d_t d ) noexcept
{
    geometry_classify_result_t result{};

    if ( !cypher::math::Vec3d_IsFinite( a ) || !cypher::math::Vec3d_IsFinite( b ) ||
         !cypher::math::Vec3d_IsFinite( c ) || !cypher::math::Vec3d_IsFinite( d ) ) {
        result.status = geometry_status_t::NUMERIC_FAILURE;
        return result;
    }

    const f64 limit = policy.fCoordinateMagnitudeLimit;
    if ( !IsWithinMagnitudeLimit( a, limit ) || !IsWithinMagnitudeLimit( b, limit ) ||
         !IsWithinMagnitudeLimit( c, limit ) || !IsWithinMagnitudeLimit( d, limit ) ) {
        result.status = geometry_status_t::LIMIT_EXCEEDED;
        return result;
    }

    result.status = geometry_status_t::OK;
    result.orientation = ToGeometryOrientation( cypher::math::Orient3D( a, b, c, d ) );
    return result;
}
    
}       // namespace cypher::editor::geometry
