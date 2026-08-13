//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FileIo.cpp
//  Purpose: Implements native-file bootstrap I/O and stream adaptation.
//  Details: Native paths are copied from bounded UTF-8 views before entering the
//           operating system. The backend uses 64-bit offsets and keeps allocation
//           ownership attached to each opaque file object.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_FileIo.h"

#include <cstdio>
#include <limits>
#include <new>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace cypher::common
{

struct native_file_t {
#if CYPHER_PLATFORM_WINDOWS
    HANDLE hFile{ INVALID_HANDLE_VALUE };
#else
    int nFileDescriptor{ -1 };
#endif
    flags32_t flags{ FILE_OPEN_FLAG_NONE };
    const allocator_t *pAllocator{ nullptr };
};

namespace
{

constexpr flags32_t CY_FILE_OPEN_FLAG_MASK =
    FILE_OPEN_FLAG_READ |
    FILE_OPEN_FLAG_WRITE |
    FILE_OPEN_FLAG_APPEND |
    FILE_OPEN_FLAG_CREATE |
    FILE_OPEN_FLAG_TRUNCATE |
    FILE_OPEN_FLAG_EXCLUSIVE;

bool_t NativePathIsValid( string_view_t nativePath ) noexcept
{
    if ( !StringView_IsValid( nativePath ) || nativePath.cchLength == 0u ) {
        return CY_FALSE;
    }
    for ( usize iChar = 0u; iChar < nativePath.cchLength; ++iChar ) {
        if ( nativePath.pData[iChar] == '\0' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t OpenFlagsAreValid( flags32_t flags ) noexcept
{
    if ( ( flags & ~CY_FILE_OPEN_FLAG_MASK ) != 0u ) {
        return CY_FALSE;
    }

    const bool_t bRead = ( flags & FILE_OPEN_FLAG_READ ) != 0u;
    const bool_t bWrite = ( flags & FILE_OPEN_FLAG_WRITE ) != 0u;
    const bool_t bAppend = ( flags & FILE_OPEN_FLAG_APPEND ) != 0u;
    const bool_t bCreate = ( flags & FILE_OPEN_FLAG_CREATE ) != 0u;
    const bool_t bTruncate = ( flags & FILE_OPEN_FLAG_TRUNCATE ) != 0u;
    const bool_t bExclusive = ( flags & FILE_OPEN_FLAG_EXCLUSIVE ) != 0u;

    if ( !bRead && !bWrite ) {
        return CY_FALSE;
    }
    if ( ( bAppend || bCreate || bTruncate ) && !bWrite ) {
        return CY_FALSE;
    }
    if ( bExclusive && ( !bCreate || bTruncate ) ) {
        return CY_FALSE;
    }
    return !( bAppend && bTruncate );
}

#if CYPHER_PLATFORM_WINDOWS

wchar_t *CopyNativePath(
    string_view_t nativePath,
    const allocator_t *pAllocator,
    usize &cbAllocationOut ) noexcept
{
    cbAllocationOut = 0u;
    if ( nativePath.cchLength > static_cast<usize>( std::numeric_limits<int>::max() ) ) {
        return nullptr;
    }

    const int cchSource = static_cast<int>( nativePath.cchLength );
    const int cchWide = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        nativePath.pData,
        cchSource,
        nullptr,
        0 );
    if ( cchWide <= 0 ) {
        return nullptr;
    }

    const usize cchAllocation = static_cast<usize>( cchWide ) + 1u;
    if ( cchAllocation > CY_USIZE_MAX / sizeof( wchar_t ) ) {
        return nullptr;
    }
    cbAllocationOut = cchAllocation * sizeof( wchar_t );

    auto *pPath = static_cast<wchar_t *>( Allocator_Allocate(
        pAllocator,
        cbAllocationOut,
        alignof( wchar_t ) ) );
    if ( pPath == nullptr ) {
        cbAllocationOut = 0u;
        return nullptr;
    }

    const int cchConverted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        nativePath.pData,
        cchSource,
        pPath,
        cchWide );
    if ( cchConverted != cchWide ) {
        Allocator_Free(
            pAllocator,
            pPath,
            cbAllocationOut,
            alignof( wchar_t ) );
        cbAllocationOut = 0u;
        return nullptr;
    }

    pPath[cchWide] = L'\0';
    return pPath;
}

void FreeNativePath(
    wchar_t *pPath,
    usize cbAllocation,
    const allocator_t *pAllocator ) noexcept
{
    Allocator_Free(
        pAllocator,
        pPath,
        cbAllocation,
        alignof( wchar_t ) );
}

bool_t NativeFileIsValid( const native_file_t *pFile ) noexcept
{
    return pFile != nullptr &&
           pFile->hFile != INVALID_HANDLE_VALUE &&
           OpenFlagsAreValid( pFile->flags ) &&
           Allocator_IsValid( pFile->pAllocator );
}

bool_t WidePathIsDirectory( const wchar_t *pPath ) noexcept
{
    const DWORD nAttributes = GetFileAttributesW( pPath );
    return nAttributes != INVALID_FILE_ATTRIBUTES &&
           ( nAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0u;
}

bool_t CreateWideDirectoryComponent( wchar_t *pPath ) noexcept
{
    if ( CreateDirectoryW( pPath, nullptr ) ) {
        return CY_TRUE;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS &&
           WidePathIsDirectory( pPath );
}

usize WidePathRootLength( const wchar_t *pPath, usize cchPath ) noexcept
{
    if ( cchPath >= 3u &&
         ( ( pPath[0] >= L'A' && pPath[0] <= L'Z' ) ||
           ( pPath[0] >= L'a' && pPath[0] <= L'z' ) ) &&
         pPath[1] == L':' &&
         ( pPath[2] == L'\\' || pPath[2] == L'/' ) ) {
        return 3u;
    }
    if ( cchPath >= 2u &&
         ( pPath[0] == L'\\' || pPath[0] == L'/' ) &&
         ( pPath[1] == L'\\' || pPath[1] == L'/' ) ) {
        usize iCursor = 2u;
        while ( iCursor < cchPath &&
                pPath[iCursor] != L'\\' && pPath[iCursor] != L'/' ) {
            ++iCursor;
        }
        if ( iCursor < cchPath ) {
            ++iCursor;
        }
        while ( iCursor < cchPath &&
                pPath[iCursor] != L'\\' && pPath[iCursor] != L'/' ) {
            ++iCursor;
        }
        return iCursor < cchPath ? iCursor + 1u : cchPath;
    }
    return 0u;
}

stream_io_result_t NativeFileRead(
    void *pUserData,
    void *pDest,
    usize cbRequested ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) ||
         ( pDest == nullptr && cbRequested != 0u ) ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }

    const usize cbMaximum = static_cast<usize>( std::numeric_limits<DWORD>::max() );
    const DWORD cbChunk = static_cast<DWORD>(
        cbRequested < cbMaximum ? cbRequested : cbMaximum );
    DWORD cbRead = 0u;
    if ( !ReadFile( pFile->hFile, pDest, cbChunk, &cbRead, nullptr ) ) {
        return { stream_status_t::IO_ERROR, 0u };
    }
    if ( cbRead == 0u ) {
        return { stream_status_t::END_OF_STREAM, 0u };
    }
    return { stream_status_t::OK, static_cast<usize>( cbRead ) };
}

stream_io_result_t NativeFileWrite(
    void *pUserData,
    const void *pSource,
    usize cbRequested ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) ||
         ( pSource == nullptr && cbRequested != 0u ) ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }

    const usize cbMaximum = static_cast<usize>( std::numeric_limits<DWORD>::max() );
    const DWORD cbChunk = static_cast<DWORD>(
        cbRequested < cbMaximum ? cbRequested : cbMaximum );
    DWORD cbWritten = 0u;
    if ( !WriteFile( pFile->hFile, pSource, cbChunk, &cbWritten, nullptr ) ||
         cbWritten == 0u ) {
        return { stream_status_t::IO_ERROR, 0u };
    }
    return { stream_status_t::OK, static_cast<usize>( cbWritten ) };
}

stream_status_t NativeFileSeek(
    void *pUserData,
    i64 nOffset,
    stream_seek_origin_t origin,
    u64 *pPositionOut ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) || pPositionOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    DWORD nMoveMethod = FILE_BEGIN;
    switch ( origin ) {
        case stream_seek_origin_t::BEGIN:
            break;
        case stream_seek_origin_t::CURRENT:
            nMoveMethod = FILE_CURRENT;
            break;
        case stream_seek_origin_t::END:
            nMoveMethod = FILE_END;
            break;
        default:
            return stream_status_t::INVALID_ARGUMENT;
    }

    LARGE_INTEGER distance{};
    distance.QuadPart = nOffset;
    LARGE_INTEGER position{};
    if ( !SetFilePointerEx( pFile->hFile, distance, &position, nMoveMethod ) ) {
        const DWORD nError = GetLastError();
        return nError == ERROR_NEGATIVE_SEEK
            ? stream_status_t::OUT_OF_RANGE
            : stream_status_t::IO_ERROR;
    }
    if ( position.QuadPart < 0 ) {
        return stream_status_t::OUT_OF_RANGE;
    }

    *pPositionOut = static_cast<u64>( position.QuadPart );
    return stream_status_t::OK;
}

