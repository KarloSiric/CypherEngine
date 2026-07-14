//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Map.h
//  Purpose: Declares CypherCommon Tier1 Map support.
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

#ifndef CYPHER_COMMON_TIER1_MAP_H
#define CYPHER_COMMON_TIER1_MAP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Map

Ordered key/value container declarations.
================
*/

namespace cypher::common
{

template <typename key_t, typename value_t>
struct map_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MAP_H
