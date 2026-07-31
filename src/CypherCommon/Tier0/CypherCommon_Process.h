//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Process.h
//  Purpose: Declares CypherCommon Tier0 Process support.
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

#ifndef CYPHER_COMMON_TIER0_PROCESS_H
#define CYPHER_COMMON_TIER0_PROCESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Process

Process identity and process utility declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using process_id_t = u64;
constexpr usize CY_PROCESS_PATH_MAX = 4096u;

// Returns the host process identifier.
CYPHER_NODISCARD CYPHER_COMMON_API process_id_t Cy_ProcessGetCurrentId() noexcept;

// Returns an immutable UTF-8 process-lifetime executable path snapshot.
CYPHER_NODISCARD CYPHER_COMMON_API const char *Cy_ProcessGetExecutablePath() noexcept;

// Performs normal C runtime process termination.
[[noreturn]] CYPHER_COMMON_API void Cy_ProcessExit( i32 nExitCode ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PROCESS_H
