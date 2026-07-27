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

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

// Returns whether a debugger is currently attached to this process.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_DebuggerIsAttached() noexcept;

// Interrupts execution so an attached debugger can inspect the process.
CYPHER_COMMON_API void Cy_DebugBreak() noexcept;

// Interrupts execution only when a debugger is currently attached.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_DebugBreakIfAttached() noexcept;

// Terminates execution immediately when continuing would be unsafe.
[[noreturn]] CYPHER_COMMON_API void Cy_DebugTrap() noexcept;

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
