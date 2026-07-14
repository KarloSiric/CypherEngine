//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Compression.h
//  Purpose: Declares CypherCommon Tier1 Compression support.
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

#ifndef CYPHER_COMMON_TIER1_COMPRESSION_H
#define CYPHER_COMMON_TIER1_COMPRESSION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Compression

Common compression API declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum class compression_codec_t : u32 {
    None = 0u,
    LZ,
    LZ4,
    Zstd
};

usize Compression_CompressBound( compression_codec_t codec, usize cbInput );
bool_t Compression_Compress( compression_codec_t codec, const void *pInput, usize cbInput, void *pOutput, usize cbOutput, usize *pOutWritten );
bool_t Compression_Decompress( compression_codec_t codec, const void *pInput, usize cbInput, void *pOutput, usize cbOutput, usize *pOutWritten );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMPRESSION_H
