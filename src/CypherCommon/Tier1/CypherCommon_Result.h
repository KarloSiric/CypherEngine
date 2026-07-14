//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Result.h
//  Purpose: Declares CypherCommon Tier1 Result support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RESULT_H
#define CYPHER_COMMON_TIER1_RESULT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Result

Generic result declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum class result_code_t : u32 {
    Ok = 0u,
    Failed,
    InvalidArgument,
    OutOfMemory,
    NotFound
};

struct result_t {
    result_code_t code;
};

bool_t Result_Succeeded( result_t result );
bool_t Result_Failed( result_t result );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RESULT_H
