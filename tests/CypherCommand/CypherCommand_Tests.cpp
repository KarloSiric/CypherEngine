//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// File: tests/CypherCommand/CypherCommand_Tests.cpp
// Purpose: Verify runtime command dispatch, bounds, and registry lifetime.
//////////////////////////////////////////////////////////////////////////

#include "CypherCommand.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using namespace cypher::engine::cmd;
using cypher::engine::common::u32;

namespace
{

struct command_scope_t {
    command_scope_t() { Cmd_Shutdown(); }
    ~command_scope_t() { Cmd_Shutdown(); }
};

struct capture_t {
    u32 calls{ 0u };
    std::vector<std::string> arguments;
};

void Capture( void *context, u32 argc, char **argv )
{
    auto &capture = *static_cast<capture_t *>( context );
    ++capture.calls;
    capture.arguments.clear();
    for ( u32 i = 0u; i < argc; ++i ) {
        capture.arguments.emplace_back( argv[i] );
    }
}

} // namespace

TEST_CASE( "Runtime command lifecycle and dispatch preserve callback context", "[Command]" )
{
    command_scope_t scope{};
    capture_t capture{};
    REQUIRE( Cmd_Register( "capture", Capture, &capture, "test" ) == cmd_error_t::ERR_NOT_INIT );
    REQUIRE( Cmd_Execute( "capture" ) == cmd_error_t::ERR_NOT_INIT );
    REQUIRE( Cmd_Init() == cmd_error_t::OK );
    REQUIRE( Cmd_Init() == cmd_error_t::ERR_IS_INIT );
    REQUIRE( Cmd_Register( nullptr, Capture, &capture, nullptr ) == cmd_error_t::ERR_INVALID_COMMAND );
    REQUIRE( Cmd_Register( "capture", nullptr, &capture, nullptr ) == cmd_error_t::ERR_INVALID_CALLBACK );
    REQUIRE( Cmd_Register( "capture", Capture, &capture, "test" ) == cmd_error_t::OK );
    REQUIRE( Cmd_Register( "capture", Capture, nullptr, nullptr ) == cmd_error_t::ERR_COMMAND_ALREADY_EXISTS );
    REQUIRE( Cmd_Execute( "\t capture  first\tsecond\r\n" ) == cmd_error_t::OK );
    REQUIRE( capture.calls == 1u );
    REQUIRE( capture.arguments == std::vector<std::string>{ "capture", "first", "second" } );
    REQUIRE( Cmd_Execute( "missing" ) == cmd_error_t::ERR_COMMAND_NOT_FOUND );
    REQUIRE( capture.calls == 1u );
    Cmd_Shutdown();
    REQUIRE( Cmd_Find( "capture" ) == nullptr );
    REQUIRE( Cmd_Init() == cmd_error_t::OK );
    REQUIRE( Cmd_Find( "capture" ) == nullptr );
}

TEST_CASE( "Runtime command registry diagnostics expose lifetime, order, and bounds", "[Command][Diagnostics]" )
{
    command_scope_t scope{};
    capture_t capture{};

    REQUIRE_FALSE( Cmd_IsInitialized() );
    REQUIRE( Cmd_Count() == 0u );
    REQUIRE( Cmd_GetByIndex( 0u ) == nullptr );

    REQUIRE( Cmd_Init() == cmd_error_t::OK );
    REQUIRE( Cmd_IsInitialized() );
    REQUIRE( Cmd_Count() == 0u );
    REQUIRE( Cmd_GetByIndex( 0u ) == nullptr );

    REQUIRE( Cmd_Register( "first", Capture, &capture, "first command" ) == cmd_error_t::OK );
    REQUIRE( Cmd_Register( "second", Capture, nullptr, "second command" ) == cmd_error_t::OK );
    REQUIRE( Cmd_Count() == 2u );

    const cmd_t *first = Cmd_GetByIndex( 0u );
    const cmd_t *second = Cmd_GetByIndex( 1u );
    REQUIRE( first != nullptr );
    REQUIRE( second != nullptr );
    REQUIRE( std::string( first->name ) == "first" );
    REQUIRE( first->pCallbackFn == Capture );
    REQUIRE( first->pExtraData == &capture );
    REQUIRE( std::string( first->description ) == "first command" );
    REQUIRE( std::string( second->name ) == "second" );
    REQUIRE( second->pExtraData == nullptr );
    REQUIRE( Cmd_GetByIndex( Cmd_Count() ) == nullptr );
    REQUIRE( Cmd_GetByIndex( CYPHER_COMMAND_MAX_COMMANDS ) == nullptr );

    Cmd_Shutdown();
    REQUIRE_FALSE( Cmd_IsInitialized() );
    REQUIRE( Cmd_Count() == 0u );
    REQUIRE( Cmd_GetByIndex( 0u ) == nullptr );
}