stream_status_t NativeFileTell( void *pUserData, u64 *pValueOut ) noexcept
{
    return NativeFileSeek(
        pUserData,
        0,
        stream_seek_origin_t::CURRENT,
        pValueOut );
}

stream_status_t NativeFileSize( void *pUserData, u64 *pValueOut ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) || pValueOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    LARGE_INTEGER size{};
    if ( !GetFileSizeEx( pFile->hFile, &size ) || size.QuadPart < 0 ) {
        return stream_status_t::IO_ERROR;
    }
    *pValueOut = static_cast<u64>( size.QuadPart );
    return stream_status_t::OK;
}

stream_status_t NativeFileFlush( void *pUserData ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) ) {
        return stream_status_t::INVALID_ARGUMENT;
    }
    return FlushFileBuffers( pFile->hFile )
        ? stream_status_t::OK
        : stream_status_t::IO_ERROR;
}

#else

char *CopyNativePath(
    string_view_t nativePath,
    const allocator_t *pAllocator,
    usize &cbAllocationOut ) noexcept
{
    cbAllocationOut = 0u;
    if ( nativePath.cchLength == CY_USIZE_MAX ) {
        return nullptr;
    }

    cbAllocationOut = nativePath.cchLength + 1u;
    auto *pPath = static_cast<char *>( Allocator_Allocate(
        pAllocator,
        cbAllocationOut,
        alignof( char ) ) );
    if ( pPath == nullptr ) {
        cbAllocationOut = 0u;
        return nullptr;
    }

    Cy_MemCopy( pPath, nativePath.pData, nativePath.cchLength );
    pPath[nativePath.cchLength] = '\0';
    return pPath;
}

