//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem.cpp
//  Purpose: Implements the CypherFileSystem FileSystem module.
//  Details: This file participates in the virtual filesystem layer that maps engine
//           paths to mounted physical or package-backed data. Keep path validation
//           strict because every asset pipeline will depend on it.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherFileSystem.h"
#include "CypherFileSystem_Runtime.h"
#include "CypherLog.h"
#include "CypherPak.h"

#include <algorithm>
#include <cstdio>           // stdio file handles and read/write operations.
#include <cstring>          // strcmp / strncpy for fixed path buffers.
#include <filesystem>       // Path probing and directory creation.
#include <limits>
#include <new>
#include <system_error>     // std::error_code for non-throwing filesystem calls.

namespace cypher::engine::fs {

namespace pak = ::cypher::engine;

namespace {

struct package_file_state_t {
	common::u8 *data{ nullptr };
	common::u64 size{ 0u };
};

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
	case pak::pak_error_t::ERR_PERMISSION_DENIED:
		return fs_error_t::ERR_PERMISSION_DENIED;
	case pak::pak_error_t::ERR_UNSUPPORTED_COMPRESSION:
		return fs_error_t::ERR_UNSUPPORTED_BACKEND;
	default:
		return fs_error_t::ERR_IO_ERROR;
	}
}

void ClearRuntimeState( runtime_state_t &state )
{
	state.~runtime_state_t();
	::new ( static_cast<void *>( &state ) ) runtime_state_t();
}

}       // namespace

/*
================
FS_Init
================
*/
fs_error_t FS_Init() {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	runtime_state_t &state = FS_RuntimeState();
	if ( state.initialized ) {
		LOG_WARNING( log::channel_t::FS, "filesystem init requested while already initialized." );
		return fs_error_t::ERR_IS_INIT;
	}

	ClearRuntimeState( state );
	state.initialized = true;
	state.nNextMountHandle = 1u;
	state.nextAsyncRequest = 1u;
	state.nNextWatchHandle = 1u;

	LOG_INFO( log::channel_t::FS, "filesystem initialized." );

	return fs_error_t::OK;
}

/*
================
FS_Shutdown
================
*/
fs_error_t FS_Shutdown() {
	{
		std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
		runtime_state_t &state = FS_RuntimeState();
		if ( !state.initialized ) {
			LOG_WARNING( log::channel_t::FS, "filesystem shutdown requested while not initialized." );
			return fs_error_t::ERR_NOT_INIT;
		}
	}

	FS_ShutdownAsyncRequests();

	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	runtime_state_t &state = FS_RuntimeState();
	LOG_INFO( log::channel_t::FS, "filesystem shutdown: mounts=%u.", state.nMountCount );
	while ( state.nWatchCount > 0u ) {
		const watch_handle_t watch = state.watches[0].handle;
		( void )FS_UnwatchPath( watch );
	}
	while ( state.nMountCount > 0u ) {
		const mount_handle_t mount = state.mounts[0].handle;
		( void )FS_Unmount( mount );
	}
	ClearRuntimeState( state );
	return fs_error_t::OK;
}

/*
================
FS_IsInitialized
================
*/
bool FS_IsInitialized() {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	return FS_RuntimeState().initialized;
}

/*
================
FS_SetWritePath
================
*/
fs_error_t FS_SetWritePath( const char *szPhysicalPath ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	runtime_state_t &state = FS_RuntimeState();
	if ( !state.initialized ) {
		LOG_ERROR( log::channel_t::FS, "set write path failed: filesystem is not initialized." );
		return fs_error_t::ERR_NOT_INIT;
	}
	if ( szPhysicalPath == nullptr || szPhysicalPath[0] == '\0' ) {
		LOG_ERROR( log::channel_t::FS, "set write path failed: invalid physical path." );
		return fs_error_t::ERR_INVALID_PATH;
	}
	const common::u32 nPhysicalPathLength = static_cast<common::u32>( std::strlen( szPhysicalPath ) );
	if ( nPhysicalPathLength + 1u > sizeof( state.szWritePath ) ) {
		LOG_ERROR( log::channel_t::FS, "set write path failed for '%s': path is too long.", szPhysicalPath );
		return fs_error_t::ERR_BUFFER_TOO_SMALL;
	}

	std::error_code ec{};
	std::filesystem::create_directories( szPhysicalPath, ec );
	if ( ec ) {
		LOG_ERROR( log::channel_t::FS, "set write path failed for '%s': create directories failed.", szPhysicalPath );
		return fs_error_t::ERR_IO_ERROR;
	}
	if ( !std::filesystem::is_directory( szPhysicalPath, ec ) || ec ) {
		LOG_ERROR( log::channel_t::FS, "set write path failed for '%s': path is not a directory.", szPhysicalPath );
		return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_NOT_DIRECTORY;
	}

	std::memcpy( state.szWritePath, szPhysicalPath, nPhysicalPathLength + 1u );
	LOG_INFO( log::channel_t::FS, "write path set to '%s'.", state.szWritePath );
	return fs_error_t::OK;
}