TEST_CASE( "Runtime command rejects oversized lines without dispatching a prefix", "[Command][Bounds]" )
{
    command_scope_t scope{};
    capture_t capture{};
    REQUIRE( Cmd_Init() == cmd_error_t::OK );
    REQUIRE( Cmd_Register( "capture", Capture, &capture, nullptr ) == cmd_error_t::OK );

    // The established dispatch buffer holds 1,023 input bytes plus its terminator.
    std::string line = "capture ";
    line.append( 1023u - line.size(), 'x' );
    REQUIRE( Cmd_Execute( line.c_str() ) == cmd_error_t::OK );
    REQUIRE( capture.arguments[1].size() == 1015u );
    REQUIRE( capture.calls == 1u );
    line += 'x';
    REQUIRE( Cmd_Execute( line.c_str() ) == cmd_error_t::ERR_PARSE_FAILED );
    REQUIRE( capture.calls == 1u );
}

TEST_CASE( "Runtime command argument capacity never dispatches partial input", "[Command][Bounds]" )
{
    command_scope_t scope{};
    capture_t capture{};
    REQUIRE( Cmd_Init() == cmd_error_t::OK );
    REQUIRE( Cmd_Register( "capture", Capture, &capture, nullptr ) == cmd_error_t::OK );
    std::string line = "capture";
    for ( u32 i = 1u; i < CYPHER_COMMAND_MAX_ARGUMENTS; ++i ) {
        line += " arg";
    }
    REQUIRE( Cmd_Execute( line.c_str() ) == cmd_error_t::OK );
    REQUIRE( capture.arguments.size() == CYPHER_COMMAND_MAX_ARGUMENTS );
    line += " excess";
    REQUIRE( Cmd_Execute( line.c_str() ) == cmd_error_t::ERR_PARSE_FAILED );
    REQUIRE( capture.calls == 1u );
}

TEST_CASE( "Runtime command parser validates output storage and clears failed counts", "[Command][Parse]" )
{
    char *arguments[CYPHER_COMMAND_MAX_ARGUMENTS]{};
    u32 count = 123u;
    REQUIRE( Cmd_Parse( nullptr, count, arguments ) == cmd_error_t::ERR_INVALID_COMMAND );
    REQUIRE( count == 0u );
    char empty[] = "";
    REQUIRE( Cmd_Parse( empty, count, arguments ) == cmd_error_t::ERR_INVALID_COMMAND );
    REQUIRE( count == 0u );
    char line[] = "capture value";
    REQUIRE( Cmd_Parse( line, count, nullptr ) == cmd_error_t::ERR_PARSE_FAILED );
    REQUIRE( count == 0u );
    REQUIRE( std::string( line ) == "capture value" );
    REQUIRE( Cmd_Parse( line, count, arguments ) == cmd_error_t::OK );
    REQUIRE( count == 2u );
    REQUIRE( std::string( arguments[0] ) == "capture" );
    REQUIRE( std::string( arguments[1] ) == "value" );
}

TEST_CASE( "Runtime command registry exhaustion preserves existing registrations", "[Command]" )
{
    // Borrowed names outlive the registry, including cleanup after failed assertions.
    std::array<std::string, CYPHER_COMMAND_MAX_COMMANDS> names;
    capture_t capture{};
    command_scope_t scope{};
    REQUIRE( Cmd_Init() == cmd_error_t::OK );
    for ( u32 i = 0u; i < names.size(); ++i ) {
        names[i] = "command_" + std::to_string( i );
        REQUIRE( Cmd_Register( names[i].c_str(), Capture, &capture, nullptr ) == cmd_error_t::OK );
    }
    REQUIRE( Cmd_Register( "overflow", Capture, &capture, nullptr ) == cmd_error_t::ERR_REGISTRY_FULL );
    REQUIRE( Cmd_Find( "overflow" ) == nullptr );
    REQUIRE( Cmd_Execute( names.front().c_str() ) == cmd_error_t::OK );
    REQUIRE( Cmd_Execute( names.back().c_str() ) == cmd_error_t::OK );
    REQUIRE( capture.calls == 2u );
}
