//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Policy.cpp
//  Purpose: Validates numerical and complexity policy for authored geometry.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Policy.h"

#include <cmath>

namespace cypher::editor::geometry
{

namespace
{

bool IsFinitePositive( f64 value ) noexcept
{
    return std::isfinite( value ) && value > 0.0;
}

} // namespace

bool GeometryNumericalPolicy_IsValid(
    const geometry_numerical_policy_t &policy ) noexcept
{
    const bool bFinitePositive =
        IsFinitePositive( policy.fCoordinateMagnitudeLimit ) &&
        IsFinitePositive( policy.fAbsoluteDistanceTolerance ) &&
        IsFinitePositive( policy.fRelativeDistanceTolerance ) &&
        IsFinitePositive( policy.fMinimumEdgeLength ) &&
        IsFinitePositive( policy.fMinimumFaceArea ) &&
        IsFinitePositive( policy.fAngularToleranceRadians ) &&
        IsFinitePositive( policy.fPlanarityTolerance ) &&
        IsFinitePositive( policy.fCoplanarDistanceTolerance ) &&
        IsFinitePositive( policy.fSnapDistance ) &&
        IsFinitePositive( policy.fWeldDistance ) &&
        IsFinitePositive( policy.fCanonicalQuantization );
    if ( !bFinitePositive ) {
        return false;
    }

    const f64 fDistanceSquared =
        policy.fAbsoluteDistanceTolerance *
        policy.fAbsoluteDistanceTolerance;
    return policy.fRelativeDistanceTolerance < 1.0 &&
           policy.fAngularToleranceRadians < 1.0 &&
           policy.fAbsoluteDistanceTolerance <=
               policy.fCanonicalQuantization &&
           policy.fCanonicalQuantization <= policy.fMinimumEdgeLength &&
           policy.fAbsoluteDistanceTolerance <= policy.fWeldDistance &&
           policy.fWeldDistance <= policy.fSnapDistance &&
           policy.fAbsoluteDistanceTolerance <= policy.fPlanarityTolerance &&
           policy.fAbsoluteDistanceTolerance <=
               policy.fCoplanarDistanceTolerance &&
           fDistanceSquared <= policy.fMinimumFaceArea &&
           policy.fSnapDistance < policy.fCoordinateMagnitudeLimit;
}

bool GeometryLimitPolicy_IsValid(
    const geometry_limit_policy_t &policy ) noexcept
{
    const bool bNonZero =
        policy.cVerticesMax > 0u &&
        policy.cHalfEdgesMax > 0u &&
        policy.cEdgesMax > 0u &&
        policy.cLoopsMax > 0u &&
        policy.cFacesMax > 0u &&
        policy.cShellsMax > 0u &&
        policy.cIntersectionEventsMax > 0u &&
        policy.cJournalRecordsMax > 0u &&
        policy.cDiagnosticsMax > 0u &&
        policy.cbScratchMax > 0u;
    if ( !bNonZero ) {
        return false;
    }

    // One manifold edge owns one or two half-edges; every face owns at least
    // one loop. These relations reject policies that cannot describe their own
    // advertised edge and face maxima.
    return policy.cHalfEdgesMax >= policy.cEdgesMax &&
           policy.cLoopsMax >= policy.cFacesMax;
}

bool GeometryPolicy_IsValid( const geometry_policy_t &policy ) noexcept
{
    return GeometryNumericalPolicy_IsValid( policy.numerical ) &&
           GeometryLimitPolicy_IsValid( policy.limits );
}

} // namespace cypher::editor::geometry