void FreeNativePath(
    char *pPath,
    usize cbAllocation,
    const allocator_t *pAllocator ) noexcept
{
    Allocator_Free(
        pAllocator,
        pPath,
        cbAllocation,
        alignof( char ) );
}

bool_t NativeFileIsValid( const native_file_t *pFile ) noexcept
{
    return pFile != nullptr &&
           pFile->nFileDescriptor >= 0 &&
           OpenFlagsAreValid( pFile->flags ) &&
           Allocator_IsValid( pFile->pAllocator );
}

bool_t NarrowPathIsDirectory( const char *pPath ) noexcept
{
    struct stat pathInfo{};
    return stat( pPath, &pathInfo ) == 0 && S_ISDIR( pathInfo.st_mode );
}

bool_t CreateNarrowDirectoryComponent( char *pPath ) noexcept
{
    if ( mkdir( pPath, 0777 ) == 0 ) {
        return CY_TRUE;
    }
    return errno == EEXIST && NarrowPathIsDirectory( pPath );
}

stream_io_result_t NativeFileRead(
    void *pUserData,
    void *pDest,
    usize cbRequested ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) ||
         ( pDest == nullptr && cbRequested != 0u ) ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }

    const usize cbMaximum = static_cast<usize>( std::numeric_limits<ssize_t>::max() );
    const usize cbChunk = cbRequested < cbMaximum ? cbRequested : cbMaximum;
    ssize_t cbRead = -1;
    do {
        cbRead = read( pFile->nFileDescriptor, pDest, cbChunk );
    } while ( cbRead < 0 && errno == EINTR );

    if ( cbRead < 0 ) {
        return { stream_status_t::IO_ERROR, 0u };
    }
    if ( cbRead == 0 ) {
        return { stream_status_t::END_OF_STREAM, 0u };
    }
    return { stream_status_t::OK, static_cast<usize>( cbRead ) };
}