/*
================
FS_GetWritePath
================
*/
const char *FS_GetWritePath() {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	const runtime_state_t &state = FS_RuntimeState();
	if ( !state.initialized ) {
		return nullptr;
	}
	if ( state.szWritePath[0] == '\0' ) {
		return nullptr;
	}
	return state.szWritePath;
}

/*
================
FS_OpenModeToCMode
================
*/
static const char *FS_OpenModeToCMode( const open_mode_t mode ) {
	switch ( mode ) {
	case open_mode_t::READ_TEXT:
		return "rb";
	case open_mode_t::READ_BINARY:
		return "rb";
	case open_mode_t::WRITE_TEXT:
		return "wb";
	case open_mode_t::WRITE_BINARY:
		return "wb";
	case open_mode_t::APPEND_TEXT:
		return "ab";
	case open_mode_t::APPEND_BINARY:
		return "ab";
	default:
		return nullptr;
	}
}

/*
================
FS_Open

Opens an OS-backed file resolved through the virtual filesystem.
================
*/
fs_error_t FS_Open( const char *szVirtualPath, open_mode_t mode, file_t &file ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	if ( !FS_RuntimeState().initialized ) {
		return fs_error_t::ERR_NOT_INIT;
	}
	file = {};
	if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
		return fs_error_t::ERR_INVALID_PATH;
	}
	const char *cMode = FS_OpenModeToCMode( mode );
	if ( cMode == nullptr ) {
		return fs_error_t::ERR_INVALID_MODE;
	}
	const bool readMode = mode == open_mode_t::READ_TEXT || mode == open_mode_t::READ_BINARY;
	const bool writeMode = mode == open_mode_t::WRITE_TEXT || mode == open_mode_t::WRITE_BINARY;
	const bool appendMode = mode == open_mode_t::APPEND_TEXT || mode == open_mode_t::APPEND_BINARY;
	char szResolvedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
	if ( readMode ) {
		resolved_file_t resolvedFile{};
		const fs_error_t err = FS_ResolveReadableFile( szVirtualPath, resolvedFile );
		if ( err != fs_error_t::OK ) {
			return err;
		}
		if ( resolvedFile.backend == file_backend_t::PACKAGE_FILE ) {
			package_file_state_t *pPackageState = new ( std::nothrow ) package_file_state_t();
			if ( pPackageState == nullptr ) {
				return fs_error_t::ERR_OUT_OF_MEMORY;
			}

			pPackageState->size = resolvedFile.nFileSize;
			if ( pPackageState->size > 0u ) {
				if ( pPackageState->size > static_cast<common::u64>( std::numeric_limits<common::usize>::max() ) ) {
					delete pPackageState;
					return fs_error_t::ERR_OUT_OF_MEMORY;
				}
				pPackageState->data = new ( std::nothrow ) common::u8[static_cast<common::usize>( pPackageState->size )];
				if ( pPackageState->data == nullptr ) {
					delete pPackageState;
					return fs_error_t::ERR_OUT_OF_MEMORY;
				}
			}

			common::u64 nBytesRead = 0u;
			const pak::pak_error_t readResult = pak::Pak_ReadFileByIndex(
				*static_cast<pak::pak_reader_t *>( resolvedFile.pPackageReader ),
				resolvedFile.nPackageFileIndex,
				pPackageState->data,
				pPackageState->size,
				nBytesRead );
			if ( readResult != pak::pak_error_t::OK || nBytesRead != pPackageState->size ) {
				delete[] pPackageState->data;
				delete pPackageState;
				return readResult != pak::pak_error_t::OK ? PakErrorToFs( readResult ) : fs_error_t::ERR_FILE_READ_FAILED;
			}

			file.backend = file_backend_t::PACKAGE_FILE;
			file.pNativeHandle = pPackageState;
			file.readable = true;
			file.writable = false;
			file.cursor = 0u;
			file.size = pPackageState->size;
			FS_RuntimeState().stats.nOpenCount++;
			return fs_error_t::OK;
		}
		if ( resolvedFile.backend != file_backend_t::OS_FILE ) {
			return fs_error_t::ERR_UNSUPPORTED_BACKEND;
		}
		std::memcpy( szResolvedPath, resolvedFile.szPhysicalPath, std::strlen( resolvedFile.szPhysicalPath ) + 1u );
	} else if ( writeMode || appendMode ) {
		const fs_error_t buildPathResult = FS_BuildWritePath( szVirtualPath, szResolvedPath, sizeof( szResolvedPath ) );
		if ( buildPathResult != fs_error_t::OK ) {
			return buildPathResult;
		}
		std::error_code ec{};
		const std::filesystem::path parent_path = std::filesystem::path( szResolvedPath ).parent_path();
		if ( !parent_path.empty() ) {
			std::filesystem::create_directories( parent_path, ec );
			if ( ec ) {
				return fs_error_t::ERR_IO_ERROR;
			}
		}
	}
	std::FILE *pNativeFile = std::fopen( szResolvedPath, cMode );
	if ( pNativeFile == nullptr ) {
		return fs_error_t::ERR_FILE_OPEN_FAILED;
	}
	file.backend = file_backend_t::OS_FILE;
	file.pNativeHandle = pNativeFile;
	file.readable = readMode;
	file.writable = writeMode || appendMode;
	file.cursor = 0u;
	file.size = 0u;

	std::error_code ec{};

	if ( std::filesystem::exists( szResolvedPath, ec ) && !std::filesystem::is_directory( szResolvedPath, ec ) ) {
		file.size = static_cast<common::u64>( std::filesystem::file_size( szResolvedPath, ec ) );

		if ( ec ) {
			std::fclose( pNativeFile );
			file = {};
			return fs_error_t::ERR_IO_ERROR;
		}
	}
	if ( appendMode ) {
		file.cursor = file.size;
	}

	FS_RuntimeState().stats.nOpenCount++;
	return fs_error_t::OK;
}

