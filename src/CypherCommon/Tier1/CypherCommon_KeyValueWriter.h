//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.h
//  Purpose: Declares deterministic CYKV 1 hierarchical text output.
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
    KEY_VALUE_WRITE_FLAG_NONE = 0u,                // Compact insertion-order output.
    KEY_VALUE_WRITE_FLAG_PRETTY = CYPHER_BIT32( 0 ), // Emit indentation and line breaks.
    KEY_VALUE_WRITE_FLAG_CANONICAL = CYPHER_BIT32( 1 ), // Sort keys and stabilize spelling.
    KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE = CYPHER_BIT32( 2 ), // End text with LF.
    KEY_VALUE_WRITE_FLAG_ASCII_ONLY = CYPHER_BIT32( 3 ) // Escape non-ASCII code points.
};

enum class key_value_write_status_t : u8 {
    OK = 0u,          // Complete text was emitted.
    INVALID_ARGUMENT, // Root, options, destination, or sink is invalid.
    INVALID_DOCUMENT, // Tree ownership, type, or linkage invariant is broken.
    DEPTH_LIMIT,      // Tree nesting exceeds writer policy.
    OUT_OF_MEMORY,    // Canonical key ordering could not allocate scratch space.
    SIZE_OVERFLOW,    // Required character count overflowed usize.
    OUTPUT_TRUNCATED, // Buffer received a prefix but cannot hold complete text.
    SINK_FAILED       // Streaming sink rejected an output fragment.
};

struct key_value_write_options_t {
    flags32_t flags{ KEY_VALUE_WRITE_FLAG_PRETTY |
                     KEY_VALUE_WRITE_FLAG_FINAL_NEWLINE }; // Formatting and canonicalization policy.
    u8 nIndentSpaces{ 4u };  // Spaces emitted for each pretty-print level.
    usize nMaxDepth{ 128u }; // Maximum tree depth accepted by the writer.
};

struct key_value_write_result_t {
    key_value_write_status_t status{ key_value_write_status_t::OK }; // Final status.
    usize cchWritten{ 0u };  // Characters published, excluding a terminator.
    usize cchRequired{ 0u }; // Complete character count even after truncation.
};

// Canonical mode emits compact text, sorts object members byte-wise by key,
// and suppresses the optional final newline so one tree has one representation.

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

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *KeyValue_WriteStatusName(
    key_value_write_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_KEYVALUEWRITER_H
