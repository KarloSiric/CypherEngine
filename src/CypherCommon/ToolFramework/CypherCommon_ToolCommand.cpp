//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCommand.cpp
//  Purpose: Implements command descriptor validation and option lookup.
//  Details: Validation rejects duplicate long and short names so generated CLI
//           help and graphical option forms resolve every setting unambiguously.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolCommand.h"

namespace cypher::common
{

tool_status_t ToolCommand_CheckDescriptor(
    const tool_command_desc_t &desc ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_COMMAND_FLAG_PROJECT_REQUIRED |
        TOOL_COMMAND_FLAG_ACCEPTS_INPUTS |
        TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS |
        TOOL_COMMAND_FLAG_SUPPORTS_DRY_RUN |
        TOOL_COMMAND_FLAG_HIDDEN;

    if ( !StringView_IsValid( desc.name ) || desc.name.cchLength == 0u ||
         !StringView_IsValid( desc.summary ) || desc.summary.cchLength == 0u ||
         !StringView_IsValid( desc.usage ) ||
         !StringView_IsValid( desc.details ) ||
         ( desc.nOptions != 0u && desc.pOptions == nullptr ) ||
         ( desc.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    if ( ( desc.flags & TOOL_COMMAND_FLAG_ALLOW_MULTIPLE_INPUTS ) != 0u &&
         ( desc.flags & TOOL_COMMAND_FLAG_ACCEPTS_INPUTS ) == 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    for ( usize i = 0u; i < desc.nOptions; ++i ) {
        const tool_option_desc_t &option = desc.pOptions[i];
        const tool_status_t status = ToolOption_CheckDescriptor( option );
        if ( ToolStatus_Failed( status ) ) {
            return status;
        }
        for ( usize j = 0u; j < i; ++j ) {
            const tool_option_desc_t &previous = desc.pOptions[j];
            if ( StringView_Equals( option.name, previous.name ) ||
                 ( option.shortName != '\0' &&
                   option.shortName == previous.shortName ) ) {
                return tool_status_t::INVALID_CONFIGURATION;
            }
        }
    }
    return tool_status_t::OK;
}

const tool_option_desc_t *ToolCommand_FindOption(
    const tool_command_desc_t &desc,
    string_view_t name ) noexcept
{
    if ( !StringView_IsValid( name ) ||
         ( desc.nOptions != 0u && desc.pOptions == nullptr ) ) {
        return nullptr;
    }
    for ( usize i = 0u; i < desc.nOptions; ++i ) {
        if ( StringView_Equals( desc.pOptions[i].name, name ) ) {
            return &desc.pOptions[i];
        }
    }
    return nullptr;
}

const tool_option_desc_t *ToolCommand_FindShortOption(
    const tool_command_desc_t &desc,
    char shortName ) noexcept
{
    if ( shortName == '\0' ||
         ( desc.nOptions != 0u && desc.pOptions == nullptr ) ) {
        return nullptr;
    }
    for ( usize i = 0u; i < desc.nOptions; ++i ) {
        if ( desc.pOptions[i].shortName == shortName ) {
            return &desc.pOptions[i];
        }
    }
    return nullptr;
}

} // namespace cypher::common