/*
================
FS_Close
================
*/
fs_error_t FS_Close( file_t &file ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	if ( !FS_RuntimeState().initialized ) {
		return fs_error_t::ERR_NOT_INIT;
	}

	if ( file.pNativeHandle == nullptr ) {
		return fs_error_t::ERR_INVALID_HANDLE;
	}

	if ( file.backend == file_backend_t::PACKAGE_FILE ) {
		package_file_state_t *pPackageState = static_cast<package_file_state_t *>( file.pNativeHandle );
		delete[] pPackageState->data;
		delete pPackageState;
		file = {};
		FS_RuntimeState().stats.nCloseCount++;
		return fs_error_t::OK;
	}

	if ( file.backend != file_backend_t::OS_FILE ) {
		return fs_error_t::ERR_UNSUPPORTED_BACKEND;
	}

	std::FILE *pNativeFile = static_cast<std::FILE *>( file.pNativeHandle );
	if ( std::fclose( pNativeFile ) != 0 ) {
		file = {};
		return fs_error_t::ERR_FILE_CLOSE_FAILED;
	}

	file = {};
	FS_RuntimeState().stats.nCloseCount++;

	return fs_error_t::OK;
}

/*
================
FS_Read
================
*/
fs_error_t FS_Read( file_t &file, void *buffer, common::u64 nBytesToRead, common::u64 &nBytesReadOut ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	nBytesReadOut = 0u;

	if ( !FS_RuntimeState().initialized ) {
		return fs_error_t::ERR_NOT_INIT;
	}

	if ( file.pNativeHandle == nullptr ) {
		return fs_error_t::ERR_INVALID_HANDLE;
	}

	if ( !file.readable ) {
		return fs_error_t::ERR_PERMISSION_DENIED;
	}

	if ( nBytesToRead == 0u ) {
		return fs_error_t::OK;
	}

	if ( buffer == nullptr ) {
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}

	if ( file.backend == file_backend_t::PACKAGE_FILE ) {
		package_file_state_t *pPackageState = static_cast<package_file_state_t *>( file.pNativeHandle );
		const common::u64 remaining = file.cursor < pPackageState->size ? pPackageState->size - file.cursor : 0u;
		const common::u64 nBytesToCopy = std::min( nBytesToRead, remaining );
		if ( nBytesToCopy > 0u ) {
			std::memcpy( buffer, pPackageState->data + file.cursor, static_cast<common::usize>( nBytesToCopy ) );
		}
		nBytesReadOut = nBytesToCopy;
		file.cursor += nBytesToCopy;
		FS_RuntimeState().stats.nReadCount++;
		FS_RuntimeState().stats.nBytesRead += nBytesReadOut;
		return fs_error_t::OK;
	}

	if ( file.backend != file_backend_t::OS_FILE ) {
		return fs_error_t::ERR_UNSUPPORTED_BACKEND;
	}

	const std::size_t nReadSize = static_cast<std::size_t>( nBytesToRead );

	if ( static_cast<common::u64>( nReadSize ) != nBytesToRead ) {
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}

	std::FILE *pNativeFile = static_cast<std::FILE *>( file.pNativeHandle );
	const std::size_t nBytesRead = std::fread( buffer, 1u, nReadSize, pNativeFile );

	nBytesReadOut = static_cast<common::u64>( nBytesRead );
	file.cursor += nBytesReadOut;
	FS_RuntimeState().stats.nReadCount++;
	FS_RuntimeState().stats.nBytesRead += nBytesReadOut;

	if ( nBytesRead != nReadSize && std::ferror( pNativeFile ) != 0 ) {
		return fs_error_t::ERR_FILE_READ_FAILED;
	}

	return fs_error_t::OK;
}

