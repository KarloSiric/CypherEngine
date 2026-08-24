//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolTarget.cpp
//  Purpose: Implements portable tool target and profile parsing.
//  Details: Parsing uses stable lowercase spellings suitable for CLI options,
//           project manifests, cache keys, reports, and remote workers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolTarget.h"

namespace cypher::common
{
namespace
{

bool TextEquals( string_view_t text, const char *pExpected ) noexcept
{
    return StringView_Equals( text, StringView_FromCString( pExpected ) );
}

} // namespace

tool_target_t ToolTarget_Host() noexcept
{
    tool_target_t target{};

    // Platform and architecture are compile-time properties of this executable,
    // not runtime guesses based on the current operating system.
#if CYPHER_PLATFORM_WINDOWS
    target.platform = tool_platform_t::WINDOWS;
#elif CYPHER_PLATFORM_LINUX
    target.platform = tool_platform_t::LINUX;
#elif CYPHER_PLATFORM_MACOS
    target.platform = tool_platform_t::MACOS;
#endif

#if CYPHER_ARCH_X86
    target.architecture = tool_architecture_t::X86;
#elif CYPHER_ARCH_X64
    target.architecture = tool_architecture_t::X64;
#elif CYPHER_ARCH_ARM32
    target.architecture = tool_architecture_t::ARM32;
#elif CYPHER_ARCH_ARM64
    target.architecture = tool_architecture_t::ARM64;
#endif

    return target;
}

bool_t ToolTarget_IsValid( tool_target_t target ) noexcept
{
    return target.platform >= tool_platform_t::WINDOWS &&
           target.platform <= tool_platform_t::MACOS &&
           target.architecture >= tool_architecture_t::X86 &&
           target.architecture <= tool_architecture_t::ARM64;
}

bool_t ToolTarget_Parse( string_view_t text, tool_target_t *pTargetOut ) noexcept
{
    if ( pTargetOut == nullptr || !StringView_IsValid( text ) ) {
        return CY_FALSE;
    }

    // Keep accepted spellings explicit. They are serialized into cache keys,
    // manifests, command lines, and remote-worker requests.
    tool_target_t target{};
    if ( TextEquals( text, "host" ) ) {
        target = ToolTarget_Host();
    } else if ( TextEquals( text, "windows-x86" ) ) {
        target = { tool_platform_t::WINDOWS, tool_architecture_t::X86 };
    } else if ( TextEquals( text, "windows-x64" ) ) {
        target = { tool_platform_t::WINDOWS, tool_architecture_t::X64 };
    } else if ( TextEquals( text, "windows-arm64" ) ) {
        target = { tool_platform_t::WINDOWS, tool_architecture_t::ARM64 };
    } else if ( TextEquals( text, "linux-x86" ) ) {
        target = { tool_platform_t::LINUX, tool_architecture_t::X86 };
    } else if ( TextEquals( text, "linux-x64" ) ) {
        target = { tool_platform_t::LINUX, tool_architecture_t::X64 };
    } else if ( TextEquals( text, "linux-arm32" ) ) {
        target = { tool_platform_t::LINUX, tool_architecture_t::ARM32 };
    } else if ( TextEquals( text, "linux-arm64" ) ) {
        target = { tool_platform_t::LINUX, tool_architecture_t::ARM64 };
    } else if ( TextEquals( text, "macos-x64" ) ) {
        target = { tool_platform_t::MACOS, tool_architecture_t::X64 };
    } else if ( TextEquals( text, "macos-arm64" ) ) {
        target = { tool_platform_t::MACOS, tool_architecture_t::ARM64 };
    } else {
        return CY_FALSE;
    }

    if ( !ToolTarget_IsValid( target ) ) {
        return CY_FALSE;
    }

    // Publish only after the complete platform/architecture pair is valid.
    *pTargetOut = target;
    return CY_TRUE;
}

const char *ToolTarget_Name( tool_target_t target ) noexcept
{
    if ( target.platform == tool_platform_t::WINDOWS ) {
        switch ( target.architecture ) {
            case tool_architecture_t::X86: return "windows-x86";
            case tool_architecture_t::X64: return "windows-x64";
            case tool_architecture_t::ARM64: return "windows-arm64";
            default: break;
        }
    } else if ( target.platform == tool_platform_t::LINUX ) {
        switch ( target.architecture ) {
            case tool_architecture_t::X86: return "linux-x86";
            case tool_architecture_t::X64: return "linux-x64";
            case tool_architecture_t::ARM32: return "linux-arm32";
            case tool_architecture_t::ARM64: return "linux-arm64";
            default: break;
        }
    } else if ( target.platform == tool_platform_t::MACOS ) {
        switch ( target.architecture ) {
            case tool_architecture_t::X64: return "macos-x64";
            case tool_architecture_t::ARM64: return "macos-arm64";
            default: break;
        }
    }
    return "unknown";
}

bool_t ToolProfile_IsValid( tool_profile_t profile ) noexcept
{
    return profile >= tool_profile_t::DEVELOPMENT &&
           profile <= tool_profile_t::SHIPPING;
}

bool_t ToolProfile_Parse( string_view_t text, tool_profile_t *pProfileOut ) noexcept
{
    if ( pProfileOut == nullptr || !StringView_IsValid( text ) ) {
        return CY_FALSE;
    }

    // Profiles are intentionally independent from CMake configuration names.
    tool_profile_t profile = tool_profile_t::UNKNOWN;
    if ( TextEquals( text, "development" ) ) {
        profile = tool_profile_t::DEVELOPMENT;
    } else if ( TextEquals( text, "release" ) ) {
        profile = tool_profile_t::RELEASE;
    } else if ( TextEquals( text, "shipping" ) ) {
        profile = tool_profile_t::SHIPPING;
    } else {
        return CY_FALSE;
    }

    *pProfileOut = profile;
    return CY_TRUE;
}

const char *ToolProfile_Name( tool_profile_t profile ) noexcept
{
    switch ( profile ) {
        case tool_profile_t::DEVELOPMENT: return "development";
        case tool_profile_t::RELEASE: return "release";
        case tool_profile_t::SHIPPING: return "shipping";
        case tool_profile_t::UNKNOWN: break;
    }
    return "unknown";
}

} // namespace cypher::common
