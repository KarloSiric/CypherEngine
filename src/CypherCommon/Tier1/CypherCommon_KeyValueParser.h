//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueParser.h
//  Purpose: Declares bounded CYKV 1 hierarchical text parsing.
//  Details: Parsing is transactional: the destination document changes only after a
//           complete successful parse. Limits bound hostile or malformed input cost.
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

#include "CypherCommon_KeyValue.h"
#include "CypherCommon_Lexer.h"

namespace cypher::common
{

enum key_value_parse_flags_t : flags32_t {
    KEY_VALUE_PARSE_FLAG_NONE                  = 0u,
    KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS        = CYPHER_BIT32( 0 ),
    KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA  = CYPHER_BIT32( 1 ),
    KEY_VALUE_PARSE_FLAG_ALLOW_UNQUOTED_KEYS   = CYPHER_BIT32( 2 ),
    KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS = CYPHER_BIT32( 3 ),
    KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE      = CYPHER_BIT32( 4 )
};

enum class key_value_parse_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INPUT_LIMIT,
    INVALID_ENCODING,
    INVALID_HEADER,
    UNSUPPORTED_VERSION,
    INVALID_SCHEMA,
    LEXER_ERROR,
    SYNTAX_ERROR,
    DUPLICATE_KEY,
    DEPTH_LIMIT,
    NODE_LIMIT,
    CONTAINER_LIMIT,
    COMMENT_DEPTH_LIMIT,
    STRING_LIMIT,
    OUT_OF_MEMORY,
    TRAILING_INPUT
};

struct key_value_parse_options_t {
    flags32_t flags{ KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS |
                     KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA |
                     KEY_VALUE_PARSE_FLAG_ALLOW_UNQUOTED_KEYS |
                     KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS };
    usize cbMaxInput{ 64u * CY_MIB };
    usize nMaxDepth{ 128u };
    usize nMaxNodes{ 1u << 20u };
    usize nMaxContainerValues{ 1u << 20u };
    usize nMaxCommentDepth{ 64u };
    usize cbMaxStringData{ 64u * CY_MIB };
};

struct key_value_parse_result_t {
    key_value_parse_status_t status{ key_value_parse_status_t::OK };
    text_location_t errorLocation{};
    usize nNodesParsed{ 0u };
    usize cbStringData{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
key_value_parse_result_t KeyValue_ParseText(
    string_view_t text,
    const key_value_parse_options_t &options,
    key_value_document_t *pDocument ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *KeyValue_ParseStatusName(
    key_value_parse_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEPARSER_H
