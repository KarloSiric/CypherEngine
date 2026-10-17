//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Prefab.h
//  Purpose: Declares prefab files (CYMAP.md 6.12, `cypher.prefab` V1):
//           writing a selection as a reusable piece, and inserting a copy of
//           one into a map with fresh IDs (Hammer's Collapse Prefab).
//  Details: Objects are stored relative to the prefab's origin: the bottom
//           centre of the selection's bounds, so an inserted copy stands on
//           the point it is placed at. Entity-owned geometry is nested in
//           its entity, as in a layer. Linked instances (6.11) are not
//           created: the map views do not load prefab files yet, so an
//           instance would be invisible.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_PREFAB_H
#define CYPHER_EDITOR_MAP_PREFAB_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

namespace cypher::editor::map
{

inline constexpr const char *MAP_PREFAB_SCHEMA_ID = "cypher.prefab";
inline constexpr common::u32 MAP_PREFAB_SCHEMA_VERSION = 1u;

// Writes the selected objects (entities with their geometry, world brushes,
// meshes, patches) as a prefab document into *pTextOut (initialized).
CYPHER_NODISCARD map_status_t MapPrefab_Write( const map_document_t *pMap, common::span_t<const common::u64> ids,
    common::string_view_t name, common::text_buffer_t *pTextOut, math::vec3d_t *pOriginOut = nullptr ) noexcept;
// The same with the prefab's origin given (an instance being written back
// keeps the origin it was placed by).
CYPHER_NODISCARD map_status_t MapPrefab_WriteAt( const map_document_t *pMap, common::span_t<const common::u64> ids,
    common::string_view_t name, math::vec3d_t origin, common::text_buffer_t *pTextOut ) noexcept;

// Inserts a copy of a prefab's objects with fresh IDs, its origin at
// `position`, into the first layer. pIdsOut (initialized) receives the new
// top-level objects.
CYPHER_NODISCARD map_status_t MapPrefab_Insert( map_document_t *pMap, common::string_view_t text, math::vec3d_t position,
    common::vector_t<common::u64> *pIdsOut ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_PREFAB_H
