//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Path.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Path module.
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

#include "CypherFileSystem.h"

#include <cctype>           // for isalpha and other isFuncs
#include <cstdio>           // for IO
#include <cstring>          // for string manipulations

/*
 * @Note: Internal helpers usually put them in anonymous namespaces as they are file-local.
 *        So we treat them that way as well, this goes for all internal helper functions.
 *        You can see that I do not put the Prefixed namings for them and that is how I can
 *        distinguish them from being helpers or core funcs.
 */
namespace {

bool IsInvalidVirtualPathChar( char c )
{
    unsigned char u = static_cast<unsigned char>( c );
    if ( u < 32 ) {
        return true;
    }
    if ( u == 127 ) {
        return true;
    }
    // @note: I am rejecting unicode characters for now as well.
    if ( u >= 128 ) {
        return true;
    }
    if ( c == ':' ) {
        return true;
    }
    if ( c == '*' ) {
        return true;
    }
    if ( c == '?' ) {
        return true;
    }
    if ( c == '"' ) {
        return true;
    }
    if ( c == '<' ) {
        return true;
    }
    if ( c == '>' ) {
        return true;
    }
    if ( c == '|' ) {
        return true;
    }
    if ( c == ' ' ) {
        return true;
    }
    return false;
}

char ToLowerAscii( const char c )
{
    if ( c >= 'A' && c <= 'Z' ) {
        return static_cast<char>( c - 'A' + 'a' );
    }
    return c;
}

const char *FindLastVirtualPathSeparator( const char *path )
{
    if ( path == nullptr ) {
        return nullptr;
    }

    const char *lastSep = nullptr;
    const char *cursor = path;
    while ( *cursor != '\0' ) {
        if ( *cursor == '/' || *cursor == '\\' ) {
            lastSep = cursor;
        }
        ++cursor;
    }

    return lastSep;
}

}       // namespace

