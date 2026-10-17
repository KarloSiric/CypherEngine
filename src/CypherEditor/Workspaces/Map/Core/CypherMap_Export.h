//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Export.h
//  Purpose: Declares exporting the map's surfaces to Wavefront OBJ
//           (TrenchBroom's File > Export > Wavefront OBJ), for modelling
//           tools and quick previews elsewhere.
//  Details: Brushes are converted to meshes on a private copy; every mesh
//           (its modifier stack applied as seen) goes through the render
//           cook, so UVs and normals follow smoothing groups and hard
//           edges; patches are tessellated. Coordinates stay the map's
//           (Z up); V is flipped to OBJ's upward convention. Faces are
//           grouped per object ("o") and per material ("usemtl", named by
//           the material path), and the .mtl lists those materials.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_EXPORT_H
#define CYPHER_EDITOR_MAP_EXPORT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <string>

namespace cypher::editor::map
{

struct map_export_report_t {
    common::usize objects{ 0u };
    common::usize triangles{ 0u };
    common::usize skipped{ 0u }; // Explicitly unsupported content only; failed cooks abort.
    common::usize skippedTerrain{ 0u };
    common::usize skippedInstances{ 0u }; // Core has no external prefab resolver.
};

// Writes OBJ/MTL text and report together on success; all outputs are unchanged
// on failure. A failed modifier/cook never falls back to the source cage.
// Enabled unknown modifier types and unsupported subdivision schemes are
// refused; disabled retained stages intentionally have no export effect.
// Terrain and unresolved linked instances are counted explicitly. The GUI
// resolves instances into a private copy before calling this core exporter.
CYPHER_NODISCARD map_status_t MapExport_Obj( const map_document_t *pMap, const std::string &mtlFileName, std::string &objOut, std::string &mtlOut,
    map_export_report_t *pReportOut ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_EXPORT_H
