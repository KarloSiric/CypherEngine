#ifndef CYPHER_COMMON_TIER0_TARGETPLATFORM_H
#define CYPHER_COMMON_TIER0_TARGETPLATFORM_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

// tiny wrapper header used for seperating platform macros
// do now want to overstuff everything inside the main CypherCommmon_Platform.h header file -----------------------------------------------
// -----------------------------------------------------------

namespace cypher::common
{

enum class platform_type_t : u8 {
    UNKNOWN = 0u,
    WINDOWS,
    LINUX,
    MACOS
};

enum class architecture_type_t : u8 {
    UNKNOWN = 0u,
    X86,
    X64,
    ARM32,
    ARM64,
    ARM64EC
};

/*
================
Platform Identity
================
*/

CYPHER_NODISCARD constexpr platform_type_t
Cy_PlatformGetType() noexcept
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

CYPHER_NODISCARD constexpr const char *
Cy_PlatformGetName() noexcept
{
    return CYPHER_PLATFORM_NAME;
}

CYPHER_NODISCARD constexpr bool_t
Cy_PlatformIsWindows() noexcept
{
    return CYPHER_PLATFORM_WINDOWS != 0;
}

CYPHER_NODISCARD constexpr bool_t
Cy_PlatformIsLinux() noexcept
{
    return CYPHER_PLATFORM_LINUX != 0;
}

CYPHER_NODISCARD constexpr bool_t
Cy_PlatformIsMacOS() noexcept
{
    return CYPHER_PLATFORM_MACOS != 0;
}

CYPHER_NODISCARD constexpr bool_t
Cy_PlatformIsPosix() noexcept
{
    return CYPHER_PLATFORM_POSIX != 0;
}
}

#endif
