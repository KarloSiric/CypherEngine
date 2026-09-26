//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_SettingsFile.cpp
//  Purpose: Implements the settings-family file policy.
//  Details: Every step that could lose data happens after a copy of what it
//           replaces exists: the backup is refreshed from the current good
//           file before the atomic replace, and a damaged file is copied to
//           .broken before it can be written over.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_SettingsFile.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_Blob.h"
#include "CypherCommon/Tier1/CypherCommon_FileIo.h"

#include <cstdio>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

// Builds "<path><suffix>" in pOut; false on allocation failure.
CYPHER_NODISCARD bool_t SiblingPath(
    const settings_file_t &file,
    const char *pSuffix,
    text_buffer_t *pOut ) noexcept
{
    return TextBuffer_Init( pOut, file.store.pAllocator ) &&
           TextBuffer_Assign( pOut, TextBuffer_View( &file.path ) ) &&
           TextBuffer_Append( pOut, StringView_FromCString( pSuffix ) );
}

enum class read_outcome_t : u8 { LOADED, MISSING, DAMAGED, IO_ERROR };

CYPHER_NODISCARD read_outcome_t ReadInto(
    settings_document_t &store,
    string_view_t path,
    settings_document_load_result_t &loadOut ) noexcept
{
    if ( !FileIo_NativeExists( path ) ) {
        return read_outcome_t::MISSING;
    }
    blob_t bytes{};
    if ( !Blob_Init( &bytes, store.pAllocator ) || !FileIo_ReadAllNative( path, &bytes ) ) {
        return read_outcome_t::IO_ERROR;
    }
    const string_view_t text{ reinterpret_cast<const char *>( Blob_Data( &bytes ) ), Blob_Size( &bytes ) };
    loadOut = SettingsDocument_Load( &store, text );
    return loadOut.status == settings_document_status_t::OK ? read_outcome_t::LOADED : read_outcome_t::DAMAGED;
}

// Copies one file byte-for-byte; the destination is replaced atomically.
CYPHER_NODISCARD bool_t CopyNativeFile(
    const settings_file_t &file,
    string_view_t source,
    const char *pDestinationSuffix ) noexcept
{
    blob_t bytes{};
    text_buffer_t destination{};
    text_buffer_t staging{};
    return Blob_Init( &bytes, file.store.pAllocator ) &&
           FileIo_ReadAllNative( source, &bytes ) &&
           SiblingPath( file, pDestinationSuffix, &destination ) &&
           SiblingPath( file, pDestinationSuffix, &staging ) &&
           TextBuffer_Append( &staging, StringView_FromCString( ".tmp" ) ) &&
           FileIo_WriteAllNative( TextBuffer_View( &staging ), Blob_Block( &bytes ) ) &&
           FileIo_ReplaceNative( TextBuffer_View( &staging ), TextBuffer_View( &destination ) );
}

// Tier0 logging takes preformatted text; settings paths are short, so a
// bounded stack buffer is enough and a longer path is simply truncated.
void LogFileEvent(
    log_level_t level,
    const char *pWhat,
    const settings_file_t &file,
    source_location_t location ) noexcept
{
    char message[512]{};
    const string_view_t path = TextBuffer_View( &file.path );
    std::snprintf( message, sizeof( message ), "%s: %.*s", pWhat, static_cast<int>( path.cchLength ), path.pData );
    Cy_LogWriteAt( level, log_channel_t::Editor, message, location );
}

} // namespace

settings_file_status_t EditorSettingsFile_Init(
    settings_file_t *pFile,
    const allocator_t *pAllocator,
    const settings_document_identity_t &identity,
    string_view_t nativePath ) noexcept
{
    if ( pFile == nullptr || pAllocator == nullptr || nativePath.cchLength == 0u ) {
        return settings_file_status_t::INVALID_ARGUMENT;
    }
    const settings_document_status_t status = SettingsDocument_Init( &pFile->store, pAllocator, identity );
    if ( status != settings_document_status_t::OK ) {
        return status == settings_document_status_t::OUT_OF_MEMORY
            ? settings_file_status_t::OUT_OF_MEMORY : settings_file_status_t::INVALID_ARGUMENT;
    }
    if ( !TextBuffer_Init( &pFile->path, pAllocator ) || !TextBuffer_Assign( &pFile->path, nativePath ) ) {
        return settings_file_status_t::OUT_OF_MEMORY;
    }
    pFile->bSaveBlocked = CY_FALSE;
    pFile->damage = {};
    return settings_file_status_t::OK;
}

void EditorSettingsFile_Shutdown( settings_file_t *pFile ) noexcept
{
    if ( pFile == nullptr ) {
        return;
    }
    SettingsDocument_Shutdown( &pFile->store );
    TextBuffer_Shutdown( &pFile->path );
    pFile->bSaveBlocked = CY_FALSE;
}