stream_io_result_t NativeFileWrite(
    void *pUserData,
    const void *pSource,
    usize cbRequested ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) ||
         ( pSource == nullptr && cbRequested != 0u ) ) {
        return { stream_status_t::INVALID_ARGUMENT, 0u };
    }
    if ( cbRequested == 0u ) {
        return {};
    }

    const usize cbMaximum = static_cast<usize>( std::numeric_limits<ssize_t>::max() );
    const usize cbChunk = cbRequested < cbMaximum ? cbRequested : cbMaximum;
    ssize_t cbWritten = -1;
    do {
        cbWritten = write( pFile->nFileDescriptor, pSource, cbChunk );
    } while ( cbWritten < 0 && errno == EINTR );

    if ( cbWritten <= 0 ) {
        return { stream_status_t::IO_ERROR, 0u };
    }
    return { stream_status_t::OK, static_cast<usize>( cbWritten ) };
}

stream_status_t NativeFileSeek(
    void *pUserData,
    i64 nOffset,
    stream_seek_origin_t origin,
    u64 *pPositionOut ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) || pPositionOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    int nWhence = SEEK_SET;
    switch ( origin ) {
        case stream_seek_origin_t::BEGIN:
            break;
        case stream_seek_origin_t::CURRENT:
            nWhence = SEEK_CUR;
            break;
        case stream_seek_origin_t::END:
            nWhence = SEEK_END;
            break;
        default:
            return stream_status_t::INVALID_ARGUMENT;
    }

    errno = 0;
    const off_t nPosition = lseek(
        pFile->nFileDescriptor,
        static_cast<off_t>( nOffset ),
        nWhence );
    if ( nPosition < 0 ) {
        return errno == EINVAL || errno == EOVERFLOW
            ? stream_status_t::OUT_OF_RANGE
            : stream_status_t::IO_ERROR;
    }
    *pPositionOut = static_cast<u64>( nPosition );
    return stream_status_t::OK;
}

stream_status_t NativeFileTell( void *pUserData, u64 *pValueOut ) noexcept
{
    return NativeFileSeek(
        pUserData,
        0,
        stream_seek_origin_t::CURRENT,
        pValueOut );
}

stream_status_t NativeFileSize( void *pUserData, u64 *pValueOut ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) || pValueOut == nullptr ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    struct stat fileInfo{};
    if ( fstat( pFile->nFileDescriptor, &fileInfo ) != 0 || fileInfo.st_size < 0 ) {
        return stream_status_t::IO_ERROR;
    }
    *pValueOut = static_cast<u64>( fileInfo.st_size );
    return stream_status_t::OK;
}

