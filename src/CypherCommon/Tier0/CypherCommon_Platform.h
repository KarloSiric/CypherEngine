//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Platform.h
//  Purpose: Declares CypherCommon Tier0 Platform support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_PLATFORM_H
#define CYPHER_COMMON_TIER0_PLATFORM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Platform

Central compiler, operating system, architecture, endian and build detection.

Rules:
- Raw target macros such as _WIN32, __linux__, __APPLE__, _MSC_VER and
  __clang__ are normalized here.
- Other Cypher code should use CYPHER_* target macros instead of raw compiler
  or operating system macros.
- All public target macros are defined as 0 or 1.
================
*/

/*
================
C++ Standard Detection
================
*/
#if !defined( __cplusplus )
    #error "CypherCommon requires C++."
#endif

#if defined( _MSVC_LANG )
    #define CYPHER_CPP_STANDARD _MSVC_LANG
#else
    #define CYPHER_CPP_STANDARD __cplusplus
#endif

#if CYPHER_CPP_STANDARD < 202002L
    #error "CypherCommon requires C++20 or newer."
#endif

/*
================
Compiler Detection
================
*/
#if defined( _MSC_VER )
    #define CYPHER_COMPILER_MSVC 1
    #define CYPHER_COMPILER_CLANG 0
    #define CYPHER_COMPILER_GCC 0
    #define CYPHER_COMPILER_NAME "MSVC"
    #define CYPHER_COMPILER_VERSION _MSC_VER
#elif defined( __clang__ )
    #define CYPHER_COMPILER_MSVC 0
    #define CYPHER_COMPILER_CLANG 1
    #define CYPHER_COMPILER_GCC 0
    #define CYPHER_COMPILER_NAME "Clang"
    #define CYPHER_COMPILER_VERSION ( ( __clang_major__ * 10000 ) + ( __clang_minor__ * 100 ) + __clang_patchlevel__ )
#elif defined( __GNUC__ )
    #define CYPHER_COMPILER_MSVC 0
    #define CYPHER_COMPILER_CLANG 0
    #define CYPHER_COMPILER_GCC 1
    #define CYPHER_COMPILER_NAME "GCC"
    #define CYPHER_COMPILER_VERSION ( ( __GNUC__ * 10000 ) + ( __GNUC_MINOR__ * 100 ) + __GNUC_PATCHLEVEL__ )
#else
    #error "Unsupported Cypher compiler."
#endif

#if ( CYPHER_COMPILER_MSVC + CYPHER_COMPILER_CLANG + CYPHER_COMPILER_GCC ) != 1
    #error "Cypher compiler detection must resolve to exactly one compiler."
#endif

/*
================
Operating System Detection
================
*/
#if defined( _WIN32 )
    #define CYPHER_PLATFORM_WINDOWS 1
    #define CYPHER_PLATFORM_LINUX 0
    #define CYPHER_PLATFORM_MACOS 0
    #define CYPHER_PLATFORM_POSIX 0
    #define CYPHER_PLATFORM_NAME "Windows"
#elif defined( __linux__ )
    #define CYPHER_PLATFORM_WINDOWS 0
    #define CYPHER_PLATFORM_LINUX 1
    #define CYPHER_PLATFORM_MACOS 0
    #define CYPHER_PLATFORM_POSIX 1
    #define CYPHER_PLATFORM_NAME "Linux"
#elif defined( __APPLE__ ) && defined( __MACH__ )
    #define CYPHER_PLATFORM_WINDOWS 0
    #define CYPHER_PLATFORM_LINUX 0
    #define CYPHER_PLATFORM_MACOS 1
    #define CYPHER_PLATFORM_POSIX 1
    #define CYPHER_PLATFORM_NAME "macOS"
#else
    #error "Unsupported Cypher platform."
#endif

#if ( CYPHER_PLATFORM_WINDOWS + CYPHER_PLATFORM_LINUX + CYPHER_PLATFORM_MACOS ) != 1
    #error "Cypher platform detection must resolve to exactly one platform."
#endif

/*
================
CPU Architecture Detection
================
*/
#if defined( _M_X64 ) || defined( __x86_64__ ) || defined( __amd64__ )
    #define CYPHER_ARCH_X64 1
    #define CYPHER_ARCH_X86 0
    #define CYPHER_ARCH_ARM64 0
    #define CYPHER_ARCH_ARM32 0
    #define CYPHER_ARCH_NAME "x64"
