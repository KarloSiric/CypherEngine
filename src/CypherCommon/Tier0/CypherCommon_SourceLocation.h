//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_SourceLocation.h
//  Purpose: Declares CypherCommon Tier0 SourceLocation support.
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

#ifndef CYPHER_COMMON_TIER0_SOURCELOCATION_H
#define CYPHER_COMMON_TIER0_SOURCELOCATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Source Location

Small source-code location record used by assertions, logs, profiling zones
and diagnostics.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_API.h"
#include "CypherCommon_Defines.h"

#include <source_location>

namespace cypher::common
{

struct source_location_t {
    const char *pFile{ "" };
    const char *pFunction{ "" };
    u32 line{ 0u };
    u32 column{ 0u };
};

// Captures a source location supplied by the compiler at the caller.
CYPHER_NODISCARD constexpr source_location_t Cy_SourceLocation_Current(
    const std::source_location &location = std::source_location::current() ) noexcept
{
    return {
        location.file_name(),
        location.function_name(),
        static_cast<u32>( location.line() ),
        static_cast<u32>( location.column() )
    };
}
// Returns true when the record identifies a source file and line.
CYPHER_NODISCARD constexpr bool_t Cy_SourceLocation_IsValid( const source_location_t &location ) noexcept
{
    return location.pFile != nullptr && location.pFile[0] != '\0' && location.line != 0u;
}

// Formats a source location and returns the required character count, excluding
// the null terminator. A null or zero-sized destination performs a size query.
CYPHER_NODISCARD CYPHER_COMMON_API usize Cy_SourceLocation_Format(
    const source_location_t &location,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#define CY_SOURCE_LOCATION ::cypher::common::Cy_SourceLocation_Current()

#endif // CYPHER_COMMON_TIER0_SOURCELOCATION_H
