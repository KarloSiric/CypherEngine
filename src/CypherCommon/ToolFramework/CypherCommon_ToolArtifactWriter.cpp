//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolArtifactWriter.cpp
//  Purpose: Implements transactional native-file publication for tool artifacts.
//  Details: Temporary files use exclusive creation in the destination directory.
//           A completed and flushed file is renamed over the destination, keeping
//           an existing artifact intact if preparation fails.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolArtifactWriter.h"

#include "CypherCommon_Atomic.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_Process.h"
#include "CypherCommon_Stream.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_TextBuffer.h"

namespace cypher::common
{
namespace
{

constexpr usize CY_TOOL_ARTIFACT_TEMP_ATTEMPTS = 64u;

atomic_u64_t g_nArtifactSequence{ 0u };

bool_t MakeTemporaryPath(
    string_view_t destination,
    text_buffer_t *pTemporaryPath ) noexcept
{
    char suffix[96]{};
    const u64 nSequence = Cy_AtomicFetchAdd(
        &g_nArtifactSequence,
        static_cast<u64>( 1u ),
        CY_MEMORY_ORDER_RELAXED );
    const string_format_result_t formatted = StringFormat_Printf(
        suffix,
        sizeof( suffix ),
        ".cytmp.%llu.%llu",
        static_cast<unsigned long long>( Cy_ProcessGetCurrentId() ),
        static_cast<unsigned long long>( nSequence ) );
    return formatted.status == string_format_status_t::OK &&
           TextBuffer_Assign( pTemporaryPath, destination ) &&
           TextBuffer_Append(
               pTemporaryPath,
               StringView_FromCString( suffix ) );
}

tool_status_t WriteTemporaryFile(
    string_view_t path,
    binary_block_t contents ) noexcept
{
    native_file_t *pFile = FileIo_OpenNative(
        path,
        FILE_OPEN_FLAG_WRITE |
            FILE_OPEN_FLAG_CREATE |
            FILE_OPEN_FLAG_EXCLUSIVE,
        Allocator_GetSystem() );
    if ( pFile == nullptr ) {
        return FileIo_NativeExists( path )
            ? tool_status_t::ALREADY_EXISTS
            : tool_status_t::IO_ERROR;
    }

    stream_t stream = FileIo_AsStream( pFile );
    const bool_t bWritten = Stream_WriteExact(
        &stream,
        contents.pData,
        contents.cbSize ) == stream_status_t::OK;
    const bool_t bFlushed = bWritten &&
        Stream_Flush( &stream ) == stream_status_t::OK;
    FileIo_CloseNative( pFile );
    return bFlushed ? tool_status_t::OK : tool_status_t::IO_ERROR;
}

} // namespace

tool_status_t ToolArtifactWriter_WriteNative(
    string_view_t nativePath,
    binary_block_t contents ) noexcept
{
    if ( !StringView_IsValid( nativePath ) ||
         nativePath.cchLength == 0u ||
         !BinaryBlock_IsValid( contents ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    const string_view_t parent = StringPath_Parent( nativePath );
    if ( parent.cchLength != 0u &&
         !FileIo_CreateDirectoriesNative( parent ) ) {
        return tool_status_t::IO_ERROR;
    }

    text_buffer_t temporaryPath{};
    if ( !TextBuffer_Init( &temporaryPath, Allocator_GetSystem() ) ) {
        return tool_status_t::OUT_OF_MEMORY;
    }

    for ( usize iAttempt = 0u;
          iAttempt < CY_TOOL_ARTIFACT_TEMP_ATTEMPTS;
          ++iAttempt ) {
        if ( !MakeTemporaryPath( nativePath, &temporaryPath ) ) {
            return tool_status_t::OUT_OF_MEMORY;
        }
        const tool_status_t writeStatus = WriteTemporaryFile(
            TextBuffer_View( &temporaryPath ),
            contents );
        if ( writeStatus == tool_status_t::ALREADY_EXISTS ) {
            continue;
        }
        if ( ToolStatus_Failed( writeStatus ) ) {
            (void)FileIo_RemoveNative( TextBuffer_View( &temporaryPath ) );
            return writeStatus;
        }
        if ( FileIo_ReplaceNative(
                 TextBuffer_View( &temporaryPath ),
                 nativePath ) ) {
            return tool_status_t::OK;
        }
        (void)FileIo_RemoveNative( TextBuffer_View( &temporaryPath ) );
        return tool_status_t::IO_ERROR;
    }
    return tool_status_t::ALREADY_EXISTS;
}

} // namespace cypher::common
