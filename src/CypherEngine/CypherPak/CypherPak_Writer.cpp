//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherPak/CypherPak_Writer.cpp
//  Purpose: Implements the CypherPak Pak Writer module.
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

#include "CypherPak_Writer.h"
#include "CypherPak_Compression.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace cypher::engine::pak
{

namespace {

constexpr common::u32 FNV1A32_OFFSET = 2166136261u;
constexpr common::u32 FNV1A32_PRIME = 16777619u;
constexpr common::u64 FNV1A64_OFFSET = 14695981039346656037ull;
constexpr common::u64 FNV1A64_PRIME = 1099511628211ull;
constexpr common::u64 IO_CHUNK_SIZE = 64u * 1024u;

std::atomic<common::u32> s_NextWriterHandle{ 1u };

struct source_file_copy_t {
    std::string szVirtualPath;
    std::string szPhysicalPath;
    pak_compression_t compression{ pak_compression_t::NONE };
    common::u32 flags{ CYPHER_PAK_ENTRY_NONE };
};

struct packed_file_t {
    std::string szVirtualPath;
    std::string szPhysicalPath;
    std::vector<common::u8> data;
    pak_compression_t compression{ pak_compression_t::NONE };
    common::u32 flags{ CYPHER_PAK_ENTRY_NONE };
    common::u64 nPathOffset{ 0u };
    common::u64 nDataOffset{ 0u };
    common::u64 nStoredSize{ 0u };
    common::u64 nUnpackedSize{ 0u };
    common::u64 modifiedTimeUtc{ 0u };
    common::u64 contentHash{ 0u };
    common::u32 szPathHash{ 0u };
};

struct writer_state_t {
    std::vector<source_file_copy_t> files;
};

pak_handle_t AllocateWriterHandle()
{
    common::u32 handle = s_NextWriterHandle.fetch_add( 1u );
    if ( handle == CYPHER_PAK_INVALID_HANDLE ) {
        handle = s_NextWriterHandle.fetch_add( 1u );
    }
    return handle;
}

bool CopyString( char *out, const common::u32 nOutSize, const char *text )
{
    if ( out == nullptr || nOutSize == 0u || text == nullptr ) {
        return false;
    }

    const common::usize nTextLen = std::strlen( text );
    if ( nTextLen + 1u > nOutSize ) {
        out[0] = '\0';
        return false;
    }

    std::memcpy( out, text, nTextLen + 1u );
    return true;
}

bool IsAsciiAlpha( const char c )
{
    return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' );
}

char ToLowerAscii( const char c )
{
    if ( c >= 'A' && c <= 'Z' ) {
        return static_cast<char>( c - 'A' + 'a' );
    }
    return c;
}

bool IsInvalidPathChar( const char c )
{
    const unsigned char u = static_cast<unsigned char>( c );
    if ( u < 32u || u == 127u || u >= 128u ) {
        return true;
    }

    switch ( c ) {
    case ':':
    case '"':
    case '<':
    case '>':
    case '|':
    case ' ':
    case '*':
    case '?':
        return true;
    default:
        return false;
    }
}

pak_error_t NormalizeVirtualPath( const char *szVirtualPath, char *szOutPath, const common::u32 nOutPathSize )
{
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return pak_error_t::ERR_INVALID_PATH;
    }
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }

    szOutPath[0] = '\0';

    if ( szVirtualPath[0] == '/' || szVirtualPath[0] == '\\' ) {
        return pak_error_t::ERR_INVALID_PATH;
    }
    if ( IsAsciiAlpha( szVirtualPath[0] ) && szVirtualPath[1] == ':' ) {
        return pak_error_t::ERR_INVALID_PATH;
    }

    common::u32 nWriteIndex = 0u;
    common::u32 nSegmentCount = 0u;
    const char *cursor = szVirtualPath;

    while ( *cursor != '\0' ) {
        while ( *cursor == '/' || *cursor == '\\' ) {
            ++cursor;
        }
        if ( *cursor == '\0' ) {
            break;
        }

        const char *segmentStart = cursor;
        while ( *cursor != '\0' && *cursor != '/' && *cursor != '\\' ) {
            if ( IsInvalidPathChar( *cursor ) ) {
                szOutPath[0] = '\0';
                return pak_error_t::ERR_INVALID_PATH;
            }
            ++cursor;
        }

        const common::usize nSegmentLen = static_cast<common::usize>( cursor - segmentStart );
        if ( nSegmentLen == 0u ) {
            continue;
        }
        if ( ( nSegmentLen == 1u && segmentStart[0] == '.' ) ||
             ( nSegmentLen == 2u && segmentStart[0] == '.' && segmentStart[1] == '.' ) ) {
            szOutPath[0] = '\0';
            return pak_error_t::ERR_INVALID_PATH;
        }
        if ( nSegmentCount > 0u ) {
            if ( nWriteIndex + 1u >= nOutPathSize ) {
                szOutPath[0] = '\0';
                return pak_error_t::ERR_BUFFER_TOO_SMALL;
            }
            szOutPath[nWriteIndex++] = '/';
        }
        for ( common::usize i = 0u; i < nSegmentLen; ++i ) {
            if ( nWriteIndex + 1u >= nOutPathSize ) {
                szOutPath[0] = '\0';
                return pak_error_t::ERR_BUFFER_TOO_SMALL;
            }
            szOutPath[nWriteIndex++] = ToLowerAscii( segmentStart[i] );
        }

        ++nSegmentCount;
    }

    if ( nSegmentCount == 0u ) {
        return pak_error_t::ERR_INVALID_PATH;
    }

    szOutPath[nWriteIndex] = '\0';
    return pak_error_t::OK;
}

