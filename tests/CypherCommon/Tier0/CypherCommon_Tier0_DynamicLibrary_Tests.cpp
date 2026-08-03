//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_DynamicLibrary_Tests.cpp
//  Purpose: Tests Tier0 dynamic-library lifecycle and symbol lookup.
//  Details: These tests validate invalid input, per-handle diagnostics, loading a
//           host system library, symbol resolution, duplicate load, and unload.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_DynamicLibrary.h"
#include "CypherCommon_Platform.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

#if CYPHER_PLATFORM_WINDOWS
constexpr const char *SYSTEM_LIBRARY = "kernel32.dll";
constexpr const char *KNOWN_SYMBOL = "GetCurrentProcessId";
#elif CYPHER_PLATFORM_MACOS
constexpr const char *SYSTEM_LIBRARY = "/usr/lib/libSystem.B.dylib";
constexpr const char *KNOWN_SYMBOL = "malloc";
#elif CYPHER_PLATFORM_LINUX
constexpr const char *SYSTEM_LIBRARY = "libc.so.6";
constexpr const char *KNOWN_SYMBOL = "malloc";
#endif

} // namespace

TEST_CASE( "DynamicLibrary validates lifecycle and records errors", "[CypherCommon][Tier0][DynamicLibrary]" )
{
    REQUIRE_FALSE( Cy_DynamicLibraryInit( nullptr ) );
    REQUIRE_FALSE( Cy_DynamicLibraryLoad( nullptr, "missing" ) );

    dynamic_library_t library{};
    REQUIRE( Cy_DynamicLibraryInit( &library ) );
    REQUIRE_FALSE( Cy_DynamicLibraryIsLoaded( &library ) );
    REQUIRE_FALSE( Cy_DynamicLibraryLoad( &library, "" ) );
    REQUIRE( Cy_DynamicLibraryGetLastError( &library )[0] != '\0' );
    REQUIRE_FALSE( Cy_DynamicLibraryLoadEx( &library, SYSTEM_LIBRARY, CY_U32_MAX ) );
    REQUIRE( Cy_DynamicLibraryGetLastError( &library )[0] != '\0' );
    REQUIRE_FALSE(
        Cy_DynamicLibraryLoad( &library, "/cypher/path/that/does/not/exist" ) );
    REQUIRE( Cy_DynamicLibraryGetLastError( &library )[0] != '\0' );
    REQUIRE( Cy_DynamicLibraryUnload( &library ) );
}

TEST_CASE( "DynamicLibrary loads resolves and unloads a host library", "[CypherCommon][Tier0][DynamicLibrary]" )
{
    dynamic_library_t library{};
    REQUIRE( Cy_DynamicLibraryInit( &library ) );
    REQUIRE( Cy_DynamicLibraryLoad( &library, SYSTEM_LIBRARY ) );
    REQUIRE( Cy_DynamicLibraryIsLoaded( &library ) );
    REQUIRE( Cy_DynamicLibraryGetSymbol( &library, KNOWN_SYMBOL ) != nullptr );

    REQUIRE_FALSE( Cy_DynamicLibraryLoad( &library, SYSTEM_LIBRARY ) );
    REQUIRE( Cy_DynamicLibraryIsLoaded( &library ) );

    REQUIRE( Cy_DynamicLibraryGetSymbol( &library, "cypher_missing_symbol" ) == nullptr );
    REQUIRE( Cy_DynamicLibraryGetLastError( &library )[0] != '\0' );

    REQUIRE( Cy_DynamicLibraryUnload( &library ) );
    REQUIRE_FALSE( Cy_DynamicLibraryIsLoaded( &library ) );
    REQUIRE( Cy_DynamicLibraryUnload( &library ) );
}
