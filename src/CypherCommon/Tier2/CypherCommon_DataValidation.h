//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier2/CypherCommon_DataValidation.h
//  Purpose: Declares reusable semantic checks for authored Cypher data.
//  Details: These allocation-free checks keep stable identifiers and virtual
//           resource paths consistent across schemas, compilers, runtime loaders,
//           command-line tools, and Mason without performing filesystem access.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER2_DATAVALIDATION_H
#define CYPHER_COMMON_TIER2_DATAVALIDATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class data_validation_status_t : u8 {
    OK = 0u,                // Value satisfies the requested policy.
    INVALID_ARGUMENT,      // View or configured limit is invalid.
    EMPTY_VALUE,           // Policy requires at least one byte.
    LENGTH_LIMIT,          // Value exceeds its configured byte limit.
    INVALID_IDENTIFIER_START,// First identifier byte is not permitted.
    INVALID_IDENTIFIER_BYTE,// Later identifier byte is not permitted.
    INVALID_PATH_BYTE,     // Path contains control, platform, or reserved syntax.
    NON_CANONICAL_PATH,    // Path is absolute, mixed-case, backslashed, or segmented badly.
    PARENT_TRAVERSAL,      // A path segment is exactly "..".
    EXTENSION_MISMATCH     // Canonical path lacks the required exact extension.
};

struct data_validation_result_t {
    data_validation_status_t status{ data_validation_status_t::OK }; // Result code.
    usize iByte{ CY_INVALID_SIZE }; // First offending byte or relevant boundary.
};

// ASCII identifiers begin with a letter or underscore and continue with ASCII
// letters, decimal digits, or underscores. Case is preserved and significant.
CYPHER_NODISCARD CYPHER_COMMON_API
data_validation_result_t DataValidation_CheckAsciiIdentifier(
    string_view_t value,
    usize cchMax ) noexcept;

// Stable identifiers begin with lowercase ASCII and continue with lowercase
// ASCII, decimal digits, underscores, or hyphens.
CYPHER_NODISCARD CYPHER_COMMON_API
data_validation_result_t DataValidation_CheckStableIdentifier(
    string_view_t value,
    usize cchMax ) noexcept;

// Canonical virtual paths are relative, lowercase ASCII paths with forward
// slashes, no empty/dot segments, no parent traversal, and no platform syntax.
CYPHER_NODISCARD CYPHER_COMMON_API
data_validation_result_t DataValidation_CheckCanonicalVirtualPath(
    string_view_t path,
    usize cchMax ) noexcept;

// Applies canonical virtual-path policy and then requires one exact lowercase
// extension including its leading dot, for example ".cymat".
CYPHER_NODISCARD CYPHER_COMMON_API
data_validation_result_t DataValidation_CheckResourcePath(
    string_view_t path,
    string_view_t extension,
    usize cchMax ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t DataValidation_Succeeded(
    const data_validation_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *DataValidation_StatusName(
    data_validation_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER2_DATAVALIDATION_H
