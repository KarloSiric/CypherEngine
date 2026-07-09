//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TestThread.cpp
//  Purpose: Implements CypherCommon Tier0 test thread helper.
//  Details: This small harness keeps thread tests concise without exposing the
//           future job system.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TestThread.h"

#include <thread>

namespace cypher::common
{

test_thread_result_t TestThread_Run( test_thread_proc_t proc, void *pUserData )
{
    test_thread_result_t result{};
    result.exit_code = -1;
    result.completed = CY_FALSE;

    if ( proc == nullptr ) {
        return result;
    }

    std::thread worker( [&]() {
        result.exit_code = proc( pUserData );
        result.completed = CY_TRUE;
    } );
    worker.join();

    return result;
}

} // namespace cypher::common
