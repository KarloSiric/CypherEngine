//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_ImportObj.h
//  Purpose: Declares importing a Wavefront OBJ as editable meshes: each
//           "o" (or "g") becomes one mesh, polygons stay polygons, UVs are
//           kept, and "usemtl" names that are material paths stay materials.
//  Details: OBJ from other tools is often Y up and in other units: `yUp`
//           turns it Z up and `scale` sizes it. Faces that are not flat are
//           split into triangles (mesh faces are planar); faces with fewer
//           than three distinct corners are skipped. What is skipped is
//           counted, never silently lost.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_IMPORT_OBJ_H
#define CYPHER_EDITOR_MAP_IMPORT_OBJ_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <vector>

namespace cypher::editor::map
{

struct map_import_options_t {
    common::f64 scale{ 1.0 };
    bool bYUp{ false };
    math::vec3d_t offset{};                 // Added after scaling and turning.
    common::string_view_t defaultMaterial{}; // For faces with no usable usemtl.
    common::f64 worldUnitsPerUv{ 128.0 };   // UVs for faces the file gives none.
};

struct map_import_report_t {
    common::usize meshes{ 0u };
    common::usize faces{ 0u };
    common::usize triangulated{ 0u }; // Non-flat polygons split.
    common::usize skipped{ 0u };      // Degenerate or unreadable faces.
};

// INVALID_ARGUMENT when the text has no usable face; GEOMETRY_FAILED when a
// group's faces do not form a valid mesh (nothing is added then).
CYPHER_NODISCARD map_status_t MapImport_Obj( map_document_t *pMap, common::string_view_t text, const map_import_options_t &options,
    std::vector<common::u64> &meshesOut, map_import_report_t *pReportOut );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_IMPORT_OBJ_H
