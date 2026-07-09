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

const char *Cy_SourceLocation_Format( const source_location_t &location, char *pDest, usize cchDest )
{
    if ( pDest == nullptr || cchDest == 0u ) {
        return "";
    }

    std::snprintf( pDest,
                   cchDest,
                   "%s:%u:%s",
                   location.pFile != nullptr ? location.pFile : "",
                   location.line,
                   location.pFunction != nullptr ? location.pFunction : "" );
    pDest[cchDest - 1u] = '\0';
    return pDest;
}

} // namespace cypher::common
