//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherEngine/CypherPak/CypherPak_Smoke.cpp
//  Purpose: Tests Pak Smoke behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherPak.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace pak = cypher::engine::pak;

namespace {

bool Check( const bool condition, const char *message )
{
    if ( condition ) {
        return true;
    }

    std::fprintf( stderr, "pak smoke failed: %s\n", message );
    return false;
}

bool CheckError( const pak::pak_error_t actual, const pak::pak_error_t expected, const char *message )
{
    if ( actual == expected ) {
        return true;
    }

    std::fprintf(
        stderr,
        "pak smoke failed: %s: expected %s, got %s\n",
        message,
        pak::CypherPak_ErrorName( expected ),
        pak::CypherPak_ErrorName( actual ) );
    return false;
}

bool WritePhysicalFile( const std::filesystem::path &path, const void *data, const std::size_t size )
{
    std::error_code ec{};
    std::filesystem::create_directories( path.parent_path(), ec );
    if ( ec ) {
        return false;
    }

    std::FILE *file = std::fopen( path.string().c_str(), "wb" );
    if ( file == nullptr ) {
        return false;
    }

    const bool bWriteOk = size == 0u || std::fwrite( data, 1u, size, file ) == size;
    const bool bCloseOk = std::fclose( file ) == 0;
    return bWriteOk && bCloseOk;
}

bool WritePhysicalTextFile( const std::filesystem::path &path, const char *text )
{
    return WritePhysicalFile( path, text, std::strlen( text ) );
}

bool WriteMinimalArchiveHeader(
    const std::filesystem::path &path,
    const bool bValidMagic,
    const cypher::engine::common::u32 version )
{
    unsigned char nHeaderBytes[pak::CYPHER_PAK_HEADER_SIZE]{};
    std::size_t offset = 0u;

    if ( bValidMagic ) {
        std::memcpy( nHeaderBytes + offset, pak::CYPHER_PAK_MAGIC, pak::CYPHER_PAK_MAGIC_SIZE );
    } else {
        std::memcpy( nHeaderBytes + offset, "NOTCYPACKAGE", 12u );
    }
    offset += pak::CYPHER_PAK_MAGIC_SIZE;

    pak::CypherPak_StoreU32LE( nHeaderBytes + offset, version );
    offset += sizeof( cypher::engine::common::u32 );
    pak::CypherPak_StoreU32LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u32 );
    pak::CypherPak_StoreU32LE( nHeaderBytes + offset, pak::CYPHER_PAK_ENDIAN_TAG );
    offset += sizeof( cypher::engine::common::u32 );
    pak::CypherPak_StoreU32LE( nHeaderBytes + offset, pak::CYPHER_PAK_FORMAT_NONE );
    offset += sizeof( cypher::engine::common::u32 );

    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, 0u );
    offset += sizeof( cypher::engine::common::u64 );

    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, 0u );
    offset += sizeof( cypher::engine::common::u64 );
    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, 0u );
    offset += sizeof( cypher::engine::common::u64 );
    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::CypherPak_StoreU64LE( nHeaderBytes + offset, 0u );

    return WritePhysicalFile( path, nHeaderBytes, sizeof( nHeaderBytes ) );
}

}       // namespace

