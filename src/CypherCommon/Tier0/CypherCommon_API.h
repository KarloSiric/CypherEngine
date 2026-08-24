//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_API.h
//  Purpose: Defines symbol visibility and calling conventions for module APIs.
//  Details: Static builds erase visibility decoration; shared builds select export
//           while producing a module and import while consuming it.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_API_H
#define CYPHER_COMMON_TIER0_API_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Platform.h"

// Generic symbol visibility used by subsystem-specific API macros.
#if CYPHER_PLATFORM_WINDOWS
    #define CYPHER_API_EXPORT __declspec( dllexport )
    #define CYPHER_API_IMPORT __declspec( dllimport )
    #define CYPHER_API_LOCAL
#elif CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    #define CYPHER_API_EXPORT __attribute__(( visibility( "default" ) ))
    #define CYPHER_API_IMPORT __attribute__(( visibility( "default" ) ))
    #define CYPHER_API_LOCAL  __attribute__(( visibility( "hidden" ) ))
#else
    #define CYPHER_API_EXPORT
    #define CYPHER_API_IMPORT
    #define CYPHER_API_LOCAL
#endif

// C linkage prevents C++ name mangling at dynamically discovered entry points.
#define CYPHER_EXTERN_C extern "C"
#define CYPHER_EXTERN_C_BEGIN extern "C" {
#define CYPHER_EXTERN_C_END }

#if CYPHER_PLATFORM_WINDOWS
    #define CYPHER_CALL __cdecl
#else
    #define CYPHER_CALL
#endif

// Exactly one linkage role may be selected for a translation unit.
#if defined( CYPHER_COMMON_STATIC ) && ( defined( CYPHER_COMMON_BUILD_DLL ) || defined( CYPHER_COMMON_USE_DLL ) )
    #error "CypherCommon cannot be both static and shared."
#endif

#if defined( CYPHER_COMMON_BUILD_DLL ) && defined( CYPHER_COMMON_USE_DLL )
    #error "CypherCommon cannot build and consume the shared library simultaneously."
#endif

#if defined( CYPHER_COMMON_STATIC )
    #define CYPHER_COMMON_API                 // Symbols resolve from the linked archive.
#elif defined( CYPHER_COMMON_BUILD_DLL )
    #define CYPHER_COMMON_API CYPHER_API_EXPORT // Producing the shared library.
#elif defined( CYPHER_COMMON_USE_DLL )
    #define CYPHER_COMMON_API CYPHER_API_IMPORT // Consuming the shared library.
#else
    #define CYPHER_COMMON_API                 // Default static/monolithic build.
#endif

#endif // CYPHER_COMMON_TIER0_API_H
