//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.h
//  Purpose: Declares deterministic CYDF-style hierarchical text output.
//  Details: Output can target a bounded buffer or callback sink. Canonical mode fixes
//           ordering and whitespace for reproducible source control and content hashing.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_KEYVALUEWRITER_H
#define CYPHER_COMMON_TIER1_KEYVALUEWRITER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValue.h"

namespace cypher::common
{

enum key_value_write_flags_t : flags32_t {
    KEY_VALUE_WRITE_FLAG_NONE          = 0u,
    KEY_VALUE_WRITE_FLAG_PRETTY        = CYPHER_BIT32( 0 ),
    KEY_VALUE_WRITE_FLAG_CANONICAL     = CYPHER_BIT32( 1 ),
    KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE = CYPHER_BIT32( 2 ),
    KEY_VALUE_WRITE_FLAG_ASCII_ONLY    = CYPHER_BIT32( 3 )
};

enum class key_value_write_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_DOCUMENT,
    DEPTH_LIMIT,
    OUTPUT_TRUNCATED,
    SINK_FAILED
};

struct key_value_write_options_t {
    flags32_t flags{ KEY_VALUE_WRITE_FLAG_PRETTY |
                     KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE };
    u8 nIndentSpaces{ 4u };
    usize nMaxDepth{ 128u };
};

struct key_value_write_result_t {
    key_value_write_status_t status{ key_value_write_status_t::OK };
    usize cchWritten{ 0u };
    usize cchRequired{ 0u };
};

using key_value_write_fn_t = bool_t ( * )(
    string_view_t text,
    void *pUserData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_write_result_t KeyValue_WriteText(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_write_result_t KeyValue_WriteTextToSink(
    const key_value_t *pRoot,
    const key_value_write_options_t &options,
    key_value_write_fn_t pfnWrite,
    void *pUserData ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEWRITER_H
