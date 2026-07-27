//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Warnings.h
//  Purpose: Declares CypherCommon Tier0 Warnings support.
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

#ifndef CYPHER_COMMON_TIER0_WARNINGS_H
#define CYPHER_COMMON_TIER0_WARNINGS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Warnings

Compiler warning push/pop helpers used around narrow code regions and
third-party headers.

Rules:
- Do not disable warnings globally from here.
- Prefer fixing Cypher code over suppressing warnings.
- Use push/pop so warning policy does not leak into unrelated files.
================
*/

#include "CypherCommon_Platform.h"

/*
================
Pragma Helpers
================
*/
#if CYPHER_COMPILER_MSVC
    #define CYPHER_WARNING_PUSH()                       __pragma( warning( push ) )
    #define CYPHER_WARNING_POP()                        __pragma( warning( pop ) )
    #define CYPHER_WARNING_DISABLE_MSVC( warning_id )   __pragma( warning( disable : warning_id ) )
    #define CYPHER_WARNING_DISABLE_CLANG( warning_name )
    #define CYPHER_WARNING_DISABLE_GCC( warning_name )
#elif CYPHER_COMPILER_CLANG
    #define CYPHER_WARNING_DO_PRAGMA( x )               _Pragma( #x )
    #define CYPHER_WARNING_PUSH()                       CYPHER_WARNING_DO_PRAGMA( clang diagnostic push )
    #define CYPHER_WARNING_POP()                        CYPHER_WARNING_DO_PRAGMA( clang diagnostic pop )
    #define CYPHER_WARNING_DISABLE_MSVC( warning_id )
    #define CYPHER_WARNING_DISABLE_CLANG( warning_name ) CYPHER_WARNING_DO_PRAGMA( clang diagnostic ignored warning_name )
    #define CYPHER_WARNING_DISABLE_GCC( warning_name )
#elif CYPHER_COMPILER_GCC
    #define CYPHER_WARNING_DO_PRAGMA( x )               _Pragma( #x )
    #define CYPHER_WARNING_PUSH()                       CYPHER_WARNING_DO_PRAGMA( GCC diagnostic push )
    #define CYPHER_WARNING_POP()                        CYPHER_WARNING_DO_PRAGMA( GCC diagnostic pop )
    #define CYPHER_WARNING_DISABLE_MSVC( warning_id )
    #define CYPHER_WARNING_DISABLE_CLANG( warning_name )
    #define CYPHER_WARNING_DISABLE_GCC( warning_name )  CYPHER_WARNING_DO_PRAGMA( GCC diagnostic ignored warning_name )
#else
    #error "Unsupported compiler for Cypher warning helpers."
#endif

/*
================
Common Warning Groups
================
*/
#define CYPHER_WARNING_DISABLE_UNUSED_PARAMETER()       \
    CYPHER_WARNING_DISABLE_MSVC( 4100 )                 \
    CYPHER_WARNING_DISABLE_CLANG( "-Wunused-parameter" ) \
    CYPHER_WARNING_DISABLE_GCC( "-Wunused-parameter" )

#define CYPHER_WARNING_DISABLE_CONSTANT_CONDITION()     \
    CYPHER_WARNING_DISABLE_MSVC( 4127 )                 \
    CYPHER_WARNING_DISABLE_CLANG( "-Wconstant-logical-operand" )

#define CYPHER_WARNING_DISABLE_SIGN_CONVERSION()        \
    CYPHER_WARNING_DISABLE_MSVC( 4365 )                 \
    CYPHER_WARNING_DISABLE_CLANG( "-Wsign-conversion" ) \
    CYPHER_WARNING_DISABLE_GCC( "-Wsign-conversion" )

#define CYPHER_WARNING_DISABLE_CONVERSION()              \
    CYPHER_WARNING_DISABLE_MSVC( 4242 )                 \
    CYPHER_WARNING_DISABLE_MSVC( 4244 )                 \
    CYPHER_WARNING_DISABLE_CLANG( "-Wconversion" )      \
    CYPHER_WARNING_DISABLE_GCC( "-Wconversion" )

#define CYPHER_WARNING_DISABLE_DEPRECATED()              \
    CYPHER_WARNING_DISABLE_MSVC( 4996 )                 \
    CYPHER_WARNING_DISABLE_CLANG( "-Wdeprecated-declarations" ) \
    CYPHER_WARNING_DISABLE_GCC( "-Wdeprecated-declarations" )

#define CYPHER_WARNING_DISABLE_SHADOW()                  \
    CYPHER_WARNING_DISABLE_MSVC( 4456 )                 \
    CYPHER_WARNING_DISABLE_MSVC( 4457 )                 \
    CYPHER_WARNING_DISABLE_MSVC( 4458 )                 \
    CYPHER_WARNING_DISABLE_CLANG( "-Wshadow" )          \
    CYPHER_WARNING_DISABLE_GCC( "-Wshadow" )

#define CYPHER_WARNING_DISABLE_DOUBLE_PROMOTION()        \
    CYPHER_WARNING_DISABLE_CLANG( "-Wdouble-promotion" ) \
    CYPHER_WARNING_DISABLE_GCC( "-Wdouble-promotion" )

#define CYPHER_WARNING_DISABLE_OLD_STYLE_CAST()          \
    CYPHER_WARNING_DISABLE_CLANG( "-Wold-style-cast" )  \
    CYPHER_WARNING_DISABLE_GCC( "-Wold-style-cast" )

#define CYPHER_WARNING_DISABLE_SWITCH_ENUM()             \
    CYPHER_WARNING_DISABLE_MSVC( 4061 )                 \
    CYPHER_WARNING_DISABLE_CLANG( "-Wswitch-enum" )     \
    CYPHER_WARNING_DISABLE_GCC( "-Wswitch-enum" )

#endif // CYPHER_COMMON_TIER0_WARNINGS_H
