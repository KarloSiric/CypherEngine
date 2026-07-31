//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Crash.h
//  Purpose: Declares CypherCommon Tier0 Crash support.
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

#ifndef CYPHER_COMMON_TIER0_CRASH_H
#define CYPHER_COMMON_TIER0_CRASH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Crash

Low-level synchronous fatal reporting. This API is allocation-free and separate
from native unhandled-exception or signal installation.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_SourceLocation.h"

namespace cypher::common
{

struct crash_info_t {
    const char *pReason{ "" };
    source_location_t location{};
};

using crash_handler_t = void ( * )( const crash_info_t &info ) noexcept;

// Installs a process-wide synchronous crash handler.
CYPHER_COMMON_API void Cy_CrashSetHandler( crash_handler_t pHandler ) noexcept;

// Returns the current handler, or nullptr when fallback reporting is active.
CYPHER_NODISCARD CYPHER_COMMON_API crash_handler_t Cy_CrashGetHandler() noexcept;

// Reports fatal context without terminating. Intended for tests and for callers
// that must perform one final operation before explicitly trapping.
CYPHER_COMMON_API void Cy_CrashReport(
    const char *pReason,
    source_location_t location ) noexcept;

// Reports fatal context and terminates the process.
[[noreturn]] CYPHER_COMMON_API void Cy_CrashTrigger(
    const char *pReason,
    source_location_t location ) noexcept;

} // namespace cypher::common

#define CY_CRASH( reason ) \
    ::cypher::common::Cy_CrashTrigger( reason, CY_SOURCE_LOCATION )

#endif // CYPHER_COMMON_TIER0_CRASH_H
