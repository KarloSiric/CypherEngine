//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherFileSystem/CypherFileSystem_Discovery.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Discovery module.
//  Details: This file participates in the virtual filesystem layer that maps engine
//           paths to mounted physical or package-backed data. Keep path validation
//           strict because every asset pipeline will depend on it.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherFileSystem_Runtime.h"
#include "CypherPak.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace cypher::engine::fs
{

namespace {

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

bool IsHiddenEntryName( const char *name )
{
    return name != nullptr && name[0] == '.';
}

bool EntryAlreadyAdded( const directory_entry_t *entries, const common::u32 nEntryCount, const char *szVirtualPath )
{
    if ( entries == nullptr || szVirtualPath == nullptr ) {
        return false;
    }

    for ( common::u32 i = 0u; i < nEntryCount; ++i ) {
        if ( std::strcmp( entries[i].szVirtualPath, szVirtualPath ) == 0 ) {
            return true;
        }
    }

    return false;
}

fs_error_t BuildVirtualChildPath(
    const char *virtualDir,
    const char *szChildName,
    char *szOutPath,
    const common::u32 nOutPathSize )
{
    if ( virtualDir == nullptr || virtualDir[0] == '\0' ) {
        return CopyString( szOutPath, nOutPathSize, szChildName ) ? fs_error_t::OK : fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    return CypherFileSystem_PathJoin( virtualDir, szChildName, szOutPath, nOutPathSize );
}

fs_error_t FillEntryFromPhysicalPath(
    const char *szVirtualPath,
    const char *szPhysicalPath,
    directory_entry_t &entryOut )
{
    entryOut = {};

    if ( !CopyString( entryOut.szVirtualPath, sizeof( entryOut.szVirtualPath ), szVirtualPath ) ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    const char *basename = CypherFileSystem_PathBasename( szVirtualPath );
    if ( basename == nullptr || !CopyString( entryOut.name, sizeof( entryOut.name ), basename ) ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    std::error_code ec{};
    const bool bIsDirectory = std::filesystem::is_directory( szPhysicalPath, ec );
    if ( ec ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    entryOut.type = bIsDirectory ? directory_entry_type_t::DIRECTORY : directory_entry_type_t::FILE;
    if ( !bIsDirectory ) {
        entryOut.size = static_cast<common::u64>( std::filesystem::file_size( szPhysicalPath, ec ) );
        if ( ec ) {
            entryOut.size = 0u;
            return fs_error_t::ERR_IO_ERROR;
        }
    }

    return fs_error_t::OK;
}

fs_error_t PakErrorToFs( const pak::pak_error_t error )
{
    switch ( error ) {
    case pak::pak_error_t::OK:
        return fs_error_t::OK;
    case pak::pak_error_t::ERR_INVALID_ARGUMENT:
        return fs_error_t::ERR_INVALID_ARGUMENT;
    case pak::pak_error_t::ERR_INVALID_PATH:
        return fs_error_t::ERR_INVALID_PATH;
    case pak::pak_error_t::ERR_INVALID_HANDLE:
        return fs_error_t::ERR_INVALID_HANDLE;
    case pak::pak_error_t::ERR_ENTRY_NOT_FOUND:
    case pak::pak_error_t::ERR_PATH_NOT_FOUND:
        return fs_error_t::ERR_PATH_NOT_FOUND;
    case pak::pak_error_t::ERR_BUFFER_TOO_SMALL:
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    case pak::pak_error_t::ERR_OUT_OF_MEMORY:
        return fs_error_t::ERR_OUT_OF_MEMORY;
    default:
        return fs_error_t::ERR_IO_ERROR;
    }
}

fs_error_t AddDirectoryEntry(
    const directory_entry_t &entry,
    directory_entry_t *entries,
    const common::u32 nMaxEntries,
    common::u32 &nOutEntryCount,
    bool &overflowOut )
{
    if ( EntryAlreadyAdded( entries, nOutEntryCount < nMaxEntries ? nOutEntryCount : nMaxEntries, entry.szVirtualPath ) ) {
        return fs_error_t::OK;
    }

    if ( nOutEntryCount < nMaxEntries && entries != nullptr ) {
        entries[nOutEntryCount] = entry;
    } else {
        overflowOut = true;
    }

    ++nOutEntryCount;
    return fs_error_t::OK;
}

fs_error_t AddMountedChildRoot(
    const char *szRequestedRoot,
    const char *szMountedRoot,
    directory_entry_t *entries,
    const common::u32 nMaxEntries,
    common::u32 &nOutEntryCount,
    bool &overflowOut )
{
    const char *szRelativePath = nullptr;
    if ( szRequestedRoot[0] != '\0' && !CypherFileSystem_VirtualPathStartsWithRoot( szMountedRoot, szRequestedRoot, &szRelativePath ) ) {
        return fs_error_t::OK;
    }

    if ( szRequestedRoot[0] == '\0' ) {
        szRelativePath = szMountedRoot;
    }
    if ( szRelativePath == nullptr || szRelativePath[0] == '\0' ) {
        return fs_error_t::OK;
    }

    char szChildName[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    common::u32 nWriteIndex = 0u;
    const char *cursor = szRelativePath;
    while ( *cursor != '\0' && *cursor != '/' ) {
        if ( nWriteIndex + 1u >= sizeof( szChildName ) ) {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        szChildName[nWriteIndex++] = *cursor++;
    }
    szChildName[nWriteIndex] = '\0';

    char szChildVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t result = BuildVirtualChildPath( szRequestedRoot, szChildName, szChildVirtualPath, sizeof( szChildVirtualPath ) );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    directory_entry_t entry{};
    if ( !CopyString( entry.name, sizeof( entry.name ), szChildName ) ||
         !CopyString( entry.szVirtualPath, sizeof( entry.szVirtualPath ), szChildVirtualPath ) ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    entry.type = directory_entry_type_t::DIRECTORY;
    return AddDirectoryEntry( entry, entries, nMaxEntries, nOutEntryCount, overflowOut );
}

fs_error_t AddPackagePathChild(
    const char *szRequestedRoot,
    const char *relativeDir,
    const pak::pak_file_info_t &fileInfo,
    directory_entry_t *entries,
    const common::u32 nMaxEntries,
    common::u32 &nOutEntryCount,
    bool &overflowOut,
    bool &szOutFoundSource )
{
    const char *tail = fileInfo.szVirtualPath;

    if ( relativeDir != nullptr && relativeDir[0] != '\0' ) {
        const common::usize nRelativeLen = std::strlen( relativeDir );
        if ( std::strncmp( fileInfo.szVirtualPath, relativeDir, nRelativeLen ) != 0 ||
             fileInfo.szVirtualPath[nRelativeLen] != '/' ) {
            return fs_error_t::OK;
        }
        tail = fileInfo.szVirtualPath + nRelativeLen + 1u;
    }

    if ( tail == nullptr || tail[0] == '\0' ) {
        return fs_error_t::OK;
    }

    szOutFoundSource = true;

    char szChildName[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    common::u32 nWriteIndex = 0u;
    const char *cursor = tail;
    while ( *cursor != '\0' && *cursor != '/' ) {
        if ( nWriteIndex + 1u >= sizeof( szChildName ) ) {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        szChildName[nWriteIndex++] = *cursor++;
    }
    szChildName[nWriteIndex] = '\0';

    const bool bChildIsDirectory = *cursor == '/';

    char szChildVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t result = BuildVirtualChildPath( szRequestedRoot, szChildName, szChildVirtualPath, sizeof( szChildVirtualPath ) );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    directory_entry_t entry{};
    if ( !CopyString( entry.name, sizeof( entry.name ), szChildName ) ||
         !CopyString( entry.szVirtualPath, sizeof( entry.szVirtualPath ), szChildVirtualPath ) ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }
    entry.type = bChildIsDirectory ? directory_entry_type_t::DIRECTORY : directory_entry_type_t::FILE;
    entry.size = bChildIsDirectory ? 0u : fileInfo.nUnpackedSize;
    entry.nModifiedTime = static_cast<std::time_t>( fileInfo.modifiedTimeUtc );

    return AddDirectoryEntry( entry, entries, nMaxEntries, nOutEntryCount, overflowOut );
}

bool MatchWildcard( const char *text, const char *pattern )
{
    if ( text == nullptr || pattern == nullptr ) {
        return false;
    }

    const char *star = nullptr;
    const char *szTextAfterStar = nullptr;

    while ( *text != '\0' ) {
        if ( *pattern == '?' || *pattern == *text ) {
            ++text;
            ++pattern;
            continue;
        }

        if ( *pattern == '*' ) {
            star = pattern++;
            szTextAfterStar = text;
            continue;
        }

        if ( star != nullptr ) {
            pattern = star + 1;
            text = ++szTextAfterStar;
            continue;
        }

        return false;
    }

    while ( *pattern == '*' ) {
        ++pattern;
    }

    return *pattern == '\0';
}

bool IsInvalidFindPatternChar( const char c )
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
        return true;
    default:
        return false;
    }
}

fs_error_t NormalizeFindPattern( const char *pattern, char *patternOut, const common::u32 nOutPatternSize )
{
    if ( pattern == nullptr || pattern[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( patternOut == nullptr || nOutPatternSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    patternOut[0] = '\0';

    common::u32 nWriteIndex = 0u;
    const char *cursor = pattern;
    while ( *cursor != '\0' ) {
        char c = *cursor;
        if ( c == '/' || c == '\\' ) {
            return fs_error_t::ERR_INVALID_ARGUMENT;
        }
        if ( c != '*' && c != '?' && IsInvalidFindPatternChar( c ) ) {
            return fs_error_t::ERR_INVALID_ARGUMENT;
        }
        if ( c >= 'A' && c <= 'Z' ) {
            c = static_cast<char>( c - 'A' + 'a' );
        }
        if ( nWriteIndex + 1u >= nOutPatternSize ) {
            patternOut[0] = '\0';
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        patternOut[nWriteIndex++] = c;
        ++cursor;
    }

    patternOut[nWriteIndex] = '\0';
    return fs_error_t::OK;
}

fs_error_t FindFilesRecursive(
    const char *szVirtualRoot,
    const char *pattern,
    const common::u32 flags,
    directory_entry_t *entries,
    const common::u32 nMaxEntries,
    common::u32 &nOutEntryCount,
    bool &overflowOut )
{
    common::u32 nChildCount = 0u;
    fs_error_t result = CypherFileSystem_ListDirectory( szVirtualRoot, nullptr, 0u, nChildCount );
    if ( result == fs_error_t::ERR_PATH_NOT_FOUND ) {
        return fs_error_t::OK;
    }
    if ( result != fs_error_t::OK && result != fs_error_t::ERR_BUFFER_TOO_SMALL ) {
        return result;
    }
    if ( nChildCount == 0u ) {
        return fs_error_t::OK;
    }

    std::vector<directory_entry_t> children( nChildCount );
    while ( true ) {
        common::u32 nListedChildCount = 0u;
        result = CypherFileSystem_ListDirectory(
            szVirtualRoot,
            children.data(),
            static_cast<common::u32>( children.size() ),
            nListedChildCount );

        if ( result == fs_error_t::ERR_BUFFER_TOO_SMALL ) {
            children.resize( nListedChildCount );
            continue;
        }
        if ( result != fs_error_t::OK ) {
            return result;
        }

        children.resize( nListedChildCount );
        break;
    }

    const common::u32 nMatchFlags = ( flags & ( CYPHER_FILESYSTEM_FIND_FILES | CYPHER_FILESYSTEM_FIND_DIRECTORIES ) ) != 0u
        ? flags
        : flags | CYPHER_FILESYSTEM_FIND_FILES | CYPHER_FILESYSTEM_FIND_DIRECTORIES;

    for ( common::u32 i = 0u; i < static_cast<common::u32>( children.size() ); ++i ) {
        const directory_entry_t &child = children[i];
        const bool bIsHidden = IsHiddenEntryName( child.name );
        if ( bIsHidden && ( flags & CYPHER_FILESYSTEM_FIND_INCLUDE_HIDDEN ) == 0u ) {
            continue;
        }

        const bool typeMatches =
            ( child.type == directory_entry_type_t::FILE && ( nMatchFlags & CYPHER_FILESYSTEM_FIND_FILES ) != 0u ) ||
            ( child.type == directory_entry_type_t::DIRECTORY && ( nMatchFlags & CYPHER_FILESYSTEM_FIND_DIRECTORIES ) != 0u );

        if ( typeMatches && MatchWildcard( child.name, pattern ) ) {
            result = AddDirectoryEntry( child, entries, nMaxEntries, nOutEntryCount, overflowOut );
            if ( result != fs_error_t::OK ) {
                return result;
            }
        }

        if ( child.type == directory_entry_type_t::DIRECTORY && ( flags & CYPHER_FILESYSTEM_FIND_RECURSIVE ) != 0u ) {
            result = FindFilesRecursive( child.szVirtualPath, pattern, flags, entries, nMaxEntries, nOutEntryCount, overflowOut );
            if ( result != fs_error_t::OK ) {
                return result;
            }
        }
    }

    return result == fs_error_t::ERR_BUFFER_TOO_SMALL ? fs_error_t::OK : result;
}

}       // namespace

fs_error_t CypherFileSystem_ListDirectory(
    const char *szVirtualPath,
    directory_entry_t *entries,
    common::u32 nMaxEntries,
    common::u32 &nOutEntryCount )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    nOutEntryCount = 0u;

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( entries == nullptr && nMaxEntries != 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    char szNormalizedRoot[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t rootResult = CypherFileSystem_NormalizeVirtualRoot( szVirtualPath, szNormalizedRoot, sizeof( szNormalizedRoot ) );
    if ( rootResult != fs_error_t::OK ) {
        return rootResult;
    }

    bool bFoundSource = false;
    bool overflow = false;

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];

        const char *szRelativePath = nullptr;
        if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_DIRECTORY &&
             CypherFileSystem_VirtualPathStartsWithRoot( szNormalizedRoot, mount.szVirtualRoot, &szRelativePath ) ) {
            char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
            fs_error_t result = CypherFileSystem_BuildPhysicalPath( mount.szPhysicalRoot, szRelativePath, szPhysicalPath, sizeof( szPhysicalPath ) );
            if ( result != fs_error_t::OK ) {
                return result;
            }

            std::error_code ec{};
            if ( !std::filesystem::exists( szPhysicalPath, ec ) ) {
                if ( ec ) {
                    return fs_error_t::ERR_IO_ERROR;
                }
                continue;
            }
            if ( !std::filesystem::is_directory( szPhysicalPath, ec ) || ec ) {
                return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_NOT_DIRECTORY;
            }

            bFoundSource = true;
            for ( const std::filesystem::directory_entry &physicalEntry : std::filesystem::directory_iterator( szPhysicalPath, ec ) ) {
                if ( ec ) {
                    return fs_error_t::ERR_IO_ERROR;
                }

                const std::string szRawName = physicalEntry.path().filename().string();
                char szNormalizedName[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
                if ( CypherFileSystem_NormalizeVirtualPath( szRawName.c_str(), szNormalizedName, sizeof( szNormalizedName ) ) != fs_error_t::OK ) {
                    continue;
                }

                char szChildVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
                result = BuildVirtualChildPath( szNormalizedRoot, szNormalizedName, szChildVirtualPath, sizeof( szChildVirtualPath ) );
                if ( result != fs_error_t::OK ) {
                    return result;
                }

                directory_entry_t entry{};
                result = FillEntryFromPhysicalPath( szChildVirtualPath, physicalEntry.path().string().c_str(), entry );
                if ( result != fs_error_t::OK ) {
                    return result;
                }

                result = AddDirectoryEntry( entry, entries, nMaxEntries, nOutEntryCount, overflow );
                if ( result != fs_error_t::OK ) {
                    return result;
                }
            }
        } else if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE &&
                    CypherFileSystem_VirtualPathStartsWithRoot( szNormalizedRoot, mount.szVirtualRoot, &szRelativePath ) ) {
            pak::pak_reader_t *reader = static_cast<pak::pak_reader_t *>( mount.pPackageReader );
            if ( reader == nullptr ) {
                continue;
            }

            common::u32 nPackageFileCount = 0u;
            pak::pak_error_t nCountResult = pak::CypherPak_GetFileCount( *reader, nPackageFileCount );
            if ( nCountResult != pak::pak_error_t::OK ) {
                return PakErrorToFs( nCountResult );
            }

            for ( common::u32 nPackageIndex = 0u; nPackageIndex < nPackageFileCount; ++nPackageIndex ) {
                pak::pak_file_info_t fileInfo{};
                const pak::pak_error_t infoResult = pak::CypherPak_GetFileInfo( *reader, nPackageIndex, fileInfo );
                if ( infoResult != pak::pak_error_t::OK ) {
                    return PakErrorToFs( infoResult );
                }

                fs_error_t result = AddPackagePathChild(
                    szNormalizedRoot,
                    szRelativePath,
                    fileInfo,
                    entries,
                    nMaxEntries,
                    nOutEntryCount,
                    overflow,
                    bFoundSource );
                if ( result != fs_error_t::OK ) {
                    return result;
                }
            }
        } else if ( CypherFileSystem_VirtualPathStartsWithRoot( mount.szVirtualRoot, szNormalizedRoot, nullptr ) ) {
            bFoundSource = true;
            const fs_error_t result = AddMountedChildRoot( szNormalizedRoot, mount.szVirtualRoot, entries, nMaxEntries, nOutEntryCount, overflow );
            if ( result != fs_error_t::OK ) {
                return result;
            }
        }
    }

    if ( !bFoundSource ) {
        state.stats.nFailedLookupCount++;
        return fs_error_t::ERR_PATH_NOT_FOUND;
    }

    return overflow ? fs_error_t::ERR_BUFFER_TOO_SMALL : fs_error_t::OK;
}

fs_error_t CypherFileSystem_FindFiles(
    const char *szVirtualRoot,
    const char *pattern,
    common::u32 flags,
    directory_entry_t *entries,
    common::u32 nMaxEntries,
    common::u32 &nOutEntryCount )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    nOutEntryCount = 0u;

    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( entries == nullptr && nMaxEntries != 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    char normalizedPattern[CYPHER_FILESYSTEM_MAX_PATTERN_LENGTH]{};
    fs_error_t result = NormalizeFindPattern( pattern, normalizedPattern, sizeof( normalizedPattern ) );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    bool overflow = false;
    result = FindFilesRecursive( szVirtualRoot, normalizedPattern, flags, entries, nMaxEntries, nOutEntryCount, overflow );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    const common::u32 nSortCount = nOutEntryCount < nMaxEntries ? nOutEntryCount : nMaxEntries;
    if ( entries != nullptr && ( flags & CYPHER_FILESYSTEM_FIND_SORT_BY_NAME ) != 0u ) {
        std::sort( entries, entries + nSortCount, []( const directory_entry_t &a, const directory_entry_t &b ) {
            return std::strcmp( a.szVirtualPath, b.szVirtualPath ) < 0;
        } );
    }

    return overflow ? fs_error_t::ERR_BUFFER_TOO_SMALL : fs_error_t::OK;
}

}       // namespace cypher::engine::fs
