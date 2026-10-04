//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_FuzzyMatch.h
//  Purpose: Declares fuzzy matching for every search box in the editor: the
//           command palette, asset and entity browsers, the outliner
//           filter, and the settings search.
//  Details: A query matches when its characters appear in the text in
//           order, ignoring case ("sgs" matches "Show Grid Snap"). Scores
//           reward what people mean when they type fast: letters at word
//           starts, runs of consecutive letters, and a match at the very
//           beginning, so "gs" ranks "Grid Snap" above "Paging Sets". One
//           ranking everywhere means a search that works in one panel works
//           the same in every other.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_FUZZY_MATCH_H
#define CYPHER_EDITOR_CORE_FUZZY_MATCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier1/CypherCommon_StringView.h"

namespace cypher::editor
{

inline constexpr common::i32 EDITOR_FUZZY_NO_MATCH = -1;
inline constexpr common::usize EDITOR_FUZZY_MAX_QUERY = 128u; // Longer queries match nothing.

// Score of query against text; higher is better, EDITOR_FUZZY_NO_MATCH when
// the query's characters do not all appear in order. An empty query matches
// everything with score 0. When pMatched is given it receives, for each
// query character, the index of the text character it matched (for
// highlighting); it must hold query.cchLength entries.
CYPHER_NODISCARD common::i32 EditorFuzzy_Score(
    common::string_view_t text,
    common::string_view_t query,
    common::u32 *pMatched = nullptr ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_FUZZY_MATCH_H