common::u32 HashPath32( const char *path )
{
    common::u32 hash = FNV1A32_OFFSET;
    const unsigned char *cursor = reinterpret_cast<const unsigned char *>( path );
    while ( *cursor != 0u ) {
        hash ^= static_cast<common::u32>( *cursor++ );
        hash *= FNV1A32_PRIME;
    }
    return hash;
}

common::u64 HashBytes64( const void *data, const common::u64 size, common::u64 hash = FNV1A64_OFFSET )
{
    const common::u8 *bytes = static_cast<const common::u8 *>( data );
    for ( common::u64 i = 0u; i < size; ++i ) {
        hash ^= static_cast<common::u64>( bytes[i] );
        hash *= FNV1A64_PRIME;
    }
    return hash;
}

bool AddOverflow( const common::u64 a, const common::u64 b, common::u64 &out )
{
    out = a + b;
    return out < a;
}

bool WriterFlagsSupported( const common::u32 flags )
{
    const common::u32 bSupportedFlags =
        CYPHER_PAK_WRITER_DETERMINISTIC |
        CYPHER_PAK_WRITER_SORT_INDEX |
        CYPHER_PAK_WRITER_WRITE_HASHES |
        CYPHER_PAK_WRITER_FAIL_ON_DUPLICATE;
    return ( flags & ~bSupportedFlags ) == 0u;
}

bool ReadExact( std::FILE *file, void *buffer, const common::u64 size )
{
    if ( size == 0u ) {
        return true;
    }
    if ( file == nullptr || buffer == nullptr ) {
        return false;
    }

    common::u8 *write = static_cast<common::u8 *>( buffer );
    common::u64 remaining = size;
    while ( remaining > 0u ) {
        const common::u64 chunk64 = remaining < IO_CHUNK_SIZE ? remaining : IO_CHUNK_SIZE;
        const common::usize chunk = static_cast<common::usize>( chunk64 );
        if ( std::fread( write, 1u, chunk, file ) != chunk ) {
            return false;
        }
        write += chunk;
        remaining -= chunk64;
    }

    return true;
}

bool WriteExact( std::FILE *file, const void *buffer, const common::u64 size )
{
    if ( size == 0u ) {
        return true;
    }
    if ( file == nullptr || buffer == nullptr ) {
        return false;
    }

    const common::u8 *read = static_cast<const common::u8 *>( buffer );
    common::u64 remaining = size;
    while ( remaining > 0u ) {
        const common::u64 chunk64 = remaining < IO_CHUNK_SIZE ? remaining : IO_CHUNK_SIZE;
        const common::usize chunk = static_cast<common::usize>( chunk64 );
        if ( std::fwrite( read, 1u, chunk, file ) != chunk ) {
            return false;
        }
        read += chunk;
        remaining -= chunk64;
    }

    return true;
}

