//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueJson.cpp
//  Purpose: Implements strict JSON interchange for CYKV documents.
//  Details: JSON reuses the bounded parser and writer cores with a deliberately
//           narrower grammar. Native CYKV binary values have no implicit JSON form.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueJson.h"

#include "CypherCommon_KeyValueParserInternal.h"
#include "CypherCommon_KeyValueWriterInternal.h"

namespace cypher::common
{

namespace
{

CYPHER_NODISCARD bool_t JsonOptionsAreValid(
    const key_value_json_options_t &options ) noexcept
{
    return options.nMaxDepth != 0u &&
           options.nMaxDepth <= CY_KEY_VALUE_MAX_DEPTH &&
           options.nMaxNodes != 0u &&
           options.cbMaxStringData != 0u;
}

} // namespace

key_value_parse_result_t KeyValueJson_Parse(
    string_view_t json,
    const key_value_json_options_t &options,
    key_value_document_t *pDocument ) noexcept
{
    if ( !JsonOptionsAreValid( options ) ) {
        key_value_parse_result_t invalid{};
        invalid.status = key_value_parse_status_t::INVALID_ARGUMENT;
        return invalid;
    }
    key_value_parse_options_t parseOptions{};
    parseOptions.flags = KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE;
    if ( options.bRejectDuplicateKeys ) {
        parseOptions.flags |= KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS;
    }
    parseOptions.nMaxDepth = options.nMaxDepth;
    parseOptions.nMaxNodes = options.nMaxNodes;
    parseOptions.cbMaxStringData = options.cbMaxStringData;
    return KeyValue_InternalParseText(
        json,
        parseOptions,
        pDocument,
        CY_TRUE );
}

key_value_write_result_t KeyValueJson_Write(
    const key_value_t *pRoot,
    const key_value_json_options_t &options,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( !JsonOptionsAreValid( options ) ) {
        return { key_value_write_status_t::INVALID_ARGUMENT, 0u, 0u };
    }
    key_value_write_options_t writeOptions{};
    writeOptions.flags = options.bPretty
        ? KEY_VALUE_WRITE_FLAG_PRETTY
        : KEY_VALUE_WRITE_FLAG_NONE;
    if ( options.bEscapeNonAscii ) {
        writeOptions.flags |= KEY_VALUE_WRITE_FLAG_ASCII_ONLY;
    }
    writeOptions.nIndentSpaces = options.nIndentSpaces;
    writeOptions.nMaxDepth = options.nMaxDepth;
    return KeyValue_InternalWriteText(
        pRoot,
        writeOptions,
        pDest,
        cchDest,
        CY_TRUE );
}

} // namespace cypher::common
