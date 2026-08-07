//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Symbol.h
//  Purpose: Declares collision-safe interned symbols and symbol tables.
//  Details: A symbol is meaningful only with the table that created it. Resolved text
//           remains stable until that table is cleared or destroyed.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SYMBOL_H
#define CYPHER_COMMON_TIER1_SYMBOL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct symbol_t {
    u32 value{ 0u };
};

constexpr symbol_t CY_SYMBOL_INVALID{};

enum symbol_table_flags_t : flags32_t {
    SYMBOL_TABLE_FLAG_NONE                    = 0u,
    SYMBOL_TABLE_FLAG_CASE_INSENSITIVE_ASCII  = CYPHER_BIT32( 0 )
};

struct symbol_table_desc_t {
    const allocator_t *pAllocator{ nullptr };
    usize nInitialCapacity{ 256u };
    flags32_t flags{ SYMBOL_TABLE_FLAG_NONE };
};

struct symbol_table_stats_t {
    usize nSymbols{ 0u };
    usize cbStringData{ 0u };
    usize cbReserved{ 0u };
};

struct symbol_table_t;

CYPHER_NODISCARD CYPHER_COMMON_API
symbol_table_t *SymbolTable_Create( const symbol_table_desc_t &desc ) noexcept;

CYPHER_COMMON_API void SymbolTable_Destroy( symbol_table_t *pTable ) noexcept;

CYPHER_COMMON_API void SymbolTable_Clear( symbol_table_t *pTable ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
symbol_t SymbolTable_Intern(
    symbol_table_t *pTable,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
symbol_t SymbolTable_Find(
    const symbol_table_t *pTable,
    string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t SymbolTable_Resolve(
    const symbol_table_t *pTable,
    symbol_t symbol ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Symbol_IsValid( symbol_t symbol ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SymbolTable_Contains(
    const symbol_table_t *pTable,
    symbol_t symbol ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
symbol_table_stats_t SymbolTable_Stats( const symbol_table_t *pTable ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SYMBOL_H
