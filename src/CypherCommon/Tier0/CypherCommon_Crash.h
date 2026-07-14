//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Crash.h
//  Purpose: Declares CypherCommon Tier0 Crash support.
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

#ifndef CYPHER_COMMON_TIER0_CRASH_H
#define CYPHER_COMMON_TIER0_CRASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Crash

Low-level crash reporting declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using crash_handler_t = void ( * )( const char *pReason, const char *pFile, i32 line );

void Crash_SetHandler( crash_handler_t handler );
void Crash_ReportFatal( const char *pReason, const char *pFile, i32 line );
void Crash_Trigger( const char *pReason );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_CRASH_H
