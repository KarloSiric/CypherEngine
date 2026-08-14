//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherPak/CypherPak_Reader.cpp
//  Purpose: Implements the CypherPak Pak Reader module.
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

#include "CypherPak_Reader.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <vector>

#if defined( _WIN32 )
#include <cstdio>
#else
#include <sys/types.h>
#endif

namespace cypher::engine::pak
{

namespace {

constexpr common::u32 FNV1A32_OFFSET = 2166136261u;
constexpr common::u32 FNV1A32_PRIME = 16777619u;
constexpr common::u64 FNV1A64_OFFSET = 14695981039346656037ull;
constexpr common::u64 FNV1A64_PRIME = 1099511628211ull;
constexpr common::u64 READ_CHUNK_SIZE = 64u * 1024u;

std::atomic<common::u32> s_NextReaderHandle{ 1u };

pak_handle_t AllocateReaderHandle()
{
    common::u32 handle = s_NextReaderHandle.fetch_add( 1u );
    if ( handle == CYPHER_PAK_INVALID_HANDLE ) {
        handle = s_NextReaderHandle.fetch_add( 1u );
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

bool RangeInside( const common::u64 offset, const common::u64 size, const common::u64 nContainerSize )
{
    common::u64 end = 0u;
    if ( AddOverflow( offset, size, end ) ) {
        return false;
    }
    return end <= nContainerSize;
}

bool OpenFlagsSupported( const common::u32 flags )
{
    const common::u32 bSupportedFlags =
        CYPHER_PAK_OPEN_VERIFY_HEADER |
        CYPHER_PAK_OPEN_VERIFY_INDEX |
        CYPHER_PAK_OPEN_VERIFY_FILE_HASHES |
        CYPHER_PAK_OPEN_MEMORY_MAP;
    return ( flags & ~bSupportedFlags ) == 0u;
}

bool SeekFile( std::FILE *file, const common::u64 offset )
{
    if ( file == nullptr ) {
        return false;
    }

#if defined( _WIN32 )
    return _fseeki64( file, static_cast<__int64>( offset ), SEEK_SET ) == 0;
#else
    if ( offset > static_cast<common::u64>( std::numeric_limits<off_t>::max() ) ) {
        return false;
    }
    return ::fseeko( file, static_cast<off_t>( offset ), SEEK_SET ) == 0;
#endif
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
        const common::u64 chunk64 = remaining < READ_CHUNK_SIZE ? remaining : READ_CHUNK_SIZE;
        const common::usize chunk = static_cast<common::usize>( chunk64 );
        if ( std::fread( write, 1u, chunk, file ) != chunk ) {
            return false;
        }
        write += chunk;
        remaining -= chunk64;
    }

    return true;
}

pak_header_t DecodeHeader( const common::u8 *bytes )
{
    pak_header_t header{};
    common::usize offset = 0u;

    std::memcpy( header.magic, bytes + offset, CYPHER_PAK_MAGIC_SIZE );
    offset += CYPHER_PAK_MAGIC_SIZE;

    header.version = CypherPak_LoadU32LE( bytes + offset );
    offset += 4u;
    header.nHeaderSize = CypherPak_LoadU32LE( bytes + offset );
    offset += 4u;
    header.endianTag = CypherPak_LoadU32LE( bytes + offset );
    offset += 4u;
    header.flags = CypherPak_LoadU32LE( bytes + offset );
    offset += 4u;

    header.nArchiveSize = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.nFileCount = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.nIndexOffset = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.nIndexSize = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.nStringTableOffset = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.nStringTableSize = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.nDataOffset = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.nDataSize = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    header.archiveHash = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;

    for ( common::u32 i = 0u; i < 4u; ++i ) {
        header.reserved[i] = CypherPak_LoadU64LE( bytes + offset );
        offset += 8u;
    }

    return header;
}

pak_disk_file_entry_t DecodeFileEntry( const common::u8 *bytes )
{
    pak_disk_file_entry_t entry{};
    common::usize offset = 0u;

    entry.nPathOffset = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    entry.nDataOffset = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    entry.nStoredSize = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    entry.nUnpackedSize = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    entry.modifiedTimeUtc = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    entry.contentHash = CypherPak_LoadU64LE( bytes + offset );
    offset += 8u;
    entry.nPathSize = CypherPak_LoadU32LE( bytes + offset );
    offset += 4u;
    entry.szPathHash = CypherPak_LoadU32LE( bytes + offset );
    offset += 4u;
    entry.compression = CypherPak_LoadU32LE( bytes + offset );
    offset += 4u;
    entry.flags = CypherPak_LoadU32LE( bytes + offset );

    return entry;
}

const char *EntryPath( const pak_reader_t &reader, const pak_disk_file_entry_t &entry )
{
    if ( reader.stringTable == nullptr || entry.nPathOffset >= reader.nStringTableSize ) {
        return nullptr;
    }
    return reader.stringTable + entry.nPathOffset;
}

pak_error_t ValidateLoadedIndex( const pak_reader_t &reader )
{
    if ( reader.header.nFileCount != reader.nFileCount ) {
        return pak_error_t::ERR_INVALID_INDEX;
    }
    if ( reader.nFileCount > 0u && ( reader.entries == nullptr || reader.stringTable == nullptr ) ) {
        return pak_error_t::ERR_INVALID_INDEX;
    }

    std::vector<const char *> sortedPaths;
    try {
        sortedPaths.reserve( reader.nFileCount );
    } catch ( const std::bad_alloc & ) {
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    const char *szPreviousPath = nullptr;
    for ( common::u32 i = 0u; i < reader.nFileCount; ++i ) {
        const pak_disk_file_entry_t &entry = reader.entries[i];
        if ( entry.nPathSize == 0u || entry.nPathSize >= CYPHER_PAK_MAX_PATH_LENGTH ) {
            return pak_error_t::ERR_INVALID_INDEX;
        }
        if ( entry.nPathOffset >= reader.nStringTableSize ) {
            return pak_error_t::ERR_INVALID_INDEX;
        }

        common::u64 szPathEnd = 0u;
        if ( AddOverflow( entry.nPathOffset, entry.nPathSize, szPathEnd ) ||
             szPathEnd >= reader.nStringTableSize ) {
            return pak_error_t::ERR_INVALID_INDEX;
        }
        if ( reader.stringTable[szPathEnd] != '\0' ) {
            return pak_error_t::ERR_INVALID_INDEX;
        }

        const char *path = EntryPath( reader, entry );
        char szNormalizedPath[CYPHER_PAK_MAX_PATH_LENGTH]{};
        const pak_error_t normalizeResult = NormalizeVirtualPath( path, szNormalizedPath, sizeof( szNormalizedPath ) );
        if ( normalizeResult != pak_error_t::OK ) {
            return normalizeResult;
        }
        if ( std::strcmp( path, szNormalizedPath ) != 0 ) {
            return pak_error_t::ERR_INVALID_INDEX;
        }
        if ( entry.szPathHash != HashPath32( path ) ) {
            return pak_error_t::ERR_ARCHIVE_CORRUPT;
        }

        const common::u32 bEntryFlagsAllowed = CYPHER_PAK_ENTRY_COMPRESSED | CYPHER_PAK_ENTRY_HAS_HASH;
        if ( ( entry.flags & ~bEntryFlagsAllowed ) != 0u ) {
            return pak_error_t::ERR_UNSUPPORTED_FLAGS;
        }
        if ( entry.compression != static_cast<common::u32>( pak_compression_t::NONE ) ) {
            return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
        }
        if ( ( entry.flags & CYPHER_PAK_ENTRY_COMPRESSED ) == 0u && entry.nStoredSize != entry.nUnpackedSize ) {
            return pak_error_t::ERR_ARCHIVE_CORRUPT;
        }
        if ( !RangeInside( entry.nDataOffset, entry.nStoredSize, reader.header.nArchiveSize ) ) {
            return pak_error_t::ERR_ARCHIVE_CORRUPT;
        }
        if ( entry.nDataOffset < reader.header.nDataOffset ) {
            return pak_error_t::ERR_ARCHIVE_CORRUPT;
        }

        if ( ( reader.header.flags & CYPHER_PAK_FORMAT_INDEX_SORTED ) != 0u && szPreviousPath != nullptr ) {
            if ( std::strcmp( szPreviousPath, path ) >= 0 ) {
                return pak_error_t::ERR_INVALID_INDEX;
            }
        }
        szPreviousPath = path;
        sortedPaths.push_back( path );
    }

    if ( ( reader.header.flags & CYPHER_PAK_FORMAT_INDEX_SORTED ) == 0u ) {
        std::sort( sortedPaths.begin(), sortedPaths.end(), []( const char *a, const char *b ) {
            return std::strcmp( a, b ) < 0;
        } );
        for ( common::u32 i = 1u; i < static_cast<common::u32>( sortedPaths.size() ); ++i ) {
            if ( std::strcmp( sortedPaths[i - 1u], sortedPaths[i] ) == 0 ) {
                return pak_error_t::ERR_DUPLICATE_ENTRY;
            }
        }
    }

    return pak_error_t::OK;
}

pak_error_t HashFileRange(
    std::FILE *file,
    const common::u64 offset,
    const common::u64 size,
    common::u64 &hashOut )
{
    hashOut = FNV1A64_OFFSET;
    if ( !SeekFile( file, offset ) ) {
        return pak_error_t::ERR_FILE_SEEK_FAILED;
    }

    common::u8 buffer[static_cast<common::usize>( READ_CHUNK_SIZE )]{};
    common::u64 remaining = size;
    while ( remaining > 0u ) {
        const common::u64 chunk64 = remaining < READ_CHUNK_SIZE ? remaining : READ_CHUNK_SIZE;
        const common::usize chunk = static_cast<common::usize>( chunk64 );
        if ( std::fread( buffer, 1u, chunk, file ) != chunk ) {
            return pak_error_t::ERR_FILE_READ_FAILED;
        }
        hashOut = HashBytes64( buffer, chunk64, hashOut );
        remaining -= chunk64;
    }

    return pak_error_t::OK;
}

void FillStats( pak_reader_t &reader )
{
    reader.stats = {};
    reader.stats.nFileCount = reader.nFileCount;
    reader.stats.nArchiveSize = reader.header.nArchiveSize;

    for ( common::u32 i = 0u; i < reader.nFileCount; ++i ) {
        const pak_disk_file_entry_t &entry = reader.entries[i];
        reader.stats.nStoredDataSize += entry.nStoredSize;
        reader.stats.nUnpackedDataSize += entry.nUnpackedSize;
        if ( entry.compression != static_cast<common::u32>( pak_compression_t::NONE ) ) {
            reader.stats.nCompressedFileCount++;
        }
    }
}

}       // namespace

pak_error_t CypherPak_ValidateHeader( const pak_header_t &header )
{
    if ( !CypherPak_MagicEquals( header.magic ) ) {
        return pak_error_t::ERR_BAD_MAGIC;
    }
    if ( header.version != CYPHER_PAK_FORMAT_VERSION ) {
        return pak_error_t::ERR_UNSUPPORTED_VERSION;
    }
    if ( header.nHeaderSize != CYPHER_PAK_HEADER_SIZE ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( header.endianTag != CYPHER_PAK_ENDIAN_TAG ) {
        return pak_error_t::ERR_ENDIAN_MISMATCH;
    }

    const common::u32 bSupportedFlags =
        CYPHER_PAK_FORMAT_INDEX_SORTED |
        CYPHER_PAK_FORMAT_HAS_FILE_HASHES |
        CYPHER_PAK_FORMAT_HAS_ARCHIVE_HASH;
    if ( ( header.flags & ~bSupportedFlags ) != 0u ) {
        return pak_error_t::ERR_UNSUPPORTED_FLAGS;
    }
    if ( header.nFileCount > static_cast<common::u64>( std::numeric_limits<common::u32>::max() ) ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( header.nFileCount > std::numeric_limits<common::u64>::max() / CYPHER_PAK_FILE_ENTRY_SIZE ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }

    const common::u64 nExpectedIndexSize = header.nFileCount * CYPHER_PAK_FILE_ENTRY_SIZE;
    if ( header.nIndexSize != nExpectedIndexSize ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }

    common::u64 nIndexEnd = 0u;
    common::u64 stringEnd = 0u;
    common::u64 pDataEnd = 0u;
    if ( header.nIndexOffset < header.nHeaderSize ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( AddOverflow( header.nIndexOffset, header.nIndexSize, nIndexEnd ) ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( header.nStringTableOffset < nIndexEnd ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( AddOverflow( header.nStringTableOffset, header.nStringTableSize, stringEnd ) ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( header.nDataOffset < stringEnd ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( AddOverflow( header.nDataOffset, header.nDataSize, pDataEnd ) ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }
    if ( header.nArchiveSize < pDataEnd ) {
        return pak_error_t::ERR_INVALID_HEADER;
    }

    return pak_error_t::OK;
}

pak_error_t CypherPak_OpenReader(
    const char *szArchivePath,
    const common::u32 flags,
    pak_reader_t &reader )
{
    reader = {};

    if ( szArchivePath == nullptr || szArchivePath[0] == '\0' ) {
        return pak_error_t::ERR_INVALID_PATH;
    }
    if ( !OpenFlagsSupported( flags ) ) {
        return pak_error_t::ERR_UNSUPPORTED_FLAGS;
    }
    if ( ( flags & CYPHER_PAK_OPEN_MEMORY_MAP ) != 0u ) {
        return pak_error_t::ERR_NOT_IMPLEMENTED;
    }
    if ( !CopyString( reader.szArchivePath, sizeof( reader.szArchivePath ), szArchivePath ) ) {
        return pak_error_t::ERR_BUFFER_TOO_SMALL;
    }

    std::error_code ec{};
    const common::u64 nArchiveSize = static_cast<common::u64>( std::filesystem::file_size( szArchivePath, ec ) );
    if ( ec ) {
        return pak_error_t::ERR_FILE_OPEN_FAILED;
    }

    std::FILE *file = std::fopen( szArchivePath, "rb" );
    if ( file == nullptr ) {
        return pak_error_t::ERR_FILE_OPEN_FAILED;
    }

    common::u8 nHeaderBytes[CYPHER_PAK_HEADER_SIZE]{};
    if ( !ReadExact( file, nHeaderBytes, sizeof( nHeaderBytes ) ) ) {
        std::fclose( file );
        reader = {};
        return pak_error_t::ERR_FILE_READ_FAILED;
    }

    reader.header = DecodeHeader( nHeaderBytes );
    pak_error_t result = CypherPak_ValidateHeader( reader.header );
    if ( result != pak_error_t::OK ) {
        std::fclose( file );
        reader = {};
        return result;
    }
    if ( reader.header.nArchiveSize != nArchiveSize ) {
        std::fclose( file );
        reader = {};
        return pak_error_t::ERR_ARCHIVE_CORRUPT;
    }

    reader.nFileCount = static_cast<common::u32>( reader.header.nFileCount );
    if ( reader.nFileCount > 0u ) {
        reader.entries = new ( std::nothrow ) pak_disk_file_entry_t[reader.nFileCount];
        if ( reader.entries == nullptr ) {
            std::fclose( file );
            reader = {};
            return pak_error_t::ERR_OUT_OF_MEMORY;
        }
    }

    if ( reader.header.nStringTableSize > static_cast<common::u64>( std::numeric_limits<common::usize>::max() - 1u ) ) {
        delete[] reader.entries;
        std::fclose( file );
        reader = {};
        return pak_error_t::ERR_OUT_OF_MEMORY;
    }

    reader.nStringTableSize = reader.header.nStringTableSize;
    if ( reader.nStringTableSize > 0u ) {
        reader.stringTable = new ( std::nothrow ) char[static_cast<common::usize>( reader.nStringTableSize ) + 1u];
        if ( reader.stringTable == nullptr ) {
            delete[] reader.entries;
            std::fclose( file );
            reader = {};
            return pak_error_t::ERR_OUT_OF_MEMORY;
        }
    }

    if ( reader.nFileCount > 0u ) {
        if ( !SeekFile( file, reader.header.nIndexOffset ) ) {
            CypherPak_CloseReader( reader );
            std::fclose( file );
            return pak_error_t::ERR_FILE_SEEK_FAILED;
        }

        common::u8 nEntryBytes[CYPHER_PAK_FILE_ENTRY_SIZE]{};
        for ( common::u32 i = 0u; i < reader.nFileCount; ++i ) {
            if ( !ReadExact( file, nEntryBytes, sizeof( nEntryBytes ) ) ) {
                CypherPak_CloseReader( reader );
                std::fclose( file );
                return pak_error_t::ERR_FILE_READ_FAILED;
            }
            reader.entries[i] = DecodeFileEntry( nEntryBytes );
        }
    }

    if ( reader.nStringTableSize > 0u ) {
        if ( !SeekFile( file, reader.header.nStringTableOffset ) ) {
            CypherPak_CloseReader( reader );
            std::fclose( file );
            return pak_error_t::ERR_FILE_SEEK_FAILED;
        }
        if ( !ReadExact( file, reader.stringTable, reader.nStringTableSize ) ) {
            CypherPak_CloseReader( reader );
            std::fclose( file );
            return pak_error_t::ERR_FILE_READ_FAILED;
        }
        reader.stringTable[reader.nStringTableSize] = '\0';
    }

    reader.pNativeFile = file;
    reader.flags = flags;
    reader.handle = AllocateReaderHandle();
    reader.open = true;
    FillStats( reader );

    result = ValidateLoadedIndex( reader );
    if ( result != pak_error_t::OK ) {
        CypherPak_CloseReader( reader );
        return result;
    }

    if ( ( flags & CYPHER_PAK_OPEN_VERIFY_FILE_HASHES ) != 0u ) {
        result = CypherPak_Verify( reader, CYPHER_PAK_VERIFY_FILE_HASHES );
        if ( result != pak_error_t::OK ) {
            CypherPak_CloseReader( reader );
            return result;
        }
    }

    return pak_error_t::OK;
}

pak_error_t CypherPak_CloseReader( pak_reader_t &reader )
{
    pak_error_t result = pak_error_t::OK;

    if ( reader.pNativeFile != nullptr ) {
        if ( std::fclose( static_cast<std::FILE *>( reader.pNativeFile ) ) != 0 ) {
            result = pak_error_t::ERR_FILE_CLOSE_FAILED;
        }
    }

    delete[] reader.entries;
    delete[] reader.stringTable;
    reader = {};
    return result;
}

bool CypherPak_IsOpen( const pak_reader_t &reader )
{
    return reader.open && reader.pNativeFile != nullptr && reader.handle != CYPHER_PAK_INVALID_HANDLE;
}

pak_error_t CypherPak_GetStats(
    const pak_reader_t &reader,
    pak_stats_t &statsOut )
{
    statsOut = {};

    if ( !CypherPak_IsOpen( reader ) ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }

    statsOut = reader.stats;
    return pak_error_t::OK;
}

pak_error_t CypherPak_GetFileCount(
    const pak_reader_t &reader,
    common::u32 &nOutFileCount )
{
    nOutFileCount = 0u;

    if ( !CypherPak_IsOpen( reader ) ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }

    nOutFileCount = reader.nFileCount;
    return pak_error_t::OK;
}

pak_error_t CypherPak_FindFile(
    const pak_reader_t &reader,
    const char *szVirtualPath,
    pak_file_index_t &nOutIndex )
{
    nOutIndex = CYPHER_PAK_INVALID_FILE_INDEX;

    if ( !CypherPak_IsOpen( reader ) ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }

    char szNormalizedPath[CYPHER_PAK_MAX_PATH_LENGTH]{};
    pak_error_t result = NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) );
    if ( result != pak_error_t::OK ) {
        return result;
    }

    if ( ( reader.header.flags & CYPHER_PAK_FORMAT_INDEX_SORTED ) != 0u ) {
        common::u32 left = 0u;
        common::u32 right = reader.nFileCount;
        while ( left < right ) {
            const common::u32 middle = left + ( right - left ) / 2u;
            const char *szEntryPath = EntryPath( reader, reader.entries[middle] );
            const int compare = std::strcmp( szEntryPath, szNormalizedPath );
            if ( compare == 0 ) {
                nOutIndex = middle;
                return pak_error_t::OK;
            }
            if ( compare < 0 ) {
                left = middle + 1u;
            } else {
                right = middle;
            }
        }
        return pak_error_t::ERR_ENTRY_NOT_FOUND;
    }

    const common::u32 szPathHash = HashPath32( szNormalizedPath );
    for ( common::u32 i = 0u; i < reader.nFileCount; ++i ) {
        if ( reader.entries[i].szPathHash != szPathHash ) {
            continue;
        }
        const char *szEntryPath = EntryPath( reader, reader.entries[i] );
        if ( szEntryPath != nullptr && std::strcmp( szEntryPath, szNormalizedPath ) == 0 ) {
            nOutIndex = i;
            return pak_error_t::OK;
        }
    }

    return pak_error_t::ERR_ENTRY_NOT_FOUND;
}

pak_error_t CypherPak_GetFileInfo(
    const pak_reader_t &reader,
    const pak_file_index_t index,
    pak_file_info_t &infoOut )
{
    infoOut = {};

    if ( !CypherPak_IsOpen( reader ) ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }
    if ( index >= reader.nFileCount ) {
        return pak_error_t::ERR_ENTRY_NOT_FOUND;
    }

    const pak_disk_file_entry_t &entry = reader.entries[index];
    const char *path = EntryPath( reader, entry );
    if ( path == nullptr || !CopyString( infoOut.szVirtualPath, sizeof( infoOut.szVirtualPath ), path ) ) {
        return pak_error_t::ERR_INVALID_INDEX;
    }

    infoOut.index = index;
    infoOut.nDataOffset = entry.nDataOffset;
    infoOut.nStoredSize = entry.nStoredSize;
    infoOut.nUnpackedSize = entry.nUnpackedSize;
    infoOut.modifiedTimeUtc = entry.modifiedTimeUtc;
    infoOut.contentHash = entry.contentHash;
    infoOut.szPathHash = entry.szPathHash;
    infoOut.compression = static_cast<pak_compression_t>( entry.compression );
    infoOut.flags = entry.flags;

    return pak_error_t::OK;
}

pak_error_t CypherPak_GetFileInfoByPath(
    const pak_reader_t &reader,
    const char *szVirtualPath,
    pak_file_info_t &infoOut )
{
    infoOut = {};

    pak_file_index_t index = CYPHER_PAK_INVALID_FILE_INDEX;
    const pak_error_t result = CypherPak_FindFile( reader, szVirtualPath, index );
    if ( result != pak_error_t::OK ) {
        return result;
    }

    return CypherPak_GetFileInfo( reader, index, infoOut );
}

pak_error_t CypherPak_ReadRawFileByIndex(
    pak_reader_t &reader,
    const pak_file_index_t index,
    void *buffer,
    const common::u64 nBufferSize,
    common::u64 &nOutBytesRead )
{
    nOutBytesRead = 0u;

    if ( !CypherPak_IsOpen( reader ) ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }
    if ( index >= reader.nFileCount ) {
        return pak_error_t::ERR_ENTRY_NOT_FOUND;
    }

    const pak_disk_file_entry_t &entry = reader.entries[index];
    if ( entry.nStoredSize > 0u && buffer == nullptr ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( nBufferSize < entry.nStoredSize ) {
        return pak_error_t::ERR_BUFFER_TOO_SMALL;
    }

    std::FILE *file = static_cast<std::FILE *>( reader.pNativeFile );
    if ( !SeekFile( file, entry.nDataOffset ) ) {
        return pak_error_t::ERR_FILE_SEEK_FAILED;
    }
    if ( !ReadExact( file, buffer, entry.nStoredSize ) ) {
        return pak_error_t::ERR_FILE_READ_FAILED;
    }

    nOutBytesRead = entry.nStoredSize;
    reader.stats.nReadCount++;
    reader.stats.nBytesRead += nOutBytesRead;
    return pak_error_t::OK;
}

pak_error_t CypherPak_ReadFileByIndex(
    pak_reader_t &reader,
    const pak_file_index_t index,
    void *buffer,
    const common::u64 nBufferSize,
    common::u64 &nOutBytesRead )
{
    nOutBytesRead = 0u;

    if ( !CypherPak_IsOpen( reader ) ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }
    if ( index >= reader.nFileCount ) {
        return pak_error_t::ERR_ENTRY_NOT_FOUND;
    }

    const pak_disk_file_entry_t &entry = reader.entries[index];
    if ( entry.compression != static_cast<common::u32>( pak_compression_t::NONE ) ) {
        return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
    }
    if ( nBufferSize < entry.nUnpackedSize ) {
        return pak_error_t::ERR_BUFFER_TOO_SMALL;
    }

    return CypherPak_ReadRawFileByIndex( reader, index, buffer, nBufferSize, nOutBytesRead );
}

pak_error_t CypherPak_ReadFile(
    pak_reader_t &reader,
    const char *szVirtualPath,
    void *buffer,
    const common::u64 nBufferSize,
    common::u64 &nOutBytesRead )
{
    nOutBytesRead = 0u;

    pak_file_index_t index = CYPHER_PAK_INVALID_FILE_INDEX;
    const pak_error_t result = CypherPak_FindFile( reader, szVirtualPath, index );
    if ( result != pak_error_t::OK ) {
        return result;
    }

    return CypherPak_ReadFileByIndex( reader, index, buffer, nBufferSize, nOutBytesRead );
}

pak_error_t CypherPak_Verify(
    pak_reader_t &reader,
    const common::u32 flags )
{
    if ( !CypherPak_IsOpen( reader ) ) {
        return pak_error_t::ERR_INVALID_HANDLE;
    }
    if ( flags == CYPHER_PAK_VERIFY_NONE ) {
        return pak_error_t::OK;
    }

    if ( ( flags & CYPHER_PAK_VERIFY_HEADER ) != 0u ) {
        const pak_error_t headerResult = CypherPak_ValidateHeader( reader.header );
        if ( headerResult != pak_error_t::OK ) {
            return headerResult;
        }
    }
    if ( ( flags & CYPHER_PAK_VERIFY_INDEX ) != 0u ) {
        const pak_error_t nIndexResult = ValidateLoadedIndex( reader );
        if ( nIndexResult != pak_error_t::OK ) {
            return nIndexResult;
        }
    }
    if ( ( flags & CYPHER_PAK_VERIFY_FILE_HASHES ) != 0u ) {
        for ( common::u32 i = 0u; i < reader.nFileCount; ++i ) {
            const pak_disk_file_entry_t &entry = reader.entries[i];
            if ( ( entry.flags & CYPHER_PAK_ENTRY_HAS_HASH ) == 0u ) {
                continue;
            }

            common::u64 hash = 0u;
            const pak_error_t hashResult = HashFileRange(
                static_cast<std::FILE *>( reader.pNativeFile ),
                entry.nDataOffset,
                entry.nStoredSize,
                hash );
            if ( hashResult != pak_error_t::OK ) {
                return hashResult;
            }
            if ( hash != entry.contentHash ) {
                return pak_error_t::ERR_CHECKSUM_MISMATCH;
            }
        }
    }
    if ( ( flags & CYPHER_PAK_VERIFY_ARCHIVE_HASH ) != 0u &&
         ( reader.header.flags & CYPHER_PAK_FORMAT_HAS_ARCHIVE_HASH ) != 0u ) {
        return pak_error_t::ERR_NOT_IMPLEMENTED;
    }

    return pak_error_t::OK;
}

}       // namespace cypher::engine::pak
