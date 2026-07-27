//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CommandLineBase.h
//  Purpose: Declares CypherCommon Tier0 CommandLineBase support.
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

#ifndef CYPHER_COMMON_TIER0_COMMANDLINEBASE_H
#define CYPHER_COMMON_TIER0_COMMANDLINEBASE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Command Line Base

Low-level process command line declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

constexpr usize CY_COMMANDLINEBASE_MAX_ARGS = 128u;

struct command_line_base_t {
    const char *ppszArgs[CY_COMMANDLINEBASE_MAX_ARGS] = {};
    usize nArgCount = 0u;
    bool_t isTruncated = CY_FALSE;
};

// Borrows argv pointers; their strings must outlive the command-line object.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_CommandLineBaseSet(
    command_line_base_t *pCommandLine,
    i32 nArgCount,
    const char *const *ppszArgs ) noexcept;

// Returns an inline/next-token value, "" for a valueless switch, or null if absent.
[[nodiscard]] CYPHER_COMMON_API const char *Cy_CommandLineBaseFindValue(
    const command_line_base_t *pCommandLine,
    const char *pszName ) noexcept;

[[nodiscard]] CYPHER_COMMON_API bool_t Cy_CommandLineBaseHasSwitch(
    const command_line_base_t *pCommandLine,
    const char *pszName ) noexcept;

[[nodiscard]] CYPHER_COMMON_API usize Cy_CommandLineBaseGetCount(
    const command_line_base_t *pCommandLine ) noexcept;

[[nodiscard]] CYPHER_COMMON_API const char *Cy_CommandLineBaseGetArg(
    const command_line_base_t *pCommandLine,
    usize nIndex ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_COMMANDLINEBASE_H
