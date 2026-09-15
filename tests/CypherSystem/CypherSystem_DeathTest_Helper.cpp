//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherSystem/CypherSystem_DeathTest_Helper.cpp
//  Purpose: Runs terminating CypherSystem operations outside the test process.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Public.h"

#include <cstdarg>
#include <cstring>

namespace
{

void PrintThroughVList( const char *format, ... )
{
    std::va_list arguments;
    va_start( arguments, format );
    cypher::engine::sys::Sys_DebugVPrintf( format, arguments );
    va_end( arguments );
}

CYPHER_NORETURN void ErrorThroughVList( const char *format, ... )
{
    std::va_list arguments;
    va_start( arguments, format );
    cypher::engine::sys::Sys_VError( format, arguments );
}

} // namespace

int main( const int argc, const char *const *argv )
{
    if ( argc != 2 || argv == nullptr ) {
        return 2;
    }
    if ( std::strcmp( argv[1], "quit" ) == 0 ) {
        cypher::engine::sys::Sys_Quit( 73 );
    }
    if ( std::strcmp( argv[1], "error" ) == 0 ) {
        cypher::engine::sys::Sys_Error( "isolated CypherSystem error test" );
    }
    if ( std::strcmp( argv[1], "error-v" ) == 0 ) {
        ErrorThroughVList( "isolated CypherSystem error-v test" );
    }
    if ( std::strcmp( argv[1], "debug-output" ) == 0 ) {
        cypher::engine::sys::Sys_DebugPrintf( "system-debug=%d\n", 17 );
        PrintThroughVList( "system-vdebug=%d\n", 23 );
        return 0;
    }
    if ( std::strcmp( argv[1], "system-report" ) == 0 ) {
        cypher::engine::sys::Sys_PrintSystemReport();
        return 0;
    }
    return 3;
}
