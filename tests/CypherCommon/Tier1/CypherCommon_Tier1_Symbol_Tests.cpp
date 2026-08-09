//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Symbol_Tests.cpp
//  Purpose: Tests collision-safe interned symbol tables.
//  Details: Covers stable identity, text resolution, case policy, table ownership,
//           clear-generation invalidation, statistics, and malformed views.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Symbol.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_symbolAssertCount = 0u;

assert_action_t CaptureSymbolAssert( const assert_info_t & ) noexcept
{
    ++g_symbolAssertCount;
    return assert_action_t::Continue;
}

void RequireSymbolText(
    const symbol_table_t *pTable,
    symbol_t symbol,
    const char *pExpected )
{
    REQUIRE( StringView_Equals(
        SymbolTable_Resolve( pTable, symbol ),
        StringView_FromCString( pExpected ) ) );
}

} // namespace

TEST_CASE( "SymbolTable interns resolves and finds stable symbols",
           "[CypherCommon][Tier1][Symbol]" )
{
    symbol_table_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    desc.nInitialCapacity = 8u;
    symbol_table_t *pTable = SymbolTable_Create( desc );
    REQUIRE( pTable != nullptr );

    const symbol_t renderer = SymbolTable_Intern(
        pTable,
        StringView_FromCString( "renderer" ) );
    const symbol_t material = SymbolTable_Intern(
        pTable,
        StringView_FromCString( "material" ) );
    REQUIRE( Symbol_IsValid( renderer ) );
    REQUIRE( Symbol_IsValid( material ) );
    REQUIRE( renderer.value != material.value );
    REQUIRE( SymbolTable_Intern(
        pTable,
        StringView_FromCString( "renderer" ) ).value == renderer.value );
    REQUIRE( SymbolTable_Find(
        pTable,
        StringView_FromCString( "renderer" ) ).value == renderer.value );
    RequireSymbolText( pTable, renderer, "renderer" );
    RequireSymbolText( pTable, material, "material" );
    REQUIRE( SymbolTable_Contains( pTable, renderer ) );

    const symbol_table_stats_t stats = SymbolTable_Stats( pTable );
    REQUIRE( stats.nSymbols == 2u );
    REQUIRE( stats.cbStringData == 18u );
    REQUIRE( stats.cbReserved >= stats.cbStringData );
    REQUIRE( SymbolTable_IsValid( pTable ) );
    SymbolTable_Destroy( pTable );
}

TEST_CASE( "SymbolTable ASCII-insensitive policy preserves first spelling",
           "[CypherCommon][Tier1][Symbol]" )
{
    symbol_table_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    desc.flags = SYMBOL_TABLE_FLAG_CASE_INSENSITIVE_ASCII;
    symbol_table_t *pTable = SymbolTable_Create( desc );
    REQUIRE( pTable != nullptr );

    const symbol_t original = SymbolTable_Intern(
        pTable,
        StringView_FromCString( "Render.World" ) );
    const symbol_t duplicate = SymbolTable_Intern(
        pTable,
        StringView_FromCString( "render.world" ) );
    REQUIRE( original.value == duplicate.value );
    RequireSymbolText( pTable, original, "Render.World" );
    REQUIRE( SymbolTable_Stats( pTable ).nSymbols == 1u );
    SymbolTable_Destroy( pTable );
}

TEST_CASE( "SymbolTable clear invalidates prior-generation symbols",
           "[CypherCommon][Tier1][Symbol]" )
{
    symbol_table_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    symbol_table_t *pTable = SymbolTable_Create( desc );
    REQUIRE( pTable != nullptr );

    const symbol_t oldSymbol = SymbolTable_Intern(
        pTable,
        StringView_FromCString( "old" ) );
    REQUIRE( SymbolTable_Contains( pTable, oldSymbol ) );
    SymbolTable_Clear( pTable );
    REQUIRE_FALSE( SymbolTable_Contains( pTable, oldSymbol ) );
    REQUIRE( StringView_IsEmpty(
        SymbolTable_Resolve( pTable, oldSymbol ) ) );

    const symbol_t newSymbol = SymbolTable_Intern(
        pTable,
        StringView_FromCString( "new" ) );
    REQUIRE( newSymbol.value != oldSymbol.value );
    REQUIRE( SymbolTable_Contains( pTable, newSymbol ) );
    REQUIRE( SymbolTable_Stats( pTable ).nSymbols == 1u );
    SymbolTable_Destroy( pTable );
}

TEST_CASE( "Symbol values are scoped to their creating table",
           "[CypherCommon][Tier1][Symbol]" )
{
    symbol_table_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    symbol_table_t *pFirst = SymbolTable_Create( desc );
    symbol_table_t *pSecond = SymbolTable_Create( desc );
    REQUIRE( pFirst != nullptr );
    REQUIRE( pSecond != nullptr );

    const symbol_t firstSymbol = SymbolTable_Intern(
        pFirst,
        StringView_FromCString( "first" ) );
    const symbol_t secondSymbol = SymbolTable_Intern(
        pSecond,
        StringView_FromCString( "second" ) );
    REQUIRE( firstSymbol.value == secondSymbol.value );
    RequireSymbolText( pFirst, firstSymbol, "first" );
    RequireSymbolText( pSecond, secondSymbol, "second" );

    SymbolTable_Destroy( pSecond );
    SymbolTable_Destroy( pFirst );
}

TEST_CASE( "SymbolTable rejects malformed borrowed text",
           "[CypherCommon][Tier1][Symbol]" )
{
    symbol_table_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    symbol_table_t *pTable = SymbolTable_Create( desc );
    REQUIRE( pTable != nullptr );

    g_symbolAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSymbolAssert );
    REQUIRE_FALSE( Symbol_IsValid(
        SymbolTable_Intern( pTable, { nullptr, 1u } ) ) );
    REQUIRE_FALSE( Symbol_IsValid(
        SymbolTable_Find( pTable, { nullptr, 1u } ) ) );
    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_symbolAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );

    SymbolTable_Destroy( pTable );
}
