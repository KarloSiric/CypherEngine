//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CharacterSet.h
//  Purpose: Declares CypherCommon Tier1 CharacterSet support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CHARACTERSET_H
#define CYPHER_COMMON_TIER1_CHARACTERSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Character Set

Byte-oriented character set declarations used by tokenizers and parsers.
All char arguments are interpreted as unsigned byte values in the [0, 255] domain.
================
*/

#include "CypherCommon_Tier0.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

constexpr usize CY_CHARACTER_SET_VALUE_COUNT    = 256u; // Complete unsigned-byte domain.
constexpr usize CY_CHARACTER_SET_WORD_BITS      = 64u;  // Membership bits stored per word.
constexpr usize CY_CHARACTER_SET_WORD_COUNT     = CY_CHARACTER_SET_VALUE_COUNT / CY_CHARACTER_SET_WORD_BITS;

static_assert( CY_CHARACTER_SET_VALUE_COUNT % CY_CHARACTER_SET_WORD_BITS == 0u,
               "character_set_t storage must cover complete words." );

// Fixed 256 bit set where each bit represents one possible byte value.
struct character_set_t {
    u64 bitWords[CY_CHARACTER_SET_WORD_COUNT]{}; // Bit N records membership of byte value N.
};

static_assert( sizeof( character_set_t ) == 32u, "character_set_t must remain a compact 256-bit value." );

/*
================
State
================
*/

// Removes every value from the set.
CYPHER_COMMON_API void CharacterSet_Clear(
    character_set_t *pSet ) noexcept;

// Adds all 256 possible byte values.
CYPHER_COMMON_API void CharacterSet_Fill(
    character_set_t *pSet ) noexcept;

// Returns true when the set contains no values.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_IsEmpty(
    const character_set_t *pSet ) noexcept;

// Returns true when the set contains all 256 values.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_IsFull(
    const character_set_t *pSet ) noexcept;

// Returns the number of values currently contained in the set.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CharacterSet_Count(
    const character_set_t *pSet ) noexcept;

// Returns true when both sets contain exactly the same values.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_Equals(
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept;

/*
================
Individual Values
================
*/

// Adds one byte value to the set.
CYPHER_COMMON_API void CharacterSet_Add(
    character_set_t *pSet,
    char chValue ) noexcept;

// Removes one byte value from the set.
CYPHER_COMMON_API void CharacterSet_Remove(
    character_set_t *pSet,
    char chValue ) noexcept;

// Returns true when the set contains chValue.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_Contains(
    const character_set_t *pSet,
    char chValue ) noexcept;

/*
================
Ranges
================
*/

// Adds every byte in the inclusive [chFirst, chLast] range.
CYPHER_COMMON_API void CharacterSet_AddRange(
    character_set_t *pSet,
    char chFirst,
    char chLast ) noexcept;

// Removes every byte in the inclusive [chFirst, chLast] range.
CYPHER_COMMON_API void CharacterSet_RemoveRange(
    character_set_t *pSet,
    char chFirst,
    char chLast ) noexcept;

/*
================
String Views
================
*/

// Adds every byte represented by view.
CYPHER_COMMON_API void CharacterSet_AddView(
    character_set_t *pSet,
    string_view_t view ) noexcept;

// Removes every byte represented by view.
CYPHER_COMMON_API void CharacterSet_RemoveView(
    character_set_t *pSet,
    string_view_t view ) noexcept;

// Returns true when at least one byte in view belongs to the set.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_ContainsAny(
    const character_set_t *pSet,
    string_view_t view ) noexcept;

// Returns true when every byte in view belongs to the set; an empty view succeeds.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_ContainsAll(
    const character_set_t *pSet,
    string_view_t view ) noexcept;

/*
================
Set Algebra
================
*/

// Writes every value found in either set into pOut.
CYPHER_COMMON_API void CharacterSet_Union(
    character_set_t *pOut,
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept;

// Writes only values found in both sets into pOut.
CYPHER_COMMON_API void CharacterSet_Intersection(
    character_set_t *pOut,
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept;

// Writes values found in A but not B into pOut.
CYPHER_COMMON_API void CharacterSet_Difference(
    character_set_t *pOut,
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept;

// Writes the inverse of pInput into pOut.
CYPHER_COMMON_API void CharacterSet_Invert(
    character_set_t *pOut,
    const character_set_t *pInput ) noexcept;

// Returns true when the sets share at least one value.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_Intersects(
    const character_set_t *pSetA,
    const character_set_t *pSetB ) noexcept;

// Returns true when every value in pSubset also exists in pSuperset.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CharacterSet_IsSubset(
    const character_set_t *pSubset,
    const character_set_t *pSuperset ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CHARACTERSET_H
