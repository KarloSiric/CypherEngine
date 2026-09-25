//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceSlice.h
//  Purpose: Identity-addressed wrapper for cutting a mesh source with a
//           plane (MeshSlice_ByPlane): slice, clip, and capped clip.
//  Details: Each piece of a cut face takes that face's material and
//           corner attributes (UVs are refit from the face's own layout, so
//           a texture continues across the cut); the largest kept piece
//           also keeps the face's source ID, so selections and references
//           to the face follow the part the user most likely means. Cap
//           faces are new surface with no parent: they get default
//           attributes, and the tool is expected to project a material onto
//           them (the cut faces' mapping runs across the cap plane, so
//           copying it would smear).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_SLICE_H
#define CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_SLICE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshAttributeTransfer.h"
#include "CypherGeometry_MeshSlice.h"

namespace cypher::editor::geometry
{

// Cuts the source with params.plane (see MeshSlice_ByPlane for the modes
// and rejections; a rejected cut leaves the source unchanged). pFacesOut
// (optional, initialized) receives the new faces with their roles.
CYPHER_NODISCARD geometry_status_t MeshSourceEdit_TrySliceByPlane(
    mesh_source_t *pSource,
    const mesh_slice_params_t &params,
    common::vector_t<mesh_slice_face_t> *pFacesOut,
    mesh_edit_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_MESH_SOURCE_SLICE_H
