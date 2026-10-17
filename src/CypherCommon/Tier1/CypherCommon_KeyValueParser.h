//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueParser.h
//  Purpose: Declares bounded CYKV hierarchical text parsing.
//  Details: Parsing is transactional: the destination document changes only after a
//           complete successful parse. Limits bound hostile or malformed input cost.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//  - Extended on 2026-10-12 for CYKV 2 `@when` build conditionals, `#extends`
//    object templates, and the `#delete` / `+=` inheritance operators
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
    KEY_VALUE_PARSE_FLAG_NONE = 0u, // Strict grammar with no extensions.
    KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS = CYPHER_BIT32( 0 ), // Accept line/block comments.
    KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA = CYPHER_BIT32( 1 ), // Permit final separators.
    KEY_VALUE_PARSE_FLAG_ALLOW_UNQUOTED_KEYS = CYPHER_BIT32( 2 ), // Accept identifiers as keys.
    KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS = CYPHER_BIT32( 3 ), // Enforce unique object keys.
    KEY_VALUE_PARSE_FLAG_ALLOW_ROOT_VALUE = CYPHER_BIT32( 4 ), // Permit scalar/array roots.
    // CYKV with ALLOW_COMMENTS: keep comments with the values they belong to
    // (KeyValue_Comments) so a tool that rewrites the file can keep them.
    KEY_VALUE_PARSE_FLAG_KEEP_COMMENTS = CYPHER_BIT32( 5 )
};

enum class key_value_parse_status_t : u8 {
    OK = 0u,           // Complete document parsed and committed.
    INVALID_ARGUMENT,  // Source, options, or destination is invalid.
    INPUT_LIMIT,       // Source byte count exceeds policy.
    INVALID_ENCODING,  // Source is not valid bounded UTF-8/CYKV text.
    INVALID_HEADER,    // Required @cykv or @schema directive is malformed.
    UNSUPPORTED_VERSION, // CYKV language revision is unsupported.
    INVALID_SCHEMA,    // Schema identifier or version is malformed.
    LEXER_ERROR,       // Tokenization failed; inspect errorLocation.
    SYNTAX_ERROR,      // Token sequence violates CYKV grammar.
    DUPLICATE_KEY,     // Object key repeats under selected comparison policy.
    DEPTH_LIMIT,       // Container nesting exceeds policy.
    NODE_LIMIT,        // Semantic value count exceeds policy.
    CONTAINER_LIMIT,   // One container has too many direct values.
    COMMENT_DEPTH_LIMIT, // Nested block comments exceed policy.
    STRING_LIMIT,      // Aggregate decoded payload exceeds policy.
    OUT_OF_MEMORY,     // Transactional destination allocation failed.
    TRAILING_INPUT,    // Non-trivia bytes follow the root value.
    DUPLICATE_DEFINITION, // A CYKV 2 local definition name is already bound.
    UNDEFINED_DEFINITION, // A $NAME reference has no prior local definition.
    DEFINITION_LIMIT,  // Local definition count exceeds policy.
    UNKNOWN_CONDITION_PROPERTY, // A @when condition names a property the build context lacks.
    CONDITION_TYPE_MISMATCH, // A @when comparison mixes kinds, or a non-Boolean property stands alone.
    MERGE_CONFLICT,    // An inherited and a local value at one path have different kinds.
    INCLUDE_CONFLICT,  // A merging #include supplies a member the document already has.
    INHERITED_VALUE_MISSING, // #delete or += names a member nothing is inherited for.
    APPEND_TYPE_MISMATCH, // += where the written or the inherited value is not an array.
    OPERATOR_WITHOUT_BASE // #delete or += outside an #extends object or a document with #base.
};

// One property a CYKV 2 `@when` condition may read: a dotted name such as
// "target.platform" and a Boolean, integer, or string value. Conditions read
// nothing else - no environment, files, clock, or hardware - so the same
// source and context always resolve to the same document.
enum class key_value_build_value_kind_t : u8 {
    BOOL = 0u,
    INTEGER,
    STRING
};

struct key_value_build_property_t {
    string_view_t name{};
    key_value_build_value_kind_t kind{ key_value_build_value_kind_t::BOOL };
    bool_t bValue{ CY_FALSE };
    i64 iValue{ 0 };
    string_view_t text{};
};

struct key_value_build_context_t {
    const key_value_build_property_t *pProperties{ nullptr };
    usize nProperties{ 0u };
};

// The running build's context: target.platform ("windows", "macos",
// "linux"), target.architecture ("x64", "arm64", ...), build.profile
// ("debug", "development", "release", "shipping"), and the Booleans
// build.debug and build.shipping. Tools that cook for another target pass
// their own context instead.
CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const key_value_build_context_t *KeyValue_HostBuildContext() noexcept;

struct key_value_parse_options_t {
    flags32_t flags{ KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS |
                     KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA |
                     KEY_VALUE_PARSE_FLAG_ALLOW_UNQUOTED_KEYS |
                     KEY_VALUE_PARSE_FLAG_REJECT_DUPLICATE_KEYS }; // Grammar policy bits.
    usize cbMaxInput{ 64u * CY_MIB };          // Maximum source bytes accepted.
    usize nMaxDepth{ 128u };                   // Maximum object/array nesting depth.
    usize nMaxNodes{ 1u << 20u };              // Maximum semantic values created.
    usize nMaxContainerValues{ 1u << 20u };    // Maximum direct values in one container.
    usize nMaxCommentDepth{ 64u };              // Maximum nested block-comment depth.
    usize cbMaxStringData{ 64u * CY_MIB };     // Maximum aggregate decoded payload bytes.
    usize nMaxDefinitions{ 256u };             // Maximum CYKV 2 definitions, including include aliases.
    // Properties CYKV 2 `@when` conditions read; null selects the running
    // build's (KeyValue_HostBuildContext).
    const key_value_build_context_t *pBuildContext{ nullptr };
};

struct key_value_parse_result_t {
    key_value_parse_status_t status{ key_value_parse_status_t::OK }; // Final parse status.
    text_location_t errorLocation{}; // First source location associated with failure.
    // Exact source locations for semantic validation of a parsed CYKV header.
    text_location_t languageVersionLocation{}; // Location of @cykv version.
    text_location_t schemaIdLocation{};        // Location of @schema identifier.
    text_location_t schemaVersionLocation{};   // Location of @schema version.
    usize nNodesParsed{ 0u };                   // Semantic values committed on success.
    usize cbStringData{ 0u };                   // Decoded name/string/binary bytes.
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
