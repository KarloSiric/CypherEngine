//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Process.h
//  Purpose: Declares CypherCommon Tier0 Process support.
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

#ifndef CYPHER_COMMON_TIER0_PROCESS_H
#define CYPHER_COMMON_TIER0_PROCESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Process

Process identity and process utility declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using process_id_t = u64;

process_id_t Process_GetCurrentId();
const char *Process_GetExecutablePath();
void Process_Exit( i32 exit_code );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PROCESS_H
