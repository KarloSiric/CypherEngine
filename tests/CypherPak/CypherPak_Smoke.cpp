//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherPak/CypherPak_Smoke.cpp
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

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if !defined( _WIN32 )
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pak = cypher::engine;

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
        pak::Pak_ErrorName( expected ),
        pak::Pak_ErrorName( actual ) );
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

bool ReadPhysicalFile( const std::filesystem::path &path, std::vector<unsigned char> &bytes )
{
    std::error_code ec{};
    const auto size = std::filesystem::file_size( path, ec );
    if ( ec || size > bytes.max_size() ) {
        return false;
    }

    bytes.resize( static_cast<std::size_t>( size ) );
    std::FILE *file = std::fopen( path.string().c_str(), "rb" );
    if ( file == nullptr ) {
        return false;
    }
    const bool readOk = bytes.empty() || std::fread( bytes.data(), 1u, bytes.size(), file ) == bytes.size();
    const bool closeOk = std::fclose( file ) == 0;
    return readOk && closeOk;
}

cypher::engine::common::u32 HashPathForFixture( const unsigned char *path )
{
    cypher::engine::common::u32 hash = 2166136261u;
    while ( *path != 0u ) {
        hash = ( hash ^ *path++ ) * 16777619u;
    }
    return hash;
}

bool CheckRejectedArchive(
    const std::filesystem::path &path,
    const std::vector<unsigned char> &bytes,
    const pak::pak_error_t expected,
    const cypher::engine::common::u32 flags = pak::CYPHER_PAK_OPEN_NONE )
{
    if ( !Check( WritePhysicalFile( path, bytes.data(), bytes.size() ), "write malformed archive fixture" ) ) {
        return false;
    }

    pak::pak_reader_t reader{};
    const std::string pathText = path.string();
    const auto result = pak::Pak_OpenReader( pathText.c_str(), flags, reader );
    const bool rejected = CheckError( result, expected, pathText.c_str() );
    const bool reset = Check(
        !reader.open && reader.handle == pak::CYPHER_PAK_INVALID_HANDLE &&
        reader.pNativeFile == nullptr && reader.entries == nullptr &&
        reader.stringTable == nullptr && reader.nFileCount == 0u,
        "failed archive open releases ownership and resets the reader" );
    pak::Pak_CloseReader( reader );
    return rejected && reset;
}

