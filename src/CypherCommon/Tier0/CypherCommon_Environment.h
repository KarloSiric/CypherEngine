//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Environment.h
//  Purpose: Declares CypherCommon Tier0 Environment support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
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
    usize cchRequired;
    bool_t exists;
    bool_t isTruncated;
};

// Reads a process environment value. cchRequired excludes the null terminator.
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