stream_status_t NativeFileFlush( void *pUserData ) noexcept
{
    auto *pFile = static_cast<native_file_t *>( pUserData );
    if ( !NativeFileIsValid( pFile ) ) {
        return stream_status_t::INVALID_ARGUMENT;
    }

    int nResult = -1;
    do {
        nResult = fsync( pFile->nFileDescriptor );
    } while ( nResult != 0 && errno == EINTR );
    return nResult == 0 ? stream_status_t::OK : stream_status_t::IO_ERROR;
}

#endif

const stream_ops_t NATIVE_FILE_STREAM_OPS{
    &NativeFileRead,
    &NativeFileWrite,
    &NativeFileSeek,
    &NativeFileTell,
    &NativeFileSize,
    &NativeFileFlush
};

} // namespace

native_file_t *FileIo_OpenNative(
    string_view_t nativePath,
    flags32_t flags,
    const allocator_t *pAllocator ) noexcept
{
    if ( !NativePathIsValid( nativePath ) ||
         !OpenFlagsAreValid( flags ) ||
         !Allocator_IsValid( pAllocator ) ) {
        return nullptr;
    }

    usize cbPathAllocation = 0u;
    auto *pPath = CopyNativePath(
        nativePath,
        pAllocator,
        cbPathAllocation );
    if ( pPath == nullptr ) {
        return nullptr;
    }

#if CYPHER_PLATFORM_WINDOWS
    DWORD nAccess = 0u;
    if ( ( flags & FILE_OPEN_FLAG_READ ) != 0u ) {
        nAccess |= GENERIC_READ;
    }
    if ( ( flags & FILE_OPEN_FLAG_WRITE ) != 0u ) {
        nAccess |= ( flags & FILE_OPEN_FLAG_APPEND ) != 0u
            ? FILE_APPEND_DATA
            : GENERIC_WRITE;
    }

    DWORD nCreation = OPEN_EXISTING;
    if ( ( flags & FILE_OPEN_FLAG_EXCLUSIVE ) != 0u ) {
        nCreation = CREATE_NEW;
    } else if ( ( flags & FILE_OPEN_FLAG_CREATE ) != 0u &&
                ( flags & FILE_OPEN_FLAG_TRUNCATE ) != 0u ) {
        nCreation = CREATE_ALWAYS;
    } else if ( ( flags & FILE_OPEN_FLAG_CREATE ) != 0u ) {
        nCreation = OPEN_ALWAYS;
    } else if ( ( flags & FILE_OPEN_FLAG_TRUNCATE ) != 0u ) {
        nCreation = TRUNCATE_EXISTING;
    }

    HANDLE hFile = CreateFileW(
        pPath,
        nAccess,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        nCreation,
        FILE_ATTRIBUTE_NORMAL,
        nullptr );
    FreeNativePath( pPath, cbPathAllocation, pAllocator );
    if ( hFile == INVALID_HANDLE_VALUE ) {
        return nullptr;
    }
#else
    static_assert( sizeof( off_t ) >= sizeof( i64 ) );

    int nOpenFlags = 0;
    const bool_t bRead = ( flags & FILE_OPEN_FLAG_READ ) != 0u;
    const bool_t bWrite = ( flags & FILE_OPEN_FLAG_WRITE ) != 0u;
    if ( bRead && bWrite ) {
        nOpenFlags |= O_RDWR;
    } else if ( bWrite ) {
        nOpenFlags |= O_WRONLY;
    } else {
        nOpenFlags |= O_RDONLY;
    }
    if ( ( flags & FILE_OPEN_FLAG_APPEND ) != 0u ) {
        nOpenFlags |= O_APPEND;
    }
    if ( ( flags & FILE_OPEN_FLAG_CREATE ) != 0u ) {
        nOpenFlags |= O_CREAT;
    }
    if ( ( flags & FILE_OPEN_FLAG_TRUNCATE ) != 0u ) {
        nOpenFlags |= O_TRUNC;
    }
    if ( ( flags & FILE_OPEN_FLAG_EXCLUSIVE ) != 0u ) {
        nOpenFlags |= O_EXCL;
    }
    #ifdef O_CLOEXEC
        nOpenFlags |= O_CLOEXEC;
    #endif

    int nFileDescriptor = -1;
    do {
        nFileDescriptor = open( pPath, nOpenFlags, 0666 );
    } while ( nFileDescriptor < 0 && errno == EINTR );
    FreeNativePath( pPath, cbPathAllocation, pAllocator );
    if ( nFileDescriptor < 0 ) {
        return nullptr;
    }

    struct stat fileInfo{};
    if ( fstat( nFileDescriptor, &fileInfo ) != 0 || !S_ISREG( fileInfo.st_mode ) ) {
        close( nFileDescriptor );
        return nullptr;
    }
#endif

    void *pStorage = Allocator_Allocate(
        pAllocator,
        sizeof( native_file_t ),
        alignof( native_file_t ) );
    if ( pStorage == nullptr ) {
#if CYPHER_PLATFORM_WINDOWS
        CloseHandle( hFile );
#else
        close( nFileDescriptor );
#endif
        return nullptr;
    }

    auto *pFile = ::new ( pStorage ) native_file_t{};
#if CYPHER_PLATFORM_WINDOWS
    pFile->hFile = hFile;
#else
    pFile->nFileDescriptor = nFileDescriptor;
#endif
    pFile->flags = flags;
    pFile->pAllocator = pAllocator;

    if ( ( flags & FILE_OPEN_FLAG_APPEND ) != 0u ) {
        u64 nPosition = 0u;
        if ( NativeFileSeek(
                 pFile,
                 0,
                 stream_seek_origin_t::END,
                 &nPosition ) != stream_status_t::OK ) {
            FileIo_CloseNative( pFile );
            return nullptr;
        }
    }
    return pFile;
}

