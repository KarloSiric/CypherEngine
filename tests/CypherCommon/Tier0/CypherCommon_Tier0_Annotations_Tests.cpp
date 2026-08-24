//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Annotations_Tests.cpp
//  Purpose: Compile-tests Tier0 API annotations.
//  Details: These probes ensure pointer, buffer, string, format, non-null, and
//           return-value annotations remain valid on every supported compiler.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Annotations.h"

#include <cstdarg>
#include <cstring>
#include <cstdio>

#include <catch2/catch_test_macros.hpp>

namespace
{

CYPHER_NORETURN void NeverReturnsAnnotated();

void CopyAnnotated(
    CY_OUT_WRITES( cchDest ) char *pDest,
    std::size_t cchDest,
    CY_IN_Z const char *pSource )
{
    if ( cchDest != 0u ) {
        pDest[0] = pSource[0];
    }
}

int FormatAnnotated( char *pDest, std::size_t cchDest, CY_PRINTF_FORMAT_STRING const char *pFormat, ... )
    CY_PRINTF_LIKE( 3, 4 );

int FormatAnnotated( char *pDest, std::size_t cchDest, const char *pFormat, ... )
{
    std::va_list args;
    va_start( args, pFormat );
    const int cchWritten = std::vsnprintf( pDest, cchDest, pFormat, args );
    va_end( args );
    return cchWritten;
}

CY_RETURNS_NONNULL const char *ReturnAnnotated();

const char *ReturnAnnotated()
{
    return "Cypher";
}

std::size_t LengthAnnotated( const char *pText ) CY_NONNULL_ARGS( 1 );

std::size_t LengthAnnotated( const char *pText )
{
    return std::strlen( pText );
}

} // namespace

TEST_CASE( "Annotations preserve annotated API behavior", "[CypherCommon][Tier0][Annotations]" )
{
    char szDest[32] = {};
    CopyAnnotated( szDest, sizeof( szDest ), "A" );
    REQUIRE( szDest[0] == 'A' );

    REQUIRE( FormatAnnotated( szDest, sizeof( szDest ), "%s-%d", "value", 7 ) > 0 );
    REQUIRE( ReturnAnnotated()[0] == 'C' );
    REQUIRE( LengthAnnotated( "Cypher" ) == 6u );
}
