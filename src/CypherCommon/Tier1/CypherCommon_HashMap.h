//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashMap.h
//  Purpose: Declares CypherCommon Tier1 HashMap support.
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

#ifndef CYPHER_COMMON_TIER1_HASHMAP_H
#define CYPHER_COMMON_TIER1_HASHMAP_H
#pragma once

/*
================
CypherCommon Hash Map

Hash-map declarations.
================
*/

namespace cypher::common
{

template <typename key_t, typename value_t>
struct hash_map_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHMAP_H
