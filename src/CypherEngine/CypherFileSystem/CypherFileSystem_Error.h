//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherFileSystem/CypherFileSystem_Error.h
//  Purpose: Declares the CypherFileSystem FileSystem Error module.
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

#ifndef CYPHER_ENGINE_FILESYSTEM_ERROR_H
#define CYPHER_ENGINE_FILESYSTEM_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Error.h"

namespace cypher::engine::fs
{

/*
================
Filesystem Error Codes
================
*/
enum class fs_error_t : common::u8 {
    OK = 0,

    ERR_NOT_INIT,
    ERR_IS_INIT,

    ERR_INVALID_PATH,
    ERR_INVALID_ROOT,
    ERR_INVALID_MODE,
    ERR_INVALID_HANDLE,
    ERR_INVALID_ARGUMENT,
    ERR_INVALID_FLAGS,
    ERR_WRITE_PATH_NOT_SET,

    ERR_TOO_MANY_MOUNTS,
    ERR_TOO_MANY_WATCHES,
    ERR_MOUNT_NOT_FOUND,
    ERR_PATH_NOT_FOUND,
    ERR_ALREADY_EXISTS,
    ERR_NOT_DIRECTORY,
    ERR_NOT_FILE,
    ERR_DIRECTORY_NOT_EMPTY,

    ERR_FILE_OPEN_FAILED,
    ERR_FILE_CLOSE_FAILED,
    ERR_FILE_READ_FAILED,
    ERR_FILE_WRITE_FAILED,
    ERR_FILE_SEEK_FAILED,
    ERR_FILE_TELL_FAILED,

