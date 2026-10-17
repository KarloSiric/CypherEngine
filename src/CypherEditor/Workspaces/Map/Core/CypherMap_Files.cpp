//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Files.cpp
//  Purpose: Implements reading and writing a map on disk.
//  Details: Loading checks the file's size before reading it, so a stray
//           multi-gigabyte file is refused instead of read into memory.
//           Saving stages the text as `<file>.tmp` and publishes it with an
//           atomic replace; see the header for the failure guarantees.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//  - Reworked on 2026-10-12 for V11: one file; chunk scanning, staging of
//    many files, and the deletion inventory are gone
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Files.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_Blob.h"
#include "CypherCommon/Tier1/CypherCommon_FileIo.h"
#include "CypherCommon/Tier1/CypherCommon_Stream.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

#include <cstdio>
#include <cstring>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

constexpr char kStagingSuffix[] = ".tmp";

// Tier0 logging takes preformatted text; a path longer than the buffer is
// truncated in the message only.
void LogPath( log_level_t level, const char *pWhat, string_view_t path, source_location_t location ) noexcept
{
    char message[640]{};
    std::snprintf( message, sizeof( message ), "%s: %.*s", pWhat, static_cast<int>( path.cchLength ), path.pData );
    Cy_LogWriteAt( level, log_channel_t::Editor, message, location );
}

// The file's size without reading it; false when it cannot be opened.
CYPHER_NODISCARD bool_t FileSize( string_view_t path, const allocator_t *pAllocator, u64 *pSizeOut ) noexcept
{
    native_file_t *pFile = FileIo_OpenNative( path, FILE_OPEN_FLAG_READ, pAllocator );
    if ( pFile == nullptr ) { return CY_FALSE; }
    stream_t stream = FileIo_AsStream( pFile );
    const bool_t bOk = Stream_Size( &stream, pSizeOut ) == stream_status_t::OK;
    FileIo_CloseNative( pFile );
    return bOk;
}

} // namespace

map_files_result_t MapFiles_Load(
    map_document_t *pMap,
    const allocator_t *pAllocator,
    const char *pPath,
    u32 flags ) noexcept
{
    map_files_result_t result{};
    if ( pMap == nullptr || pAllocator == nullptr || pPath == nullptr || pPath[0] == '\0' ) {
        result.status = map_files_status_t::INVALID_ARGUMENT;
        return result;
    }
    const string_view_t path = StringView_FromCString( pPath );
    if ( !FileIo_NativeExists( path ) ) {
        result.status = map_files_status_t::ROOT_MISSING;
        return result;
    }
    u64 cbSize = 0u;
    if ( !FileSize( path, pAllocator, &cbSize ) ) {
        LogPath( log_level_t::Error, "Map could not be read", path, CY_SOURCE_LOCATION );
        result.status = map_files_status_t::READ_FAILED;
        return result;
    }
    if ( cbSize > MAP_TEXT_MAX ) {
        LogPath( log_level_t::Error, "Map is larger than the format allows; not read", path, CY_SOURCE_LOCATION );
        result.status = map_files_status_t::READ_FAILED;
        return result;
    }
    blob_t bytes{};
    if ( !Blob_Init( &bytes, pAllocator ) ) {
        result.status = map_files_status_t::DOCUMENT_FAILED;
        result.documentStatus = map_status_t::OUT_OF_MEMORY;
        return result;
    }
    if ( !FileIo_ReadAllNative( path, &bytes ) ) {
        LogPath( log_level_t::Error, "Map could not be read", path, CY_SOURCE_LOCATION );
        result.status = map_files_status_t::READ_FAILED;
        return result;
    }
    result.cbFile = Blob_Size( &bytes );
    const string_view_t text{ reinterpret_cast<const char *>( Blob_Data( &bytes ) ), Blob_Size( &bytes ) };
    result.documentStatus = MapDocument_Load( pMap, pAllocator, text.pData != nullptr ? text : StringView_FromCString( "" ), flags );
    if ( result.documentStatus != map_status_t::OK && result.documentStatus != map_status_t::READ_ONLY ) {
        LogPath( log_level_t::Error, "Map could not be opened", path, CY_SOURCE_LOCATION );
        result.status = map_files_status_t::DOCUMENT_FAILED;
        return result;
    }
    LogPath( log_level_t::Info, "Map opened", path, CY_SOURCE_LOCATION );
    return result;
}

