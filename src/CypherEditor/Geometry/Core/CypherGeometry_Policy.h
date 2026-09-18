//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Policy.h
//  Purpose: Declares numerical and complexity policy for authored geometry.
//  Details: Geometry algorithms receive one explicit policy instead of relying
//           on a hidden global epsilon or unbounded temporary allocations.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_POLICY_H
#define CYPHER_EDITOR_GEOMETRY_POLICY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"

namespace cypher::editor::geometry
{

using common::f64;

// Defaults assume editor coordinates measured in metres. Importers may derive a
// policy for a different document scale, but every operation on one mesh uses the
// same validated policy for the duration of its transaction.
struct geometry_numerical_policy_t {
    f64 fCoordinateMagnitudeLimit{ 1.0e6 };
    f64 fAbsoluteDistanceTolerance{ 1.0e-8 };
    f64 fRelativeDistanceTolerance{ 1.0e-12 };
    f64 fMinimumEdgeLength{ 1.0e-6 };
    f64 fMinimumFaceArea{ 1.0e-12 };
    f64 fAngularToleranceRadians{ 1.0e-8 };
    f64 fPlanarityTolerance{ 1.0e-7 };
    f64 fCoplanarDistanceTolerance{ 1.0e-7 };
    f64 fSnapDistance{ 1.0e-4 };
    f64 fWeldDistance{ 1.0e-6 };
    f64 fCanonicalQuantization{ 1.0e-8 };
};

// Limits make expensive edits and malformed imports fail predictably before
// they consume unbounded memory. They are operation guards, not file-format
// integer widths, and can be tightened by a host application.
struct geometry_limit_policy_t {
    u64 cVerticesMax{ 1'000'000u };
    u64 cHalfEdgesMax{ 6'000'000u };
    u64 cEdgesMax{ 3'000'000u };
    u64 cLoopsMax{ 1'250'000u };
    u64 cFacesMax{ 1'000'000u };
    u64 cShellsMax{ 65'536u };
    u64 cIntersectionEventsMax{ 8'000'000u };
    u64 cJournalRecordsMax{ 8'000'000u };
    u64 cDiagnosticsMax{ 4'096u };
    u64 cbScratchMax{ 2u * common::CY_GIB };
};

struct geometry_policy_t {
    geometry_numerical_policy_t numerical{};
    geometry_limit_policy_t limits{};
};

CYPHER_NODISCARD bool GeometryNumericalPolicy_IsValid(
    const geometry_numerical_policy_t &policy ) noexcept;
CYPHER_NODISCARD bool GeometryLimitPolicy_IsValid(
    const geometry_limit_policy_t &policy ) noexcept;
CYPHER_NODISCARD bool GeometryPolicy_IsValid(
    const geometry_policy_t &policy ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_POLICY_H
