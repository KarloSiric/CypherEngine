//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueJson.h
//  Purpose: Declares strict JSON interchange for KeyValue documents.
//  Details: JSON is an interchange adapter, not the native CYKV source format. Binary
//           nodes are rejected unless explicit text encoding policy is selected later.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_KEYVALUEJSON_H
#define CYPHER_COMMON_TIER1_KEYVALUEJSON_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_KeyValueWriter.h"

namespace cypher::common
{

struct key_value_json_options_t {
    bool_t bPretty{ CY_TRUE };
    bool_t bRejectDuplicateKeys{ CY_TRUE };
    bool_t bEscapeNonAscii{ CY_FALSE };
    u8 nIndentSpaces{ 2u };
    usize nMaxDepth{ 128u };
    usize nMaxNodes{ 1u << 20u };
    usize cbMaxStringData{ 64u * CY_MIB };
};

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_parse_result_t KeyValueJson_Parse(
    string_view_t json,
    const key_value_json_options_t &options,
    key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_write_result_t KeyValueJson_Write(
    const key_value_t *pRoot,
    const key_value_json_options_t &options,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEJSON_H
