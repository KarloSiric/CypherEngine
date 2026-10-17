//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_SelectionSets.h
//  Purpose: Declares editing Hammer 5's selection sets: named lists of
//           objects kept in the map root (`selection_sets`, CYMAP.md),
//           made from the selection (Ctrl+R), grown, shrunk, renamed and
//           removed.
//
//  History:
//  - Created by Karlo Siric on 2026-10-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_SELECTION_SETS_H
#define CYPHER_EDITOR_MAP_SELECTION_SETS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <string>
#include <vector>

namespace cypher::editor::map
{

struct map_selection_set_t {
    common::u64 id{ 0u };
    std::string name{};
    std::vector<common::u64> members{};
};

CYPHER_NODISCARD std::vector<map_selection_set_t> MapSelectionSets_All( const map_document_t *pMap );

// A new set of `members` (sorted, without repeats) named `name`.
CYPHER_NODISCARD map_status_t MapSelectionSets_New( map_document_t *pMap, common::string_view_t name, common::span_t<const common::u64> members,
    common::u64 *pIdOut ) noexcept;
// Adds and removes members; *pChangedOut whether the set changed.
CYPHER_NODISCARD map_status_t MapSelectionSets_Edit( map_document_t *pMap, common::u64 set, common::span_t<const common::u64> add,
    common::span_t<const common::u64> remove, bool *pChangedOut ) noexcept;
CYPHER_NODISCARD map_status_t MapSelectionSets_Rename( map_document_t *pMap, common::u64 set, common::string_view_t name, bool *pChangedOut ) noexcept;
CYPHER_NODISCARD map_status_t MapSelectionSets_Delete( map_document_t *pMap, common::u64 set ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_SELECTION_SETS_H