settings_file_status_t EditorSettingsFile_Load( settings_file_t *pFile ) noexcept
{
    if ( pFile == nullptr || !SettingsDocument_IsInitialized( &pFile->store ) ) {
        return settings_file_status_t::INVALID_ARGUMENT;
    }
    pFile->damage = {};
    pFile->bSaveBlocked = CY_FALSE;
    settings_document_load_result_t load{};
    switch ( ReadInto( pFile->store, TextBuffer_View( &pFile->path ), load ) ) {
        case read_outcome_t::LOADED: return settings_file_status_t::OK;
        case read_outcome_t::MISSING: return settings_file_status_t::MISSING;
        case read_outcome_t::IO_ERROR: return settings_file_status_t::IO_ERROR;
        case read_outcome_t::DAMAGED: break;
    }
    pFile->damage = load;

    // Keep the damaged file before anything can replace it, then try the backup.
    const bool_t bKeptDamaged = CopyNativeFile( *pFile, TextBuffer_View( &pFile->path ), ".broken" );
    text_buffer_t backup{};
    if ( !SiblingPath( *pFile, ".bak", &backup ) ) {
        return settings_file_status_t::OUT_OF_MEMORY;
    }
    settings_document_load_result_t backupLoad{};
    if ( bKeptDamaged &&
         ReadInto( pFile->store, TextBuffer_View( &backup ), backupLoad ) == read_outcome_t::LOADED ) {
        LogFileEvent( log_level_t::Warning, "Settings file was damaged; restored from backup and kept as .broken",
                      *pFile, CY_SOURCE_LOCATION );
        return settings_file_status_t::RESTORED_FROM_BACKUP;
    }
    // No usable backup (or the damaged file could not be preserved): run on
    // inherited values and refuse to write until the user decides.
    const settings_document_identity_t identity = pFile->store.identity;
    if ( SettingsDocument_Init( &pFile->store, pFile->store.pAllocator, identity ) != settings_document_status_t::OK ) {
        return settings_file_status_t::OUT_OF_MEMORY;
    }
    pFile->bSaveBlocked = CY_TRUE;
    LogFileEvent( log_level_t::Error, "Settings file unreadable and no usable backup; using inherited values, saving blocked",
                  *pFile, CY_SOURCE_LOCATION );
    return settings_file_status_t::UNREADABLE;
}

settings_file_status_t EditorSettingsFile_Save( settings_file_t *pFile ) noexcept
{
    if ( pFile == nullptr || !SettingsDocument_IsInitialized( &pFile->store ) ) {
        return settings_file_status_t::INVALID_ARGUMENT;
    }
    if ( pFile->bSaveBlocked ) {
        return settings_file_status_t::SAVE_BLOCKED;
    }
    text_buffer_t text{};
    if ( !TextBuffer_Init( &text, pFile->store.pAllocator ) ) {
        return settings_file_status_t::OUT_OF_MEMORY;
    }
    const settings_document_status_t written = SettingsDocument_Write( &pFile->store, &text );
    if ( written != settings_document_status_t::OK ) {
        return written == settings_document_status_t::OUT_OF_MEMORY
            ? settings_file_status_t::OUT_OF_MEMORY : settings_file_status_t::IO_ERROR;
    }
    text_buffer_t staging{};
    if ( !SiblingPath( *pFile, ".tmp", &staging ) ) {
        return settings_file_status_t::OUT_OF_MEMORY;
    }
    const binary_block_t bytes{ reinterpret_cast<const byte *>( TextBuffer_Data( &text ) ), TextBuffer_Length( &text ) };
    if ( !FileIo_WriteAllNative( TextBuffer_View( &staging ), bytes ) ) {
        return settings_file_status_t::IO_ERROR;
    }
    // Refresh the backup from the current file first; it only ever holds a
    // file this build wrote or successfully read.
    const string_view_t path = TextBuffer_View( &pFile->path );
    if ( FileIo_NativeExists( path ) && !CopyNativeFile( *pFile, path, ".bak" ) ) {
        ( void )FileIo_RemoveNative( TextBuffer_View( &staging ) );
        return settings_file_status_t::IO_ERROR;
    }
    if ( !FileIo_ReplaceNative( TextBuffer_View( &staging ), path ) ) {
        ( void )FileIo_RemoveNative( TextBuffer_View( &staging ) );
        LogFileEvent( log_level_t::Error, "Settings file could not be replaced; previous file kept", *pFile, CY_SOURCE_LOCATION );
        return settings_file_status_t::IO_ERROR;
    }
    return settings_file_status_t::OK;
}

settings_file_status_t EditorSettingsFile_AcceptOverwrite( settings_file_t *pFile ) noexcept
{
    if ( pFile == nullptr || !SettingsDocument_IsInitialized( &pFile->store ) ) {
        return settings_file_status_t::INVALID_ARGUMENT;
    }
    const string_view_t path = TextBuffer_View( &pFile->path );
    if ( pFile->bSaveBlocked && FileIo_NativeExists( path ) && !CopyNativeFile( *pFile, path, ".broken" ) ) {
        return settings_file_status_t::IO_ERROR;
    }
    pFile->bSaveBlocked = CY_FALSE;
    return settings_file_status_t::OK;
}

const char *EditorSettingsFile_StatusName( settings_file_status_t status ) noexcept
{
    switch ( status ) {
        case settings_file_status_t::OK: return "OK";
        case settings_file_status_t::MISSING: return "MISSING";
        case settings_file_status_t::RESTORED_FROM_BACKUP: return "RESTORED_FROM_BACKUP";
        case settings_file_status_t::UNREADABLE: return "UNREADABLE";
        case settings_file_status_t::SAVE_BLOCKED: return "SAVE_BLOCKED";
        case settings_file_status_t::IO_ERROR: return "IO_ERROR";
        case settings_file_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case settings_file_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor
