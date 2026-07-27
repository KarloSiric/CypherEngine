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
- All Boolean target and feature macros are always defined as 0 or 1.
- CypherEngine currently supports 64-bit, little-endian desktop targets only.
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
#if defined( __clang__ )
    #define CYPHER_COMPILER_MSVC 0
    #define CYPHER_COMPILER_CLANG 1
    #define CYPHER_COMPILER_GCC 0
    #if defined( _MSC_VER )
        #define CYPHER_COMPILER_CLANG_CL 1
        #define CYPHER_COMPILER_MSVC_ABI 1
        #define CYPHER_COMPILER_NAME "clang-cl"
    #else
        #define CYPHER_COMPILER_CLANG_CL 0
        #define CYPHER_COMPILER_MSVC_ABI 0
        #define CYPHER_COMPILER_NAME "Clang"
    #endif
    #define CYPHER_COMPILER_VERSION_MAJOR __clang_major__
    #define CYPHER_COMPILER_VERSION_MINOR __clang_minor__
    #define CYPHER_COMPILER_VERSION_PATCH __clang_patchlevel__
#elif defined( _MSC_VER )
    #define CYPHER_COMPILER_MSVC 1
    #define CYPHER_COMPILER_CLANG 0
    #define CYPHER_COMPILER_GCC 0
    #define CYPHER_COMPILER_CLANG_CL 0
    #define CYPHER_COMPILER_MSVC_ABI 1
    #define CYPHER_COMPILER_NAME "MSVC"
    #define CYPHER_COMPILER_VERSION_MAJOR ( _MSC_VER / 100 )
    #define CYPHER_COMPILER_VERSION_MINOR ( _MSC_VER % 100 )
    #if defined( _MSC_FULL_VER )
        #define CYPHER_COMPILER_VERSION_PATCH ( _MSC_FULL_VER % 100000 )
    #else
        #define CYPHER_COMPILER_VERSION_PATCH 0
    #endif
#elif defined( __GNUC__ )
    #define CYPHER_COMPILER_MSVC 0
    #define CYPHER_COMPILER_CLANG 0
    #define CYPHER_COMPILER_GCC 1
    #define CYPHER_COMPILER_CLANG_CL 0
    #define CYPHER_COMPILER_MSVC_ABI 0
    #define CYPHER_COMPILER_NAME "GCC"
    #define CYPHER_COMPILER_VERSION_MAJOR __GNUC__
    #define CYPHER_COMPILER_VERSION_MINOR __GNUC_MINOR__
    #define CYPHER_COMPILER_VERSION_PATCH __GNUC_PATCHLEVEL__
#else
    #error "Unsupported Cypher compiler."
#endif

#define CYPHER_COMPILER_VERSION                                                                                  \
    ( ( ( CYPHER_COMPILER_VERSION_MAJOR ) << 24u ) | ( ( CYPHER_COMPILER_VERSION_MINOR ) << 16u ) |             \
      ( ( CYPHER_COMPILER_VERSION_PATCH ) & 0xFFFFu ) )

#if ( CYPHER_COMPILER_MSVC + CYPHER_COMPILER_CLANG + CYPHER_COMPILER_GCC ) != 1
    #error "Cypher compiler detection must resolve to exactly one compiler."
#endif

#if ( CYPHER_COMPILER_CLANG_CL != 0 ) && ( CYPHER_COMPILER_CLANG_CL != 1 )
    #error "CYPHER_COMPILER_CLANG_CL must be either 0 or 1."
#endif

#if ( CYPHER_COMPILER_MSVC_ABI != 0 ) && ( CYPHER_COMPILER_MSVC_ABI != 1 )
    #error "CYPHER_COMPILER_MSVC_ABI must be either 0 or 1."
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
    #include <TargetConditionals.h>
    #if !TARGET_OS_OSX
        #error "CypherEngine currently supports macOS desktop targets only."
    #endif
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
#if defined( _M_ARM64EC )
    #define CYPHER_ARCH_X64 0
    #define CYPHER_ARCH_X86 0
    #define CYPHER_ARCH_ARM64 1
    #define CYPHER_ARCH_ARM32 0
    #define CYPHER_ARCH_ARM64EC 1
    #define CYPHER_ARCH_NAME "arm64ec"
