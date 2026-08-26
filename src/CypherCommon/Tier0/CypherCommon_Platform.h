//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Platform.h
//  Purpose: Normalizes compiler, operating-system, CPU, and build properties.
//  Details: Every target selector produced here is either zero or one so the rest
//           of the engine can use it safely in preprocessor expressions.
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

#include <cstddef> // std::size_t used by the typed target query API.

//=============================================================================
//
// Platform contract
//
// Raw compiler and operating-system defines are translated here. Code outside
// Tier0 should use the CYPHER_* names so unsupported targets fail in one place.
// CypherEngine currently supports 64-bit, little-endian desktop targets only.
//
//=============================================================================

//-----------------------------------------------------------------------------
// C++ language level
//-----------------------------------------------------------------------------
#if !defined( __cplusplus )
    #error "CypherCommon requires C++."
#endif

#if defined( _MSVC_LANG )
    #define CYPHER_CPP_STANDARD _MSVC_LANG  // MSVC reports the selected /std mode here.
#else
    #define CYPHER_CPP_STANDARD __cplusplus // Language level reported by GCC and Clang.
#endif

#if CYPHER_CPP_STANDARD < 202002L
    #error "CypherCommon requires C++20 or newer."
#endif

//-----------------------------------------------------------------------------
// Compiler and ABI
//-----------------------------------------------------------------------------
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

// Packed version used for diagnostics and build records. Use the tuple helper
// below for comparisons because vendor patch components are not equally wide.
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

//-----------------------------------------------------------------------------
// Compiler capability queries
//
// Version numbers are only approximations of feature support. Query the exact
// builtin, attribute, or feature whenever the compiler provides that facility.
//-----------------------------------------------------------------------------
#if defined( __has_builtin )
    #define CYPHER_HAS_BUILTIN( builtin ) __has_builtin( builtin )
#else
    #define CYPHER_HAS_BUILTIN( builtin ) 0
#endif

#if defined( __has_attribute )
    #define CYPHER_HAS_ATTRIBUTE( attribute ) __has_attribute( attribute )
#else
    #define CYPHER_HAS_ATTRIBUTE( attribute ) 0
#endif

#if defined( __has_feature )
    #define CYPHER_HAS_FEATURE( feature ) __has_feature( feature )
#else
    #define CYPHER_HAS_FEATURE( feature ) 0
#endif

// Arguments must be integer literals or other preprocessor constants.
#define CYPHER_COMPILER_VERSION_AT_LEAST( major, minor, patch )                                                  \
    ( ( CYPHER_COMPILER_VERSION_MAJOR > ( major ) ) ||                                                          \
      ( CYPHER_COMPILER_VERSION_MAJOR == ( major ) && CYPHER_COMPILER_VERSION_MINOR > ( minor ) ) ||           \
      ( CYPHER_COMPILER_VERSION_MAJOR == ( major ) && CYPHER_COMPILER_VERSION_MINOR == ( minor ) &&            \
        CYPHER_COMPILER_VERSION_PATCH >= ( patch ) ) )

//-----------------------------------------------------------------------------
// Operating system
//-----------------------------------------------------------------------------
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

#define CYPHER_PLATFORM_DESKTOP \
    ( CYPHER_PLATFORM_WINDOWS || CYPHER_PLATFORM_LINUX || CYPHER_PLATFORM_MACOS )

//-----------------------------------------------------------------------------
// Native host names
//
// These values describe physical host files. Virtual filesystem paths always
// use '/' and must never be constructed with CYPHER_NATIVE_PATH_SEPARATOR.
//-----------------------------------------------------------------------------
#if CYPHER_PLATFORM_WINDOWS
    #define CYPHER_NATIVE_PATH_SEPARATOR_STR "\\"      // Win32 host path separator string form.
    #define CYPHER_NATIVE_PATH_SEPARATOR_CHAR '\\'      // Win32 host path separator char form.
    #define CYPHER_NATIVE_PATH_LIST_SEPARATOR ';'   // PATH-style environment list separator.
    #define CYPHER_SHARED_LIBRARY_PREFIX ""         // foo.dll, never libfoo.dll.
    #define CYPHER_SHARED_LIBRARY_EXTENSION ".dll"
    #define CYPHER_EXECUTABLE_EXTENSION ".exe"
#elif CYPHER_PLATFORM_MACOS
    #define CYPHER_NATIVE_PATH_SEPARATOR_STR "/"    // MacOS/OSX host path separator string form.
    #define CYPHER_NATIVE_PATH_SEPARATOR_CHAR '/'   // MacOS/OSX host path separator char form.
    #define CYPHER_NATIVE_PATH_LIST_SEPARATOR ':'
    #define CYPHER_SHARED_LIBRARY_PREFIX "lib"       // libfoo.dylib.
    #define CYPHER_SHARED_LIBRARY_EXTENSION ".dylib"
    #define CYPHER_EXECUTABLE_EXTENSION ""           // Mach-O executables have no required suffix.
