//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Cell.h
//  Purpose: Declares map cells: the deterministic spatial key that, with a
//           layer, decides which chunk file an object is written to.
//  Details: A cell is either `global` (objects without a position) or an
//           integer index per axis the map cuts: floor(position / size).
//           Its text form names the chunk file: "global", "x0_y-1", or
//           "x0_y-1_z2" when the z axis is cut (CYMAP.md section 5). Every
//           editor computes the same cell for the same object, so maps save
//           to the same chunk set everywhere.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_CELL_H
#define CYPHER_EDITOR_MAP_CELL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Mathlib/CypherMath_Vector3.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

namespace cypher::editor::map
{

inline constexpr common::usize MAP_CELL_NAME_CAPACITY = 72u;
inline constexpr common::i64 MAP_CELL_INDEX_LIMIT = 1'000'000'000; // Keeps names and math bounded.

struct map_cell_t {
    common::bool_t bGlobal{ common::CY_TRUE };
    common::u8 axes{ 0u };            // Bit n set: axis n is cut (x = 1, y = 2, z = 4).
    common::i64 index[3]{ 0, 0, 0 };  // Valid only for cut axes.
};

// Cell sizes per axis; 0 leaves the axis uncut. All zero puts every
// positioned object in the global cell as well.
struct map_cell_grid_t {
    common::f64 size[3]{ 8192.0, 8192.0, 0.0 };
};

CYPHER_NODISCARD map_cell_t MapCell_Global() noexcept;

// The cell holding a position. A non-finite position, or one whose index
// would leave MAP_CELL_INDEX_LIMIT, falls back to global.
CYPHER_NODISCARD map_cell_t MapCell_ForPosition( const map_cell_grid_t &grid, math::vec3d_t position ) noexcept;

CYPHER_NODISCARD common::bool_t MapCell_Equals( const map_cell_t &a, const map_cell_t &b ) noexcept;

// Total order: global first, then by cut-axis mask, then x, y, z.
CYPHER_NODISCARD common::i32 MapCell_Compare( const map_cell_t &a, const map_cell_t &b ) noexcept;

// Writes the chunk file stem ("global", "x0_y-1", ...). Returns its length.
common::usize MapCell_FormatName( const map_cell_t &cell, char ( &buffer )[MAP_CELL_NAME_CAPACITY] ) noexcept;

// Parses a stem written by MapCell_FormatName.
CYPHER_NODISCARD common::bool_t MapCell_ParseName( common::string_view_t name, map_cell_t *pCellOut ) noexcept;

// Reads a chunk's `cell` member: "global" or an array of two or three
// integers (x, y[, z]). Two integers mean x and y are cut.
CYPHER_NODISCARD common::bool_t MapCell_Read( const common::key_value_t *pValue, map_cell_t *pCellOut ) noexcept;

// Writes a cell as a chunk's `cell` member value into an existing node.
CYPHER_NODISCARD common::bool_t MapCell_Write(
    common::key_value_document_t *pDocument,
    common::key_value_t *pValue,
    const map_cell_t &cell ) noexcept;

// Whether the grid's cut axes are exactly the cell's cut axes.
CYPHER_NODISCARD common::bool_t MapCell_MatchesGrid( const map_cell_t &cell, const map_cell_grid_t &grid ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_CELL_H