pak_error_t WriteZeros( std::FILE *file, const common::u64 count )
{
    common::u8 zeros[4096]{};
    common::u64 remaining = count;

    while ( remaining > 0u ) {
        const common::u64 chunk64 = remaining < sizeof( zeros ) ? remaining : sizeof( zeros );
        if ( !WriteExact( file, zeros, chunk64 ) ) {
            return pak_error_t::ERR_FILE_WRITE_FAILED;
        }
        remaining -= chunk64;
    }

    return pak_error_t::OK;
}

common::u64 FileModifiedTimeUtc( const std::filesystem::path &path )
{
    std::error_code ec{};
    const auto bFileTime = std::filesystem::last_write_time( path, ec );
    if ( ec ) {
        return 0u;
    }

    const auto nSystemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        bFileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now() );
    const std::time_t time = std::chrono::system_clock::to_time_t( nSystemTime );
    return time < 0 ? 0u : static_cast<common::u64>( time );
}

pak_error_t ReadPhysicalFile(
    const char *szPhysicalPath,
    std::vector<common::u8> &dataOut,
    common::u64 &nOutModifiedTime )
{
    dataOut.clear();
    nOutModifiedTime = 0u;

    if ( szPhysicalPath == nullptr || szPhysicalPath[0] == '\0' ) {
        return pak_error_t::ERR_INVALID_PATH;
    }

    std::error_code ec{};
    const std::filesystem::path path( szPhysicalPath );
    if ( !std::filesystem::exists( path, ec ) || ec ) {
        return ec ? pak_error_t::ERR_IO_ERROR : pak_error_t::ERR_PATH_NOT_FOUND;
    }
    if ( !std::filesystem::is_regular_file( path, ec ) || ec ) {
        return ec ? pak_error_t::ERR_IO_ERROR : pak_error_t::ERR_INVALID_PATH;
    }

    const common::u64 nFileSize = static_cast<common::u64>( std::filesystem::file_size( path, ec ) );
    if ( ec ) {
        return pak_error_t::ERR_IO_ERROR;
    }
    if ( nFileSize > static_cast<common::u64>( std::numeric_limits<common::usize>::max() ) ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    try {
        dataOut.resize( static_cast<common::usize>( nFileSize ) );
    } catch ( const std::bad_alloc & ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    std::FILE *file = std::fopen( szPhysicalPath, "rb" );
    if ( file == nullptr ) {
        dataOut.clear();
        return pak_error_t::ERR_FILE_OPEN_FAILED;
    }

    const bool bReadOk = ReadExact( file, dataOut.data(), nFileSize );
    const bool bCloseOk = std::fclose( file ) == 0;
    if ( !bReadOk || !bCloseOk ) {
        dataOut.clear();
        return bReadOk ? pak_error_t::ERR_FILE_CLOSE_FAILED : pak_error_t::ERR_FILE_READ_FAILED;
    }

    nOutModifiedTime = FileModifiedTimeUtc( path );
    return pak_error_t::OK;
}

void EncodeHeader( const pak_header_t &header, common::u8 *bytes )
{
    common::usize offset = 0u;

    std::memcpy( bytes + offset, header.magic, CYPHER_PAK_MAGIC_SIZE );
    offset += CYPHER_PAK_MAGIC_SIZE;

    CypherPak_StoreU32LE( bytes + offset, header.version );
    offset += 4u;
    CypherPak_StoreU32LE( bytes + offset, header.nHeaderSize );
    offset += 4u;
    CypherPak_StoreU32LE( bytes + offset, header.endianTag );
    offset += 4u;
    CypherPak_StoreU32LE( bytes + offset, header.flags );
    offset += 4u;

    CypherPak_StoreU64LE( bytes + offset, header.nArchiveSize );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.nFileCount );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.nIndexOffset );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.nIndexSize );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.nStringTableOffset );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.nStringTableSize );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.nDataOffset );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.nDataSize );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, header.archiveHash );
    offset += 8u;

    for ( common::u32 i = 0u; i < 4u; ++i ) {
        CypherPak_StoreU64LE( bytes + offset, header.reserved[i] );
        offset += 8u;
    }
}