bool CheckMalformedArchives(
    const std::filesystem::path &root,
    const std::filesystem::path &archivePath )
{
    std::vector<unsigned char> original;
    if ( !Check( ReadPhysicalFile( archivePath, original ), "read valid archive for corruption fixtures" ) ) {
        return false;
    }

    // These are serialized byte offsets, independent of native struct layout.
    constexpr std::size_t headerIndexOffset = 48u;
    constexpr std::size_t headerStringTableOffset = 64u;
    constexpr std::size_t headerDataSize = 88u;
    constexpr std::size_t entryPathSize = 48u;
    constexpr std::size_t entryPathHash = 52u;
    constexpr std::size_t entryCompression = 56u;
    constexpr std::size_t entryFlags = 60u;
    const auto indexOffset = static_cast<std::size_t>( pak::Pak_LoadU64LE( original.data() + headerIndexOffset ) );
    const auto stringOffset = static_cast<std::size_t>( pak::Pak_LoadU64LE( original.data() + headerStringTableOffset ) );
    const auto pathOffset = stringOffset + static_cast<std::size_t>( pak::Pak_LoadU64LE( original.data() + indexOffset ) );
    const auto pathSize = pak::Pak_LoadU32LE( original.data() + indexOffset + entryPathSize );

    // Keep both the advertised final terminator and the prefix hash valid. The
    // reader must reject the earlier NUL rather than silently shorten the name.
    auto bytes = original;
    bytes[pathOffset + pathSize - 1u] = '\0';
    pak::Pak_StoreU32LE( bytes.data() + indexOffset + entryPathHash,
                        HashPathForFixture( bytes.data() + pathOffset ) );
    if ( !CheckRejectedArchive( root / "embedded_nul.cypak", bytes, pak::pak_error_t::ERR_INVALID_INDEX ) ) {
        return false;
    }

    bytes = original;
    bytes[pathOffset + pathSize] = 'x';
    if ( !CheckRejectedArchive( root / "missing_terminator.cypak", bytes, pak::pak_error_t::ERR_INVALID_INDEX ) ) {
        return false;
    }

    bytes = original;
    bytes[pathOffset] = '.';
    bytes[pathOffset + 1u] = '.';
    bytes[pathOffset + 2u] = '/';
    pak::Pak_StoreU32LE( bytes.data() + indexOffset + entryPathHash,
                        HashPathForFixture( bytes.data() + pathOffset ) );
    if ( !CheckRejectedArchive( root / "traversal_path.cypak", bytes, pak::pak_error_t::ERR_INVALID_PATH ) ) {
        return false;
    }

    // Preserve the physical archive length while shrinking its declared payload
    // region. The last file now crosses the section boundary by one byte.
    bytes = original;
    pak::Pak_StoreU64LE( bytes.data() + headerDataSize,
                        pak::Pak_LoadU64LE( bytes.data() + headerDataSize ) - 1u );
    if ( !CheckRejectedArchive( root / "payload_outside_section.cypak", bytes, pak::pak_error_t::ERR_ARCHIVE_CORRUPT ) ) {
        return false;
    }

    bytes = original;
    pak::Pak_StoreU32LE( bytes.data() + indexOffset + entryFlags,
                        pak::Pak_LoadU32LE( bytes.data() + indexOffset + entryFlags ) | pak::CYPHER_PAK_ENTRY_COMPRESSED );
    if ( !CheckRejectedArchive( root / "compressed_flag_with_none.cypak", bytes, pak::pak_error_t::ERR_ARCHIVE_CORRUPT ) ) {
        return false;
    }

    bytes = original;
    pak::Pak_StoreU32LE( bytes.data() + indexOffset + entryCompression,
                        static_cast<cypher::engine::common::u32>( pak::pak_compression_t::LZ4 ) );
    if ( !CheckRejectedArchive( root / "unsupported_codec.cypak", bytes, pak::pak_error_t::ERR_UNSUPPORTED_COMPRESSION ) ) {
        return false;
    }

    // Exercise truncation in the header, index, string table, and final payload.
    const std::size_t truncatedSizes[] = {
        pak::CYPHER_PAK_HEADER_SIZE - 1u,
        indexOffset + pak::CYPHER_PAK_FILE_ENTRY_SIZE - 1u,
        stringOffset + pathSize - 1u,
        original.size() - 1u
    };
    for ( const auto size : truncatedSizes ) {
        bytes.assign( original.begin(), original.begin() + size );
        const auto expected = size < pak::CYPHER_PAK_HEADER_SIZE
            ? pak::pak_error_t::ERR_FILE_READ_FAILED : pak::pak_error_t::ERR_ARCHIVE_CORRUPT;
        if ( !CheckRejectedArchive( root / "truncated_section.cypak", bytes, expected ) ) {
            return false;
        }
    }

    bytes = original;
    bytes.back() ^= 1u;
    return CheckRejectedArchive( root / "corrupt_payload_hash.cypak", bytes,
                                 pak::pak_error_t::ERR_CHECKSUM_MISMATCH,
                                 pak::CYPHER_PAK_OPEN_VERIFY_FILE_HASHES );
}

bool CheckNoStagingFiles( const std::filesystem::path &directory )
{
    std::error_code ec{};
    const std::filesystem::directory_iterator files( directory, ec );
    if ( !Check( !ec, "enumerate publication directory" ) ) {
        return false;
    }
    for ( const auto &file : files ) {
        if ( !Check( file.path().filename().string().find( ".cytmp." ) == std::string::npos,
                     "publication leaves no staging file behind" ) ) {
            return false;
        }
    }
    return true;
}