#elif CYPHER_PLATFORM_LINUX
    #define CYPHER_NATIVE_PATH_SEPARATOR_STR "/"    // Linux host path separator string form.
    #define CYPHER_NATIVE_PATH_SEPARATOR_CHAR '/'   // Linux host path separator char form.
    #define CYPHER_NATIVE_PATH_LIST_SEPARATOR ':'
    #define CYPHER_SHARED_LIBRARY_PREFIX "lib"       // libfoo.so.
    #define CYPHER_SHARED_LIBRARY_EXTENSION ".so"
    #define CYPHER_EXECUTABLE_EXTENSION ""           // ELF executables have no required suffix.
#endif

// Compatibility spelling retained while callers migrate to the explicit char form.
#define CYPHER_NATIVE_PATH_SEPARATOR CYPHER_NATIVE_PATH_SEPARATOR_CHAR

//-----------------------------------------------------------------------------
// CPU architecture
//-----------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------
// Pointer width
//-----------------------------------------------------------------------------
#if CYPHER_ARCH_X64 || CYPHER_ARCH_ARM64
    #define CYPHER_TARGET_64BIT 1
    #define CYPHER_TARGET_32BIT 0
    #define CYPHER_POINTER_SIZE 8 // Bytes, not bits.
#else
    #define CYPHER_TARGET_64BIT 0
    #define CYPHER_TARGET_32BIT 1
    #define CYPHER_POINTER_SIZE 4 // Bytes, not bits.
#endif

#if ( CYPHER_TARGET_64BIT + CYPHER_TARGET_32BIT ) != 1
    #error "Cypher pointer width detection must resolve to exactly one width."
#endif

//-----------------------------------------------------------------------------
// Native byte order
//-----------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------
// Build configuration
//-----------------------------------------------------------------------------
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

// Development, release, and shipping targets are expected to enable optimization.
#define CYPHER_BUILD_OPTIMIZED \
    ( CYPHER_CONFIG_DEVELOPMENT || CYPHER_CONFIG_RELEASE || CYPHER_CONFIG_SHIPPING )

//-----------------------------------------------------------------------------
// Optional C++ runtime features
//-----------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------
// Sanitizers
//
// The build may override these when a compiler has no reliable feature macro.
// Keep every value normalized to zero or one; several low-level implementations
// use them to disable operations that sanitizer runtimes cannot instrument.
//-----------------------------------------------------------------------------
#if !defined( CYPHER_SANITIZER_ADDRESS )
    #if defined( __SANITIZE_ADDRESS__ ) || CYPHER_HAS_FEATURE( address_sanitizer )
        #define CYPHER_SANITIZER_ADDRESS 1
    #else
        #define CYPHER_SANITIZER_ADDRESS 0
    #endif
#endif

#if !defined( CYPHER_SANITIZER_THREAD )
    #if defined( __SANITIZE_THREAD__ ) || CYPHER_HAS_FEATURE( thread_sanitizer )
        #define CYPHER_SANITIZER_THREAD 1
    #else
        #define CYPHER_SANITIZER_THREAD 0
    #endif
#endif

#if !defined( CYPHER_SANITIZER_MEMORY )
    #if defined( __SANITIZE_MEMORY__ ) || CYPHER_HAS_FEATURE( memory_sanitizer )
        #define CYPHER_SANITIZER_MEMORY 1
    #else
        #define CYPHER_SANITIZER_MEMORY 0
    #endif
#endif

#if !defined( CYPHER_SANITIZER_UNDEFINED )
    #if defined( __SANITIZE_UNDEFINED__ ) || CYPHER_HAS_FEATURE( undefined_behavior_sanitizer )
        #define CYPHER_SANITIZER_UNDEFINED 1
    #else
        #define CYPHER_SANITIZER_UNDEFINED 0
    #endif
#endif

#define CYPHER_SANITIZER_ANY                                                                                     \
    ( CYPHER_SANITIZER_ADDRESS || CYPHER_SANITIZER_THREAD || CYPHER_SANITIZER_MEMORY ||                          \
      CYPHER_SANITIZER_UNDEFINED )

#if ( CYPHER_SANITIZER_ADDRESS != 0 ) && ( CYPHER_SANITIZER_ADDRESS != 1 )
    #error "CYPHER_SANITIZER_ADDRESS must be either 0 or 1."
#endif

#if ( CYPHER_SANITIZER_THREAD != 0 ) && ( CYPHER_SANITIZER_THREAD != 1 )
    #error "CYPHER_SANITIZER_THREAD must be either 0 or 1."
#endif

#if ( CYPHER_SANITIZER_MEMORY != 0 ) && ( CYPHER_SANITIZER_MEMORY != 1 )
    #error "CYPHER_SANITIZER_MEMORY must be either 0 or 1."
#endif

#if ( CYPHER_SANITIZER_UNDEFINED != 0 ) && ( CYPHER_SANITIZER_UNDEFINED != 1 )
    #error "CYPHER_SANITIZER_UNDEFINED must be either 0 or 1."
#endif

