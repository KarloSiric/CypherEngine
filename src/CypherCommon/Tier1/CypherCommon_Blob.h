//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Blob.h
//  Purpose: Declares CypherCommon Tier1 Blob support.
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

#ifndef CYPHER_COMMON_TIER1_BLOB_H
#define CYPHER_COMMON_TIER1_BLOB_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Blob

Owned binary memory block declarations used by VFS reads, pak entries, tools,
asset importers and serialized data.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct blob_t {
    void *pData;
    usize cbSize;
    usize cbCapacity;
};

bool_t Blob_Init( blob_t *pBlob, usize cbInitialCapacity );
void Blob_Free( blob_t *pBlob );
bool_t Blob_Resize( blob_t *pBlob, usize cbSize );
bool_t Blob_Reserve( blob_t *pBlob, usize cbCapacity );
bool_t Blob_Assign( blob_t *pBlob, const void *pData, usize cbData );
void Blob_Clear( blob_t *pBlob );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BLOB_H
