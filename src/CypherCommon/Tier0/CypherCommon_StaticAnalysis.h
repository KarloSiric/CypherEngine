//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_StaticAnalysis.h
//  Purpose: Declares optimizer assumptions and unreachable-code contracts.
//  Details: An assumption is not validation. If its expression can be false in a
//           release build, the optimizer is permitted to miscompile later code.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_STATICANALYSIS_H
#define CYPHER_COMMON_TIER0_STATICANALYSIS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Static Analysis

Static-analysis and optimizer contract surface.

Rules:
- Assumptions describe facts already validated by control flow.
- Assumption expressions must not contain side effects.
- Debug and development builds trap when a contract is violated.
- Release and shipping builds may optimize under the stated contract.
================
*/

#include "CypherCommon_Debug.h"
#include "CypherCommon_Defines.h"

#if CYPHER_CONFIG_DEBUG || CYPHER_CONFIG_DEVELOPMENT
    // Contract violations remain visible while developing the engine.
    #define CY_ANALYSIS_ASSUME( expression )                   \
        do {                                                   \
            if ( !( expression ) ) {                           \
                CY_TRAP();                                     \
            }                                                  \
        } while ( 0 )
    #define CY_ANALYSIS_UNREACHABLE() CY_TRAP()
#elif CYPHER_COMPILER_MSVC_ABI
    // Optimized builds communicate the already-proven fact to the compiler.
    #define CY_ANALYSIS_ASSUME( expression ) __assume( expression )
    #define CY_ANALYSIS_UNREACHABLE() __assume( 0 )
#elif CYPHER_COMPILER_CLANG
    #define CY_ANALYSIS_ASSUME( expression ) __builtin_assume( expression )
    #define CY_ANALYSIS_UNREACHABLE() __builtin_unreachable()
#elif CYPHER_COMPILER_GCC
    #define CY_ANALYSIS_ASSUME( expression )                   \
        do {                                                   \
            if ( !( expression ) ) {                           \
                __builtin_unreachable();                       \
            }                                                  \
        } while ( 0 )
    #define CY_ANALYSIS_UNREACHABLE() __builtin_unreachable()
#else
    #error "Unsupported compiler for Cypher static-analysis helpers."
#endif

#if CYPHER_COMPILER_MSVC_ABI
    #define CY_ANALYSIS_SUPPRESS( warningId ) __pragma( warning( suppress : warningId ) )
#else
    #define CY_ANALYSIS_SUPPRESS( warningId )
#endif

#endif // CYPHER_COMMON_TIER0_STATICANALYSIS_H
