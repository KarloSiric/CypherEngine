//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Files.cpp
//  Purpose: Implements reading and writing a map on disk.
//  Details: Loading gathers every chunk's path and bytes into two flat
//           buffers before building the document's input views, because a
//           growing buffer moves its bytes and would invalidate views taken
//           earlier. Saving stages every file as `.tmp` through the
//           document's sink and publishes them only after the document has
//           reported success; see the header for the failure guarantees.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Files.h"

#include "CypherCommon/FileSystem/CypherCommon_VfsDirectory.h"
#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier1/CypherCommon_Blob.h"
#include "CypherCommon/Tier1/CypherCommon_FileIo.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

#include <cstdio>
#include <cstring>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

constexpr char kStagingSuffix[] = ".tmp";
constexpr usize kChunkFilesMax = 65536u; // Far above any real map; stops a runaway scan.

// Tier0 logging takes preformatted text; a path longer than the buffer is
// truncated in the message only.
void LogPath( log_level_t level, const char *pWhat, string_view_t path, source_location_t location ) noexcept
{
    char message[640]{};
    std::snprintf( message, sizeof( message ), "%s: %.*s", pWhat, static_cast<int>( path.cchLength ), path.pData );
    Cy_LogWriteAt( level, log_channel_t::Editor, message, location );
}

CYPHER_NODISCARD bool_t EndsWith( string_view_t text, const char *pSuffix ) noexcept
{
    const usize cchSuffix = std::strlen( pSuffix );
    return text.cchLength >= cchSuffix && std::memcmp( text.pData + text.cchLength - cchSuffix, pSuffix, cchSuffix ) == 0;
}

// "<layer>/<cell>.cymapchunk" and nothing deeper or shallower.
CYPHER_NODISCARD bool_t IsChunkPath( string_view_t path ) noexcept
{
    usize nSlashes = 0u;
    for ( usize i = 0u; i < path.cchLength; ++i ) { nSlashes += path.pData[i] == '/' ? 1u : 0u; }
    return nSlashes == 1u && EndsWith( path, MAP_CHUNK_FILE_EXTENSION ) &&
           path.pData[0] != '/' && path.pData[path.cchLength - 1u - std::strlen( MAP_CHUNK_FILE_EXTENSION )] != '/';
}

CYPHER_NODISCARD bool_t JoinPath( text_buffer_t *pOut, string_view_t directory, string_view_t relative ) noexcept
{
    return TextBuffer_Assign( pOut, directory ) && TextBuffer_AppendChar( pOut, '/' ) && TextBuffer_Append( pOut, relative );
}

