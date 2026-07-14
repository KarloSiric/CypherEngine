//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FileIo.h
//  Purpose: Declares CypherCommon Tier1 FileIo support.
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

#ifndef CYPHER_COMMON_TIER1_FILEIO_H
#define CYPHER_COMMON_TIER1_FILEIO_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon File IO

Small raw file utility declarations. VFS policy stays in CypherFileSystem.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

bool_t FileIo_ReadAllBytes( const char *pPath, void *pDest, usize cbDest, usize *pOutBytesRead );
bool_t FileIo_WriteAllBytes( const char *pPath, const void *pData, usize cbData );
bool_t FileIo_Exists( const char *pPath );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FILEIO_H
