//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCVar/CypherCVar_Tests.cpp
//  Purpose: Verifies read-only runtime CVar registry diagnostics.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCVar.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using namespace cypher::engine::cvar;

namespace
{

struct cvar_scope_t {
    cvar_scope_t() { Cvar_Shutdown(); }
    ~cvar_scope_t() { Cvar_Shutdown(); }
};

} // namespace

TEST_CASE( "Runtime CVar registry diagnostics expose lifetime, order, and bounds", "[CVar][Diagnostics]" )
{
    cvar_scope_t scope{};

    REQUIRE_FALSE( Cvar_IsInitialized() );
    REQUIRE( Cvar_Count() == 0u );
    REQUIRE( Cvar_GetByIndex( 0u ) == nullptr );

    REQUIRE( Cvar_Init() == cvar_error_t::OK );
    REQUIRE( Cvar_IsInitialized() );
    REQUIRE( Cvar_Count() == 0u );
    REQUIRE( Cvar_GetByIndex( 0u ) == nullptr );

    REQUIRE( Cvar_Register( "first", "17", CYPHER_CVAR_ARCHIVE ) == cvar_error_t::OK );
    REQUIRE( Cvar_Register( "second", "true", CYPHER_CVAR_READONLY ) == cvar_error_t::OK );
    REQUIRE( Cvar_Count() == 2u );

    const cvar_t *first = Cvar_GetByIndex( 0u );
    const cvar_t *second = Cvar_GetByIndex( 1u );
    REQUIRE( first != nullptr );
    REQUIRE( second != nullptr );
    REQUIRE( std::string( first->name ) == "first" );
    REQUIRE( std::string( first->valueString ) == "17" );
    REQUIRE( std::string( first->defaultString ) == "17" );
    REQUIRE( first->valueInt == 17u );
    REQUIRE( first->flags == CYPHER_CVAR_ARCHIVE );
    REQUIRE( std::string( second->name ) == "second" );
    REQUIRE( second->valueBool );
    REQUIRE( second->flags == CYPHER_CVAR_READONLY );
    REQUIRE( Cvar_GetByIndex( Cvar_Count() ) == nullptr );
    REQUIRE( Cvar_GetByIndex( CYPHER_CVAR_MAX_CVARS ) == nullptr );

    REQUIRE( Cvar_Shutdown() == cvar_error_t::OK );
    REQUIRE_FALSE( Cvar_IsInitialized() );
    REQUIRE( Cvar_Count() == 0u );
    REQUIRE( Cvar_GetByIndex( 0u ) == nullptr );
}

TEST_CASE( "Runtime CVar float cache requires a complete numeric token", "[CVar][Cache]" )
{
    cvar_scope_t scope{};
    REQUIRE( Cvar_Init() == cvar_error_t::OK );

    REQUIRE( Cvar_Register( "value", "info", CYPHER_CVAR_NONE ) == cvar_error_t::OK );
    REQUIRE( Cvar_GetFloat( "value" ) == 0.0f );

    REQUIRE( Cvar_Set( "value", "12.5ms" ) == cvar_error_t::OK );
    REQUIRE( Cvar_GetFloat( "value" ) == 0.0f );
    REQUIRE( Cvar_Set( "value", "12.5" ) == cvar_error_t::OK );
    REQUIRE( Cvar_GetFloat( "value" ) == 12.5f );
    REQUIRE( Cvar_Set( "value", "1e1000" ) == cvar_error_t::OK );
    REQUIRE( std::isinf( Cvar_GetFloat( "value" ) ) );
}

TEST_CASE( "Runtime CVar modified flag tracks the current value against its default", "[CVar][Flags]" )
{
    cvar_scope_t scope{};
    REQUIRE( Cvar_Init() == cvar_error_t::OK );
    REQUIRE( Cvar_Register( "value", "default", CYPHER_CVAR_ARCHIVE ) == cvar_error_t::OK );

    REQUIRE( Cvar_Set( "value", "default" ) == cvar_error_t::OK );
    REQUIRE( Cvar_Find( "value" )->flags == CYPHER_CVAR_ARCHIVE );

    REQUIRE( Cvar_Set( "value", "changed" ) == cvar_error_t::OK );
    REQUIRE( Cvar_Find( "value" )->flags ==
        static_cast<flags_t>( CYPHER_CVAR_ARCHIVE | CYPHER_CVAR_MODIFIED ) );

    REQUIRE( Cvar_Set( "value", "changed" ) == cvar_error_t::OK );
    REQUIRE( Cvar_Find( "value" )->flags ==
        static_cast<flags_t>( CYPHER_CVAR_ARCHIVE | CYPHER_CVAR_MODIFIED ) );

    REQUIRE( Cvar_Set( "value", "default" ) == cvar_error_t::OK );
    REQUIRE( Cvar_Find( "value" )->flags == CYPHER_CVAR_ARCHIVE );
}