void FileIo_CloseNative( native_file_t *pFile ) noexcept
{
    if ( pFile == nullptr ) {
        return;
    }

    const allocator_t *pAllocator = pFile->pAllocator;
    if ( !NativeFileIsValid( pFile ) || !Allocator_IsValid( pAllocator ) ) {
        return;
    }

#if CYPHER_PLATFORM_WINDOWS
    CloseHandle( pFile->hFile );
    pFile->hFile = INVALID_HANDLE_VALUE;
#else
    // A descriptor may already be closed when close reports EINTR; retrying can
    // accidentally close a descriptor reused by another thread.
    close( pFile->nFileDescriptor );
    pFile->nFileDescriptor = -1;
#endif

    pFile->~native_file_t();
    Allocator_Free(
        pAllocator,
        pFile,
        sizeof( native_file_t ),
        alignof( native_file_t ) );
}

stream_t FileIo_AsStream( native_file_t *pFile ) noexcept
{
    if ( !NativeFileIsValid( pFile ) ) {
        return {};
    }

    flags32_t capabilities = STREAM_CAPABILITY_SEEK | STREAM_CAPABILITY_SIZE;
    if ( ( pFile->flags & FILE_OPEN_FLAG_READ ) != 0u ) {
        capabilities |= STREAM_CAPABILITY_READ;
    }
    if ( ( pFile->flags & FILE_OPEN_FLAG_WRITE ) != 0u ) {
        capabilities |= STREAM_CAPABILITY_WRITE | STREAM_CAPABILITY_FLUSH;
    }
    return { &NATIVE_FILE_STREAM_OPS, pFile, capabilities };
}

