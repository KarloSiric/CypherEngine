//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Debug.h
//  Purpose: Provides debugger breaks, fatal traps, and build-gated statements.
//  Details: These primitives have no logging, allocation, or engine dependency so
//           assertion and crash paths can use them safely.
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

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

// Returns whether a debugger is currently attached to this process.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_DebuggerIsAttached() noexcept;

// Interrupts execution so an attached debugger can inspect the process.
CYPHER_COMMON_API void Cy_DebugBreak() noexcept;

// Interrupts execution only when a debugger is currently attached.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_DebugBreakIfAttached() noexcept;

// Terminates execution immediately when continuing would be unsafe.
[[noreturn]] CYPHER_COMMON_API void Cy_DebugTrap() noexcept;

} // namespace cypher::common

// Route call sites through the platform-independent Tier0 implementation.
#define CY_DEBUG_BREAK() ::cypher::common::Cy_DebugBreak() // May return after debugger continuation.
#define CY_TRAP()        ::cypher::common::Cy_DebugTrap()  // Never returns.

// Statements in disabled branches are not evaluated and must not carry required
// side effects. Use ordinary control flow when execution is semantically required.
#if CYPHER_CONFIG_DEBUG
    #define CY_DEBUG_ONLY( statement ) do { statement; } while ( 0 )
#else
    #define CY_DEBUG_ONLY( statement ) do { } while ( 0 )
#endif

#if CYPHER_CONFIG_DEVELOPMENT
    #define CY_DEVELOPMENT_ONLY( statement ) do { statement; } while ( 0 )
#else
    #define CY_DEVELOPMENT_ONLY( statement ) do { } while ( 0 )
#endif

#if CYPHER_CONFIG_RELEASE
    #define CY_RELEASE_ONLY( statement ) do { statement; } while ( 0 )
#else
    #define CY_RELEASE_ONLY( statement ) do { } while ( 0 )
#endif

#if CYPHER_CONFIG_SHIPPING
    #define CY_SHIPPING_ONLY( statement ) do { statement; } while ( 0 )
#else
    #define CY_SHIPPING_ONLY( statement ) do { } while ( 0 )
#endif

#if CYPHER_CONFIG_DEBUG || CYPHER_CONFIG_DEVELOPMENT
    #define CY_DIAGNOSTIC_ONLY( statement ) do { statement; } while ( 0 )
#else
    #define CY_DIAGNOSTIC_ONLY( statement ) do { } while ( 0 )
#endif

#if !CYPHER_CONFIG_SHIPPING
    #define CY_NON_SHIPPING_ONLY( statement ) do { statement; } while ( 0 )
#else
    #define CY_NON_SHIPPING_ONLY( statement ) do { } while ( 0 )
#endif

#endif // CYPHER_COMMON_TIER0_DEBUG_H
