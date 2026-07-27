//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Assert.cpp
//  Purpose: Implements CypherCommon Tier0 assertion handling.
//  Details: Owns the process-wide assertion callback, fallback diagnostics,
//           recursive-failure protection, and failure-action dispatch.
//
//  History:
//  - Created by Karlo Siric on 2026-07-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"

#include "CypherCommon_Atomic.h"
#include "CypherCommon_Debug.h"

#include <cstdio>

namespace cypher::common
{
namespace
{

atomic_t<assert_handler_t> g_assertHandler{ nullptr };
thread_local bool_t g_assertHandlingFailure = CY_FALSE;

assert_action_t Cy_AssertDefaultHandler( const assert_info_t &info ) noexcept
{
    std::fprintf( stderr,
                  "[Assert] Expression: %s\n"
                  "         Message: %s\n"
                  "         File: %s:%u\n"
                  "         Function: %s\n",
                  info.pExpression,
                  info.pMessage,
                  info.location.pFile,
                  info.location.line,
                  info.location.pFunction );
    std::fflush( stderr );

    return Cy_DebuggerIsAttached() ? assert_action_t::Break : assert_action_t::Abort;
}

} // namespace

void Cy_AssertSetHandler( assert_handler_t pHandler ) noexcept
{
    Cy_AtomicStore( &g_assertHandler, pHandler, CY_MEMORY_ORDER_RELEASE );
}

assert_handler_t Cy_AssertGetHandler() noexcept
{
    return Cy_AtomicLoad( &g_assertHandler, CY_MEMORY_ORDER_ACQUIRE );
}

void Cy_AssertHandleFailure( const char *pExpression,
                             const char *pMessage,
                             source_location_t location ) noexcept
{
    if ( g_assertHandlingFailure ) {
        std::fprintf( stderr, "[Assert] Recursive assertion failure.\n" );
        std::fflush( stderr );
        Cy_DebugTrap();
    }

    g_assertHandlingFailure = CY_TRUE;

    assert_info_t info{};
    info.pExpression = pExpression != nullptr ? pExpression : "<unknown expression>";
    info.pMessage = pMessage != nullptr ? pMessage : "";
    info.location.pFile = location.pFile != nullptr && location.pFile[0] != '\0'
        ? location.pFile
        : "<unknown file>";
    info.location.pFunction = location.pFunction != nullptr && location.pFunction[0] != '\0'
        ? location.pFunction
        : "<unknown function>";
    info.location.line = location.line;
    info.location.column = location.column;

    const assert_handler_t pHandler = Cy_AssertGetHandler();
    const assert_action_t action = pHandler != nullptr
        ? pHandler( info )
        : Cy_AssertDefaultHandler( info );

    g_assertHandlingFailure = CY_FALSE;

    switch ( action ) {
        case assert_action_t::Continue:
            return;
        case assert_action_t::Break:
            CYPHER_UNUSED( Cy_DebugBreakIfAttached() );
            return;
        case assert_action_t::Abort:
            Cy_DebugTrap();
        case assert_action_t::Count:
            break;
    }

    Cy_DebugTrap();
}

} // namespace cypher::common
