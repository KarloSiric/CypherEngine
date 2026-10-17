//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Groups.h
//  Purpose: Declares group editing beyond creating and finding groups:
//           names, membership, and keeping groups whole when the objects in
//           them are deleted or copied (CYMAP.md 6.10).
//  Details: Hammer 5 groups "contain arbitrary collections of entities and
//           meshes" that move, rotate and scale as one in Groups mode, while
//           Objects mode reaches the objects inside. TrenchBroom adds
//           membership edits and copies groups with the objects in them.
//           A group lists its direct members (objects and nested groups);
//           a group with no members left is dissolved.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GROUPS_H
#define CYPHER_EDITOR_MAP_GROUPS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <string>
#include <utility>
#include <vector>

namespace cypher::editor::map
{

// Every group in the map, in file order.
CYPHER_NODISCARD std::vector<common::u64> MapGroups_All( const map_document_t *pMap );

// The group that lists `id` directly; 0 when none does.
CYPHER_NODISCARD common::u64 MapGroups_Parent( const map_document_t *pMap, common::u64 id ) noexcept;

// PRIVATE working-copy operation: replace an old object's direct membership
// with freshly created roots, keeping the group record and all its parents.
// No group is created or dissolved. Ungrouped originals are a no-op. As with
// the geometry replacement, discard the working map on any failure.
// Replacement roots must have no existing group parent. Unchanged members
// retain their comments; the original member's comments follow the lowest
// replacement ID exactly once, independent of replacement input order.
CYPHER_NODISCARD map_status_t MapGroups_ReplaceMember( map_document_t *pMap, common::u64 original,
    common::span_t<const common::u64> replacements ) noexcept;

// A group's direct members (objects and nested groups), and its name.
CYPHER_NODISCARD map_status_t MapGroups_Members( const map_document_t *pMap, common::u64 group, std::vector<common::u64> &out );
CYPHER_NODISCARD std::string MapGroups_Name( const map_document_t *pMap, common::u64 group );

// Renames a group (empty clears the name). *pChangedOut: whether it changed.
CYPHER_NODISCARD map_status_t MapGroups_Rename( map_document_t *pMap, common::u64 group, common::string_view_t name, bool *pChangedOut ) noexcept;

// Puts objects (or groups) into `group`, out of the group each was directly
// in. A group cannot go into itself or into a group nested inside it.
CYPHER_NODISCARD map_status_t MapGroups_Add( map_document_t *pMap, common::u64 group, common::span_t<const common::u64> ids, bool *pChangedOut );

// Takes objects (or groups) out of the group each is directly in; a group
// left empty is dissolved.
CYPHER_NODISCARD map_status_t MapGroups_Remove( map_document_t *pMap, common::span_t<const common::u64> ids, bool *pChangedOut );

// Members that no longer exist are dropped and groups left empty dissolve,
// so a group never hands a deleted object to the selection.
CYPHER_NODISCARD map_status_t MapGroups_Prune( map_document_t *pMap, bool *pChangedOut );

// After objects were copied (pairs: original, copy): every group all of
// whose members were copied gets a copy (same name, comment and visgroups)
// holding the copies, nested groups innermost first; each copy then joins
// the copy of its original's group, or that group itself when it was not
// copied (a copy made inside a group stays in it). newGroupsOut receives
// the new groups.
CYPHER_NODISCARD map_status_t MapGroups_Copied( map_document_t *pMap, const std::vector<std::pair<common::u64, common::u64>> &copies,
    std::vector<common::u64> *pNewGroupsOut );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GROUPS_H
