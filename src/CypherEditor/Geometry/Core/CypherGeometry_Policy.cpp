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
    // Canonical lattice indices are formed in binary64 before checked integer
    // conversion. Staying within 2^53 - 1 keeps every supported integer index
    // exactly representable and makes ordering independent of rounded division.
    constexpr f64 cLargestExactInteger = 9'007'199'254'740'991.0;

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
    const f64 cCanonicalSteps =
        policy.fCoordinateMagnitudeLimit /
        policy.fCanonicalQuantization;
    return policy.fRelativeDistanceTolerance < 1.0 &&
           policy.fAngularToleranceRadians < 1.0 &&
           std::isfinite( cCanonicalSteps ) &&
           cCanonicalSteps <= cLargestExactInteger &&
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
        policy.cBrushesMax > 0u &&
        policy.cBrushSidesMax > 0u &&
        policy.cBrushSidesPerBrushMax > 0u &&
        policy.cVerticesMax > 0u &&
        policy.cHalfEdgesMax > 0u &&
        policy.cEdgesMax > 0u &&
        policy.cLoopsMax > 0u &&
        policy.cFacesMax > 0u &&
        policy.cShellsMax > 0u &&
        policy.cIntersectionEventsMax > 0u &&
        policy.cJournalRecordsMax > 0u &&
        policy.cDiagnosticsMax > 0u &&
        policy.cTraversalDepthMax > 0u &&
        policy.cbScratchMax > 0u;
    if ( !bNonZero ) {
        return false;
    }

    const u64 cHandleCapacityMax =
        static_cast<u64>( common::CY_INVALID_INDEX );
    const bool bHandleCountsEncodable =
        policy.cBrushesMax <= cHandleCapacityMax &&
        policy.cBrushSidesMax <= cHandleCapacityMax &&
        policy.cBrushSidesPerBrushMax <= cHandleCapacityMax &&
        policy.cVerticesMax <= cHandleCapacityMax &&
        policy.cHalfEdgesMax <= cHandleCapacityMax &&
        policy.cEdgesMax <= cHandleCapacityMax &&
        policy.cLoopsMax <= cHandleCapacityMax &&
        policy.cFacesMax <= cHandleCapacityMax &&
        policy.cShellsMax <= cHandleCapacityMax;
    if ( !bHandleCountsEncodable ) {
        return false;
    }

    // A bounded 3D convex brush needs at least four sides, a closed manifold
    // edge needs two half-edges, and every face needs at least one loop. These
    // relations keep the advertised aggregate maxima mutually achievable.
    return policy.cBrushesMax <= policy.cBrushSidesMax / 4u &&
           policy.cBrushSidesPerBrushMax <= policy.cBrushSidesMax &&
           policy.cEdgesMax <= policy.cHalfEdgesMax / 2u &&
           policy.cLoopsMax >= policy.cFacesMax;
}

bool GeometryPolicy_IsValid( const geometry_policy_t &policy ) noexcept
{
    return GeometryNumericalPolicy_IsValid( policy.numerical ) &&
           GeometryLimitPolicy_IsValid( policy.limits );
}

} // namespace cypher::editor::geometry
