//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Environment.h
//  Purpose: Declares bounded UTF-8 access to process environment variables.
//  Details: Reads support a size-query pass and distinguish a missing variable
//           from a present variable whose value is the empty string.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ENVIRONMENT_H
#define CYPHER_COMMON_TIER0_ENVIRONMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Environment

Environment variable declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

struct cy_environment_get_result_t {
    usize cchRequired; // Value characters required, excluding null terminator.
    bool_t exists;     // Name is present even when cchRequired is zero.
    bool_t isTruncated; // Supplied destination could not hold the whole value.
};

// Reads a process environment value. cchRequired excludes the null terminator.
// A size query uses a null/zero destination. If a supplied buffer is too small,
// isTruncated is true and the destination is left as an empty string.
CYPHER_NODISCARD CYPHER_COMMON_API cy_environment_get_result_t Cy_EnvironmentGet(
    const char *pszName,
    char *pszDst,
    usize cchDst ) noexcept;

// Sets an environment value. An empty string is a present, empty value.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EnvironmentSet(
    const char *pszName,
    const char *pszValue ) noexcept;

// Removes an environment value.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EnvironmentUnset(
    const char *pszName ) noexcept;

// Returns true for present variables, including present empty values.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_EnvironmentHas(
    const char *pszName ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ENVIRONMENT_H
