//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Interface.h
//  Purpose: Declares CypherCommon Tier1 Interface support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_INTERFACE_H
#define CYPHER_COMMON_TIER1_INTERFACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Interface

Named interface registry declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

using interface_factory_t = void *( * )( const char *pName );

void Interface_RegisterFactory( interface_factory_t factory );
void *Interface_Create( const char *pName );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_INTERFACE_H
