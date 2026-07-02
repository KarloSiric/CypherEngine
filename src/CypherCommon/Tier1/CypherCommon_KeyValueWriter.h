//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.h
//  Purpose: Declares CypherCommon Tier1 KeyValueWriter support.
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

#ifndef CYPHER_COMMON_TIER1_KEYVALUEWRITER_H
#define CYPHER_COMMON_TIER1_KEYVALUEWRITER_H
#pragma once

/*
================
CypherCommon KeyValue Writer

Key/value text writer declarations.
================
*/

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

usize KeyValue_WriteText( const key_value_t *pRoot, char *pDest, usize cchDest );
usize KeyValue_WriteCompactText( const key_value_t *pRoot, char *pDest, usize cchDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEWRITER_H