bool CheckWriterPublication( const std::filesystem::path &root )
{
    const auto directory = root / "publication";
    const auto sourcePath = directory / "source.bin";
    const auto archivePath = directory / "game.cypak";
    const std::string sourceText = sourcePath.string();
    const std::string archiveText = archivePath.string();
    const char oldPayload[] = "previous valid payload";
    if ( !Check( WritePhysicalFile( sourcePath, oldPayload, sizeof( oldPayload ) ), "write initial publication source" ) ) {
        return false;
    }

    const pak::pak_source_file_t source{ "data/item.bin", sourceText.c_str() };
    pak::pak_writer_config_t config{};
    config.szArchivePath = archiveText.c_str();
    if ( !CheckError( pak::Pak_CreateArchive( config, &source, 1u ), pak::pak_error_t::OK, "create publication baseline" ) ) {
        return false;
    }
    std::vector<unsigned char> original;
    if ( !Check( ReadPhysicalFile( archivePath, original ), "save publication baseline" ) ) {
        return false;
    }

    const std::string replacementPayload( 4096u, 'R' );
    if ( !Check( WritePhysicalFile( sourcePath, replacementPayload.data(), replacementPayload.size() ), "write replacement source" ) ) {
        return false;
    }

#if !defined( _WIN32 )
    // Force a real partial write after header and index serialization. Only the
    // child changes signal policy and RLIMIT_FSIZE; the test runner is unaffected.
    const pid_t child = ::fork();
    if ( child == 0 ) {
        struct rlimit limit{};
        if ( ::getrlimit( RLIMIT_FSIZE, &limit ) != 0 ||
             std::signal( SIGXFSZ, SIG_IGN ) == SIG_ERR ) {
            std::_Exit( 2 );
        }
        limit.rlim_cur = pak::CYPHER_PAK_HEADER_SIZE + pak::CYPHER_PAK_FILE_ENTRY_SIZE;
        if ( ::setrlimit( RLIMIT_FSIZE, &limit ) != 0 ) {
            std::_Exit( 2 );
        }
        const auto result = pak::Pak_CreateArchive( config, &source, 1u );
        std::_Exit( result == pak::pak_error_t::ERR_FILE_WRITE_FAILED ||
                    result == pak::pak_error_t::ERR_FILE_CLOSE_FAILED ? 0 : 3 );
    }
    if ( !Check( child > 0, "start isolated write-failure process" ) ) {
        return false;
    }
    int childStatus = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid( child, &childStatus, 0 );
    } while ( waited < 0 && errno == EINTR );
    if ( !Check( waited == child && WIFEXITED( childStatus ) && WEXITSTATUS( childStatus ) == 0,
                 "archive replacement encounters forced write failure" ) ) {
        return false;
    }

    std::vector<unsigned char> afterFailure;
    if ( !Check( ReadPhysicalFile( archivePath, afterFailure ) && afterFailure == original,
                 "failed replacement preserves the previous archive byte for byte" ) ||
         !CheckNoStagingFiles( directory ) ) {
        return false;
    }
    pak::pak_reader_t preservedReader{};
    if ( !CheckError( pak::Pak_OpenReader( archiveText.c_str(), pak::CYPHER_PAK_OPEN_VERIFY_FILE_HASHES, preservedReader ),
                      pak::pak_error_t::OK, "previous archive remains valid after failed replacement" ) ) {
        return false;
    }
    pak::Pak_CloseReader( preservedReader );
