//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CharacterSet.h
//  Purpose: Declares CypherCommon Tier1 CharacterSet support.
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

#ifndef CYPHER_COMMON_TIER1_CHARACTERSET_H
#define CYPHER_COMMON_TIER1_CHARACTERSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Character Set

ASCII character set declarations used by tokenizers and parsers.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct character_set_t {
    u8 bits[32];
};

void CharacterSet_Clear( character_set_t *pSet );
void CharacterSet_Add( character_set_t *pSet, char ch );
void CharacterSet_AddRange( character_set_t *pSet, char chFirst, char chLast );
bool_t CharacterSet_Contains( const character_set_t *pSet, char ch );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CHARACTERSET_H
