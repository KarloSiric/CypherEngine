//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Compiler_Tests.cpp
//  Purpose: Tests Tier0 compiler identity contracts.
//  Details: These compile-time checks verify that the typed compiler API mirrors
//           the normalized frontend, ABI, version, and language-feature macros.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Compiler.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Compiler information mirrors platform detection", "[CypherCommon][Tier0][Compiler]" )
{
    constexpr compiler_info_t info = Cy_CompilerGetInfo();

    STATIC_REQUIRE( info.type == Cy_CompilerGetType() );
    STATIC_REQUIRE( info.pName[0] != '\0' );
    STATIC_REQUIRE( info.version == Cy_CompilerGetVersion() );
    STATIC_REQUIRE( info.versionMajor == Cy_CompilerGetVersionMajor() );
    STATIC_REQUIRE( info.versionMinor == Cy_CompilerGetVersionMinor() );
    STATIC_REQUIRE( info.versionPatch == Cy_CompilerGetVersionPatch() );
    STATIC_REQUIRE( info.isClangCl == Cy_CompilerIsClangCl() );
    STATIC_REQUIRE( info.usesMsvcAbi == Cy_CompilerUsesMsvcAbi() );
    STATIC_REQUIRE( info.hasExceptions == Cy_CompilerHasExceptions() );
    STATIC_REQUIRE( info.hasRtti == Cy_CompilerHasRtti() );
}

TEST_CASE( "Compiler frontend type is unambiguous", "[CypherCommon][Tier0][Compiler]" )
{
#if CYPHER_COMPILER_MSVC
    STATIC_REQUIRE( Cy_CompilerGetType() == compiler_type_t::Msvc );
#elif CYPHER_COMPILER_CLANG
    STATIC_REQUIRE( Cy_CompilerGetType() == compiler_type_t::Clang );
#elif CYPHER_COMPILER_GCC
    STATIC_REQUIRE( Cy_CompilerGetType() == compiler_type_t::Gcc );
#endif

    STATIC_REQUIRE( Cy_CompilerGetType() != compiler_type_t::Unknown );
}

TEST_CASE( "Compiler version exposes stable components", "[CypherCommon][Tier0][Compiler]" )
{
    STATIC_REQUIRE( Cy_CompilerGetVersion() != 0u );
    STATIC_REQUIRE( Cy_CompilerGetVersionMajor() > 0u );
    STATIC_REQUIRE( Cy_CompilerGetVersionMajor() == static_cast<u32>( CYPHER_COMPILER_VERSION_MAJOR ) );
    STATIC_REQUIRE( Cy_CompilerGetVersionMinor() == static_cast<u32>( CYPHER_COMPILER_VERSION_MINOR ) );
    STATIC_REQUIRE( Cy_CompilerGetVersionPatch() == static_cast<u32>( CYPHER_COMPILER_VERSION_PATCH ) );
}

TEST_CASE( "Compiler ABI and feature helpers expose Boolean values", "[CypherCommon][Tier0][Compiler]" )
{
    STATIC_REQUIRE( Cy_CompilerIsClangCl() == CY_FALSE || Cy_CompilerIsClangCl() == CY_TRUE );
    STATIC_REQUIRE( Cy_CompilerUsesMsvcAbi() == CY_FALSE || Cy_CompilerUsesMsvcAbi() == CY_TRUE );
    STATIC_REQUIRE( Cy_CompilerHasExceptions() == CY_FALSE || Cy_CompilerHasExceptions() == CY_TRUE );
    STATIC_REQUIRE( Cy_CompilerHasRtti() == CY_FALSE || Cy_CompilerHasRtti() == CY_TRUE );

#if CYPHER_COMPILER_CLANG_CL
    STATIC_REQUIRE( Cy_CompilerGetType() == compiler_type_t::Clang );
    STATIC_REQUIRE( Cy_CompilerUsesMsvcAbi() == CY_TRUE );
#endif
}