namespace cypher::engine::fs
{

/*
================
FS_NormalizeVirtualPath
================
*/
fs_error_t FS_NormalizeVirtualPath( const char *szVirtualPath, char *szOutPath, common::u32 nOutPathSize )
{
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    szOutPath[0] = '\0';

    if ( szVirtualPath[0] == '/' || szVirtualPath[0] == '\\' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    if ( std::isalpha(static_cast<unsigned char>( szVirtualPath[0] ) ) && szVirtualPath[1] == ':' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    const char *cursor = szVirtualPath;

    common::u32 nWriteIndex = 0u;

    while ( *cursor != '\0' ) {
        // @Skipping the characters for slashes
        while ( *cursor == '/' || *cursor == '\\' ) {
            ++cursor;
        }
        const char *segmentStart = cursor;
        while ( *cursor != '/' && *cursor != '\\' && *cursor != '\0' ) {
            ++cursor;
        }
        const char *segmentEnd = cursor;

        const common::u32 nSegmentLength = static_cast<common::u32>( segmentEnd - segmentStart );
        if ( nSegmentLength == 0u ) {
            continue;
        }
        if ( nSegmentLength == 1u && segmentStart[0] == '.' ) {
            continue;
        }
        if ( nSegmentLength == 2u && segmentStart[0] == '.' && segmentStart[1] == '.' ) {
            szOutPath[0] = '\0';
            return fs_error_t::ERR_INVALID_PATH;
        }
        if ( nWriteIndex != 0u ) {
            if ( nWriteIndex + 1u >= nOutPathSize ) {
                szOutPath[0] = '\0';
                return fs_error_t::ERR_BUFFER_TOO_SMALL;
            }

            szOutPath[nWriteIndex] = '/';
            ++nWriteIndex;
        }
        // @copying - we are copying the segment characters into this out buffer
        for ( common::u32 i = 0u; i < nSegmentLength; ++i ) {
            char c = segmentStart[i];
            if ( IsInvalidVirtualPathChar( c ) ) {
                szOutPath[0] = '\0';
                return fs_error_t::ERR_INVALID_PATH;
            }
            c = ToLowerAscii( c );
            if ( nWriteIndex + 1u >= nOutPathSize ) {
                szOutPath[0] = '\0';
                return fs_error_t::ERR_BUFFER_TOO_SMALL;
            }
            szOutPath[nWriteIndex] = c;
            ++nWriteIndex;
        }

        szOutPath[nWriteIndex] = '\0';
    }

    if ( nWriteIndex == 0u ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    return fs_error_t::OK;
}

/*
================
FS_NormalizeVirtualRoot
================
*/
fs_error_t FS_NormalizeVirtualRoot( const char *szVirtualRoot, char *szOutRoot, common::u32 nOutSize )
{
    if ( szOutRoot == nullptr || nOutSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    szOutRoot[0] = '\0';

    if ( szVirtualRoot == nullptr ) {
        return fs_error_t::ERR_INVALID_ROOT;
    }

    if ( szVirtualRoot[0] == '\0' ) {
        return fs_error_t::OK;
    }

    return FS_NormalizeVirtualPath( szVirtualRoot, szOutRoot, nOutSize );
}

/*
================
FS_VirtualPathStartsWithRoot
================
*/
bool FS_VirtualPathStartsWithRoot( const char *szVirtualPath, const char *szVirtualRoot, const char **szOutRelativePath )
{
    if ( szOutRelativePath != nullptr ) {
        *szOutRelativePath = nullptr;
    }
    if ( szVirtualPath == nullptr || szVirtualRoot == nullptr ) {
        return false;
    }
    if ( szVirtualRoot[0] == '\0' ) {
        if ( szOutRelativePath != nullptr ) {
            *szOutRelativePath = szVirtualPath;
        }
        return true;
    }

    common::usize nRootLen = std::strlen( szVirtualRoot );

    if ( std::strncmp( szVirtualPath, szVirtualRoot, nRootLen ) != 0 ) {
        return false;
    }
    char nextChar = szVirtualPath[nRootLen];
    if ( nextChar != '\0' && nextChar != '/' ) {
        return false;
    }
    const char *szRelativePath = szVirtualPath + nRootLen;

    if ( szRelativePath[0] == '/' ) {
        ++szRelativePath;
    }

    if ( szOutRelativePath != nullptr ) {
        *szOutRelativePath = szRelativePath;
    }

    return true;
}

/*
================
FS_BuildPhysicalPath
================
*/
fs_error_t FS_BuildPhysicalPath( const char *szPhysicalRoot, const char *szRelativePath, char *szOutPath, common::u32 nOutPathSize )
{
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    szOutPath[0] = '\0';
    if ( szPhysicalRoot == nullptr || szPhysicalRoot[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_ROOT;
    }
    if ( szRelativePath == nullptr ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    if ( szRelativePath[0] == '\0' ) {
        common::usize required = std::strlen( szPhysicalRoot ) + 1;
        if ( required > nOutPathSize ) {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        std::memcpy( szOutPath, szPhysicalRoot, required );
        return fs_error_t::OK;
    }

    const int written = std::snprintf( szOutPath, nOutPathSize, "%s/%s", szPhysicalRoot, szRelativePath );

    if ( written < 0 ) {
        szOutPath[0] = '\0';
        // @Note: here we can also have ERR_INVALID_PATH as well but lets keep IO for now.
        return fs_error_t::ERR_IO_ERROR;
    }
    if ( static_cast<common::u32>( written ) >= nOutPathSize ) {
        szOutPath[0] = '\0';
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    return fs_error_t::OK;
}

bool FS_IsValidVirtualPath( const char *szVirtualPath )
{
    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t result = FS_NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) );
    return ( result == fs_error_t::OK );
}

fs_error_t FS_PathJoin(
    const char *left,
    const char *right,
    char *szOutPath,
    common::u32 nOutPathSize )
{
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    szOutPath[0] = '\0';
    if ( left == nullptr || left[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    if ( right == nullptr || right[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    char normalizedLeft[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char normalizedRight[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    fs_error_t err = FS_NormalizeVirtualPath( left, normalizedLeft, sizeof( normalizedLeft ) );
    if ( err != fs_error_t::OK ) {
        return err;
    }
    err = FS_NormalizeVirtualPath( right, normalizedRight, sizeof( normalizedRight ) );
    if ( err != fs_error_t::OK ) {
        return err;
    }
    const common::usize nLeftLen = std::strlen( normalizedLeft );
    const common::usize nRightLen = std::strlen( normalizedRight );

    const common::usize required = nLeftLen + 1u + nRightLen + 1u;
    if ( required > nOutPathSize ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }
    // @safetyFirst! safe now we can safely copy things
    std::memcpy( szOutPath, normalizedLeft, nLeftLen );
    szOutPath[nLeftLen] = '/';
    std::memcpy( szOutPath + nLeftLen + 1, normalizedRight, nRightLen + 1 );

    return fs_error_t::OK;
}

const char *FS_PathBasename( const char *szVirtualPath )
{
    if ( szVirtualPath == nullptr ) {
        return nullptr;
    }
    const char *lastSeparator = FindLastVirtualPathSeparator( szVirtualPath );
    if ( lastSeparator != nullptr ) {
        return lastSeparator + 1;
    }

    return szVirtualPath;
}

fs_error_t FS_PathDirname(
    const char *szVirtualPath,
    char *szOutPath,
    common::u32 nOutPathSize )
{
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    szOutPath[0] = '\0';
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t err = FS_NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) );
    if ( err != fs_error_t::OK ) {
        return err;
    }
    const char *lastSlash = FindLastVirtualPathSeparator( szNormalizedPath );
    if ( lastSlash == nullptr ) {
        szOutPath[0] = '\0';
        return fs_error_t::OK;
    }
    const common::usize nDirnameLen = static_cast<common::usize>( lastSlash - szNormalizedPath );
    if ( nDirnameLen + 1u > nOutPathSize ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy( szOutPath, szNormalizedPath, nDirnameLen );
    szOutPath[nDirnameLen] = '\0';

    return fs_error_t::OK;
}

const char *FS_PathExtension( const char *szVirtualPath )
{
    if ( szVirtualPath == nullptr ) {
        return nullptr;
    }

    const char *basename = FS_PathBasename( szVirtualPath );
    const char *cursor = basename;
    const char *lastDot = nullptr;

    while ( *cursor != '\0' ) {
        if ( *cursor == '.' ) {
            lastDot = cursor;
        }
        ++cursor;
    }

    if ( lastDot == nullptr || lastDot == basename ) {
        return cursor;
    }

    return lastDot;
}

fs_error_t FS_PathWithoutExtension(
    const char *szVirtualPath,
    char *szOutPath,
    common::u32 nOutPathSize )
{
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    szOutPath[0] = '\0';

    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t err = FS_NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) );
    if ( err != fs_error_t::OK ) {
        return err;
    }

    const char *extension = FS_PathExtension( szNormalizedPath );
    const common::usize nCopyLen = ( extension != nullptr && extension[0] != '\0' )
        ? static_cast<common::usize>( extension - szNormalizedPath )
        : std::strlen( szNormalizedPath );

    if ( nCopyLen + 1u > nOutPathSize ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    std::memcpy( szOutPath, szNormalizedPath, nCopyLen );
    szOutPath[nCopyLen] = '\0';
    return fs_error_t::OK;
}

bool FS_PathHasExtension( const char *szVirtualPath, const char *extension )
{
    if ( szVirtualPath == nullptr || extension == nullptr || extension[0] == '\0' ) {
        return false;
    }

    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    if ( FS_NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) ) != fs_error_t::OK ) {
        return false;
    }

    char szNormalizedExtension[CYPHER_FILESYSTEM_MAX_EXTENSION_LENGTH]{};
    common::u32 nWriteIndex = 0u;

    if ( extension[0] != '.' ) {
        szNormalizedExtension[nWriteIndex++] = '.';
    }

    const char *cursor = extension;
    if ( cursor[0] == '.' ) {
        szNormalizedExtension[nWriteIndex++] = '.';
        ++cursor;
    }

    while ( *cursor != '\0' ) {
        const char c = *cursor;
        if ( c == '/' || c == '\\' || IsInvalidVirtualPathChar( c ) ) {
            return false;
        }
        if ( nWriteIndex + 1u >= sizeof( szNormalizedExtension ) ) {
            return false;
        }
        szNormalizedExtension[nWriteIndex++] = ToLowerAscii( c );
        ++cursor;
    }

    szNormalizedExtension[nWriteIndex] = '\0';

    const char *szPathExtension = FS_PathExtension( szNormalizedPath );
    return szPathExtension != nullptr && std::strcmp( szPathExtension, szNormalizedExtension ) == 0;
}

}       // namespace cypher::engine::fs
