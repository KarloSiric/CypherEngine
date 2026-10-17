//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Hierarchy.h
//  Purpose: Declares entity attachment (CYMAP.md 6.2 `parent`): reading and
//           editing which entity another is attached to.
//  Details: An attached entity goes with its parent, as in Hammer 5's
//           hierarchy: moving, turning, scaling, shearing, copying or
//           deleting the parent does the same to it (CypherMap_Edit expands
//           a selection by these links, as it does by geometry ownership).
//           Only entity records carry `parent`; geometry belongs to an
//           entity by ownership (Tie to Entity) instead.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_HIERARCHY_H
#define CYPHER_EDITOR_MAP_HIERARCHY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <utility>
#include <vector>

namespace cypher::editor::map
{

// True for an entity record (it can have and be a parent).
CYPHER_NODISCARD bool MapHierarchy_IsEntity( const map_document_t *pMap, common::u64 id ) noexcept;
// The entity `id` is attached to; 0 when none or `id` is not an entity.
CYPHER_NODISCARD common::u64 MapHierarchy_Parent( const map_document_t *pMap, common::u64 id ) noexcept;
// Every (child, parent) link in the map, in record order; empty for most maps.
CYPHER_NODISCARD std::vector<std::pair<common::u64, common::u64>> MapHierarchy_Links( const map_document_t *pMap );
// The entities attached to `id`: directly, or at any depth (children first,
// then theirs).
CYPHER_NODISCARD std::vector<common::u64> MapHierarchy_Children( const map_document_t *pMap, common::u64 id, bool bRecursive );
// Attaches every entity in `ids` to `parent`, or detaches them (parent 0).
// INVALID_ARGUMENT when one of them or the parent is not an entity, the
// parent is among them, or the parent hangs (at any depth) from one of them,
// which would make a loop. Nothing changes on failure.
CYPHER_NODISCARD map_status_t MapHierarchy_SetParent( map_document_t *pMap, common::span_t<const common::u64> ids, common::u64 parent ) noexcept;
// After a copy (old, new ID pairs): a copied child whose parent was copied
// too is attached to the parent's copy; one copied alone keeps its parent.
CYPHER_NODISCARD map_status_t MapHierarchy_Copied( map_document_t *pMap, const std::vector<std::pair<common::u64, common::u64>> &copies ) noexcept;
// Entities whose parent is not an entity in the map (Check for Problems).
CYPHER_NODISCARD std::vector<common::u64> MapHierarchy_Orphans( const map_document_t *pMap );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_HIERARCHY_H
