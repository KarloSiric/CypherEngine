//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValuePack.h
//  Purpose: Declares CypherCommon Tier1 KeyValuePack support.
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

#ifndef CYPHER_COMMON_TIER1_KEYVALUEPACK_H
#define CYPHER_COMMON_TIER1_KEYVALUEPACK_H
#pragma once

/*
================
CypherCommon KeyValue Pack

Binary packed key/value declarations.
================
*/

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

usize KeyValuePack_Size( const key_value_t *pRoot );
bool_t KeyValuePack_Write( const key_value_t *pRoot, void *pDest, usize cbDest );
bool_t KeyValuePack_Read( const void *pData, usize cbData, key_value_t **ppOutRoot );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEPACK_H
