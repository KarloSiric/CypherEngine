//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ByteReader.h
//  Purpose: Declares CypherCommon Tier1 ByteReader support.
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

#ifndef CYPHER_COMMON_TIER1_BYTEREADER_H
#define CYPHER_COMMON_TIER1_BYTEREADER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Byte Reader

Sequential byte reader declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct byte_reader_t {
    const byte *pData;
    usize cbSize;
    usize offset;
};

void ByteReader_Init( byte_reader_t *pReader, const void *pData, usize cbSize );
bool_t ByteReader_Read( byte_reader_t *pReader, void *pDest, usize cbData );
bool_t ByteReader_Skip( byte_reader_t *pReader, usize cbData );
usize ByteReader_Remaining( const byte_reader_t *pReader );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BYTEREADER_H
