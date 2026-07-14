//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueParser.h
//  Purpose: Declares CypherCommon Tier1 KeyValueParser support.
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

#ifndef CYPHER_COMMON_TIER1_KEYVALUEPARSER_H
#define CYPHER_COMMON_TIER1_KEYVALUEPARSER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon KeyValue Parser

Key/value parser declarations.
================
*/

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

struct key_value_parse_result_t {
    key_value_t *pRoot;
    u32 error_line;
    const char *pError;
};

bool_t KeyValue_ParseText( const char *pText, key_value_parse_result_t *pOutResult );
bool_t KeyValue_ParseFileData( const void *pData, usize cbData, key_value_parse_result_t *pOutResult );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEPARSER_H
