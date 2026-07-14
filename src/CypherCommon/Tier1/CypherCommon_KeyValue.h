//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValue.h
//  Purpose: Declares CypherCommon Tier1 KeyValue support.
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

#ifndef CYPHER_COMMON_TIER1_KEYVALUE_H
#define CYPHER_COMMON_TIER1_KEYVALUE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon KeyValue

Hierarchical key/value data declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum class key_value_type_t : u32 {
    Null = 0u,
    String,
    Integer,
    Float,
    Boolean,
    Object,
    Array
};

struct key_value_t;

key_value_t *KeyValue_Find( key_value_t *pRoot, const char *pName );
const key_value_t *KeyValue_Find( const key_value_t *pRoot, const char *pName );
key_value_type_t KeyValue_GetType( const key_value_t *pValue );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUE_H
