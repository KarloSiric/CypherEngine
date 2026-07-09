//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Environment.cpp
//  Purpose: Implements CypherCommon Tier0 environment variable helpers.
//  Details: Tools and runtime diagnostics use this layer instead of scattering
//           platform-specific environment access.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Environment.h"

#include "CypherCommon_Platform.h"

#include <cstdlib>
#include <cstring>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace cypher::common
{

usize Environment_Get( const char *pName, char *pDest, usize cchDest )
{
    if ( pDest != nullptr && cchDest != 0u ) {
        pDest[0] = '\0';
    }

    if ( pName == nullptr || pName[0] == '\0' ) {
        return 0u;
    }

#if CYPHER_PLATFORM_WINDOWS
    const DWORD cchRequired = ::GetEnvironmentVariableA( pName, pDest, static_cast<DWORD>( cchDest ) );
    if ( cchRequired == 0u ) {
        return 0u;
    }
    if ( pDest != nullptr && cchDest != 0u ) {
        pDest[cchDest - 1u] = '\0';
    }
    return static_cast<usize>( cchRequired );
#else
    const char *pValue = std::getenv( pName );
    if ( pValue == nullptr ) {
        return 0u;
    }

    const usize cchRequired = std::strlen( pValue );
    if ( pDest != nullptr && cchDest != 0u ) {
        usize i = 0u;
        for ( ; i + 1u < cchDest && pValue[i] != '\0'; ++i ) {
            pDest[i] = pValue[i];
        }
        pDest[i] = '\0';
    }
    return cchRequired;
#endif
}

bool_t Environment_Set( const char *pName, const char *pValue )
{
    if ( pName == nullptr || pName[0] == '\0' ) {
        return CY_FALSE;
    }

    const char *pWriteValue = pValue != nullptr ? pValue : "";

#if CYPHER_PLATFORM_WINDOWS
    return ::SetEnvironmentVariableA( pName, pWriteValue ) != 0;
#else
    return ::setenv( pName, pWriteValue, 1 ) == 0;
#endif
}

bool_t Environment_Has( const char *pName )
{
    return Environment_Get( pName, nullptr, 0u ) != 0u;
}

} // namespace cypher::common
