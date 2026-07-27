//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Platform_Tests.cpp
//  Purpose: Tests Tier0 platform detection contracts.
//  Details: These compile-time checks ensure the active compiler, operating
//           system, architecture, byte order, and build configuration resolve
//           to one internally consistent target description.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Platform.h"

#include <bit>

#include <catch2/catch_test_macros.hpp>

TEST_CASE( "Platform selects exactly one compiler frontend", "[CypherCommon][Tier0][Platform]" )
{
    STATIC_REQUIRE( CYPHER_COMPILER_MSVC + CYPHER_COMPILER_CLANG + CYPHER_COMPILER_GCC == 1 );
    STATIC_REQUIRE( CYPHER_COMPILER_CLANG_CL == 0 || CYPHER_COMPILER_CLANG_CL == 1 );
    STATIC_REQUIRE( CYPHER_COMPILER_MSVC_ABI == 0 || CYPHER_COMPILER_MSVC_ABI == 1 );
    STATIC_REQUIRE( CYPHER_COMPILER_VERSION_MAJOR > 0 );
    STATIC_REQUIRE( CYPHER_COMPILER_VERSION != 0 );
    STATIC_REQUIRE( CYPHER_COMPILER_NAME[0] != '\0' );

#if defined( __clang__ ) && defined( _MSC_VER )
    STATIC_REQUIRE( CYPHER_COMPILER_CLANG == 1 );
    STATIC_REQUIRE( CYPHER_COMPILER_CLANG_CL == 1 );
    STATIC_REQUIRE( CYPHER_COMPILER_MSVC_ABI == 1 );
#elif defined( __clang__ )
    STATIC_REQUIRE( CYPHER_COMPILER_CLANG == 1 );
    STATIC_REQUIRE( CYPHER_COMPILER_CLANG_CL == 0 );
#elif defined( _MSC_VER )
    STATIC_REQUIRE( CYPHER_COMPILER_MSVC == 1 );
    STATIC_REQUIRE( CYPHER_COMPILER_MSVC_ABI == 1 );
#elif defined( __GNUC__ )
    STATIC_REQUIRE( CYPHER_COMPILER_GCC == 1 );
#endif
}

TEST_CASE( "Platform selects one supported operating system", "[CypherCommon][Tier0][Platform]" )
{
    STATIC_REQUIRE( CYPHER_PLATFORM_WINDOWS + CYPHER_PLATFORM_LINUX + CYPHER_PLATFORM_MACOS == 1 );
    STATIC_REQUIRE( CYPHER_PLATFORM_POSIX == 0 || CYPHER_PLATFORM_POSIX == 1 );
    STATIC_REQUIRE( CYPHER_PLATFORM_NAME[0] != '\0' );

#if CYPHER_PLATFORM_WINDOWS
    STATIC_REQUIRE( CYPHER_PLATFORM_POSIX == 0 );
#else
    STATIC_REQUIRE( CYPHER_PLATFORM_POSIX == 1 );
#endif
}

TEST_CASE( "Platform architecture and pointer width agree", "[CypherCommon][Tier0][Platform]" )
{
    STATIC_REQUIRE( CYPHER_ARCH_X64 + CYPHER_ARCH_X86 + CYPHER_ARCH_ARM64 + CYPHER_ARCH_ARM32 == 1 );
    STATIC_REQUIRE( CYPHER_ARCH_X86_FAMILY + CYPHER_ARCH_ARM_FAMILY == 1 );
    STATIC_REQUIRE( CYPHER_ARCH_NAME[0] != '\0' );
    STATIC_REQUIRE( CYPHER_ARCH_X86 == 0 );
    STATIC_REQUIRE( CYPHER_ARCH_ARM32 == 0 );
    STATIC_REQUIRE( CYPHER_TARGET_64BIT == 1 );
    STATIC_REQUIRE( CYPHER_TARGET_32BIT == 0 );
    STATIC_REQUIRE( CYPHER_POINTER_SIZE == sizeof( void * ) );
}

TEST_CASE( "Platform byte order matches the C++20 implementation", "[CypherCommon][Tier0][Platform]" )
{
    STATIC_REQUIRE( CYPHER_ENDIAN_LITTLE + CYPHER_ENDIAN_BIG == 1 );
    STATIC_REQUIRE( CYPHER_ENDIAN_LITTLE == 1 );
    STATIC_REQUIRE( CYPHER_ENDIAN_BIG == 0 );
    STATIC_REQUIRE( std::endian::native == std::endian::little );
}

TEST_CASE( "Platform exposes one build configuration", "[CypherCommon][Tier0][Platform]" )
{
    STATIC_REQUIRE(
        CYPHER_CONFIG_DEBUG + CYPHER_CONFIG_DEVELOPMENT + CYPHER_CONFIG_RELEASE + CYPHER_CONFIG_SHIPPING == 1
    );
    STATIC_REQUIRE(
        CYPHER_BUILD_DEBUG + CYPHER_BUILD_DEVELOPMENT + CYPHER_BUILD_RELEASE + CYPHER_BUILD_SHIPPING == 1
    );
    STATIC_REQUIRE( CYPHER_BUILD_DEBUG == CYPHER_CONFIG_DEBUG );
    STATIC_REQUIRE( CYPHER_BUILD_DEVELOPMENT == CYPHER_CONFIG_DEVELOPMENT );
    STATIC_REQUIRE( CYPHER_BUILD_RELEASE == CYPHER_CONFIG_RELEASE );
    STATIC_REQUIRE( CYPHER_BUILD_SHIPPING == CYPHER_CONFIG_SHIPPING );
    STATIC_REQUIRE( CYPHER_BUILD_OPTIMIZED == 0 || CYPHER_BUILD_OPTIMIZED == 1 );
}

TEST_CASE( "Platform reports the required C++ language features", "[CypherCommon][Tier0][Platform]" )
{
    STATIC_REQUIRE( CYPHER_CPP_STANDARD >= 202002L );
    STATIC_REQUIRE( CYPHER_CPP_EXCEPTIONS == 0 || CYPHER_CPP_EXCEPTIONS == 1 );
    STATIC_REQUIRE( CYPHER_CPP_RTTI == 0 || CYPHER_CPP_RTTI == 1 );
}
