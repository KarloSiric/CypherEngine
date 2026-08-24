//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Assert.h
//  Purpose: Reports failed programmer invariants through a configurable handler.
//  Details: Assertions are for internal contract violations, never recoverable errors;
//           handlers must be thread-safe, allocation-aware, and non-throwing.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ASSERT_H
#define CYPHER_COMMON_TIER0_ASSERT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Assert

Programmer-invariant diagnostics with configurable failure handling.

Rules:
- No logging dependency.
- No filesystem dependency.
- Assertion handlers must be thread-safe and must not throw.
- Assertions must not be used for recoverable runtime errors.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"
#include "CypherCommon_SourceLocation.h"

namespace cypher::common
{

enum class assert_action_t : u8 {
    Continue = 0u, // Return to the failing call site.
    Break,         // Interrupt an attached debugger, then return.
    Abort,         // Terminate because continuing is unsafe.
    Count          // Sentinel; never returned by a valid handler.
};

struct assert_info_t {
    const char *pExpression{ "" }; // Stringized expression; borrowed during the handler call.
    const char *pMessage{ "" };    // Optional borrowed explanation; empty when absent.
    source_location_t location{};  // Source location captured at the assertion call site.
};

using assert_handler_t = assert_action_t ( * )( const assert_info_t &info ) noexcept;

// Installs a process-wide handler; passing nullptr restores default handling.
CYPHER_COMMON_API void Cy_AssertSetHandler( assert_handler_t pHandler ) noexcept;

// Returns the currently installed process-wide handler, or nullptr for the default.
CYPHER_NODISCARD CYPHER_COMMON_API assert_handler_t Cy_AssertGetHandler() noexcept;

// Reports a failed assertion and performs the action selected by its handler.
CYPHER_COMMON_API void Cy_AssertHandleFailure(
    const char *pExpression,
    const char *pMessage,
    source_location_t location ) noexcept;

} // namespace cypher::common

#define CY_STATIC_ASSERT( expression, message ) static_assert( expression, message )

#define CYPHER_ASSERTS_ENABLED ( CYPHER_CONFIG_DEBUG || CYPHER_CONFIG_DEVELOPMENT ) // Compile-time gate.

// ASSERT expressions disappear entirely when disabled. VERIFY expressions still
// execute for side effects, but failure reporting is compiled out.
#if CYPHER_ASSERTS_ENABLED
    #define CY_ASSERT( expression )                                                             \
        do {                                                                                     \
            if ( CYPHER_UNLIKELY( !( expression ) ) ) {                                          \
                ::cypher::common::Cy_AssertHandleFailure( #expression, nullptr,                  \
                                                          CY_SOURCE_LOCATION );                  \
            }                                                                                    \
        } while ( 0 )

    #define CY_ASSERT_MSG( expression, message )                                                 \
        do {                                                                                     \
            if ( CYPHER_UNLIKELY( !( expression ) ) ) {                                          \
                ::cypher::common::Cy_AssertHandleFailure( #expression, message,                  \
                                                          CY_SOURCE_LOCATION );                  \
            }                                                                                    \
        } while ( 0 )

    #define CY_VERIFY( expression )                                                             \
        do {                                                                                     \
            if ( CYPHER_UNLIKELY( !( expression ) ) ) {                                          \
                ::cypher::common::Cy_AssertHandleFailure( #expression, nullptr,                  \
                                                          CY_SOURCE_LOCATION );                  \
            }                                                                                    \
        } while ( 0 )

    #define CY_VERIFY_MSG( expression, message )                                                 \
        do {                                                                                     \
            if ( CYPHER_UNLIKELY( !( expression ) ) ) {                                          \
                ::cypher::common::Cy_AssertHandleFailure( #expression, message,                  \
                                                          CY_SOURCE_LOCATION );                  \
            }                                                                                    \
        } while ( 0 )
#else
    #define CY_ASSERT( expression )                  do { } while ( 0 )
    #define CY_ASSERT_MSG( expression, message )     do { } while ( 0 )

    #define CY_VERIFY( expression )                  do { ( void )( expression ); } while ( 0 )
    #define CY_VERIFY_MSG( expression, message )     do { ( void )( expression ); } while ( 0 )
#endif

#endif // CYPHER_COMMON_TIER0_ASSERT_H