//=============================================================================
//
// Typed target identity
//
// These are compile-time views of the selectors above. Native language types
// are intentional: Platform.h is the dependency root used while BaseTypes and
// Annotations are themselves being declared.
//
//=============================================================================

namespace cypher::common
{

enum class platform_type_t : unsigned char {
    UNKNOWN = 0u, // No supported target matched; unreachable in a valid build.
    WINDOWS,      // Microsoft Windows desktop.
    LINUX,        // Linux desktop.
    MACOS         // Apple macOS desktop.
};

enum class architecture_type_t : unsigned char {
    UNKNOWN = 0u, // No supported instruction-set target matched.
    X86,          // 32-bit x86; recognized but rejected by the current contract.
    X64,          // 64-bit x86-64.
    ARM32,        // 32-bit ARM; recognized but rejected by the current contract.
    ARM64,        // 64-bit AArch64.
    ARM64EC       // Windows ARM64EC compatibility ABI.
};

[[nodiscard]] constexpr platform_type_t Cy_PlatformGetType() noexcept
{
#if CYPHER_PLATFORM_WINDOWS
    return platform_type_t::WINDOWS;
#elif CYPHER_PLATFORM_LINUX
    return platform_type_t::LINUX;
#elif CYPHER_PLATFORM_MACOS
    return platform_type_t::MACOS;
#else
    return platform_type_t::UNKNOWN;
#endif
}

[[nodiscard]] constexpr const char *Cy_PlatformGetName() noexcept
{
    return CYPHER_PLATFORM_NAME;
}

[[nodiscard]] constexpr bool Cy_PlatformIsWindows() noexcept
{
    return CYPHER_PLATFORM_WINDOWS != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsLinux() noexcept
{
    return CYPHER_PLATFORM_LINUX != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsMacOS() noexcept
{
    return CYPHER_PLATFORM_MACOS != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsPosix() noexcept
{
    return CYPHER_PLATFORM_POSIX != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsDesktop() noexcept
{
    return CYPHER_PLATFORM_DESKTOP != 0;
}

[[nodiscard]] constexpr architecture_type_t Cy_PlatformGetArchitecture() noexcept
{
#if CYPHER_ARCH_ARM64EC
    return architecture_type_t::ARM64EC;
#elif CYPHER_ARCH_X64
    return architecture_type_t::X64;
#elif CYPHER_ARCH_X86
    return architecture_type_t::X86;
#elif CYPHER_ARCH_ARM64
    return architecture_type_t::ARM64;
#elif CYPHER_ARCH_ARM32
    return architecture_type_t::ARM32;
#else
    return architecture_type_t::UNKNOWN;
#endif
}

[[nodiscard]] constexpr const char *Cy_PlatformGetArchitectureName() noexcept
{
    return CYPHER_ARCH_NAME;
}

[[nodiscard]] constexpr bool Cy_PlatformIsX64() noexcept
{
    return CYPHER_ARCH_X64 != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsX86() noexcept
{
    return CYPHER_ARCH_X86 != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsArm64() noexcept
{
    return CYPHER_ARCH_ARM64 != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsArm32() noexcept
{
    return CYPHER_ARCH_ARM32 != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsArm64EC() noexcept
{
    return CYPHER_ARCH_ARM64EC != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsX86Family() noexcept
{
    return CYPHER_ARCH_X86_FAMILY != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsArmFamily() noexcept
{
    return CYPHER_ARCH_ARM_FAMILY != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIs64Bit() noexcept
{
    return CYPHER_TARGET_64BIT != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIs32Bit() noexcept
{
    return CYPHER_TARGET_32BIT != 0;
}

[[nodiscard]] constexpr std::size_t Cy_PlatformGetPointerSize() noexcept
{
    return static_cast<std::size_t>( CYPHER_POINTER_SIZE );
}

[[nodiscard]] constexpr bool Cy_PlatformIsLittleEndian() noexcept
{
    return CYPHER_ENDIAN_LITTLE != 0;
}

[[nodiscard]] constexpr bool Cy_PlatformIsBigEndian() noexcept
{
    return CYPHER_ENDIAN_BIG != 0;
}

[[nodiscard]] constexpr char Cy_PlatformGetNativePathSeparator() noexcept
{
    return CYPHER_NATIVE_PATH_SEPARATOR_CHAR;
}

[[nodiscard]] constexpr char Cy_PlatformGetNativePathListSeparator() noexcept
{
    return CYPHER_NATIVE_PATH_LIST_SEPARATOR;
}

[[nodiscard]] constexpr const char *Cy_PlatformGetSharedLibraryPrefix() noexcept
{
    return CYPHER_SHARED_LIBRARY_PREFIX;
}

[[nodiscard]] constexpr const char *Cy_PlatformGetSharedLibraryExtension() noexcept
{
    return CYPHER_SHARED_LIBRARY_EXTENSION;
}

[[nodiscard]] constexpr const char *Cy_PlatformGetExecutableExtension() noexcept
{
    return CYPHER_EXECUTABLE_EXTENSION;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PLATFORM_H