bool_t FileIo_ReadAllNative(
    string_view_t nativePath,
    blob_t *pDest ) noexcept
{
    if ( !Blob_IsValid( pDest ) ||
         !Allocator_IsValid( pDest->pAllocator ) ) {
        return CY_FALSE;
    }

    native_file_t *pFile = FileIo_OpenNative(
        nativePath,
        FILE_OPEN_FLAG_READ,
        pDest->pAllocator );
    if ( pFile == nullptr ) {
        return CY_FALSE;
    }

    stream_t stream = FileIo_AsStream( pFile );
    u64 cbFileSize = 0u;
    if ( Stream_Size( &stream, &cbFileSize ) != stream_status_t::OK ||
         cbFileSize > CY_USIZE_MAX ) {
        FileIo_CloseNative( pFile );
        return CY_FALSE;
    }

    blob_t pending{};
    const bool_t bInitialized = Blob_Init(
        &pending,
        pDest->pAllocator,
        static_cast<usize>( cbFileSize ) );
    const bool_t bResized = bInitialized && Blob_Resize(
        &pending,
        static_cast<usize>( cbFileSize ) );
    const bool_t bRead = bResized &&
        Stream_ReadExact(
            &stream,
            pending.pData,
            pending.cbSize ) == stream_status_t::OK;
    FileIo_CloseNative( pFile );

    if ( !bRead ) {
        return CY_FALSE;
    }

    Blob_Shutdown( pDest );
    Blob_Move( pDest, &pending );
    return CY_TRUE;
}

bool_t FileIo_WriteAllNative(
    string_view_t nativePath,
    binary_block_t source ) noexcept
{
    if ( !BinaryBlock_IsValid( source ) ) {
        return CY_FALSE;
    }

    const allocator_t *pAllocator = Allocator_GetSystem();
    native_file_t *pFile = FileIo_OpenNative(
        nativePath,
        FILE_OPEN_FLAG_WRITE |
        FILE_OPEN_FLAG_CREATE |
        FILE_OPEN_FLAG_TRUNCATE,
        pAllocator );
    if ( pFile == nullptr ) {
        return CY_FALSE;
    }

    stream_t stream = FileIo_AsStream( pFile );
    const bool_t bWritten = Stream_WriteExact(
        &stream,
        source.pData,
        source.cbSize ) == stream_status_t::OK;
    const bool_t bFlushed = bWritten &&
        Stream_Flush( &stream ) == stream_status_t::OK;
    FileIo_CloseNative( pFile );
    return bFlushed;
}

bool_t FileIo_CreateDirectoriesNative( string_view_t nativePath ) noexcept
{
    if ( !NativePathIsValid( nativePath ) ) {
        return CY_FALSE;
    }

    const allocator_t *pAllocator = Allocator_GetSystem();
    usize cbPathAllocation = 0u;
    auto *pPath = CopyNativePath(
        nativePath,
        pAllocator,
        cbPathAllocation );
    if ( pPath == nullptr ) {
        return CY_FALSE;
    }

    bool_t bCreated = CY_TRUE;
#if CYPHER_PLATFORM_WINDOWS
    const usize cchPath = cbPathAllocation / sizeof( wchar_t ) - 1u;
    const usize iRootEnd = WidePathRootLength( pPath, cchPath );
    for ( usize iChar = iRootEnd; iChar < cchPath; ++iChar ) {
        if ( pPath[iChar] != L'\\' && pPath[iChar] != L'/' ) {
            continue;
        }
        const wchar_t chSeparator = pPath[iChar];
        pPath[iChar] = L'\0';
        if ( iChar != 0u && !CreateWideDirectoryComponent( pPath ) ) {
            bCreated = CY_FALSE;
            pPath[iChar] = chSeparator;
            break;
        }
        pPath[iChar] = chSeparator;
    }
    if ( bCreated && !WidePathIsDirectory( pPath ) ) {
        bCreated = CreateWideDirectoryComponent( pPath );
    }
#else
    const usize cchPath = cbPathAllocation - 1u;
    const usize iRootEnd = cchPath != 0u && pPath[0] == '/' ? 1u : 0u;
    for ( usize iChar = iRootEnd; iChar < cchPath; ++iChar ) {
        if ( pPath[iChar] != '/' ) {
            continue;
        }
        pPath[iChar] = '\0';
        if ( iChar != 0u && !CreateNarrowDirectoryComponent( pPath ) ) {
            bCreated = CY_FALSE;
            pPath[iChar] = '/';
            break;
        }
        pPath[iChar] = '/';
    }
    if ( bCreated && !NarrowPathIsDirectory( pPath ) ) {
        bCreated = CreateNarrowDirectoryComponent( pPath );
    }
#endif

    FreeNativePath( pPath, cbPathAllocation, pAllocator );
    return bCreated;
}

