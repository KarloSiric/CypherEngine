//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Minidump.h
//  Purpose: Declares CypherCommon Tier0 Minidump support.
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

#ifndef CYPHER_COMMON_TIER0_MINIDUMP_H
#define CYPHER_COMMON_TIER0_MINIDUMP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Minidump

Portable diagnostic dump declarations for crash and validation reporting.
This Tier0 layer writes a deterministic text report, not a Windows native
minidump or POSIX core dump. Native crash artifacts require separate backends.
================
*/

#include "CypherCommon_Annotations.h"
#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_SourceLocation.h"
#include "CypherCommon_StackTrace.h"

namespace cypher::common
{

enum class minidump_result_t : u8 {
    Ok = 0u,
    InvalidArgument,
    PathTooLong,
    Busy,
    OpenFailed,
    WriteFailed,
    Count
};

struct minidump_info_t {
    const char *pApplicationName{ "" };
    const char *pVersion{ "" };
    const char *pOutputPath{ nullptr };
    const char *pReason{ "" };
    source_location_t location{};
    u32 maxFrames{ CYPHER_STACK_TRACE_MAX_FRAMES };
    u32 skipFrames{ 1u };
};

// Writes one portable text diagnostic report. It returns Busy rather than
// blocking when another thread is already writing a report.
CYPHER_NODISCARD CYPHER_COMMON_API minidump_result_t Cy_MinidumpWrite(
    const minidump_info_t &info ) noexcept;

// Sets the default output path. Null or empty input restores the built-in path.
CYPHER_NODISCARD CYPHER_COMMON_API minidump_result_t Cy_MinidumpSetOutputPath(
    const char *pPath ) noexcept;

// Copies the effective default path and returns its required character count,
// excluding the null terminator. Null output performs a size query.
CYPHER_NODISCARD CYPHER_COMMON_API usize Cy_MinidumpGetOutputPath(
    char *pDest,
    usize cchDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_MinidumpResultName(
    minidump_result_t result ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MINIDUMP_H
