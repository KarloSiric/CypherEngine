//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolApplication.h
//  Purpose: Declares metadata for named Cypher tool products and modules.
//  Details: Product descriptors let launchers and Mason discover delivery mode
//           and capabilities without linking application-specific UI classes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Application Contract

A tool run owns one invocation, host callback set, cancellation state, and final report.
Tool-specific code borrows that context only for the duration of execution.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLAPPLICATION_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLAPPLICATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class tool_delivery_t : u8 {
    UNKNOWN = 0u, // Delivery model is not declared.
    LIBRARY,      // Embedded API without a standalone frontend.
    COMMAND_LINE, // Headless process driven by command arguments.
    GUI,          // Standalone graphical application.
    HYBRID,       // Graphical application with supported headless commands.
    SERVICE       // Long-lived worker or network service.
};

enum tool_application_flags_t : flags32_t {
    TOOL_APPLICATION_FLAG_NONE = 0u,                         // No optional capabilities.
    TOOL_APPLICATION_FLAG_PROJECT_AWARE = CYPHER_BIT32( 0 ), // Consumes project/workspace context.
    TOOL_APPLICATION_FLAG_HEADLESS = CYPHER_BIT32( 1 ),      // Can execute without a display server.
    TOOL_APPLICATION_FLAG_INTERACTIVE = CYPHER_BIT32( 2 ),   // Supports supervised user interaction.
    TOOL_APPLICATION_FLAG_REMOTE_CAPABLE = CYPHER_BIT32( 3 ),// Can run through a remote worker boundary.
    TOOL_APPLICATION_FLAG_EMBEDDABLE = CYPHER_BIT32( 4 )     // Can be hosted inside Mason or another tool.
};

struct tool_application_desc_t {
    string_view_t id{};          // Stable lowercase product identifier.
    string_view_t displayName{}; // Human-readable product name.
    string_view_t summary{};     // One-line purpose used by launchers/help.
    tool_delivery_t delivery{ tool_delivery_t::UNKNOWN }; // Frontend/hosting model.
    u32 nApiVersion{ 0u };       // Descriptor and embedding contract version.
    flags32_t flags{ TOOL_APPLICATION_FLAG_NONE }; // tool_application_flags_t bitset.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolApplication_CheckDescriptor(
    const tool_application_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolApplication_DeliveryName( tool_delivery_t delivery ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLAPPLICATION_H