void EncodeFileEntry( const pak_disk_file_entry_t &entry, common::u8 *bytes )
{
    common::usize offset = 0u;

    CypherPak_StoreU64LE( bytes + offset, entry.nPathOffset );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, entry.nDataOffset );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, entry.nStoredSize );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, entry.nUnpackedSize );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, entry.modifiedTimeUtc );
    offset += 8u;
    CypherPak_StoreU64LE( bytes + offset, entry.contentHash );
    offset += 8u;
    CypherPak_StoreU32LE( bytes + offset, entry.nPathSize );
    offset += 4u;
    CypherPak_StoreU32LE( bytes + offset, entry.szPathHash );
    offset += 4u;
    CypherPak_StoreU32LE( bytes + offset, entry.compression );
    offset += 4u;
    CypherPak_StoreU32LE( bytes + offset, entry.flags );
}

pak_error_t MakeSourceCopy( const pak_source_file_t &source, source_file_copy_t &copyOut )
{
    copyOut = {};

    if ( source.szVirtualPath == nullptr || source.szVirtualPath[0] == '\0' ||
         source.szPhysicalPath == nullptr || source.szPhysicalPath[0] == '\0' ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }

    const common::u32 bAllowedEntryFlags = CYPHER_PAK_ENTRY_HAS_HASH;
    if ( ( source.flags & ~bAllowedEntryFlags ) != 0u ) {
        return pak_error_t::ERR_UNSUPPORTED_FLAGS;
    }
    if ( !CypherPak_CompressionSupported( source.compression ) ) {
        return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
    }

    try {
        copyOut.szVirtualPath = source.szVirtualPath;
        copyOut.szPhysicalPath = source.szPhysicalPath;
    } catch ( const std::bad_alloc & ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }
    copyOut.compression = source.compression;
    copyOut.flags = source.flags;
    return pak_error_t::OK;
}

