//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Stream.h
//  Purpose: Declares CypherCommon Tier1 Stream support.
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

#ifndef CYPHER_COMMON_TIER1_STREAM_H
#define CYPHER_COMMON_TIER1_STREAM_H
#pragma once

/*
================
CypherCommon Stream

Generic stream interface declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct stream_t;

using stream_read_t = usize ( * )( stream_t *pStream, void *pDest, usize cbRead );
using stream_write_t = usize ( * )( stream_t *pStream, const void *pSrc, usize cbWrite );
using stream_seek_t = bool_t ( * )( stream_t *pStream, u64 offset );

struct stream_vtable_t {
    stream_read_t Read;
    stream_write_t Write;
    stream_seek_t Seek;
};

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STREAM_H
