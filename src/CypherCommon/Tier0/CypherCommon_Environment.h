//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Environment.h
//  Purpose: Declares CypherCommon Tier0 Environment support.
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

#ifndef CYPHER_COMMON_TIER0_ENVIRONMENT_H
#define CYPHER_COMMON_TIER0_ENVIRONMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Environment

Environment variable declarations.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

usize Environment_Get( const char *pName, char *pDest, usize cchDest );
bool_t Environment_Set( const char *pName, const char *pValue );
bool_t Environment_Has( const char *pName );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ENVIRONMENT_H
