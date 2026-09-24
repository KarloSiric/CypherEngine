//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentValidation.h
//  Purpose: Declares whole-document geometry diagnostics: the checks a map
//           compiler or level designer needs that no single brush can
//           answer about itself.
//  Details: Every committed brush value is already valid on its own, so
//           this pass looks across brushes and at authoring hazards:
//
//             BRUSH_OVERLAP          two brushes share volume
//             COPLANAR_FACE_FIGHT    faces of two brushes lie on the same
//                                    plane, facing the same way, and
//                                    overlap (z-fighting)
//             HIDDEN_FACE            a face is pressed against an opposing
//                                    face of another brush (a compiler may
//                                    cull it; informational)
//             DUPLICATE_BRUSH        two brushes enclose the same solid
//             TINY_BRUSH             volume below the configured minimum
//             THIN_BRUSH             an extent below the configured minimum
//             NEAR_COORDINATE_LIMIT  bounds beyond the configured fraction
//                                    of the policy coordinate limit
//             UNASSIGNED_MATERIAL    a side with no material reference
//             REVALIDATION_FAILED    a value fails the current policy
//
//           Diagnostics go to a caller-owned bounded buffer; overflow is
//           counted, never allocated around. Pair checks use a spatial
//           index and visit pairs in ascending brush ID order, so output is
//           deterministic. Validation never mutates or repairs.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_DOCUMENT_VALIDATION_H
#define CYPHER_EDITOR_GEOMETRY_DOCUMENT_VALIDATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Diagnostics.h"
#include "CypherGeometry_Snapshot.h"

namespace cypher::editor::geometry
{

// Module-local diagnostic codes (combine with GeometryDiagnosticCode_Make
// and geometry_diagnostic_module_t::VALIDATION). Values are stable.
enum class geometry_validation_code_t : common::u32 {
    BRUSH_OVERLAP = 1u,
    COPLANAR_FACE_FIGHT = 2u,
    HIDDEN_FACE = 3u,
    DUPLICATE_BRUSH = 4u,
    TINY_BRUSH = 5u,
    THIN_BRUSH = 6u,
    NEAR_COORDINATE_LIMIT = 7u,
    UNASSIGNED_MATERIAL = 8u,
    REVALIDATION_FAILED = 9u,
};

CYPHER_NODISCARD constexpr geometry_diagnostic_code_t GeometryValidation_Code(
    geometry_validation_code_t code ) noexcept
{
    return GeometryDiagnosticCode_Make(
        geometry_diagnostic_module_t::VALIDATION, static_cast<common::u32>( code ) );
}

// Diagnostic component code used when a target names a brush side.
inline constexpr geometry_diagnostic_component_code_t GEOMETRY_DIAGNOSTIC_COMPONENT_BRUSH_SIDE = 1u;

struct geometry_validation_options_t {
    common::bool_t bCheckOverlaps{ true };
    common::bool_t bCheckCoplanarFaces{ true };
    common::bool_t bReportHiddenFaces{ false };
    common::bool_t bCheckMaterials{ false };
    common::bool_t bRevalidateValues{ false };
    // Volume below which a brush is reported tiny.
    math::f64 fTinyVolume{ 1.0e-6 };
    // Extent (on any axis) below which a brush is reported thin.
    math::f64 fThinExtent{ 1.0e-3 };
    // Fraction of policy.fCoordinateMagnitudeLimit beyond which bounds are
    // reported near the limit.
    math::f64 fLimitFraction{ 0.9 };
    // Overlap volume / face overlap area that counts; smaller is touching.
    math::f64 fOverlapVolume{ 1.0e-9 };
    math::f64 fOverlapArea{ 1.0e-9 };
};

struct geometry_validation_summary_t {
    common::u32 cErrors{ 0u };
    common::u32 cWarnings{ 0u };
    common::u32 cNotes{ 0u };
    // Diagnostics that did not fit in the buffer.
    common::u64 cTruncated{ 0u };
};

// Runs every enabled check over the snapshot and appends diagnostics.
// NOT_INITIALIZED for an unusable buffer, INVALID_ARGUMENT for a null
// snapshot, ALLOCATION_FAILED for scratch failures (diagnostics appended so
// far remain). OK even when problems were found; read the summary.
CYPHER_NODISCARD geometry_status_t GeometryValidation_TryValidateSnapshot(
    const geometry_document_snapshot_t *pSnapshot,
    const geometry_validation_options_t &options,
    geometry_diagnostic_buffer_t *pDiagnostics,
    geometry_validation_summary_t *pSummaryOut ) noexcept;

// Area of overlap between two coplanar convex face polygons of the given
// values (faces iFaceA, iFaceB), measured in the plane of face A. Zero when
// they do not overlap. Exposed for tools that highlight z-fighting.
CYPHER_NODISCARD geometry_status_t GeometryValidation_TryCoplanarOverlapArea(
    const geometry_brush_value_t *pA, common::usize iFaceA,
    const geometry_brush_value_t *pB, common::usize iFaceB,
    math::f64 *pAreaOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_DOCUMENT_VALIDATION_H
