//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookSourceMap.h
//  Purpose: Declares the record that ties a cooked primitive back to its
//           authored origin.
//  Details: Every cooked product (render triangle, collision triangle,
//           navigation polygon, ...) carries one of these per primitive, so
//           picking in a cooked view, compiler diagnostics, and lightmap
//           baking can name the exact authored brush and side. Only
//           persistent source IDs are stored; never handles, never host
//           object IDs.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_COOK_SOURCE_MAP_H
#define CYPHER_EDITOR_GEOMETRY_COOK_SOURCE_MAP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Types.h"

#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

struct geometry_cook_source_t {
    geometry_source_representation_kind_t representation{
        geometry_source_representation_kind_t::INVALID };
    geometry_source_id_t rootId{};       // brush, mesh, region, ...
    geometry_source_id_t componentId{};  // side, face, ... (zero for root-level)
};

// Resolves primitive iPrimitive of a cooked product's source table.
CYPHER_NODISCARD geometry_status_t CookSourceMap_TryResolve(
    const common::vector_t<geometry_cook_source_t> *pSources,
    common::usize iPrimitive,
    geometry_cook_source_t *pSourceOut ) noexcept;

// Counts primitives originating from the given root (and, when valid, the
// given component). Hosts use it to highlight what an authored element
// became after cooking.
CYPHER_NODISCARD common::usize CookSourceMap_CountFrom(
    const common::vector_t<geometry_cook_source_t> *pSources,
    geometry_source_id_t rootId,
    geometry_source_id_t componentId ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_COOK_SOURCE_MAP_H