/*
================
FS_Write
================
*/
fs_error_t FS_Write( file_t &file, const void *buffer, common::u64 nBytesToWrite, common::u64 &nBytesWrittenOut ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	nBytesWrittenOut = 0u;

	if ( !FS_RuntimeState().initialized ) {
		return fs_error_t::ERR_NOT_INIT;
	}
	if ( file.backend == file_backend_t::PACKAGE_FILE ) {
		return fs_error_t::ERR_PERMISSION_DENIED;
	}
	if ( file.backend != file_backend_t::OS_FILE ) {
		return fs_error_t::ERR_UNSUPPORTED_BACKEND;
	}
	if ( file.pNativeHandle == nullptr ) {
		return fs_error_t::ERR_INVALID_HANDLE;
	}
	if ( !file.writable ) {
		return fs_error_t::ERR_PERMISSION_DENIED;
	}
	if ( nBytesToWrite == 0u ) {
		return fs_error_t::OK;
	}
	if ( buffer == nullptr ) {
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}
	const std::size_t nWriteSize = static_cast<std::size_t>( nBytesToWrite );

	if ( static_cast<common::u64>( nWriteSize ) != nBytesToWrite ) {
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}
	std::FILE *pNativeFile = static_cast<std::FILE *>( file.pNativeHandle );
	const std::size_t nBytesWritten = std::fwrite( buffer, 1u, nWriteSize, pNativeFile );

	nBytesWrittenOut = static_cast<common::u64>( nBytesWritten );
	file.cursor += nBytesWrittenOut;
	FS_RuntimeState().stats.nWriteCount++;
	FS_RuntimeState().stats.nBytesWritten += nBytesWrittenOut;

	if ( file.cursor > file.size ) {
		file.size = file.cursor;
	}

	if ( nBytesWritten != nWriteSize ) {
		return fs_error_t::ERR_FILE_WRITE_FAILED;
	}

	return fs_error_t::OK;
}

