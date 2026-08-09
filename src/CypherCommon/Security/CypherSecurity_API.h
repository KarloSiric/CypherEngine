//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Security/CypherSecurity_API.h
//  Purpose: Declares CypherSecurity symbol visibility.
//  Details: Security is static today while retaining an explicit boundary for a
//           future shared tools or dedicated-server deployment.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_SECURITY_API_H
#define CYPHER_SECURITY_API_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_API.h"

#if defined( CYPHER_SECURITY_STATIC ) && \
    ( defined( CYPHER_SECURITY_BUILD_DLL ) || defined( CYPHER_SECURITY_USE_DLL ) )
    #error "CypherSecurity cannot be both static and shared."
#endif

#if defined( CYPHER_SECURITY_BUILD_DLL ) && defined( CYPHER_SECURITY_USE_DLL )
    #error "CypherSecurity cannot build and consume its shared library simultaneously."
#endif

#if defined( CYPHER_SECURITY_STATIC )
    #define CYPHER_SECURITY_API
#elif defined( CYPHER_SECURITY_BUILD_DLL )
    #define CYPHER_SECURITY_API CYPHER_API_EXPORT
#elif defined( CYPHER_SECURITY_USE_DLL )
    #define CYPHER_SECURITY_API CYPHER_API_IMPORT
#else
    #define CYPHER_SECURITY_API
#endif

#endif // CYPHER_SECURITY_API_H