#elif defined( _M_X64 ) || defined( __x86_64__ ) || defined( __amd64__ )
    #define CYPHER_ARCH_X64 1
    #define CYPHER_ARCH_X86 0
    #define CYPHER_ARCH_ARM64 0
    #define CYPHER_ARCH_ARM32 0
    #define CYPHER_ARCH_ARM64EC 0
    #define CYPHER_ARCH_NAME "x64"
#elif defined( _M_IX86 ) || defined( __i386__ )
    #define CYPHER_ARCH_X64 0
    #define CYPHER_ARCH_X86 1
    #define CYPHER_ARCH_ARM64 0
    #define CYPHER_ARCH_ARM32 0
    #define CYPHER_ARCH_ARM64EC 0
    #define CYPHER_ARCH_NAME "x86"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
    #define CYPHER_ARCH_X64 0
    #define CYPHER_ARCH_X86 0
    #define CYPHER_ARCH_ARM64 1
    #define CYPHER_ARCH_ARM32 0
    #define CYPHER_ARCH_ARM64EC 0
    #define CYPHER_ARCH_NAME "arm64"
#elif defined( _M_ARM ) || defined( __arm__ )
    #define CYPHER_ARCH_X64 0
    #define CYPHER_ARCH_X86 0
    #define CYPHER_ARCH_ARM64 0
    #define CYPHER_ARCH_ARM32 1
    #define CYPHER_ARCH_ARM64EC 0
    #define CYPHER_ARCH_NAME "arm32"
#else
    #error "Unsupported Cypher CPU architecture."
#endif

#if ( CYPHER_ARCH_X64 + CYPHER_ARCH_X86 + CYPHER_ARCH_ARM64 + CYPHER_ARCH_ARM32 ) != 1
    #error "Cypher architecture detection must resolve to exactly one architecture."
#endif

#if CYPHER_ARCH_X86 || CYPHER_ARCH_ARM32
    #error "CypherEngine currently requires a 64-bit x64 or ARM64 target."
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
#if CYPHER_ARCH_X64 || CYPHER_ARCH_ARM64
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
#if CYPHER_PLATFORM_WINDOWS && ( CYPHER_ARCH_X86_FAMILY || CYPHER_ARCH_ARM_FAMILY )
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

#if CYPHER_ENDIAN_BIG
    #error "CypherEngine currently requires a little-endian target."
#endif

/*
================
Build Configuration Detection
================
*/
#if !defined( CYPHER_CONFIG_DEBUG )
    #define CYPHER_CONFIG_DEBUG 0
#endif

#if !defined( CYPHER_CONFIG_DEVELOPMENT )
    #define CYPHER_CONFIG_DEVELOPMENT 0
#endif

#if !defined( CYPHER_CONFIG_RELEASE )
    #define CYPHER_CONFIG_RELEASE 0
#endif

#if !defined( CYPHER_CONFIG_SHIPPING )
    #define CYPHER_CONFIG_SHIPPING 0
#endif

#if ( CYPHER_CONFIG_DEBUG + CYPHER_CONFIG_DEVELOPMENT + CYPHER_CONFIG_RELEASE + CYPHER_CONFIG_SHIPPING ) == 0
    #if defined( NDEBUG )
        #undef CYPHER_CONFIG_RELEASE
        #define CYPHER_CONFIG_RELEASE 1
    #else
        #undef CYPHER_CONFIG_DEBUG
        #define CYPHER_CONFIG_DEBUG 1
    #endif
#endif

#if ( CYPHER_CONFIG_DEBUG + CYPHER_CONFIG_DEVELOPMENT + CYPHER_CONFIG_RELEASE + CYPHER_CONFIG_SHIPPING ) != 1
    #error "Cypher build detection must resolve to exactly one configuration."
#endif

#define CYPHER_BUILD_DEBUG CYPHER_CONFIG_DEBUG
#define CYPHER_BUILD_DEVELOPMENT CYPHER_CONFIG_DEVELOPMENT
#define CYPHER_BUILD_RELEASE CYPHER_CONFIG_RELEASE
#define CYPHER_BUILD_SHIPPING CYPHER_CONFIG_SHIPPING
#define CYPHER_BUILD_OPTIMIZED ( CYPHER_CONFIG_DEVELOPMENT || CYPHER_CONFIG_RELEASE || CYPHER_CONFIG_SHIPPING )

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
