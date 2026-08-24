//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolApplication.cpp
//  Purpose: Implements validation for named Cypher tool descriptors.
//  Details: Stable lowercase IDs are suitable for manifests, logs, IPC, command
//           dispatch, and registry lookup while display names remain user-facing.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Application Implementation Notes

A tool run owns one invocation, host callback set, cancellation state, and final report.
Tool-specific code borrows that context only for the duration of execution.
================
*/

#include "CypherCommon_ToolApplication.h"

namespace cypher::common
{
namespace
{

bool IsValidId( string_view_t id ) noexcept
{
    if ( !StringView_IsValid( id ) || id.cchLength == 0u ) {
        return false;
    }

    // Stable IDs begin with lowercase ASCII so they are safe in manifests,
    // command names, registry keys, and case-sensitive host filesystems.
    const char first = id.pData[0];
    if ( first < 'a' || first > 'z' ) {
        return false;
    }

    // Keep the grammar deliberately portable; display names carry rich text.
    for ( usize i = 1u; i < id.cchLength; ++i ) {
        const char value = id.pData[i];
        const bool_t bLetter = value >= 'a' && value <= 'z';
        const bool_t bDigit = value >= '0' && value <= '9';
        if ( !bLetter && !bDigit && value != '_' && value != '-' && value != '.' ) {
            return false;
        }
    }
    return true;
}

} // namespace

tool_status_t ToolApplication_CheckDescriptor(
    const tool_application_desc_t &desc ) noexcept
{
    // Reject unknown capability bits at module boundaries.
    constexpr flags32_t knownFlags =
        TOOL_APPLICATION_FLAG_PROJECT_AWARE |
        TOOL_APPLICATION_FLAG_HEADLESS |
        TOOL_APPLICATION_FLAG_INTERACTIVE |
        TOOL_APPLICATION_FLAG_REMOTE_CAPABLE |
        TOOL_APPLICATION_FLAG_EMBEDDABLE;

    if ( !IsValidId( desc.id ) ||
         !StringView_IsValid( desc.displayName ) ||
         desc.displayName.cchLength == 0u ||
         !StringView_IsValid( desc.summary ) ||
         desc.delivery < tool_delivery_t::LIBRARY ||
         desc.delivery > tool_delivery_t::SERVICE ||
         desc.nApiVersion == 0u ||
         ( desc.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    // Command-line delivery must remain usable without a windowing host.
    const bool_t bHeadless =
        ( desc.flags & TOOL_APPLICATION_FLAG_HEADLESS ) != 0u;
    if ( desc.delivery == tool_delivery_t::COMMAND_LINE && !bHeadless ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    return tool_status_t::OK;
}

const char *ToolApplication_DeliveryName( tool_delivery_t delivery ) noexcept
{
    // These stable names are used in help output and structured reports.
    switch ( delivery ) {
        case tool_delivery_t::LIBRARY: return "library";
        case tool_delivery_t::COMMAND_LINE: return "command-line";
        case tool_delivery_t::GUI: return "gui";
        case tool_delivery_t::HYBRID: return "hybrid";
        case tool_delivery_t::SERVICE: return "service";
        case tool_delivery_t::UNKNOWN: break;
    }
    return "unknown";
}

} // namespace cypher::common