map_files_result_t MapFiles_Save( map_document_t *pMap, const char *pPath ) noexcept
{
    map_files_result_t result{};
    if ( pMap == nullptr || pMap->pAllocator == nullptr || pPath == nullptr || pPath[0] == '\0' ) {
        result.status = map_files_status_t::INVALID_ARGUMENT;
        return result;
    }
    const string_view_t path = StringView_FromCString( pPath );
    text_buffer_t text{};
    text_buffer_t staging{};
    if ( !TextBuffer_Init( &text, pMap->pAllocator ) || !TextBuffer_Init( &staging, pMap->pAllocator ) ||
         !TextBuffer_Assign( &staging, path ) || !TextBuffer_Append( &staging, StringView_FromCString( kStagingSuffix ) ) ) {
        result.status = map_files_status_t::DOCUMENT_FAILED;
        result.documentStatus = map_status_t::OUT_OF_MEMORY;
        return result;
    }
    // Saving writes resolved values in CYKV 1: a map read as CYKV 2 loses
    // its definitions (`$NAME` is replaced by the value). Say so, because
    // the person who wrote them will look for them.
    if ( KeyValue_DocumentHeader( pMap->root.pDocument ).nLanguageVersion == CYKV_LANGUAGE_VERSION_2 ) {
        LogPath( log_level_t::Warning, "Map was written in CYKV 2; saving writes its resolved values as CYKV 1", path, CY_SOURCE_LOCATION );
    }
    // A save that never reaches the disk has not written the IDs that
    // loading assigned; the next save must still write them.
    const bool_t bIdsAssigned = pMap->bIdsAssigned;
    result.documentStatus = MapDocument_Save( pMap, &text );
    if ( result.documentStatus != map_status_t::OK ) {
        result.status = map_files_status_t::DOCUMENT_FAILED;
        LogPath( log_level_t::Error, "Map was not saved; the file on disk is unchanged", path, CY_SOURCE_LOCATION );
        return result;
    }
    const string_view_t view = TextBuffer_View( &text );
    const binary_block_t block{ reinterpret_cast<const byte *>( view.pData ), view.cchLength };
    const string_view_t stagingPath = TextBuffer_View( &staging );
    // Save As may name a folder that does not exist yet.
    usize cchFolder = path.cchLength;
    while ( cchFolder > 0u && path.pData[cchFolder - 1u] != '/' && path.pData[cchFolder - 1u] != '\\' ) { --cchFolder; }
    const bool_t bFolder = cchFolder <= 1u || FileIo_CreateDirectoriesNative( { path.pData, cchFolder - 1u } );
    if ( !bFolder || !FileIo_WriteAllNative( stagingPath, block ) || !FileIo_ReplaceNative( stagingPath, path ) ) {
        // A partial `.tmp` may exist; the map itself was never touched.
        if ( FileIo_NativeExists( stagingPath ) ) { ( void )FileIo_RemoveNative( stagingPath ); }
        pMap->bIdsAssigned = bIdsAssigned;
        LogPath( log_level_t::Error, "Map could not be written; the file on disk is unchanged", path, CY_SOURCE_LOCATION );
        result.status = map_files_status_t::WRITE_FAILED;
        return result;
    }
    result.cbFile = view.cchLength;
    LogPath( log_level_t::Info, "Map saved", path, CY_SOURCE_LOCATION );
    return result;
}

const char *MapFiles_StatusName( map_files_status_t status ) noexcept
{
    switch ( status ) {
        case map_files_status_t::OK: return "OK";
        case map_files_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case map_files_status_t::ROOT_MISSING: return "ROOT_MISSING";
        case map_files_status_t::READ_FAILED: return "READ_FAILED";
        case map_files_status_t::WRITE_FAILED: return "WRITE_FAILED";
        case map_files_status_t::DOCUMENT_FAILED: return "DOCUMENT_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor::map