/*
================
FS_Seek
================
*/
fs_error_t FS_Seek( file_t &file, common::i64 offset, seek_origin_t origin ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	if ( !FS_RuntimeState().initialized ) {
		return fs_error_t::ERR_NOT_INIT;
	}

	if ( file.pNativeHandle == nullptr ) {
		return fs_error_t::ERR_INVALID_HANDLE;
	}

	if ( file.backend == file_backend_t::PACKAGE_FILE ) {
		common::i64 base = 0;
		switch ( origin ) {
		case seek_origin_t::CYPHER_FILESYSTEM_SEEK_START:
			base = 0;
			break;
		case seek_origin_t::CYPHER_FILESYSTEM_SEEK_CURRENT:
			base = static_cast<common::i64>( file.cursor );
			break;
		case seek_origin_t::CYPHER_FILESYSTEM_SEEK_END:
			base = static_cast<common::i64>( file.size );
			break;
		default:
			return fs_error_t::ERR_INVALID_ARGUMENT;
		}

		const common::i64 target = base + offset;
		if ( target < 0 || static_cast<common::u64>( target ) > file.size ) {
			return fs_error_t::ERR_FILE_SEEK_FAILED;
		}

		file.cursor = static_cast<common::u64>( target );
		return fs_error_t::OK;
	}

	if ( file.backend != file_backend_t::OS_FILE ) {
		return fs_error_t::ERR_UNSUPPORTED_BACKEND;
	}

	int cOrigin{};

	switch ( origin ) {
	case seek_origin_t::CYPHER_FILESYSTEM_SEEK_START:
		cOrigin = SEEK_SET;
		break;
	case seek_origin_t::CYPHER_FILESYSTEM_SEEK_CURRENT:
		cOrigin = SEEK_CUR;
		break;
	case seek_origin_t::CYPHER_FILESYSTEM_SEEK_END:
		cOrigin = SEEK_END;
		break;
	default:
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}

	std::FILE *pNativeFile = static_cast<std::FILE *>( file.pNativeHandle );

	if ( std::fseek( pNativeFile, static_cast<long>( offset ), cOrigin ) != 0 ) {
		return fs_error_t::ERR_FILE_SEEK_FAILED;
	}

	const long position = std::ftell( pNativeFile );

	if ( position < 0 ) {
		return fs_error_t::ERR_FILE_TELL_FAILED;
	}

	file.cursor = static_cast<common::u64>( position );

	return fs_error_t::OK;
}

/*
================
FS_Tell
================
*/
fs_error_t FS_Tell( file_t &file, common::u64 &nOutPosition ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	nOutPosition = 0u;

	if ( !FS_RuntimeState().initialized ) {
		return fs_error_t::ERR_NOT_INIT;
	}

	if ( file.pNativeHandle == nullptr ) {
		return fs_error_t::ERR_INVALID_HANDLE;
	}

	if ( file.backend == file_backend_t::PACKAGE_FILE ) {
		nOutPosition = file.cursor;
		return fs_error_t::OK;
	}

	if ( file.backend != file_backend_t::OS_FILE ) {
		return fs_error_t::ERR_UNSUPPORTED_BACKEND;
	}

	std::FILE *pNativeFile = static_cast<std::FILE *>( file.pNativeHandle );
	const long position = std::ftell( pNativeFile );

	if ( position < 0 ) {
		return fs_error_t::ERR_FILE_TELL_FAILED;
	}

	nOutPosition = static_cast<common::u64>( position );
	file.cursor = nOutPosition;

	return fs_error_t::OK;
}

/*
================
FS_Flush
================
*/
fs_error_t FS_Flush( file_t &file ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	if ( !FS_RuntimeState().initialized ) {
		return fs_error_t::ERR_NOT_INIT;
	}
	if ( file.backend == file_backend_t::PACKAGE_FILE ) {
		return fs_error_t::ERR_PERMISSION_DENIED;
	}
	if ( file.backend != file_backend_t::OS_FILE ) {
		return fs_error_t::ERR_UNSUPPORTED_BACKEND;
	}
	if ( file.pNativeHandle == nullptr ) {
		return fs_error_t::ERR_INVALID_HANDLE;
	}
	if ( !file.writable ) {
		return fs_error_t::ERR_PERMISSION_DENIED;
	}

	std::FILE *pNativeFile = static_cast<std::FILE *>( file.pNativeHandle );
	return std::fflush( pNativeFile ) == 0 ? fs_error_t::OK : fs_error_t::ERR_FILE_WRITE_FAILED;
}

