//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_API.h
//  Purpose: Declares CypherMath symbol-visibility policy.
//  Details: Math is built statically today, but a dedicated boundary prevents its
//           ABI policy from being coupled to the CypherCommon Tier0 library.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_MATH_API_H
#define CYPHER_COMMON_MATH_API_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_API.h"

#if defined( CYPHER_MATH_STATIC ) && \
    ( defined( CYPHER_MATH_BUILD_DLL ) || defined( CYPHER_MATH_USE_DLL ) )
    #error "CypherMath cannot be both static and shared."
#endif

#if defined( CYPHER_MATH_BUILD_DLL ) && defined( CYPHER_MATH_USE_DLL )
    #error "CypherMath cannot build and consume the shared library simultaneously."
#endif

#if defined( CYPHER_MATH_STATIC )
    #define CYPHER_MATH_API
#elif defined( CYPHER_MATH_BUILD_DLL )
    #define CYPHER_MATH_API CYPHER_API_EXPORT
#elif defined( CYPHER_MATH_USE_DLL )
    #define CYPHER_MATH_API CYPHER_API_IMPORT
#else
    #define CYPHER_MATH_API
#endif

#endif // CYPHER_COMMON_MATH_API_H