#endif

    // An occupied directory cannot be replaced by a regular archive. This tests
    // cleanup after the staging file has been fully written on every platform.
    const auto blockedPath = directory / "blocked.cypak";
    const auto sentinelPath = blockedPath / "keep.txt";
    if ( !Check( WritePhysicalTextFile( sentinelPath, "keep" ), "prepare blocked publication target" ) ) {
        return false;
    }
    const std::string blockedText = blockedPath.string();
    pak::pak_writer_config_t blockedConfig = config;
    blockedConfig.szArchivePath = blockedText.c_str();
    if ( !CheckError( pak::Pak_CreateArchive( blockedConfig, &source, 1u ), pak::pak_error_t::ERR_IO_ERROR,
                      "failed publication after a complete staged write" ) ||
         !CheckNoStagingFiles( directory ) ) {
        return false;
    }
    std::vector<unsigned char> sentinel;
    if ( !Check( ReadPhysicalFile( sentinelPath, sentinel ) &&
                 sentinel == std::vector<unsigned char>( { 'k', 'e', 'e', 'p' } ),
                 "failed publication preserves the occupied destination" ) ) {
        return false;
    }

    if ( !CheckError( pak::Pak_CreateArchive( config, &source, 1u ), pak::pak_error_t::OK,
                      "replace existing archive successfully" ) ||
         !CheckNoStagingFiles( directory ) ) {
        return false;
    }
    pak::pak_reader_t reader{};
    if ( !CheckError( pak::Pak_OpenReader( archiveText.c_str(), pak::CYPHER_PAK_OPEN_VERIFY_FILE_HASHES, reader ),
                      pak::pak_error_t::OK, "open replacement archive" ) ) {
        return false;
    }
    std::vector<char> payload( replacementPayload.size() );
    cypher::engine::common::u64 bytesRead = 0u;
    const auto result = pak::Pak_ReadFile( reader, "data/item.bin", payload.data(), payload.size(), bytesRead );
    pak::Pak_CloseReader( reader );
    return CheckError( result, pak::pak_error_t::OK, "read replacement payload" ) &&
           Check( bytesRead == replacementPayload.size() &&
                  std::memcmp( payload.data(), replacementPayload.data(), payload.size() ) == 0,
                  "successful replacement publishes complete new contents" );
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

    pak::Pak_StoreU32LE( nHeaderBytes + offset, version );
    offset += sizeof( cypher::engine::common::u32 );
    pak::Pak_StoreU32LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u32 );
    pak::Pak_StoreU32LE( nHeaderBytes + offset, pak::CYPHER_PAK_ENDIAN_TAG );
    offset += sizeof( cypher::engine::common::u32 );
    pak::Pak_StoreU32LE( nHeaderBytes + offset, pak::CYPHER_PAK_FORMAT_NONE );
    offset += sizeof( cypher::engine::common::u32 );

    pak::Pak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::Pak_StoreU64LE( nHeaderBytes + offset, 0u );
    offset += sizeof( cypher::engine::common::u64 );

    pak::Pak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::Pak_StoreU64LE( nHeaderBytes + offset, 0u );
    offset += sizeof( cypher::engine::common::u64 );
    pak::Pak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::Pak_StoreU64LE( nHeaderBytes + offset, 0u );
    offset += sizeof( cypher::engine::common::u64 );
    pak::Pak_StoreU64LE( nHeaderBytes + offset, pak::CYPHER_PAK_HEADER_SIZE );
    offset += sizeof( cypher::engine::common::u64 );
    pak::Pak_StoreU64LE( nHeaderBytes + offset, 0u );

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
    if ( !CheckError( pak::Pak_CreateArchive( config, szSourceFiles, 3u ), pak::pak_error_t::OK, "create archive" ) ) {
        return 1;
    }
    if ( !Check( std::filesystem::exists( szArchivePath, ec ), "archive exists" ) ) {
        return 1;
    }

    pak::pak_reader_t reader{};
    if ( !CheckError( pak::Pak_OpenReader( szBadMagicArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ), pak::pak_error_t::ERR_BAD_MAGIC, "open bad magic archive" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_OpenReader( szWrongVersionArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ), pak::pak_error_t::ERR_UNSUPPORTED_VERSION, "open wrong version archive" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_OpenReader( szTruncatedArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ), pak::pak_error_t::ERR_FILE_READ_FAILED, "open truncated archive" ) ) {
        return 1;
    }

    cypher::engine::common::u32 nUnopenedFileCount = 99u;
    if ( !CheckError( pak::Pak_GetFileCount( reader, nUnopenedFileCount ), pak::pak_error_t::ERR_INVALID_HANDLE, "unopened reader file count" ) ) {
        return 1;
    }
    if ( !Check( nUnopenedFileCount == 0u, "unopened reader file count reset" ) ) {
        return 1;
    }
    pak::pak_file_index_t nUnopenedIndex = 0u;
    if ( !CheckError( pak::Pak_FindFile( reader, "scripts/player.cfg", nUnopenedIndex ), pak::pak_error_t::ERR_INVALID_HANDLE, "unopened reader find" ) ) {
        return 1;
    }
    if ( !Check( nUnopenedIndex == pak::CYPHER_PAK_INVALID_FILE_INDEX, "unopened reader find index reset" ) ) {
        return 1;
    }
    char pUnopenedBuffer[8]{};
    cypher::engine::common::u64 nUnopenedBytesRead = 99u;
    if ( !CheckError( pak::Pak_ReadFile( reader, "scripts/player.cfg", pUnopenedBuffer, sizeof( pUnopenedBuffer ), nUnopenedBytesRead ), pak::pak_error_t::ERR_INVALID_HANDLE, "unopened reader read" ) ) {
        return 1;
    }
    if ( !Check( nUnopenedBytesRead == 0u, "unopened reader read byte count reset" ) ) {
        return 1;
    }

    if ( !CheckError( pak::Pak_OpenReader( szArchivePathString.c_str(), pak::CYPHER_PAK_OPEN_VERIFY_FILE_HASHES, reader ), pak::pak_error_t::OK, "open reader" ) ) {
        return 1;
    }
    if ( !Check( reader.header.version == pak::CYPHER_PAK_FORMAT_VERSION, "reader version" ) ) {
        return 1;
    }
    if ( !Check( pak::Pak_MagicEquals( reader.header.magic ), "reader magic" ) ) {
        return 1;
    }

    const auto readerHandle = reader.handle;
    const auto readerFile = reader.pNativeFile;
    const auto readerEntries = reader.entries;
    if ( !CheckError( pak::Pak_OpenReader( szArchivePath2String.c_str(), pak::CYPHER_PAK_OPEN_NONE, reader ), pak::pak_error_t::ERR_INVALID_STATE, "reopen live reader" ) ) {
        return 1;
    }
    if ( !Check( reader.handle == readerHandle && reader.pNativeFile == readerFile && reader.entries == readerEntries && pak::Pak_IsOpen( reader ), "reopening preserves live reader ownership" ) ) {
        return 1;
    }

    cypher::engine::common::u32 nFileCount = 0u;
    if ( !CheckError( pak::Pak_GetFileCount( reader, nFileCount ), pak::pak_error_t::OK, "get file count" ) ) {
        return 1;
    }
    if ( !Check( nFileCount == 3u, "file count value" ) ) {
        return 1;
    }

    pak::pak_file_index_t nPlayerIndex = pak::CYPHER_PAK_INVALID_FILE_INDEX;
    if ( !CheckError( pak::Pak_FindFile( reader, "SCRIPTS\\PLAYER.CFG", nPlayerIndex ), pak::pak_error_t::OK, "find normalized file" ) ) {
        return 1;
    }

    pak::pak_file_info_t playerInfo{};
    if ( !CheckError( pak::Pak_GetFileInfo( reader, nPlayerIndex, playerInfo ), pak::pak_error_t::OK, "get file info by index" ) ) {
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
    if ( !CheckError( pak::Pak_ReadFile( reader, "scripts/player.cfg", pReadBuffer, sizeof( pReadBuffer ), nBytesRead ), pak::pak_error_t::OK, "read file" ) ) {
        return 1;
    }
    if ( !Check( nBytesRead == std::strlen( playerCfg ) && std::memcmp( pReadBuffer, playerCfg, std::strlen( playerCfg ) ) == 0, "read file bytes" ) ) {
        return 1;
    }

    nBytesRead = 99u;
    if ( !CheckError( pak::Pak_ReadFile( reader, "empty/file.bin", nullptr, 0u, nBytesRead ), pak::pak_error_t::OK, "read empty file without a buffer" ) ) {
        return 1;
    }
    if ( !Check( nBytesRead == 0u, "empty file byte count" ) ) {
        return 1;
    }

    if ( !CheckError( pak::Pak_ReadFile( reader, "textures/wall.dds", pReadBuffer, 2u, nBytesRead ), pak::pak_error_t::ERR_BUFFER_TOO_SMALL, "read buffer too small" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_FindFile( reader, "missing/file.txt", nPlayerIndex ), pak::pak_error_t::ERR_ENTRY_NOT_FOUND, "missing file" ) ) {
        return 1;
    }

    pak::pak_stats_t stats{};
    if ( !CheckError( pak::Pak_GetStats( reader, stats ), pak::pak_error_t::OK, "get stats" ) ) {
        return 1;
    }
    if ( !Check( stats.nFileCount == 3u && stats.nStoredDataSize >= sizeof( pWallData ), "stats values" ) ) {
        return 1;
    }

    if ( !CheckError( pak::Pak_Verify( reader, pak::CYPHER_PAK_VERIFY_FULL ), pak::pak_error_t::OK, "verify full" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_CloseReader( reader ), pak::pak_error_t::OK, "close reader" ) ) {
        return 1;
    }
    if ( !CheckMalformedArchives( szRootPath, szArchivePath ) ) {
        return 1;
    }

    pak::pak_writer_config_t incrementalConfig{};
    incrementalConfig.szArchivePath = szArchivePath2String.c_str();
    pak::pak_writer_t writer{};
    if ( !CheckError( pak::Pak_BeginWriter( incrementalConfig, writer ), pak::pak_error_t::OK, "begin writer" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_AddFile( writer, szSourceFiles[0] ), pak::pak_error_t::OK, "add file" ) ) {
        return 1;
    }
    const auto writerHandle = writer.handle;
    const auto writerState = writer.pBuilderState;
    if ( !CheckError( pak::Pak_BeginWriter( config, writer ), pak::pak_error_t::ERR_INVALID_STATE, "restart live writer" ) ) {
        return 1;
    }
    if ( !Check( writer.handle == writerHandle && writer.pBuilderState == writerState && writer.nFileCount == 1u && writer.open, "restarting preserves live writer ownership and inputs" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_FinishWriter( writer ), pak::pak_error_t::OK, "finish writer" ) ) {
        return 1;
    }

    if ( !CheckError( pak::Pak_OpenReader( szArchivePath2String.c_str(), pak::CYPHER_PAK_OPEN_VERIFY_FILE_HASHES, reader ), pak::pak_error_t::OK, "open incremental archive" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_GetFileCount( reader, nFileCount ), pak::pak_error_t::OK, "get incremental file count" ) ) {
        return 1;
    }
    if ( !Check( nFileCount == 1u, "incremental file count value" ) ) {
        return 1;
    }
    if ( !CheckError( pak::Pak_CloseReader( reader ), pak::pak_error_t::OK, "close incremental reader" ) ) {
        return 1;
    }

    const pak::pak_source_file_t duplicateFiles[] = {
        { "duplicate/file.txt", szPlayerCfgPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "DUPLICATE\\FILE.TXT", szPlayerCfgPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE }
    };
    config.szArchivePath = szDuplicateArchivePathString.c_str();
    if ( !CheckError( pak::Pak_CreateArchive( config, duplicateFiles, 2u ), pak::pak_error_t::ERR_DUPLICATE_ENTRY, "duplicate normalized path" ) ) {
        return 1;
    }

    pak::pak_compression_config_t compressionConfig{};
    unsigned char compressed[16]{};
    cypher::engine::common::u64 nCompressedSize = 0u;
    if ( !CheckError( pak::Pak_Compress( compressionConfig, pWallData, sizeof( pWallData ), compressed, sizeof( compressed ), nCompressedSize ), pak::pak_error_t::OK, "none compression copy" ) ) {
        return 1;
    }
    if ( !Check( nCompressedSize == sizeof( pWallData ) && std::memcmp( compressed, pWallData, sizeof( pWallData ) ) == 0, "none compression bytes" ) ) {
        return 1;
    }
    cypher::engine::common::u64 bound = 0u;
    if ( !CheckError( pak::Pak_CompressBound( pak::pak_compression_t::LZ4, sizeof( pWallData ), bound ), pak::pak_error_t::ERR_UNSUPPORTED_COMPRESSION, "lz4 unsupported for now" ) ) {
        return 1;
    }

    if ( !CheckWriterPublication( szRootPath ) ) {
        return 1;
    }

    std::filesystem::remove_all( szRootPath, ec );
    return 0;
}
