//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_CoordinateKey.cpp
//  Purpose: Implements canonical coordinate quantization.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Kernel_CoordinateKey.h"
#include "CypherMath_Scalar.h"

#include <cmath>

namespace cypher::editor::geometry
{

namespace 
{

using cypher::common::bool_t;
using cypher::math::f64;
using cypher::math::vec3d_t;

bool_t IsWithinMagnitudeLimit( f64 value, f64 limit ) noexcept
{
    return cypher::math::Scalar_Abs( value ) <= limit;
}

i64 QuantizeAxis( f64 value, f64 quantum ) noexcept
{
    // Round half up, written explicitly rather than via std::nearbyint or
    // std::rint: those honor the ambient floating-point rounding mode, which is
    // process state this library does not own. A canonical key that changes
    // because some unrelated code set a different rounding mode would break
    // exactly the reproducibility this type exists to provide.
    //
    // The cast is in range by construction: the caller has already bounded
    // |value| by fCoordinateMagnitudeLimit, and GeometryNumericalPolicy_IsValid
    // requires fCoordinateMagnitudeLimit / fCanonicalQuantization <= 2^53 - 1,
    // so the quotient cannot approach the i64 limit.
    return static_cast<i64>( std::floor( value / quantum + 0.5 ) );
}
 
}       // namespace

geometry_status_t Kernel_TryQuantizePoint(
    const geometry_numerical_policy_t &policy,
    vec3d_t point,
    geometry_coordinate_key_t *pKey ) noexcept
{
    if ( pKey == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pKey = {};

    // Checked before use rather than trusting the operation context, because a
    // non-positive or non-finite quantum would silently produce garbage indices
    // instead of failing -- and unlike classification, a bad key corrupts
    // identity, so every later lookup and sort inherits the damage.
    const f64 quantum = policy.fCanonicalQuantization;
    if ( !cypher::math::Scalar_IsFinite( quantum ) || quantum <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !cypher::math::Vec3d_IsFinite( point ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !IsWithinMagnitudeLimit( point.x, policy.fCoordinateMagnitudeLimit ) ||
         !IsWithinMagnitudeLimit( point.y, policy.fCoordinateMagnitudeLimit ) ||
         !IsWithinMagnitudeLimit( point.z, policy.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    pKey->x = QuantizeAxis( point.x, quantum );
    pKey->y = QuantizeAxis( point.y, quantum );
    pKey->z = QuantizeAxis( point.z, quantum );
    return geometry_status_t::OK;
}
    
}       // namespace cypher::editor::geometry
