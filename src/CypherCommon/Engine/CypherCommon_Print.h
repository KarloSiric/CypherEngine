//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Engine/CypherCommon_Print.h
//  Purpose: Declares the CypherCommon Print module.
//  Details: This file holds engine-scoped common metadata or helpers used across
//           runtime subsystems. Keep it narrow so it does not become a dumping
//           ground.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_COMMON_PRINT_H
#define CYPHER_ENGINE_COMMON_PRINT_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Error.h"

#include <cstdarg>     // va_list for variadic print forwarding.

namespace cypher::engine::common
{

constexpr usize COM_MSG_MAX = 2048u;

/*
================
Common Print API

Small printf-style output layer used before and beside the structured logger.
================
*/
void CypherCommon_Printf( const char *message, ... );

void CypherCommon_DPrintf( const char *message, ... );

void CypherCommon_VPrintf( const char *message, va_list args );

void CypherCommon_Errorf( const error_t error, const char *message, ... );

void CypherCommon_VErrorf( const error_t error, const char *message, va_list args );

}

/*
================
Common Print Aliases

Preferred call-site names for console-style output. The full CypherCommon_*
functions remain the real API and can still be called directly.
================
*/
#define COM_PRINTF( ... )                   ::cypher::engine::common::CypherCommon_Printf( __VA_ARGS__ )
#define COM_DPRINTF( ... )                  ::cypher::engine::common::CypherCommon_DPrintf( __VA_ARGS__ )
#define COM_VPRINTF( MESSAGE, ARGS )        ::cypher::engine::common::CypherCommon_VPrintf( ( MESSAGE ), ( ARGS ) )
#define COM_ERRORF( ERROR, ... )            ::cypher::engine::common::CypherCommon_Errorf( ( ERROR ), __VA_ARGS__ )
#define COM_VERRORF( ERROR, MESSAGE, ARGS ) ::cypher::engine::common::CypherCommon_VErrorf( ( ERROR ), ( MESSAGE ), ( ARGS ) )

#endif // CYPHER_ENGINE_COMMON_PRINT_H
