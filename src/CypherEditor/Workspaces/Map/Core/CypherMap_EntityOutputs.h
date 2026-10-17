//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_EntityOutputs.h
//  Purpose: Declares reading and writing an entity's I/O connections
//           (CYMAP.md 6.2 `outputs`): Hammer's Outputs tab.
//  Details: An entity's outputs are read into owned values and written back
//           as a whole list, in firing order, with all six members, so an
//           edit, insertion, removal or reorder is one write. Inputs (the
//           connections that target an entity) need no storage of their
//           own: the wireframe resolves them (map_wire_connection_t).
//
//           Write is a PRIVATE working-copy operation as CypherMap_MeshEdit.h.
//
//  History:
//  - Created by Karlo Siric on 2026-10-06
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_ENTITY_OUTPUTS_H
#define CYPHER_EDITOR_MAP_ENTITY_OUTPUTS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"

#include <string>
#include <vector>

namespace cypher::editor::map
{

inline constexpr common::usize MAP_ENTITY_OUTPUTS_MAX = 256u; // CYMAP.md budgets.
inline constexpr common::usize MAP_ENTITY_OUTPUT_TEXT_MAX = 256u;

struct map_output_t {
    std::string output;    // The output name on this entity's class.
    std::string target;    // A target name, `name*` prefix, or `!self` / `!activator` / `!caller` / `!player`.
    std::string input;     // The input name on the target's class.
    std::string parameter; // Passed to the input; empty passes the output's own value.
    common::f64 delay{ 0.0 };
    common::i64 times{ -1 }; // -1: unlimited.
};

// The entity's outputs in firing order (none is an empty list). Members a
// file left out read as their defaults.
CYPHER_NODISCARD map_status_t MapEntityOutputs_Read( const map_document_t *pMap, common::u64 entity,
    std::vector<map_output_t> &outputsOut );

// Replaces the entity's outputs. Text up to MAP_ENTITY_OUTPUT_TEXT_MAX
// bytes (names may be empty while being typed: the wireframe marks such a
// connection invalid), delay finite and >= 0, times -1 or >= 1. An empty
// list removes the member. *pChangedOut is false when the list was already
// exactly this.
CYPHER_NODISCARD map_status_t MapEntityOutputs_Write( map_document_t *pMap, common::u64 entity,
    const std::vector<map_output_t> &outputs, bool *pChangedOut );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_ENTITY_OUTPUTS_H