CYPHER_NODISCARD bool_t StagingPath( text_buffer_t *pOut, string_view_t path ) noexcept
{
    return TextBuffer_Assign( pOut, path ) && TextBuffer_Append( pOut, StringView_FromCString( kStagingSuffix ) );
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

// One chunk file's slice of the flat path and text buffers.
struct chunk_slice_t {
    usize iPath{ 0u };
    usize cchPath{ 0u };
    usize iText{ 0u };
    usize cbText{ 0u };
};

struct chunk_scan_t {
    text_buffer_t *pPaths{ nullptr };
    vector_t<chunk_slice_t> *pSlices{ nullptr };
    bool_t bOutOfMemory{ CY_FALSE };
};

// Only records paths; reading happens after the scan, so the provider is
// never asked to read while it is enumerating.
bool_t VisitChunk( string_view_t virtualPath, const vfs_file_info_t &info, void *pUserData ) noexcept
{
    auto *pScan = static_cast<chunk_scan_t *>( pUserData );
    CY_ASSERT( pScan != nullptr );
    if ( info.type != vfs_entry_type_t::FILE || !IsChunkPath( virtualPath ) ) { return CY_TRUE; }
    if ( pScan->pSlices->nCount >= kChunkFilesMax ) {
        CY_LOG_WRITE( Error, Editor, "Map chunk scan stopped: too many chunk files" );
        return CY_FALSE;
    }
    chunk_slice_t slice{};
    slice.iPath = TextBuffer_Length( pScan->pPaths );
    slice.cchPath = virtualPath.cchLength;
    if ( !TextBuffer_Append( pScan->pPaths, virtualPath ) || !Vector_PushBack( pScan->pSlices, slice ) ) {
        pScan->bOutOfMemory = CY_TRUE;
        return CY_FALSE;
    }
    return CY_TRUE;
}

// Reads every scanned chunk into pTexts. A file that cannot be read, or is
// too large, gets an empty slice: the document then reports it unreadable
// and keeps its path untouched.
CYPHER_NODISCARD bool_t ReadChunks(
    const vfs_t &vfs,
    const text_buffer_t &paths,
    vector_t<chunk_slice_t> *pSlices,
    blob_t *pTexts,
    const allocator_t *pAllocator ) noexcept
{
    blob_t bytes{};
    if ( !Blob_Init( &bytes, pAllocator ) ) { return CY_FALSE; }
    for ( usize i = 0u; i < pSlices->nCount; ++i ) {
        chunk_slice_t &slice = pSlices->pData[i];
        const string_view_t path{ TextBuffer_Data( &paths ) + slice.iPath, slice.cchPath };
        Blob_Clear( &bytes );
        const vfs_status_t status = Vfs_ReadAll( &vfs, path, MAP_CHUNK_TEXT_MAX, &bytes );
        slice.iText = Blob_Size( pTexts );
        slice.cbText = 0u;
        if ( status == vfs_status_t::OUT_OF_MEMORY ) { return CY_FALSE; }
        if ( status != vfs_status_t::OK ) {
            LogPath( log_level_t::Warning, status == vfs_status_t::SIZE_LIMIT
                ? "Map chunk is over the size limit; kept unread"
                : "Map chunk could not be read; kept unread", path, CY_SOURCE_LOCATION );
            continue;
        }
        if ( !Blob_Append( pTexts, Blob_Block( &bytes ) ) ) { return CY_FALSE; }
        slice.cbText = Blob_Size( &bytes );
    }
    return CY_TRUE;
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

// Staged and pending paths are kept newline-separated in one buffer each;
// native map paths never contain a newline, and a flat buffer avoids a
// container of owning strings.
struct disk_sink_t {
    string_view_t chunkDirectory{};
    string_view_t rootPath{};
    text_buffer_t staged{};  // Target paths whose `.tmp` sibling is written; root last.
    text_buffer_t removed{}; // Chunk files to delete once everything is published.
    text_buffer_t scratch{};
    text_buffer_t staging{};
    usize nChunkFiles{ 0u };
    bool_t bIoFailed{ CY_FALSE }; // Distinguishes a disk failure from running out of memory.
    bool_t bUseInventory{ CY_FALSE };
    span_t<const string_view_t> persistedChunks{};
    text_buffer_t *pPublishedChunks{ nullptr }; // Optional caller-owned recovery inventory.
};

bool_t ListContains( const text_buffer_t &list, string_view_t path ) noexcept
{
    const string_view_t all = TextBuffer_View( &list );
    usize start = 0u;
    for ( usize i = 0u; i < all.cchLength; ++i ) {
        if ( all.pData[i] != '\n' ) { continue; }
        if ( StringView_Equals( { all.pData + start, i - start }, path ) ) { return CY_TRUE; }
        start = i + 1u;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t StageFile( disk_sink_t &sink, string_view_t target, string_view_t text ) noexcept
{
    if ( !StagingPath( &sink.staging, target ) ) { return CY_FALSE; }
    const binary_block_t bytes{ reinterpret_cast<const byte *>( text.pData ), text.cchLength };
    if ( !FileIo_WriteAllNative( TextBuffer_View( &sink.staging ), bytes ) ) {
        sink.bIoFailed = CY_TRUE;
        LogPath( log_level_t::Error, "Map file could not be written", TextBuffer_View( &sink.staging ), CY_SOURCE_LOCATION );
        // A partial `.tmp` may exist; the discard pass only knows listed
        // paths, so remove this one here.
        ( void )FileIo_RemoveNative( TextBuffer_View( &sink.staging ) );
        return CY_FALSE;
    }
    return TextBuffer_Append( &sink.staged, target ) && TextBuffer_AppendChar( &sink.staged, '\n' );
}

bool_t SinkWriteChunk( void *pContext, string_view_t path, string_view_t text ) noexcept
{
    auto &sink = *static_cast<disk_sink_t *>( pContext );
    CY_ASSERT( IsChunkPath( path ) );
    if ( !JoinPath( &sink.scratch, sink.chunkDirectory, path ) ) { return CY_FALSE; }
    // The layer folder: everything before the last slash of the target.
    const string_view_t target = TextBuffer_View( &sink.scratch );
    usize cchFolder = target.cchLength;
    while ( cchFolder > 0u && target.pData[cchFolder - 1u] != '/' ) { --cchFolder; }
    CY_ASSERT( cchFolder > 0u );
    if ( !FileIo_CreateDirectoriesNative( string_view_t{ target.pData, cchFolder - 1u } ) ) {
        sink.bIoFailed = CY_TRUE;
        LogPath( log_level_t::Error, "Map chunk folder could not be created", target, CY_SOURCE_LOCATION );
        return CY_FALSE;
    }
    if ( !StageFile( sink, target, text ) ) { return CY_FALSE; }
    ++sink.nChunkFiles;
    return CY_TRUE;
}

bool_t SinkRemoveChunk( void *pContext, string_view_t path ) noexcept
{
    auto &sink = *static_cast<disk_sink_t *>( pContext );
    CY_ASSERT( IsChunkPath( path ) );
    if ( sink.bUseInventory ) {
        bool_t bManaged = CY_FALSE;
        for ( usize i = 0u; i < sink.persistedChunks.nCount; ++i ) { bManaged |= StringView_Equals( path, sink.persistedChunks.pData[i] ); }
        if ( !bManaged ) { return CY_TRUE; }
    }
    if ( !JoinPath( &sink.scratch, sink.chunkDirectory, path ) ) { return CY_FALSE; }
    const string_view_t target = TextBuffer_View( &sink.scratch );
    if ( ListContains( sink.staged, target ) || ListContains( sink.removed, target ) ) { return CY_TRUE; }
    return TextBuffer_Append( &sink.removed, target ) &&
           TextBuffer_AppendChar( &sink.removed, '\n' );
}

bool_t SinkWriteRoot( void *pContext, string_view_t text ) noexcept
{
    auto &sink = *static_cast<disk_sink_t *>( pContext );
    return StageFile( sink, sink.rootPath, text );
}

// Calls fn for each line of a newline-separated list until it returns false.
template <typename fn_t>
CYPHER_NODISCARD bool_t ForEachLine( const text_buffer_t &list, fn_t fn ) noexcept
{
    const string_view_t all = TextBuffer_View( &list );
    usize iStart = 0u;
    for ( usize i = 0u; i < all.cchLength; ++i ) {
        if ( all.pData[i] != '\n' ) { continue; }
        if ( !fn( string_view_t{ all.pData + iStart, i - iStart } ) ) { return CY_FALSE; }
        iStart = i + 1u;
    }
    return CY_TRUE;
}

// Removes every staged `.tmp` still present: after a failed save, and after
// a publish that stopped part-way.
void DiscardStaged( disk_sink_t &sink ) noexcept
{
    ( void )ForEachLine( sink.staged, [&sink]( string_view_t target ) noexcept {
        if ( StagingPath( &sink.staging, target ) && FileIo_NativeExists( TextBuffer_View( &sink.staging ) ) ) {
            ( void )FileIo_RemoveNative( TextBuffer_View( &sink.staging ) );
        }
        return CY_TRUE;
    } );
}

// Renames staged files over their targets in the order the document wrote
// them (chunks, then root), then deletes emptied chunks. Deleting comes
// strictly last: a chunk is only removed once every file that received its
// objects is in place.
CYPHER_NODISCARD bool_t PublishStaged( disk_sink_t &sink, usize *pnRemovedOut ) noexcept
{
    const bool_t bPublished = ForEachLine( sink.staged, [&sink]( string_view_t target ) noexcept {
        if ( !StagingPath( &sink.staging, target ) ) { return CY_FALSE; }
        if ( !FileIo_ReplaceNative( TextBuffer_View( &sink.staging ), target ) ) {
            LogPath( log_level_t::Error, "Map file could not be replaced; save stopped before deleting anything", target, CY_SOURCE_LOCATION );
            return CY_FALSE;
        }
        if ( sink.pPublishedChunks != nullptr && !StringView_Equals( target, sink.rootPath ) ) {
            const usize prefix = sink.chunkDirectory.cchLength + 1u;
            CY_ASSERT( target.cchLength > prefix );
            const string_view_t relative{ target.pData + prefix, target.cchLength - prefix };
            // Capacity was reserved for the larger absolute path list before
            // the first rename, so this append cannot allocate or fail.
            const bool_t bRecorded = TextBuffer_Append( sink.pPublishedChunks, relative ) &&
                TextBuffer_AppendChar( sink.pPublishedChunks, '\n' );
            CY_ASSERT( bRecorded );
            ( void )bRecorded;
        }
        return CY_TRUE;
    } );
    if ( !bPublished ) {
        DiscardStaged( sink );
        return CY_FALSE;
    }
    usize nRemoved = 0u;
    const bool_t bRemoved = ForEachLine( sink.removed, [&nRemoved]( string_view_t path ) noexcept {
        // Save As to a new folder names chunks that were never there.
        if ( !FileIo_NativeExists( path ) ) { return CY_TRUE; }
        if ( !FileIo_RemoveNative( path ) ) {
            LogPath( log_level_t::Error, "Emptied map chunk could not be deleted; its objects will load twice", path, CY_SOURCE_LOCATION );
            return CY_FALSE;
        }
        ++nRemoved;
        return CY_TRUE;
    } );
    *pnRemovedOut = nRemoved;
    return bRemoved;
}

} // namespace

usize MapFiles_ChunkDirectory( const char *pRootPath, char *pBuffer, usize cchBuffer ) noexcept
{
    if ( pRootPath == nullptr || pBuffer == nullptr || cchBuffer == 0u ) { return 0u; }
    const string_view_t root = StringView_FromCString( pRootPath );
    const usize cchExtension = std::strlen( MAP_FILE_EXTENSION );
    // A bare ".cymap" or "dir/.cymap" has no stem to name the folder after.
    if ( !EndsWith( root, MAP_FILE_EXTENSION ) || root.cchLength <= cchExtension ) { return 0u; }
    const usize cchDirectory = root.cchLength - cchExtension;
    if ( root.pData[cchDirectory - 1u] == '/' || root.pData[cchDirectory - 1u] == '\\' || cchDirectory >= cchBuffer ) { return 0u; }
    std::memcpy( pBuffer, root.pData, cchDirectory );
    pBuffer[cchDirectory] = '\0';
    return cchDirectory;
}

map_files_result_t MapFiles_Load(
    map_document_t *pMap,
    const allocator_t *pAllocator,
    const char *pRootPath,
    u32 flags ) noexcept
{
    map_files_result_t result{};
    if ( pMap == nullptr || pAllocator == nullptr || pRootPath == nullptr ) {
        result.status = map_files_status_t::INVALID_ARGUMENT;
        return result;
    }
    const string_view_t rootPath = StringView_FromCString( pRootPath );
    char chunkDirectory[1024]{};
    if ( MapFiles_ChunkDirectory( pRootPath, chunkDirectory, sizeof( chunkDirectory ) ) == 0u ) {
        result.status = map_files_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !FileIo_NativeExists( rootPath ) ) {
        result.status = map_files_status_t::ROOT_MISSING;
        return result;
    }

    blob_t rootBytes{};
    blob_t texts{};
    text_buffer_t paths{};
    vector_t<chunk_slice_t> slices{};
    vector_t<map_chunk_input_t> inputs{};
    if ( !Blob_Init( &rootBytes, pAllocator ) || !Blob_Init( &texts, pAllocator ) || !TextBuffer_Init( &paths, pAllocator ) ||
         !Vector_Init( &slices, pAllocator ) || !Vector_Init( &inputs, pAllocator ) ) {
        result.status = map_files_status_t::DOCUMENT_FAILED;
        result.documentStatus = map_status_t::OUT_OF_MEMORY;
        return result;
    }
    if ( !FileIo_ReadAllNative( rootPath, &rootBytes ) ) {
        LogPath( log_level_t::Error, "Map root could not be read", rootPath, CY_SOURCE_LOCATION );
        result.status = map_files_status_t::READ_FAILED;
        return result;
    }

    // A map with no chunks yet has no chunk folder; that is not an error.
    const string_view_t chunkDirectoryView = StringView_FromCString( chunkDirectory );
    if ( FileIo_NativeExists( chunkDirectoryView ) ) {
        vfs_directory_t provider{};
        if ( VfsDirectory_Init( &provider, chunkDirectoryView ) != vfs_status_t::OK ) {
            LogPath( log_level_t::Error, "Map chunk folder could not be opened", chunkDirectoryView, CY_SOURCE_LOCATION );
            result.status = map_files_status_t::READ_FAILED;
            return result;
        }
        const vfs_t vfs = VfsDirectory_Make( &provider );
        chunk_scan_t scan{};
        scan.pPaths = &paths;
        scan.pSlices = &slices;
        const vfs_status_t scanned = Vfs_Enumerate( &vfs, string_view_t{}, CY_TRUE, &VisitChunk, &scan );
        const bool_t bRead = scanned == vfs_status_t::OK && ReadChunks( vfs, paths, &slices, &texts, pAllocator );
        VfsDirectory_Shutdown( &provider );
        if ( scan.bOutOfMemory || ( scanned == vfs_status_t::OK && !bRead ) ) {
            result.status = map_files_status_t::DOCUMENT_FAILED;
            result.documentStatus = map_status_t::OUT_OF_MEMORY;
            return result;
        }
        if ( scanned != vfs_status_t::OK ) {
            LogPath( log_level_t::Error, "Map chunk folder could not be scanned", chunkDirectoryView, CY_SOURCE_LOCATION );
            result.status = map_files_status_t::READ_FAILED;
            return result;
        }
    }

    // Views are taken only now that both buffers have stopped growing.
    if ( !Vector_Reserve( &inputs, slices.nCount ) ) {
        result.status = map_files_status_t::DOCUMENT_FAILED;
        result.documentStatus = map_status_t::OUT_OF_MEMORY;
        return result;
    }
    const char *pTextBase = reinterpret_cast<const char *>( Blob_Data( &texts ) );
    for ( usize i = 0u; i < slices.nCount; ++i ) {
        const chunk_slice_t &slice = slices.pData[i];
        map_chunk_input_t input{};
        input.path = string_view_t{ TextBuffer_Data( &paths ) + slice.iPath, slice.cchPath };
        input.text = string_view_t{ slice.cbText != 0u ? pTextBase + slice.iText : "", slice.cbText };
        const bool_t bPushed = Vector_PushBack( &inputs, input ); // Reserved above; cannot fail.
        CY_ASSERT( bPushed );
        ( void )bPushed;
    }
    result.nChunkFiles = slices.nCount;

    const string_view_t rootText{ reinterpret_cast<const char *>( Blob_Data( &rootBytes ) ), Blob_Size( &rootBytes ) };
    result.documentStatus = MapDocument_Load(
        pMap, pAllocator, rootText, span_t<const map_chunk_input_t>{ Vector_Data( &inputs ), inputs.nCount }, flags );
    if ( result.documentStatus != map_status_t::OK && result.documentStatus != map_status_t::READ_ONLY ) {
        LogPath( log_level_t::Error, "Map could not be opened", rootPath, CY_SOURCE_LOCATION );
        result.status = map_files_status_t::DOCUMENT_FAILED;
        return result;
    }
    LogPath( log_level_t::Info, "Map opened", rootPath, CY_SOURCE_LOCATION );
    return result;
}

static map_files_result_t SaveFiles( map_document_t *pMap, const char *pRootPath,
                                    span_t<const string_view_t> persistedChunks, bool_t bUseInventory,
                                    text_buffer_t *pPublishedChunks ) noexcept
{
    map_files_result_t result{};
    if ( pPublishedChunks != nullptr ) {
        if ( !TextBuffer_IsValid( pPublishedChunks ) || !Allocator_IsValid( pPublishedChunks->pAllocator ) ) {
            result.status = map_files_status_t::INVALID_ARGUMENT;
            return result;
        }
        TextBuffer_Clear( pPublishedChunks );
    }
    char chunkDirectory[1024]{};
    if ( pMap == nullptr || pMap->pAllocator == nullptr || pRootPath == nullptr || !Span_IsValid( persistedChunks ) ||
         MapFiles_ChunkDirectory( pRootPath, chunkDirectory, sizeof( chunkDirectory ) ) == 0u ) {
        result.status = map_files_status_t::INVALID_ARGUMENT;
        return result;
    }
    for ( usize i = 0u; i < persistedChunks.nCount; ++i ) {
        const string_view_t path = persistedChunks.pData[i];
        bool_t bSafe = StringView_IsValid( path ) && IsChunkPath( path );
        usize slash = 0u;
        for ( usize j = 0u; bSafe && j < path.cchLength; ++j ) {
            if ( path.pData[j] == '/' ) { slash = j; }
            bSafe = static_cast<unsigned char>( path.pData[j] ) >= 32u && path.pData[j] != '\\';
        }
        bSafe = bSafe && !StringView_Equals( { path.pData, slash }, StringView_FromCString( "." ) ) &&
                        !StringView_Equals( { path.pData, slash }, StringView_FromCString( ".." ) );
        if ( !bSafe ) { result.status = map_files_status_t::INVALID_ARGUMENT; return result; }
    }
    disk_sink_t disk{};
    disk.chunkDirectory = StringView_FromCString( chunkDirectory );
    disk.rootPath = StringView_FromCString( pRootPath );
    disk.bUseInventory = bUseInventory;
    disk.persistedChunks = persistedChunks;
    disk.pPublishedChunks = pPublishedChunks;
    if ( !TextBuffer_Init( &disk.staged, pMap->pAllocator ) || !TextBuffer_Init( &disk.removed, pMap->pAllocator ) ||
         !TextBuffer_Init( &disk.scratch, pMap->pAllocator ) || !TextBuffer_Init( &disk.staging, pMap->pAllocator ) ) {
        result.status = map_files_status_t::DOCUMENT_FAILED;
        result.documentStatus = map_status_t::OUT_OF_MEMORY;
        return result;
    }
    map_save_sink_t sink{};
    sink.pContext = &disk;
    sink.pfnWriteChunk = &SinkWriteChunk;
    sink.pfnRemoveChunk = &SinkRemoveChunk;
    sink.pfnWriteRoot = &SinkWriteRoot;

    result.documentStatus = MapDocument_Save( pMap, sink );
    if ( result.documentStatus != map_status_t::OK ) {
        DiscardStaged( disk );
        result.status = result.documentStatus == map_status_t::SINK_FAILED && disk.bIoFailed
            ? map_files_status_t::WRITE_FAILED : map_files_status_t::DOCUMENT_FAILED;
        LogPath( log_level_t::Error, "Map was not saved; files on disk are unchanged", disk.rootPath, CY_SOURCE_LOCATION );
        return result;
    }
    // Restored history may no longer contain the chunks that were written by
    // the last save. Queue those obsolete files without reintroducing their
    // old object contents into the restored document. Deletion remains last.
    for ( usize i = 0u; i < persistedChunks.nCount; ++i ) {
        if ( !SinkRemoveChunk( &disk, persistedChunks.pData[i] ) ) {
            DiscardStaged( disk );
            result.status = map_files_status_t::DOCUMENT_FAILED;
            result.documentStatus = map_status_t::OUT_OF_MEMORY;
            return result;
        }
    }
    if ( pPublishedChunks != nullptr && !TextBuffer_Reserve( pPublishedChunks, TextBuffer_Length( &disk.staged ) ) ) {
        DiscardStaged( disk );
        result.status = map_files_status_t::DOCUMENT_FAILED;
        result.documentStatus = map_status_t::OUT_OF_MEMORY;
        return result;
    }
    if ( !PublishStaged( disk, &result.nRemoved ) ) {
        result.status = map_files_status_t::WRITE_FAILED;
        return result;
    }
    result.nChunkFiles = disk.nChunkFiles;
    LogPath( log_level_t::Info, "Map saved", disk.rootPath, CY_SOURCE_LOCATION );
    return result;
}

map_files_result_t MapFiles_Save( map_document_t *pMap, const char *pRootPath ) noexcept
{
    return SaveFiles( pMap, pRootPath, {}, CY_FALSE, nullptr );
}

map_files_result_t MapFiles_Save( map_document_t *pMap, const char *pRootPath,
                                 span_t<const string_view_t> persistedChunkPaths, text_buffer_t *pPublishedChunkPaths ) noexcept
{
    return SaveFiles( pMap, pRootPath, persistedChunkPaths, CY_TRUE, pPublishedChunkPaths );
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
