//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_SourceLocation.h
//  Purpose: Defines the source-code location record used by diagnostics.
//  Details: File and function strings are compiler-owned static storage and are never freed.
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

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_API.h"
#include "CypherCommon_Defines.h"

#include <source_location>

namespace cypher::common
{

struct source_location_t {
    const char *pFile{ "" };     // Compiler-owned source path; empty when unavailable.
    const char *pFunction{ "" }; // Compiler-owned function name; empty when unavailable.
    u32 line{ 0u };               // One-based source line; zero means unknown.
    u32 column{ 0u };             // One-based source column; zero means unknown.
};

// The default argument is evaluated at the call site, not inside this function.
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
CYPHER_NODISCARD constexpr bool_t Cy_SourceLocation_IsValid( const source_location_t &location ) noexcept
{
    return location.pFile != nullptr && location.pFile[0] != '\0' && location.line != 0u;
}

// Returns the required character count excluding the terminator. Passing a null
// or zero-sized destination performs a size query without writing output.
CYPHER_NODISCARD CYPHER_COMMON_API usize Cy_SourceLocation_Format(
    const source_location_t &location,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#define CY_SOURCE_LOCATION ::cypher::common::Cy_SourceLocation_Current()

#endif // CYPHER_COMMON_TIER0_SOURCELOCATION_H
