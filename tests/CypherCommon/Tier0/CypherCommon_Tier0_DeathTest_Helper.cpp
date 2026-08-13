//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_DeathTest_Helper.cpp
//  Purpose: Runs process-terminating Tier0 APIs in an isolated child process.
//  Details: CTest invokes one mode per process so debug traps, fatal crash paths,
//           and explicit exits can be verified without terminating a test runner.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Crash.h"
#include "CypherCommon_Debug.h"
#include "CypherCommon_Process.h"

#include <cstring>

#if CYPHER_PLATFORM_WINDOWS
    #include <crtdbg.h>
#endif

using namespace cypher::common;

namespace
{

void DeathTest_DisableInteractiveCrashReporting() noexcept
{
    #if CYPHER_PLATFORM_WINDOWS
        // The MSVC debug CRT may enter Windows Error Reporting after abort().
        // Death tests run unattended, so suppress dialogs while preserving the
        // abnormal process termination that the parent CTest script verifies.
        _set_abort_behavior( 0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT );
    #endif
}

} // namespace

int main( int argc, char **argv )
{
    DeathTest_DisableInteractiveCrashReporting();

    if ( argc != 2 || argv[1] == nullptr ) {
        return 64;
    }

    if ( std::strcmp( argv[1], "debug-break" ) == 0 ) {
        Cy_DebugBreak();
        return 65;
    }
    if ( std::strcmp( argv[1], "debug-trap" ) == 0 ) {
        Cy_DebugTrap();
    }
    if ( std::strcmp( argv[1], "crash-trigger" ) == 0 ) {
        Cy_CrashTrigger( "Tier0 death-test trigger", CY_SOURCE_LOCATION );
    }
    if ( std::strcmp( argv[1], "process-exit" ) == 0 ) {
        Cy_ProcessExit( 73 );
    }

    return 66;
}
