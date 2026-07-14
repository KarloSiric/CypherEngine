//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherPak/CypherPak_Reader.h
//  Purpose: Declares the CypherPak Pak Reader module.
//  Details: This file participates in the CypherPak archive format and package access
//           path. Keep binary layout, endian rules, and validation stable so shipped
//           content remains readable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_PAK_READER_H
#define CYPHER_ENGINE_PAK_READER_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Types.h"

namespace cypher::engine::pak
{

pak_error_t CypherPak_OpenReader(
    const char *szArchivePath,
    common::u32 flags,
    pak_reader_t &reader );

pak_error_t CypherPak_CloseReader( pak_reader_t &reader );

bool CypherPak_IsOpen( const pak_reader_t &reader );

pak_error_t CypherPak_ValidateHeader( const pak_header_t &header );

pak_error_t CypherPak_GetStats(
    const pak_reader_t &reader,
    pak_stats_t &statsOut );

pak_error_t CypherPak_GetFileCount(
    const pak_reader_t &reader,
    common::u32 &nOutFileCount );

pak_error_t CypherPak_FindFile(
    const pak_reader_t &reader,
    const char *szVirtualPath,
    pak_file_index_t &nOutIndex );

pak_error_t CypherPak_GetFileInfo(
    const pak_reader_t &reader,
    pak_file_index_t index,
    pak_file_info_t &infoOut );

pak_error_t CypherPak_GetFileInfoByPath(
    const pak_reader_t &reader,
    const char *szVirtualPath,
    pak_file_info_t &infoOut );

pak_error_t CypherPak_ReadFile(
    pak_reader_t &reader,
    const char *szVirtualPath,
    void *buffer,
    common::u64 nBufferSize,
    common::u64 &nOutBytesRead );

pak_error_t CypherPak_ReadFileByIndex(
    pak_reader_t &reader,
    pak_file_index_t index,
    void *buffer,
    common::u64 nBufferSize,
    common::u64 &nOutBytesRead );

pak_error_t CypherPak_ReadRawFileByIndex(
    pak_reader_t &reader,
    pak_file_index_t index,
    void *buffer,
    common::u64 nBufferSize,
    common::u64 &nOutBytesRead );

pak_error_t CypherPak_Verify(
    pak_reader_t &reader,
    common::u32 flags );

}       // namespace cypher::engine::pak

#endif // CYPHER_ENGINE_PAK_READER_H
