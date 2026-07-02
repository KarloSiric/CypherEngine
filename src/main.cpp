//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/main.cpp
//  Purpose: Implements the CypherEngine executable entry point.
//  Details: The executable entry point delegates startup and frame ownership to the
//           host layer. Keep this file thin so platform and subsystem behavior stays
//           testable elsewhere.
//
//  History:
//  - Created by Karlo Siric on 2026-04-19
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherHost.h"

#include <cstdlib>     // EXIT_SUCCESS / EXIT_FAILURE.

namespace rr = cypher::engine;
namespace host = rr::host;

/*
================
main

Keeps the executable entry point thin; host owns engine startup, frame flow
and shutdown ordering.
================
*/
int main(int argc, char const *argv[])
{
    host::state_t  pHostState{};
    pHostState.config.argc = argc;
    pHostState.config.argv = argv;

    if ( host::CypherHost_Init( pHostState ) != host::host_error_t::OK ) {
        return ( EXIT_FAILURE );
    }

    while( host::CypherHost_IsRunning( pHostState ) ) {
        host::CypherHost_BeginFrame( pHostState );
        host::CypherHost_Update( pHostState );
        host::CypherHost_Render( pHostState );
        host::CypherHost_EndFrame( pHostState );
    }

    host::CypherHost_Shutdown( pHostState );

    return ( EXIT_SUCCESS );
}
