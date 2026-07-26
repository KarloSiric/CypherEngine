//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Debug.h
//  Purpose: Declares CypherCommon Tier0 Debug support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_DEBUG_H
#define CYPHER_COMMON_TIER0_DEBUG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Debug

Lowest-level debug break and trap helpers.

Rules:
- No logging.
- No allocation.
- No CypherEngine dependency.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

// Returns whether a debugger is currently attached to this process.
bool_t Cy_DebuggerIsAttached();

// Interrupts execution so an attached debugger can inspect the process.
void Cy_DebugBreak();

// Terminates execution immediately when continuing would be unsafe.
[[noreturn]] void Cy_DebugTrap();

} // namespace cypher::common

/*
================
Debugger Break / Trap
================
*/
// Routes call sites through the platform-independent Tier0 implementation.
#define CY_DEBUG_BREAK() ::cypher::common::Cy_DebugBreak()
#define CY_TRAP()        ::cypher::common::Cy_DebugTrap()

/*
================
Build Gated Code Helpers
================
*/
// Emits a statement only for the selected build class.
#if CYPHER_BUILD_DEBUG
    #define CY_DEBUG_ONLY( statement )  do { statement; } while ( 0 )
    #define CY_RELEASE_ONLY( statement ) do { } while ( 0 )
#else
    #define CY_DEBUG_ONLY( statement )  do { } while ( 0 )
    #define CY_RELEASE_ONLY( statement ) do { statement; } while ( 0 )
#endif

#endif // CYPHER_COMMON_TIER0_DEBUG_H
