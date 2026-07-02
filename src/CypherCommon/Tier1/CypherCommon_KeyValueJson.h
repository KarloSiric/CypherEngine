//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueJson.h
//  Purpose: Declares CypherCommon Tier1 KeyValueJson support.
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

#ifndef CYPHER_COMMON_TIER1_KEYVALUEJSON_H
#define CYPHER_COMMON_TIER1_KEYVALUEJSON_H
#pragma once

/*
================
CypherCommon KeyValue JSON

JSON conversion declarations for key/value data.
================
*/

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

bool_t KeyValueJson_Parse( const char *pText, key_value_t **ppOutRoot );
usize KeyValueJson_Write( const key_value_t *pRoot, char *pDest, usize cchDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEJSON_H
