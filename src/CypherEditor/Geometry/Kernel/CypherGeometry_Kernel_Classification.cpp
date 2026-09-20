/*======================================================================
   File: CypherGeometry_Kernel_Classification.cpp
   Project: CYPHER
   Author: ksiric <email@example.com>
   Created: 2026-09-20 21:17:25
   Last Modified by: ksiric
   Last Modified: 2026-09-20 22:30:51
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
    
using cypher::math::f64;
using cypher::math::plane_side_t;
using cypher::math::planed_t;
using cypher::math::vec3d_t;
using cypher::common::bool_t;

bool_t IsMagnitudeLimit( f64 value, f64 limit ) noexcept
{
    return cypher::math::Scalar_Abs( value ) <= limit;
}

bool_t IsWithinMagnitudeLimit( vec3d_t value, f64 limit ) noexcept
{
    return ( 
            IsMagnitudeLimit( value.x, limit ) &&
            IsMagnitudeLimit( value.y, limit ) &&
            IsMagnitudeLimit( value.z, limit )
            );
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
    
}       // namespace

/*
using cypher::math::f64;
using cypher::math::plane_side_t;
using cypher::math::planed_t;
using cypher::math::vec3d_t;
*/

geometry_classify_result_t Kernel_ClassifyPoint( const geometry_numerical_policy_t &policy, planed_t plane, vec3d_t point ) noexcept
{
    geometry_classify_result_t result{};
    if ( !cypher::math::Vec3d_IsFinite( point ) || !cypher::math::Planed_IsFinite( plane ) ) {
        result.status = geometry_status_t::NUMERIC_FAILURE;
        return result;   
    }
    if ( !IsWithinMagnitudeLimit( point, policy.fCoordinateMagnitudeLimit ) || !IsMagnitudeLimit( plane.d, policy.fCoordinateMagnitudeLimit ) ) {
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
    
}       // namespace cypher::editor::geometry
