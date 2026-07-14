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

#include "CypherCommon_Platform.h"

#if CYPHER_COMPILER_MSVC
    #include <intrin.h>
#endif

/*
================
Debugger Break / Trap
================
*/
// Breaks into the debugger when available; trap terminates execution.
#if CYPHER_COMPILER_MSVC
    #define CYPHER_DEBUG_BREAK()            __debugbreak()
    #define CYPHER_TRAP()                   __debugbreak()
#elif CYPHER_COMPILER_CLANG
    #define CYPHER_DEBUG_BREAK()            __builtin_debugtrap()
    #define CYPHER_TRAP()                   __builtin_trap()
#elif CYPHER_COMPILER_GCC
    #define CYPHER_DEBUG_BREAK()            __builtin_trap()
    #define CYPHER_TRAP()                   __builtin_trap()
#else
    #error "Unsupported compiler for Cypher debug helpers."
#endif

/*
================
Build Gated Code Helpers
================
*/
// Emits a statement only for the selected build class.
#if CYPHER_BUILD_DEBUG
    #define CYPHER_DEBUG_ONLY( statement )  do { statement; } while ( 0 )
    #define CYPHER_RELEASE_ONLY( statement ) do { } while ( 0 )
#else
    #define CYPHER_DEBUG_ONLY( statement )  do { } while ( 0 )
    #define CYPHER_RELEASE_ONLY( statement ) do { statement; } while ( 0 )
#endif

#endif // CYPHER_COMMON_TIER0_DEBUG_H