    ERR_BUFFER_TOO_SMALL,
    ERR_WATCH_QUEUE_FULL,
    ERR_OUT_OF_MEMORY,
    ERR_UNSUPPORTED_BACKEND,
    ERR_NOT_IMPLEMENTED,
    ERR_PERMISSION_DENIED,
    ERR_CANCELLED,
    ERR_IO_ERROR
};

/*
================
Filesystem Error Helpers
================
*/
constexpr inline const char *CypherFileSystem_ErrorName( const fs_error_t error ) {
    switch ( error ) {
    case fs_error_t::OK:
        return "OK";
    case fs_error_t::ERR_NOT_INIT:
        return "ERR_NOT_INIT";
    case fs_error_t::ERR_IS_INIT:
        return "ERR_IS_INIT";
    case fs_error_t::ERR_INVALID_PATH:
        return "ERR_INVALID_PATH";
    case fs_error_t::ERR_INVALID_ROOT:
        return "ERR_INVALID_ROOT";
    case fs_error_t::ERR_INVALID_MODE:
        return "ERR_INVALID_MODE";
    case fs_error_t::ERR_INVALID_HANDLE:
        return "ERR_INVALID_HANDLE";
    case fs_error_t::ERR_INVALID_ARGUMENT:
        return "ERR_INVALID_ARGUMENT";
    case fs_error_t::ERR_INVALID_FLAGS:
        return "ERR_INVALID_FLAGS";
    case fs_error_t::ERR_WRITE_PATH_NOT_SET:
        return "ERR_WRITE_PATH_NOT_SET";
    case fs_error_t::ERR_TOO_MANY_MOUNTS:
        return "ERR_TOO_MANY_MOUNTS";
    case fs_error_t::ERR_TOO_MANY_WATCHES:
        return "ERR_TOO_MANY_WATCHES";
    case fs_error_t::ERR_MOUNT_NOT_FOUND:
        return "ERR_MOUNT_NOT_FOUND";
    case fs_error_t::ERR_PATH_NOT_FOUND:
        return "ERR_PATH_NOT_FOUND";
    case fs_error_t::ERR_ALREADY_EXISTS:
        return "ERR_ALREADY_EXISTS";
    case fs_error_t::ERR_NOT_DIRECTORY:
        return "ERR_NOT_DIRECTORY";
    case fs_error_t::ERR_NOT_FILE:
        return "ERR_NOT_FILE";
    case fs_error_t::ERR_DIRECTORY_NOT_EMPTY:
        return "ERR_DIRECTORY_NOT_EMPTY";
    case fs_error_t::ERR_FILE_OPEN_FAILED:
        return "ERR_FILE_OPEN_FAILED";
    case fs_error_t::ERR_FILE_CLOSE_FAILED:
        return "ERR_FILE_CLOSE_FAILED";
    case fs_error_t::ERR_FILE_READ_FAILED:
        return "ERR_FILE_READ_FAILED";
    case fs_error_t::ERR_FILE_WRITE_FAILED:
        return "ERR_FILE_WRITE_FAILED";
    case fs_error_t::ERR_FILE_SEEK_FAILED:
        return "ERR_FILE_SEEK_FAILED";
    case fs_error_t::ERR_FILE_TELL_FAILED:
        return "ERR_FILE_TELL_FAILED";
    case fs_error_t::ERR_BUFFER_TOO_SMALL:
        return "ERR_BUFFER_TOO_SMALL";
    case fs_error_t::ERR_WATCH_QUEUE_FULL:
        return "ERR_WATCH_QUEUE_FULL";
    case fs_error_t::ERR_OUT_OF_MEMORY:
        return "ERR_OUT_OF_MEMORY";
    case fs_error_t::ERR_UNSUPPORTED_BACKEND:
        return "ERR_UNSUPPORTED_BACKEND";
    case fs_error_t::ERR_NOT_IMPLEMENTED:
        return "ERR_NOT_IMPLEMENTED";
    case fs_error_t::ERR_PERMISSION_DENIED:
        return "ERR_PERMISSION_DENIED";
    case fs_error_t::ERR_CANCELLED:
        return "ERR_CANCELLED";
    case fs_error_t::ERR_IO_ERROR:
        return "ERR_IO_ERROR";
    default:
        return "ERR_UNKNOWN";
    }
}

constexpr inline const char *CypherFileSystem_ErrorDesc( const fs_error_t error ) {
    switch ( error ) {
    case fs_error_t::OK:
        return "operation completed successfully";
    case fs_error_t::ERR_NOT_INIT:
        return "fs subsystem is not initialized";
    case fs_error_t::ERR_IS_INIT:
        return "fs subsystem is already initialized";
    case fs_error_t::ERR_INVALID_PATH:
        return "invalid filesystem path";
    case fs_error_t::ERR_INVALID_ROOT:
        return "invalid filesystem root";
    case fs_error_t::ERR_INVALID_MODE:
        return "invalid filesystem open mode";
    case fs_error_t::ERR_INVALID_HANDLE:
        return "invalid filesystem file handle";
    case fs_error_t::ERR_INVALID_FLAGS:
        return "invalid filesystem flags set";
    case fs_error_t::ERR_INVALID_ARGUMENT:
        return "invalid filesystem argument";
    case fs_error_t::ERR_WRITE_PATH_NOT_SET:
        return "filesystem write path is not set";
    case fs_error_t::ERR_TOO_MANY_MOUNTS:
        return "filesystem mount table is full";
    case fs_error_t::ERR_TOO_MANY_WATCHES:
        return "filesystem watch table is full";
    case fs_error_t::ERR_MOUNT_NOT_FOUND:
        return "filesystem mount was not found";
    case fs_error_t::ERR_PATH_NOT_FOUND:
        return "filesystem path was not found";
    case fs_error_t::ERR_ALREADY_EXISTS:
        return "filesystem path already exists";
    case fs_error_t::ERR_NOT_DIRECTORY:
        return "filesystem path is not a directory";
    case fs_error_t::ERR_NOT_FILE:
        return "filesystem path is not a file";
    case fs_error_t::ERR_DIRECTORY_NOT_EMPTY:
        return "filesystem directory is not empty";
    case fs_error_t::ERR_FILE_OPEN_FAILED:
        return "file open failed";
    case fs_error_t::ERR_FILE_CLOSE_FAILED:
        return "file close failed";
    case fs_error_t::ERR_FILE_READ_FAILED:
        return "file read failed";
    case fs_error_t::ERR_FILE_WRITE_FAILED:
        return "file write failed";
    case fs_error_t::ERR_FILE_SEEK_FAILED:
        return "file seek failed";
    case fs_error_t::ERR_FILE_TELL_FAILED:
        return "file tell failed";
    case fs_error_t::ERR_BUFFER_TOO_SMALL:
        return "provided filesystem buffer is too small";
    case fs_error_t::ERR_WATCH_QUEUE_FULL:
        return "filesystem watch event queue is full";
    case fs_error_t::ERR_OUT_OF_MEMORY:
        return "filesystem memory allocation failed";
    case fs_error_t::ERR_UNSUPPORTED_BACKEND:
        return "filesystem backend is unsupported for this operation";
    case fs_error_t::ERR_NOT_IMPLEMENTED:
        return "filesystem operation is declared but not implemented yet";
    case fs_error_t::ERR_PERMISSION_DENIED:
        return "filesystem permission denied";
    case fs_error_t::ERR_CANCELLED:
        return "filesystem operation was cancelled";
    case fs_error_t::ERR_IO_ERROR:
        return "filesystem IO error";
    default:
        return "unknown filesystem error";
    }
}

constexpr inline common::error_t CypherFileSystem_ErrorCode( fs_error_t  error ) {
    return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_FS, static_cast<common::u16>( error ) );
}

}       // namespace cypher::engine::fs

#endif // CYPHER_ENGINE_FILESYSTEM_ERROR_H
