//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_SourceLocation.cpp
//  Purpose: Implements CypherCommon Tier0 source location formatting.
//  Details: Source locations appear in assertions, logs, profiling zones, crash
//           reports, and validation diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SourceLocation.h"

#include <cstdio>

namespace cypher::common
{

usize Cy_SourceLocation_Format( const source_location_t &location, char *pDest, usize cchDest ) noexcept
{
    const char *pFile = location.pFile != nullptr && location.pFile[0] != '\0'
        ? location.pFile
        : "<unknown>";
    const char *pFunction = location.pFunction != nullptr && location.pFunction[0] != '\0'
        ? location.pFunction
        : "<unknown>";

    const usize cchWrite = pDest != nullptr ? cchDest : 0u;
    int cchRequired = 0;
    if ( location.column != 0u ) {
        cchRequired = std::snprintf( pDest,
                                     cchWrite,
                                     "%s:%u:%u:%s",
                                     pFile,
                                     location.line,
                                     location.column,
                                     pFunction );
    } else {
        cchRequired = std::snprintf( pDest,
                                     cchWrite,
                                     "%s:%u:%s",
                                     pFile,
                                     location.line,
                                     pFunction );
    }

    if ( pDest != nullptr && cchDest != 0u ) {
        pDest[cchDest - 1u] = '\0';
    }

    return cchRequired > 0 ? static_cast<usize>( cchRequired ) : 0u;
}

} // namespace cypher::common
