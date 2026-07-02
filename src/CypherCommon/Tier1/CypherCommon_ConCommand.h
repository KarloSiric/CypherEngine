//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ConCommand.h
//  Purpose: Declares CypherCommon Tier1 ConCommand support.
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

#ifndef CYPHER_COMMON_TIER1_CONCOMMAND_H
#define CYPHER_COMMON_TIER1_CONCOMMAND_H
#pragma once

/*
================
CypherCommon ConCommand

Console command declarations used by the runtime console, config files,
developer tools and editor command routing.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum concommand_flags_t : flags32_t {
    CONCOMMAND_FLAG_NONE = 0u,
    CONCOMMAND_FLAG_CHEAT = CYPHER_BIT32( 0 ),
    CONCOMMAND_FLAG_DEVELOPMENT_ONLY = CYPHER_BIT32( 1 ),
    CONCOMMAND_FLAG_TOOL_ONLY = CYPHER_BIT32( 2 )
};

struct command_args_t {
    u32 argc;
    const char **ppArgv;
    const char *pCommandLine;
};

using concommand_callback_t = void ( * )( const command_args_t &args, void *pUserData );

struct concommand_desc_t {
    const char *pName;
    const char *pHelpText;
    flags32_t flags;
    concommand_callback_t pCallback;
    void *pUserData;
};

bool_t ConCommand_IsValidName( const char *pName );
bool_t ConCommand_ParseArgs( const char *pCommandLine, command_args_t *pOutArgs );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONCOMMAND_H
