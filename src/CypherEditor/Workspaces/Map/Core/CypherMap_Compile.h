//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Compile.h
//  Purpose: Declares Compile Map: the geometry library's cook pipeline run
//           over the map's geometry (surface soup, dependency keys,
//           collision, visibility hulls, navigation candidates, lightmap
//           charts and render batches), and the compiler interchange text
//           headless compilers read.
//  Details: Everything is cooked from one snapshot, so the products agree.
//           Objects the cook could not tessellate are reported, not fatal.
//           The final runtime map (.cymap_c) is the scene compiler's job.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_COMPILE_H
#define CYPHER_EDITOR_MAP_COMPILE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <string>
#include <vector>

namespace cypher::editor::map
{

struct map_compile_options_t {
    common::f64 lightmapTexelsPerUnit{ 1.0 / 16.0 };
    common::u32 lightmapPageSize{ 1024u };
    common::f64 walkableSlopeDegrees{ 45.0 };
    common::f64 batchChunkSize{ 1024.0 };
};

struct map_compile_report_t {
    common::usize objects{ 0u }, vertices{ 0u }, triangles{ 0u };
    common::usize collisionSurfaces{ 0u }, collisionTriangles{ 0u }, collisionDiagnostics{ 0u };
    common::usize visibilityHulls{ 0u }, visibilityShells{ 0u }, visibilityDetail{ 0u };
    common::usize walkableTriangles{ 0u };
    common::usize lightmapCharts{ 0u }, lightmapPages{ 0u };
    common::usize batches{ 0u };
    std::vector<std::string> problems;  // Objects skipped, with why.
    std::string interchange;            // cypher.geometry.interchange v1 text.
    common::f64 milliseconds{ 0.0 };
};

// Cooks the map's geometry. Fails only for allocation or bad options; a
// map whose objects all fail still compiles, with every one reported.
CYPHER_NODISCARD map_status_t MapCompile_Run( const map_document_t *pMap, const map_compile_options_t &options, map_compile_report_t &report );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_COMPILE_H
