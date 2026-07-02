//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandBuffer.h
//  Purpose: Declares CypherCommon Tier1 CommandBuffer support.
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

#ifndef CYPHER_COMMON_TIER1_COMMANDBUFFER_H
#define CYPHER_COMMON_TIER1_COMMANDBUFFER_H
#pragma once

/*
================
CypherCommon Command Buffer

Queued text command declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct command_buffer_t;

bool_t CommandBuffer_AddText( command_buffer_t *pBuffer, const char *pText );
bool_t CommandBuffer_GetNext( command_buffer_t *pBuffer, char *pDest, usize cchDest );
void CommandBuffer_Clear( command_buffer_t *pBuffer );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMMANDBUFFER_H
