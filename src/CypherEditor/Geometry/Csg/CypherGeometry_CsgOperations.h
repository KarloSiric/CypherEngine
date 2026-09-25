//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgOperations.h
//  Purpose: Declares the document-level Boolean commands the editor calls:
//           union / intersect / subtract / clip / split for meshes, and
//           union / intersect / carve for brushes - each one transaction
//           that replaces its operands by the result or changes nothing.
//  Details: Evaluation runs first (Csg/Mesh, Csg/Brush); only a complete,
//           validated result is published, through the document's atomic
//           replacement primitives, so an undo layer sees one change and a
//           failure leaves the map exactly as it was, with diagnostics.
//
//           Operand fate: A is always replaced (the result takes A's root
//           ID for meshes, so selection and references to A follow it).
//           B is removed unless bKeepB - Hammer's carve keeps the cutter,
//           a modelling subtract usually consumes it. A kept B must not
//           share identities with the result, so B-origin elements in the
//           result get fresh IDs in that case.
//
//           Split (the "slice" region operator): A becomes two meshes,
//           A inside B and A outside B, both closed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_OPERATIONS_H
#define CYPHER_EDITOR_GEOMETRY_CSG_OPERATIONS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgBrush.h"
#include "CypherGeometry_CsgMesh.h"

namespace cypher::editor::geometry
{

struct csg_document_options_t {
    csg_mesh_options_t mesh{};
    bool bKeepB{ false };
};

// Mesh Boolean in place. *pResultIdOut (optional) receives the result
// mesh's root (A's). An empty result removes A (and B unless kept).
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryCsgMeshes(
    geometry_document_t *pDocument,
    geometry_source_id_t meshA,
    geometry_source_id_t meshB,
    const csg_document_options_t &options,
    geometry_source_id_t *pResultIdOut,
    csg_diagnostics_t *pDiag ) noexcept;

// Splits mesh A along B's surface into the part inside B (keeps A's root)
// and the part outside (a fresh root), B untouched. Either part may be
// empty (then only the other exists; *pInsideIdOut / *pOutsideIdOut get
// GEOMETRY_SOURCE_ID_INVALID for a missing part).
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryCsgSplitMesh(
    geometry_document_t *pDocument,
    geometry_source_id_t meshA,
    geometry_source_id_t meshB,
    const csg_mesh_options_t &options,
    geometry_source_id_t *pInsideIdOut,
    geometry_source_id_t *pOutsideIdOut,
    csg_diagnostics_t *pDiag ) noexcept;

// Brush-set Boolean in place: the A brushes are replaced by the convex
// result pieces (B brushes are removed unless bKeepB). *pNewBrushIdsOut
// (optional, initialized) receives the pieces' IDs.
CYPHER_NODISCARD geometry_status_t GeometryDocument_TryCsgBrushes(
    geometry_document_t *pDocument,
    csg_operator_t op,
    common::span_t<const geometry_source_id_t> brushesA,
    common::span_t<const geometry_source_id_t> brushesB,
    bool bKeepB,
    common::vector_t<geometry_source_id_t> *pNewBrushIdsOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_OPERATIONS_H
