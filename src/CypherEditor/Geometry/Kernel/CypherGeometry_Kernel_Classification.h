//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_Classification.h
//  Purpose: Declares policy-aware point/plane orientation classification.
//  Details: Wraps Cypher::Math's raw, tolerance-agnostic predicates with the
//           explicit range and normalization checks Gate 1 requires, so no
//           call site picks its own epsilon or trusts an unverified plane.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_KERNEL_CLASSIFICATION_H
#define CYPHER_EDITOR_GEOMETRY_KERNEL_CLASSIFICATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Policy.h"
#include "CypherGeometry_Types.h"

#include "CypherMath_Plane.h"

namespace cypher::editor::geometry 
{
    
// Geometry-owned mirror of cypher::math::plane_side_t. Kept as a distinct type so Geometry's
// public contract never depends on CypherMath's internal calssification vocabulary changing shape.

enum class geometry_orientation_t : common::u8 {
    NEGATIVE = 0u,          // behind the plane, beyond tolerance.
    ON_PLANE,           // Within tolerance of the plane.
    POSITIVE,           // In front of the plane, beyond tolerance.
    COUNT           // enum bound; 
};

struct geometry_classify_result_t {
    geometry_status_t status{ geometry_status_t::INVALID_ARGUMENT };
    geometry_orientation_t orientation{ geometry_orientation_t::ON_PLANE };
};

// Classifies point against plane using policy.fCoplanarDistanceTolerance as the
// single, documented tolerance for this decision. Returns INVALID_ARGUMENT
// without classifying if point or plane is non-finite, if either exceeds
// policy.fCoordinateMagnitudeLimit, or if plane.normal is not unit length
// within policy.fUnitNormalTolerance -- Planed_ClassifyPoint's signed-distance
// contract is only metric for a genuinely unit-length normal, and Kernel is
// the layer responsible for verifying that precondition rather than trusting it.
CYPHER_NODISCARD geometry_classify_result_t Kernel_ClassifyPoint(
    const geometry_numerical_policy_t &policy,
    cypher::math::planed_t plane,
    cypher::math::vec3d_t point ) noexcept;

}               // namespace cypher::editor::geometry
#endif              // ENDIF CYPHER_EDITOR_GEOMETRY_KERNEL_CLASSIFICATION_H
