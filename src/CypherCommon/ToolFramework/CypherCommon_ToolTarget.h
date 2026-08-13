//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolTarget.h
//  Purpose: Declares portable tool target and build-profile descriptors.
//  Details: Cookers use explicit target identity instead of inferring runtime
//           formats from the machine that happens to execute the tool.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLTARGET_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLTARGET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class tool_platform_t : u8 {
    UNKNOWN = 0u,
    WINDOWS,
    LINUX,
    MACOS
};

enum class tool_architecture_t : u8 {
    UNKNOWN = 0u,
    X86,
    X64,
    ARM32,
    ARM64
};

enum class tool_profile_t : u8 {
    UNKNOWN = 0u,
    DEVELOPMENT,
    RELEASE,
    SHIPPING
};

struct tool_target_t {
    tool_platform_t platform{ tool_platform_t::UNKNOWN };
    tool_architecture_t architecture{ tool_architecture_t::UNKNOWN };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_target_t ToolTarget_Host() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolTarget_IsValid( tool_target_t target ) noexcept;

// Parses "host" or an explicit platform-architecture pair such as linux-x64.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolTarget_Parse( string_view_t text, tool_target_t *pTargetOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolTarget_Name( tool_target_t target ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolProfile_IsValid( tool_profile_t profile ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolProfile_Parse( string_view_t text, tool_profile_t *pProfileOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolProfile_Name( tool_profile_t profile ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLTARGET_H
