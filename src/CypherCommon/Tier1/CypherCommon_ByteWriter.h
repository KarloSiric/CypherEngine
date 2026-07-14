//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ByteWriter.h
//  Purpose: Declares CypherCommon Tier1 ByteWriter support.
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

#ifndef CYPHER_COMMON_TIER1_BYTEWRITER_H
#define CYPHER_COMMON_TIER1_BYTEWRITER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Byte Writer

Sequential byte writer declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct byte_writer_t {
    byte *pData;
    usize cbCapacity;
    usize offset;
};

void ByteWriter_Init( byte_writer_t *pWriter, void *pData, usize cbCapacity );
bool_t ByteWriter_Write( byte_writer_t *pWriter, const void *pSrc, usize cbData );
bool_t ByteWriter_Seek( byte_writer_t *pWriter, usize offset );
usize ByteWriter_BytesWritten( const byte_writer_t *pWriter );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BYTEWRITER_H