int main()
{
    const std::filesystem::path szRootPath =
        std::filesystem::temp_directory_path() / "CypherPak_Smoke";

    std::error_code ec{};
    std::filesystem::remove_all( szRootPath, ec );
    std::filesystem::create_directories( szRootPath, ec );
    if ( ec ) {
        std::fprintf( stderr, "pak smoke failed: temp directory setup failed\n" );
        return 1;
    }

    const std::filesystem::path szSourcePath = szRootPath / "Source";
    const std::filesystem::path szPlayerCfgPath = szSourcePath / "Player.cfg";
    const std::filesystem::path szWallPath = szSourcePath / "Wall.dds";
    const std::filesystem::path szEmptyPath = szSourcePath / "Empty.bin";
    const std::filesystem::path szArchivePath = szRootPath / "game.cypak";
    const std::filesystem::path szArchivePath2 = szRootPath / "game_incremental.cypak";
    const std::filesystem::path szDuplicateArchivePath = szRootPath / "duplicate.cypak";
    const std::filesystem::path szBadMagicArchivePath = szRootPath / "bad_magic.cypak";
    const std::filesystem::path szWrongVersionArchivePath = szRootPath / "wrong_version.cypak";
    const std::filesystem::path szTruncatedArchivePath = szRootPath / "truncated.cypak";

    const std::string szPlayerCfgPathString = szPlayerCfgPath.string();
    const std::string szWallPathString = szWallPath.string();
    const std::string szEmptyPathString = szEmptyPath.string();
    const std::string szArchivePathString = szArchivePath.string();
    const std::string szArchivePath2String = szArchivePath2.string();
    const std::string szDuplicateArchivePathString = szDuplicateArchivePath.string();
    const std::string szBadMagicArchivePathString = szBadMagicArchivePath.string();
    const std::string szWrongVersionArchivePathString = szWrongVersionArchivePath.string();
    const std::string szTruncatedArchivePathString = szTruncatedArchivePath.string();

    const char playerCfg[] = "name player1\nrate 1\n";
    const unsigned char pWallData[] = { 0x13u, 0x37u, 0xC0u, 0xDEu, 0x00u, 0x42u };

    if ( !Check( WritePhysicalTextFile( szPlayerCfgPath, playerCfg ), "write player config source" ) ) {
        return 1;
    }
    if ( !Check( WritePhysicalFile( szWallPath, pWallData, sizeof( pWallData ) ), "write wall source" ) ) {
        return 1;
    }
    if ( !Check( WritePhysicalFile( szEmptyPath, "", 0u ), "write empty source" ) ) {
        return 1;
    }

    if ( !Check( WriteMinimalArchiveHeader( szBadMagicArchivePath, false, pak::CYPHER_PAK_FORMAT_VERSION ), "write bad magic archive" ) ) {
        return 1;
    }
    if ( !Check( WriteMinimalArchiveHeader( szWrongVersionArchivePath, true, pak::CYPHER_PAK_FORMAT_VERSION + 1u ), "write wrong version archive" ) ) {
        return 1;
    }
    const unsigned char pTruncatedBytes[] = { 'C', 'Y', 'P', 'A' };
    if ( !Check( WritePhysicalFile( szTruncatedArchivePath, pTruncatedBytes, sizeof( pTruncatedBytes ) ), "write truncated archive" ) ) {
        return 1;
    }

    const pak::pak_source_file_t szSourceFiles[] = {
        { "Scripts\\Player.CFG", szPlayerCfgPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "textures/Wall.DDS", szWallPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "empty/file.bin", szEmptyPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE }
    };

    pak::pak_writer_config_t config{};
    config.szArchivePath = szArchivePathString.c_str();
    if ( !CheckError( pak::CypherPak_CreateArchive( config, szSourceFiles, 3u ), pak::pak_error_t::OK, "create archive" ) ) {
        return 1;
    }
    if ( !Check( std::filesystem::exists( szArchivePath, ec ), "archive exists" ) ) {
        return 1;
    }

    pak::pak_reader_t reader{};
    if ( !CheckError( pak::CypherPak_OpenReader( szBadMagicArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ), pak::pak_error_t::ERR_BAD_MAGIC, "open bad magic archive" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_OpenReader( szWrongVersionArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ), pak::pak_error_t::ERR_UNSUPPORTED_VERSION, "open wrong version archive" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_OpenReader( szTruncatedArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ), pak::pak_error_t::ERR_FILE_READ_FAILED, "open truncated archive" ) ) {
        return 1;
    }

    cypher::engine::common::u32 nUnopenedFileCount = 99u;
    if ( !CheckError( pak::CypherPak_GetFileCount( reader, nUnopenedFileCount ), pak::pak_error_t::ERR_INVALID_HANDLE, "unopened reader file count" ) ) {
        return 1;
    }
    if ( !Check( nUnopenedFileCount == 0u, "unopened reader file count reset" ) ) {
        return 1;
    }
    pak::pak_file_index_t nUnopenedIndex = 0u;
    if ( !CheckError( pak::CypherPak_FindFile( reader, "scripts/player.cfg", nUnopenedIndex ), pak::pak_error_t::ERR_INVALID_HANDLE, "unopened reader find" ) ) {
        return 1;
    }
    if ( !Check( nUnopenedIndex == pak::CYPHER_PAK_INVALID_FILE_INDEX, "unopened reader find index reset" ) ) {
        return 1;
    }
    char pUnopenedBuffer[8]{};
    cypher::engine::common::u64 nUnopenedBytesRead = 99u;
    if ( !CheckError( pak::CypherPak_ReadFile( reader, "scripts/player.cfg", pUnopenedBuffer, sizeof( pUnopenedBuffer ), nUnopenedBytesRead ), pak::pak_error_t::ERR_INVALID_HANDLE, "unopened reader read" ) ) {
        return 1;
    }
    if ( !Check( nUnopenedBytesRead == 0u, "unopened reader read byte count reset" ) ) {
        return 1;
    }

    if ( !CheckError( pak::CypherPak_OpenReader( szArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_VERIFY_FILE_HASHES, reader ), pak::pak_error_t::OK, "open reader" ) ) {
        return 1;
    }
    if ( !Check( reader.header.version == pak::CYPHER_PAK_FORMAT_VERSION, "reader version" ) ) {
        return 1;
    }
    if ( !Check( pak::CypherPak_MagicEquals( reader.header.magic ), "reader magic" ) ) {
        return 1;
    }

    cypher::engine::common::u32 nFileCount = 0u;
    if ( !CheckError( pak::CypherPak_GetFileCount( reader, nFileCount ), pak::pak_error_t::OK, "get file count" ) ) {
        return 1;
    }
    if ( !Check( nFileCount == 3u, "file count value" ) ) {
        return 1;
    }

    pak::pak_file_index_t nPlayerIndex = pak::CYPHER_PAK_INVALID_FILE_INDEX;
    if ( !CheckError( pak::CypherPak_FindFile( reader, "SCRIPTS\\PLAYER.CFG", nPlayerIndex ), pak::pak_error_t::OK, "find normalized file" ) ) {
        return 1;
    }

    pak::pak_file_info_t playerInfo{};
    if ( !CheckError( pak::CypherPak_GetFileInfo( reader, nPlayerIndex, playerInfo ), pak::pak_error_t::OK, "get file info by index" ) ) {
        return 1;
    }
    if ( !Check( std::strcmp( playerInfo.szVirtualPath, "scripts/player.cfg" ) == 0, "normalized file info path" ) ) {
        return 1;
    }
    if ( !Check( playerInfo.nStoredSize == std::strlen( playerCfg ), "stored size" ) ) {
        return 1;
    }

    char pReadBuffer[128]{};
    cypher::engine::common::u64 nBytesRead = 0u;
    if ( !CheckError( pak::CypherPak_ReadFile( reader, "scripts/player.cfg", pReadBuffer, sizeof( pReadBuffer ), nBytesRead ), pak::pak_error_t::OK, "read file" ) ) {
        return 1;
    }
    if ( !Check( nBytesRead == std::strlen( playerCfg ) && std::memcmp( pReadBuffer, playerCfg, std::strlen( playerCfg ) ) == 0, "read file bytes" ) ) {
        return 1;
    }

    if ( !CheckError( pak::CypherPak_ReadFile( reader, "textures/wall.dds", pReadBuffer, 2u, nBytesRead ), pak::pak_error_t::ERR_BUFFER_TOO_SMALL, "read buffer too small" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_FindFile( reader, "missing/file.txt", nPlayerIndex ), pak::pak_error_t::ERR_ENTRY_NOT_FOUND, "missing file" ) ) {
        return 1;
    }

    pak::pak_stats_t stats{};
    if ( !CheckError( pak::CypherPak_GetStats( reader, stats ), pak::pak_error_t::OK, "get stats" ) ) {
        return 1;
    }
    if ( !Check( stats.nFileCount == 3u && stats.nStoredDataSize >= sizeof( pWallData ), "stats values" ) ) {
        return 1;
    }

    if ( !CheckError( pak::CypherPak_Verify( reader, pak::CYPHER_PAK_VERIFY_FULL ), pak::pak_error_t::OK, "verify full" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_CloseReader( reader ), pak::pak_error_t::OK, "close reader" ) ) {
        return 1;
    }

    pak::pak_writer_config_t incrementalConfig{};
    incrementalConfig.szArchivePath = szArchivePath2String.c_str();
    pak::pak_writer_t writer{};
    if ( !CheckError( pak::CypherPak_BeginWriter( incrementalConfig, writer ), pak::pak_error_t::OK, "begin writer" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_AddFile( writer, szSourceFiles[0] ), pak::pak_error_t::OK, "add file" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_FinishWriter( writer ), pak::pak_error_t::OK, "finish writer" ) ) {
        return 1;
    }

    if ( !CheckError( pak::CypherPak_OpenReader( szArchivePath2String.c_str(), pak::CYPHER_PAK_OPEN_VERIFY_FILE_HASHES, reader ), pak::pak_error_t::OK, "open incremental archive" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_GetFileCount( reader, nFileCount ), pak::pak_error_t::OK, "get incremental file count" ) ) {
        return 1;
    }
    if ( !Check( nFileCount == 1u, "incremental file count value" ) ) {
        return 1;
    }
    if ( !CheckError( pak::CypherPak_CloseReader( reader ), pak::pak_error_t::OK, "close incremental reader" ) ) {
        return 1;
    }

    const pak::pak_source_file_t duplicateFiles[] = {
        { "duplicate/file.txt", szPlayerCfgPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "DUPLICATE\\FILE.TXT", szPlayerCfgPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE }
    };
    config.szArchivePath = szDuplicateArchivePathString.c_str();
    if ( !CheckError( pak::CypherPak_CreateArchive( config, duplicateFiles, 2u ), pak::pak_error_t::ERR_DUPLICATE_ENTRY, "duplicate normalized path" ) ) {
        return 1;
    }

    pak::pak_compression_config_t compressionConfig{};
    unsigned char compressed[16]{};
    cypher::engine::common::u64 nCompressedSize = 0u;
    if ( !CheckError( pak::CypherPak_Compress( compressionConfig, pWallData, sizeof( pWallData ), compressed, sizeof( compressed ), nCompressedSize ), pak::pak_error_t::OK, "none compression copy" ) ) {
        return 1;
    }
    if ( !Check( nCompressedSize == sizeof( pWallData ) && std::memcmp( compressed, pWallData, sizeof( pWallData ) ) == 0, "none compression bytes" ) ) {
        return 1;
    }
    cypher::engine::common::u64 bound = 0u;
    if ( !CheckError( pak::CypherPak_CompressBound( pak::pak_compression_t::LZ4, sizeof( pWallData ), bound ), pak::pak_error_t::ERR_UNSUPPORTED_COMPRESSION, "lz4 unsupported for now" ) ) {
        return 1;
    }

    std::filesystem::remove_all( szRootPath, ec );
    return 0;
}
