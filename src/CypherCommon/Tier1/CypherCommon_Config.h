//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Config.h
//  Purpose: Declares CypherCommon Tier1 Config support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CONFIG_H
#define CYPHER_COMMON_TIER1_CONFIG_H
#pragma once

/*
================
CypherCommon Config

Config loading and saving declarations used by autoexec files, tools, editor
preferences and command-system startup.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum config_flags_t : flags32_t {
    CONFIG_FLAG_NONE = 0u,
    CONFIG_FLAG_REQUIRED = CYPHER_BIT32( 0 ),
    CONFIG_FLAG_ALLOW_COMMANDS = CYPHER_BIT32( 1 ),
    CONFIG_FLAG_ALLOW_CVARS = CYPHER_BIT32( 2 )
};

struct config_source_t {
    const char *pVirtualPath;
    const char *pText;
    usize cchText;
    flags32_t flags;
};

struct config_writer_t;
struct command_system_t;

bool_t Config_LoadSource( const config_source_t &source, command_system_t *pCommandSystem );
bool_t Config_LoadFile( const char *pVirtualPath, flags32_t flags, command_system_t *pCommandSystem );
bool_t Config_SaveFile( const char *pVirtualPath, const config_writer_t *pWriter );
bool_t Config_WriteConVars( config_writer_t *pWriter, command_system_t *pCommandSystem );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONFIG_H
