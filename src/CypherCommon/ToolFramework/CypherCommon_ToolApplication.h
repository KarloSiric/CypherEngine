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
    UNKNOWN = 0u,
    LIBRARY,
    COMMAND_LINE,
    GUI,
    HYBRID,
    SERVICE
};

enum tool_application_flags_t : flags32_t {
    TOOL_APPLICATION_FLAG_NONE = 0u,
    TOOL_APPLICATION_FLAG_PROJECT_AWARE = CYPHER_BIT32( 0 ),
    TOOL_APPLICATION_FLAG_HEADLESS = CYPHER_BIT32( 1 ),
    TOOL_APPLICATION_FLAG_INTERACTIVE = CYPHER_BIT32( 2 ),
    TOOL_APPLICATION_FLAG_REMOTE_CAPABLE = CYPHER_BIT32( 3 ),
    TOOL_APPLICATION_FLAG_EMBEDDABLE = CYPHER_BIT32( 4 )
};

struct tool_application_desc_t {
    string_view_t id{};
    string_view_t displayName{};
    string_view_t summary{};
    tool_delivery_t delivery{ tool_delivery_t::UNKNOWN };
    u32 nApiVersion{ 0u };
    flags32_t flags{ TOOL_APPLICATION_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolApplication_CheckDescriptor(
    const tool_application_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolApplication_DeliveryName( tool_delivery_t delivery ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLAPPLICATION_H