/*
================
FS_ReadEntireFile
================
*/
fs_error_t FS_ReadEntireFile( const char *szVirtualPath, void *buffer, common::u64 nBytesToRead, common::u64 &nBytesReadOut ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	nBytesReadOut = 0u;

	if ( !FS_RuntimeState().initialized ) {
		LOG_ERROR( log::channel_t::FS, "read entire file failed for '%s': filesystem is not initialized.", szVirtualPath ? szVirtualPath : "<null>" );
		return fs_error_t::ERR_NOT_INIT;
	}

	if ( buffer == nullptr ) {
		LOG_ERROR( log::channel_t::FS, "read entire file failed for '%s': output buffer is null.", szVirtualPath ? szVirtualPath : "<null>" );
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}

	file_t file{};
	fs_error_t err = FS_Open( szVirtualPath, open_mode_t::READ_BINARY, file );

	if ( err != fs_error_t::OK ) {
		LOG_ERROR( log::channel_t::FS, "read entire file failed for '%s': open failed: %s.", szVirtualPath ? szVirtualPath : "<null>", FS_ErrorDesc( err ) );
		return err;
	}

	if ( file.size > nBytesToRead ) {
		FS_Close( file );
		LOG_ERROR( log::channel_t::FS, "read entire file failed for '%s': buffer too small, file=%llu bytes, buffer=%llu bytes.",
		                  szVirtualPath ? szVirtualPath : "<null>",
		                  static_cast<unsigned long long>( file.size ),
		                  static_cast<unsigned long long>( nBytesToRead ) );
		return fs_error_t::ERR_BUFFER_TOO_SMALL;
	}

	if ( file.size == 0u ) {
		return FS_Close( file );
	}

	const common::u64 nExpectedSize = file.size;
	err = FS_Read( file, buffer, nExpectedSize, nBytesReadOut );
	const fs_error_t closeErr = FS_Close( file );

	if ( err != fs_error_t::OK ) {
		LOG_ERROR( log::channel_t::FS, "read entire file failed for '%s': read failed: %s.", szVirtualPath ? szVirtualPath : "<null>", FS_ErrorDesc( err ) );
		return err;
	}

	if ( closeErr != fs_error_t::OK ) {
		LOG_ERROR( log::channel_t::FS, "read entire file failed for '%s': close failed: %s.", szVirtualPath ? szVirtualPath : "<null>", FS_ErrorDesc( closeErr ) );
		return closeErr;
	}

	if ( nBytesReadOut != nExpectedSize ) {
		LOG_ERROR( log::channel_t::FS, "read entire file failed for '%s': short read, expected=%llu, actual=%llu.",
		                  szVirtualPath ? szVirtualPath : "<null>",
		                  static_cast<unsigned long long>( nExpectedSize ),
		                  static_cast<unsigned long long>( nBytesReadOut ) );
		return fs_error_t::ERR_FILE_READ_FAILED;
	}

	return fs_error_t::OK;
}

/*
================
FS_WriteEntireFile
================
*/
fs_error_t FS_WriteEntireFile( const char *szVirtualPath, const void *buffer, common::u64 nBytesToWrite ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	if ( nBytesToWrite != 0u && buffer == nullptr ) {
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}

	file_t file{};
	fs_error_t err = FS_Open( szVirtualPath, open_mode_t::WRITE_BINARY, file );
	if ( err != fs_error_t::OK ) {
		return err;
	}

	common::u64 nBytesWritten = 0u;
	err = FS_Write( file, buffer, nBytesToWrite, nBytesWritten );
	const fs_error_t flushErr = err == fs_error_t::OK ? FS_Flush( file ) : fs_error_t::OK;
	const fs_error_t closeErr = FS_Close( file );

	if ( err != fs_error_t::OK ) {
		return err;
	}
	if ( flushErr != fs_error_t::OK ) {
		return flushErr;
	}
	if ( closeErr != fs_error_t::OK ) {
		return closeErr;
	}
	return nBytesWritten == nBytesToWrite ? fs_error_t::OK : fs_error_t::ERR_FILE_WRITE_FAILED;
}

/*
================
FS_AppendEntireFile
================
*/
fs_error_t FS_AppendEntireFile( const char *szVirtualPath, const void *buffer, common::u64 nBytesToWrite ) {
	std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
	if ( nBytesToWrite != 0u && buffer == nullptr ) {
		return fs_error_t::ERR_INVALID_ARGUMENT;
	}

	file_t file{};
	fs_error_t err = FS_Open( szVirtualPath, open_mode_t::APPEND_BINARY, file );
	if ( err != fs_error_t::OK ) {
		return err;
	}

	common::u64 nBytesWritten = 0u;
	err = FS_Write( file, buffer, nBytesToWrite, nBytesWritten );
	const fs_error_t flushErr = err == fs_error_t::OK ? FS_Flush( file ) : fs_error_t::OK;
	const fs_error_t closeErr = FS_Close( file );

	if ( err != fs_error_t::OK ) {
		return err;
	}
	if ( flushErr != fs_error_t::OK ) {
		return flushErr;
	}
	if ( closeErr != fs_error_t::OK ) {
		return closeErr;
	}
	return nBytesWritten == nBytesToWrite ? fs_error_t::OK : fs_error_t::ERR_FILE_WRITE_FAILED;
}

} // namespace cypher::engine::fs