#elif defined( _M_IX86 ) || defined( __i386__ )
    #define CYPHER_ARCH_X64 0
    #define CYPHER_ARCH_X86 1
    #define CYPHER_ARCH_ARM64 0
    #define CYPHER_ARCH_ARM32 0
    #define CYPHER_ARCH_NAME "x86"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
    #define CYPHER_ARCH_X64 0
    #define CYPHER_ARCH_X86 0
    #define CYPHER_ARCH_ARM64 1
    #define CYPHER_ARCH_ARM32 0
    #define CYPHER_ARCH_NAME "arm64"
#elif defined( _M_ARM ) || defined( __arm__ )
    #define CYPHER_ARCH_X64 0
    #define CYPHER_ARCH_X86 0
    #define CYPHER_ARCH_ARM64 0
    #define CYPHER_ARCH_ARM32 1
    #define CYPHER_ARCH_NAME "arm32"
#else
    #error "Unsupported Cypher CPU architecture."
#endif

#if ( CYPHER_ARCH_X64 + CYPHER_ARCH_X86 + CYPHER_ARCH_ARM64 + CYPHER_ARCH_ARM32 ) != 1
    #error "Cypher architecture detection must resolve to exactly one architecture."
#endif

#if CYPHER_ARCH_X64 || CYPHER_ARCH_X86
    #define CYPHER_ARCH_X86_FAMILY 1
    #define CYPHER_ARCH_ARM_FAMILY 0
#elif CYPHER_ARCH_ARM64 || CYPHER_ARCH_ARM32
    #define CYPHER_ARCH_X86_FAMILY 0
    #define CYPHER_ARCH_ARM_FAMILY 1
#endif

/*
================
Pointer Width Detection
================
*/
#if defined( _WIN64 ) || defined( __LP64__ ) || defined( _LP64 ) || \
    defined( __x86_64__ ) || defined( __aarch64__ ) || defined( _M_X64 ) || defined( _M_ARM64 )
    #define CYPHER_TARGET_64BIT 1
    #define CYPHER_TARGET_32BIT 0
    #define CYPHER_POINTER_SIZE 8
#else
    #define CYPHER_TARGET_64BIT 0
    #define CYPHER_TARGET_32BIT 1
    #define CYPHER_POINTER_SIZE 4
#endif

#if ( CYPHER_TARGET_64BIT + CYPHER_TARGET_32BIT ) != 1
    #error "Cypher pointer width detection must resolve to exactly one width."
#endif

/*
================
Endian Detection
================
*/
#if CYPHER_COMPILER_MSVC && ( CYPHER_ARCH_X86_FAMILY || CYPHER_ARCH_ARM_FAMILY )
    #define CYPHER_ENDIAN_LITTLE 1
    #define CYPHER_ENDIAN_BIG 0
#elif defined( __BYTE_ORDER__ ) && defined( __ORDER_LITTLE_ENDIAN__ ) && ( __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ )
    #define CYPHER_ENDIAN_LITTLE 1
    #define CYPHER_ENDIAN_BIG 0
#elif defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && ( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )
    #define CYPHER_ENDIAN_LITTLE 0
    #define CYPHER_ENDIAN_BIG 1
#else
    #error "Unsupported Cypher endian target."
#endif

#if ( CYPHER_ENDIAN_LITTLE + CYPHER_ENDIAN_BIG ) != 1
    #error "Cypher endian detection must resolve to exactly one byte order."
#endif

/*
================
Build Configuration Detection
================
*/
#if defined( NDEBUG )
    #define CYPHER_BUILD_DEBUG 0
    #define CYPHER_BUILD_RELEASE 1
#else
    #define CYPHER_BUILD_DEBUG 1
    #define CYPHER_BUILD_RELEASE 0
#endif

#if ( CYPHER_BUILD_DEBUG + CYPHER_BUILD_RELEASE ) != 1
    #error "Cypher build detection must resolve to exactly one configuration."
#endif

/*
================
C++ Feature Detection
================
*/
#if defined( _CPPUNWIND ) || defined( __EXCEPTIONS )
    #define CYPHER_CPP_EXCEPTIONS 1
#else
    #define CYPHER_CPP_EXCEPTIONS 0
#endif

#if defined( _CPPRTTI ) || defined( __GXX_RTTI )
    #define CYPHER_CPP_RTTI 1
#else
    #define CYPHER_CPP_RTTI 0
#endif

#endif // CYPHER_COMMON_TIER0_PLATFORM_H
