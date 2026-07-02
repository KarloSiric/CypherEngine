//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TestThread.h
//  Purpose: Declares CypherCommon Tier0 TestThread support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_TESTTHREAD_H
#define CYPHER_COMMON_TIER0_TESTTHREAD_H
#pragma once

/*
================
CypherCommon Test Thread

Small thread test harness declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using test_thread_proc_t = i32 ( * )( void *pUserData );

struct test_thread_result_t {
    i32 exit_code;
    bool_t completed;
};

test_thread_result_t TestThread_Run( test_thread_proc_t proc, void *pUserData );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TESTTHREAD_H
