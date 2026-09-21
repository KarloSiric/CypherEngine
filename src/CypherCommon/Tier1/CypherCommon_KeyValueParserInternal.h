//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueParserInternal.h
//  Purpose: Shares the private CYKV/JSON parser entry point.
//  Details: Native CYKV and strict JSON use one bounded recursive-descent core with
//           separate grammar policy. This interface is not public engine API.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Key Value Parser Internal Contract

Parses bounded UTF-8 source into caller-owned CYKV storage. Source offsets and diagnostics must
continue to refer to the original input, and malformed text must not escape as a usable partial
document.
================
*/

#ifndef CYPHER_COMMON_TIER1_KEYVALUEPARSERINTERNAL_H
#define CYPHER_COMMON_TIER1_KEYVALUEPARSERINTERNAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_KeyValueParser.h"

namespace cypher::common
{

struct key_value_definition_seed_t {
    string_view_t name{};
    const key_value_t *pValue{ nullptr };
};

CYPHER_NODISCARD key_value_parse_result_t KeyValue_InternalParseText(
    string_view_t text,
    const key_value_parse_options_t &options,
    key_value_document_t *pDocument,
    bool_t bStrictJson ) noexcept;

// Parses with immutable, already-resolved CYKV 2 definitions. Seed names must be
// valid definition identifiers and values must be valid trees. Values are
// deep-copied into private parser state before local #define declarations.
CYPHER_NODISCARD key_value_parse_result_t
KeyValue_InternalParseTextWithDefinitions(
    string_view_t text,
    const key_value_parse_options_t &options,
    const key_value_definition_seed_t *pSeeds,
    usize nSeeds,
    key_value_document_t *pDocument,
    bool_t bStrictJson ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEPARSERINTERNAL_H
