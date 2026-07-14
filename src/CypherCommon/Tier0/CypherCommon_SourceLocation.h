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
#include "CypherCommon_Defines.h"

namespace cypher::common
{

struct source_location_t {
    const char *pFile;
    const char *pFunction;
    u32 line;
};

const char *Cy_SourceLocation_Format( const source_location_t &location, char *pDest, usize cchDest );

} // namespace cypher::common

#define CY_SOURCE_LOCATION ::cypher::common::source_location_t{ CYPHER_FILE, CYPHER_FUNCTION_NAME, static_cast<::cypher::common::u32>( CYPHER_LINE ) }

#endif // CYPHER_COMMON_TIER0_SOURCELOCATION_H