bool_t FileIo_RemoveNative( string_view_t nativePath ) noexcept
{
    if ( !NativePathIsValid( nativePath ) ) {
        return CY_FALSE;
    }

    const allocator_t *pAllocator = Allocator_GetSystem();
    usize cbPathAllocation = 0u;
    auto *pPath = CopyNativePath(
        nativePath,
        pAllocator,
        cbPathAllocation );
    if ( pPath == nullptr ) {
        return CY_FALSE;
    }

#if CYPHER_PLATFORM_WINDOWS
    const bool_t bRemoved = DeleteFileW( pPath ) ? CY_TRUE : CY_FALSE;
#else
    int nResult = -1;
    do {
        nResult = unlink( pPath );
    } while ( nResult != 0 && errno == EINTR );
    const bool_t bRemoved = nResult == 0 ? CY_TRUE : CY_FALSE;
#endif
    FreeNativePath( pPath, cbPathAllocation, pAllocator );
    return bRemoved;
}

bool_t FileIo_ReplaceNative(
    string_view_t sourcePath,
    string_view_t destinationPath ) noexcept
{
    if ( !NativePathIsValid( sourcePath ) ||
         !NativePathIsValid( destinationPath ) ) {
        return CY_FALSE;
    }

    const allocator_t *pAllocator = Allocator_GetSystem();
    usize cbSourceAllocation = 0u;
    usize cbDestinationAllocation = 0u;
    auto *pSource = CopyNativePath(
        sourcePath,
        pAllocator,
        cbSourceAllocation );
    auto *pDestination = CopyNativePath(
        destinationPath,
        pAllocator,
        cbDestinationAllocation );
    if ( pSource == nullptr || pDestination == nullptr ) {
        if ( pSource != nullptr ) {
            FreeNativePath( pSource, cbSourceAllocation, pAllocator );
        }
        if ( pDestination != nullptr ) {
            FreeNativePath(
                pDestination,
                cbDestinationAllocation,
                pAllocator );
        }
        return CY_FALSE;
    }

#if CYPHER_PLATFORM_WINDOWS
    const bool_t bReplaced = MoveFileExW(
        pSource,
        pDestination,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH )
        ? CY_TRUE
        : CY_FALSE;
#else
    int nResult = -1;
    do {
        nResult = std::rename( pSource, pDestination );
    } while ( nResult != 0 && errno == EINTR );
    const bool_t bReplaced = nResult == 0 ? CY_TRUE : CY_FALSE;
#endif
    FreeNativePath( pSource, cbSourceAllocation, pAllocator );
    FreeNativePath(
        pDestination,
        cbDestinationAllocation,
        pAllocator );
    return bReplaced;
}

bool_t FileIo_NativeExists( string_view_t nativePath ) noexcept
{
    if ( !NativePathIsValid( nativePath ) ) {
        return CY_FALSE;
    }

    const allocator_t *pAllocator = Allocator_GetSystem();
    usize cbPathAllocation = 0u;
    auto *pPath = CopyNativePath(
        nativePath,
        pAllocator,
        cbPathAllocation );
    if ( pPath == nullptr ) {
        return CY_FALSE;
    }

#if CYPHER_PLATFORM_WINDOWS
    const bool_t bExists = GetFileAttributesW( pPath ) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat pathInfo{};
    const bool_t bExists = stat( pPath, &pathInfo ) == 0;
#endif
    FreeNativePath( pPath, cbPathAllocation, pAllocator );
    return bExists;
}

} // namespace cypher::common