pak_error_t PrepareFiles(
    const std::vector<source_file_copy_t> &szSourceFiles,
    const common::u32 nWriterFlags,
    std::vector<packed_file_t> &filesOut )
{
    filesOut.clear();

    try {
        filesOut.reserve( szSourceFiles.size() );
    } catch ( const std::bad_alloc & ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    for ( const source_file_copy_t &source : szSourceFiles ) {
        packed_file_t file{};

        char szNormalizedPath[CYPHER_PAK_MAX_PATH_LENGTH]{};
        pak_error_t result = NormalizeVirtualPath( source.szVirtualPath.c_str(), szNormalizedPath, sizeof( szNormalizedPath ) );
        if ( result != pak_error_t::OK ) {
            return result;
        }

        try {
            file.szVirtualPath = szNormalizedPath;
            file.szPhysicalPath = source.szPhysicalPath;
        } catch ( const std::bad_alloc & ) {
            return pak_error_t::ERR_OUT_OF_MEMORY;
        }

        file.compression = source.compression;
        file.flags = source.flags;
        if ( ( nWriterFlags & CYPHER_PAK_WRITER_WRITE_HASHES ) != 0u ) {
            file.flags |= CYPHER_PAK_ENTRY_HAS_HASH;
        }
        if ( file.compression != pak_compression_t::NONE ) {
            file.flags |= CYPHER_PAK_ENTRY_COMPRESSED;
        }

        result = ReadPhysicalFile( source.szPhysicalPath.c_str(), file.data, file.modifiedTimeUtc );
        if ( result != pak_error_t::OK ) {
            return result;
        }

        file.nStoredSize = static_cast<common::u64>( file.data.size() );
        file.nUnpackedSize = file.nStoredSize;
        file.szPathHash = HashPath32( file.szVirtualPath.c_str() );
        file.contentHash = HashBytes64( file.data.data(), file.nStoredSize );

        try {
            filesOut.push_back( std::move( file ) );
        } catch ( const std::bad_alloc & ) {
            return pak_error_t::ERR_OUT_OF_MEMORY;
        }
    }

    if ( ( nWriterFlags & CYPHER_PAK_WRITER_SORT_INDEX ) != 0u ) {
        std::sort( filesOut.begin(), filesOut.end(), []( const packed_file_t &a, const packed_file_t &b ) {
            return a.szVirtualPath < b.szVirtualPath;
        } );
    }

    std::vector<std::string> sortedPaths;
    try {
        sortedPaths.reserve( filesOut.size() );
        for ( const packed_file_t &file : filesOut ) {
            sortedPaths.push_back( file.szVirtualPath );
        }
    } catch ( const std::bad_alloc & ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    std::sort( sortedPaths.begin(), sortedPaths.end() );
    for ( common::usize i = 1u; i < sortedPaths.size(); ++i ) {
        if ( sortedPaths[i - 1u] == sortedPaths[i] ) {
            return pak_error_t::ERR_DUPLICATE_ENTRY;
        }
    }

    return pak_error_t::OK;
}

pak_error_t BuildStringTable(
    std::vector<packed_file_t> &files,
    std::vector<char> &stringTableOut )
{
    stringTableOut.clear();

    for ( packed_file_t &file : files ) {
        if ( stringTableOut.size() > std::numeric_limits<common::u64>::max() ) {
            return pak_error_t::ERR_INTEGER_OVERFLOW;
        }

        file.nPathOffset = static_cast<common::u64>( stringTableOut.size() );
        try {
            stringTableOut.insert( stringTableOut.end(), file.szVirtualPath.begin(), file.szVirtualPath.end() );
            stringTableOut.push_back( '\0' );
        } catch ( const std::bad_alloc & ) {
            return pak_error_t::ERR_OUT_OF_MEMORY;
        }
    }

    return pak_error_t::OK;
}

pak_error_t BuildArchive(
    const char *szArchivePath,
    const common::u32 nWriterFlags,
    const pak_compression_t defaultCompression,
    const common::u32 nDataAlignment,
    const std::vector<source_file_copy_t> &szSourceFiles )
{
    if ( szArchivePath == nullptr || szArchivePath[0] == '\0' ) {
        return pak_error_t::ERR_INVALID_PATH;
    }
    if ( !WriterFlagsSupported( nWriterFlags ) ) {
        return pak_error_t::ERR_UNSUPPORTED_FLAGS;
    }
    if ( !CypherPak_CompressionSupported( defaultCompression ) ) {
        return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
    }
    if ( szSourceFiles.size() > static_cast<common::usize>( std::numeric_limits<common::u32>::max() ) ) {
        return pak_error_t::ERR_INTEGER_OVERFLOW;
    }

    std::vector<packed_file_t> files;
    pak_error_t result = PrepareFiles( szSourceFiles, nWriterFlags, files );
    if ( result != pak_error_t::OK ) {
        return result;
    }

    std::vector<char> stringTable;
    result = BuildStringTable( files, stringTable );
    if ( result != pak_error_t::OK ) {
        return result;
    }

    const common::u64 nFileCount = static_cast<common::u64>( files.size() );
    if ( nFileCount > std::numeric_limits<common::u64>::max() / CYPHER_PAK_FILE_ENTRY_SIZE ) {
        return pak_error_t::ERR_INTEGER_OVERFLOW;
    }

    const common::u64 alignment = nDataAlignment == 0u ? 1u : nDataAlignment;
    pak_header_t header{};
    std::memcpy( header.magic, CYPHER_PAK_MAGIC, CYPHER_PAK_MAGIC_SIZE );
    header.version = CYPHER_PAK_FORMAT_VERSION;
    header.nHeaderSize = CYPHER_PAK_HEADER_SIZE;
    header.endianTag = CYPHER_PAK_ENDIAN_TAG;
    header.flags = CYPHER_PAK_FORMAT_NONE;
    if ( ( nWriterFlags & CYPHER_PAK_WRITER_SORT_INDEX ) != 0u ) {
        header.flags |= CYPHER_PAK_FORMAT_INDEX_SORTED;
    }
    if ( ( nWriterFlags & CYPHER_PAK_WRITER_WRITE_HASHES ) != 0u ) {
        header.flags |= CYPHER_PAK_FORMAT_HAS_FILE_HASHES;
    }

    header.nFileCount = nFileCount;
    header.nIndexOffset = CYPHER_PAK_HEADER_SIZE;
    header.nIndexSize = nFileCount * CYPHER_PAK_FILE_ENTRY_SIZE;

    if ( AddOverflow( header.nIndexOffset, header.nIndexSize, header.nStringTableOffset ) ) {
        return pak_error_t::ERR_INTEGER_OVERFLOW;
    }

    header.nStringTableSize = static_cast<common::u64>( stringTable.size() );
    common::u64 stringTableEnd = 0u;
    if ( AddOverflow( header.nStringTableOffset, header.nStringTableSize, stringTableEnd ) ) {
        return pak_error_t::ERR_INTEGER_OVERFLOW;
    }
    header.nDataOffset = CypherPak_AlignUp64( stringTableEnd, alignment );

    common::u64 cursor = header.nDataOffset;
    for ( packed_file_t &file : files ) {
        cursor = CypherPak_AlignUp64( cursor, alignment );
        file.nDataOffset = cursor;
        if ( AddOverflow( cursor, file.nStoredSize, cursor ) ) {
            return pak_error_t::ERR_INTEGER_OVERFLOW;
        }
    }

    header.nDataSize = cursor >= header.nDataOffset ? cursor - header.nDataOffset : 0u;
    header.nArchiveSize = cursor;

    std::error_code ec{};
    const std::filesystem::path szOutputPath( szArchivePath );
    const std::filesystem::path parent_path = szOutputPath.parent_path();
    if ( !parent_path.empty() ) {
        std::filesystem::create_directories( parent_path, ec );
        if ( ec ) {
            return pak_error_t::ERR_IO_ERROR;
        }
    }

    std::FILE *archive = std::fopen( szArchivePath, "wb" );
    if ( archive == nullptr ) {
        return pak_error_t::ERR_FILE_OPEN_FAILED;
    }

    auto fail = [&]( const pak_error_t error ) {
        std::fclose( archive );
        std::filesystem::remove( szOutputPath, ec );
        return error;
    };

    common::u8 nHeaderBytes[CYPHER_PAK_HEADER_SIZE]{};
    EncodeHeader( header, nHeaderBytes );
    if ( !WriteExact( archive, nHeaderBytes, sizeof( nHeaderBytes ) ) ) {
        return fail( pak_error_t::ERR_FILE_WRITE_FAILED );
    }

    common::u8 nEntryBytes[CYPHER_PAK_FILE_ENTRY_SIZE]{};
    for ( const packed_file_t &file : files ) {
        pak_disk_file_entry_t diskEntry{};
        diskEntry.nPathOffset = file.nPathOffset;
        diskEntry.nDataOffset = file.nDataOffset;
        diskEntry.nStoredSize = file.nStoredSize;
        diskEntry.nUnpackedSize = file.nUnpackedSize;
        diskEntry.modifiedTimeUtc = file.modifiedTimeUtc;
        diskEntry.contentHash = file.contentHash;
        diskEntry.nPathSize = static_cast<common::u32>( file.szVirtualPath.size() );
        diskEntry.szPathHash = file.szPathHash;
        diskEntry.compression = static_cast<common::u32>( file.compression );
        diskEntry.flags = file.flags;

        EncodeFileEntry( diskEntry, nEntryBytes );
        if ( !WriteExact( archive, nEntryBytes, sizeof( nEntryBytes ) ) ) {
            return fail( pak_error_t::ERR_FILE_WRITE_FAILED );
        }
    }

    if ( !WriteExact( archive, stringTable.data(), stringTable.size() ) ) {
        return fail( pak_error_t::ERR_FILE_WRITE_FAILED );
    }

    common::u64 nWrittenOffset = header.nStringTableOffset + header.nStringTableSize;
    if ( header.nDataOffset > nWrittenOffset ) {
        result = WriteZeros( archive, header.nDataOffset - nWrittenOffset );
        if ( result != pak_error_t::OK ) {
            return fail( result );
        }
        nWrittenOffset = header.nDataOffset;
    }

    for ( const packed_file_t &file : files ) {
        if ( file.nDataOffset > nWrittenOffset ) {
            result = WriteZeros( archive, file.nDataOffset - nWrittenOffset );
            if ( result != pak_error_t::OK ) {
                return fail( result );
            }
            nWrittenOffset = file.nDataOffset;
        }
        if ( !WriteExact( archive, file.data.data(), file.nStoredSize ) ) {
            return fail( pak_error_t::ERR_FILE_WRITE_FAILED );
        }
        nWrittenOffset += file.nStoredSize;
    }

    if ( nWrittenOffset < header.nArchiveSize ) {
        result = WriteZeros( archive, header.nArchiveSize - nWrittenOffset );
        if ( result != pak_error_t::OK ) {
            return fail( result );
        }
    }

    if ( std::fclose( archive ) != 0 ) {
        std::filesystem::remove( szOutputPath, ec );
        return pak_error_t::ERR_FILE_CLOSE_FAILED;
    }

    return pak_error_t::OK;
}

}       // namespace

pak_error_t CypherPak_CreateArchive(
    const pak_writer_config_t &config,
    const pak_source_file_t *files,
    const common::u32 nFileCount )
{
    if ( nFileCount > 0u && files == nullptr ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }

    std::vector<source_file_copy_t> szSourceFiles;
    try {
        szSourceFiles.reserve( nFileCount );
    } catch ( const std::bad_alloc & ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    for ( common::u32 i = 0u; i < nFileCount; ++i ) {
        source_file_copy_t copy{};
        const pak_error_t result = MakeSourceCopy( files[i], copy );
        if ( result != pak_error_t::OK ) {
            return result;
        }
        try {
            szSourceFiles.push_back( std::move( copy ) );
        } catch ( const std::bad_alloc & ) {
            return pak_error_t::ERR_OUT_OF_MEMORY;
        }
    }

    return BuildArchive(
        config.szArchivePath,
        config.flags,
        config.defaultCompression,
        config.nDataAlignment,
        szSourceFiles );
}

pak_error_t CypherPak_BeginWriter(
    const pak_writer_config_t &config,
    pak_writer_t &writer )
{
    writer = {};

    if ( config.szArchivePath == nullptr || config.szArchivePath[0] == '\0' ) {
        return pak_error_t::ERR_INVALID_PATH;
    }
    if ( !WriterFlagsSupported( config.flags ) ) {
        return pak_error_t::ERR_UNSUPPORTED_FLAGS;
    }
    if ( !CypherPak_CompressionSupported( config.defaultCompression ) ) {
        return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
    }
    if ( !CopyString( writer.szArchivePath, sizeof( writer.szArchivePath ), config.szArchivePath ) ) {
        return pak_error_t::ERR_BUFFER_TOO_SMALL;
    }

    writer_state_t *state = new ( std::nothrow ) writer_state_t();
    if ( state == nullptr ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    writer.handle = AllocateWriterHandle();
    writer.pBuilderState = state;
    writer.flags = config.flags;
    writer.defaultCompression = config.defaultCompression;
    writer.nDataAlignment = config.nDataAlignment;
    writer.open = true;
    writer.finalized = false;
    return pak_error_t::OK;
}

pak_error_t CypherPak_AddFile(
    pak_writer_t &writer,
    const pak_source_file_t &file )
{
    if ( !writer.open || writer.pBuilderState == nullptr || writer.handle == CYPHER_PAK_INVALID_HANDLE ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }
    if ( writer.finalized ) {
        return pak_error_t::ERR_INVALID_STATE;
    }

    source_file_copy_t copy{};
    pak_error_t result = MakeSourceCopy( file, copy );
    if ( result != pak_error_t::OK ) {
        return result;
    }

    writer_state_t *state = static_cast<writer_state_t *>( writer.pBuilderState );
    try {
        state->files.push_back( std::move( copy ) );
    } catch ( const std::bad_alloc & ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    writer.nFileCount = static_cast<common::u32>( state->files.size() );
    return pak_error_t::OK;
}

pak_error_t CypherPak_FinishWriter( pak_writer_t &writer )
{
    if ( !writer.open || writer.pBuilderState == nullptr || writer.handle == CYPHER_PAK_INVALID_HANDLE ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }
    if ( writer.finalized ) {
        return pak_error_t::ERR_INVALID_STATE;
    }

    writer_state_t *state = static_cast<writer_state_t *>( writer.pBuilderState );
    const pak_error_t result = BuildArchive(
        writer.szArchivePath,
        writer.flags,
        writer.defaultCompression,
        writer.nDataAlignment,
        state->files );

    delete state;
    writer.pBuilderState = nullptr;
    writer.pNativeFile = nullptr;
    writer.open = false;
    writer.finalized = result == pak_error_t::OK;
    writer.handle = CYPHER_PAK_INVALID_HANDLE;
    return result;
}

pak_error_t CypherPak_CancelWriter( pak_writer_t &writer )
{
    if ( writer.pBuilderState != nullptr ) {
        delete static_cast<writer_state_t *>( writer.pBuilderState );
    }
    writer = {};
    return pak_error_t::OK;
}

}       // namespace cypher::engine::pak
