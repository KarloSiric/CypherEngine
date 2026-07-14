//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherPak/CypherPak_Error.h
//  Purpose: Declares the CypherPak Pak Error module.
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

#ifndef CYPHER_ENGINE_PAK_ERROR_H
#define CYPHER_ENGINE_PAK_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon.h"
#include "CypherCommon_Error.h"

namespace cypher::engine::pak
{

/*
================
CypherPak Error Codes
================
*/
enum class pak_error_t : common::u8 {
    OK = 0,

    ERR_INVALID_ARGUMENT,
    ERR_INVALID_PATH,
    ERR_INVALID_HANDLE,
    ERR_INVALID_STATE,

    ERR_FILE_OPEN_FAILED,
    ERR_FILE_CLOSE_FAILED,
    ERR_FILE_READ_FAILED,
    ERR_FILE_WRITE_FAILED,
    ERR_FILE_SEEK_FAILED,

    ERR_BAD_MAGIC,
    ERR_INVALID_HEADER,
    ERR_UNSUPPORTED_VERSION,
    ERR_UNSUPPORTED_FLAGS,
    ERR_ENDIAN_MISMATCH,
    ERR_ARCHIVE_CORRUPT,

    ERR_INVALID_INDEX,
    ERR_ENTRY_NOT_FOUND,
    ERR_DUPLICATE_ENTRY,
    ERR_PATH_NOT_FOUND,

    ERR_BUFFER_TOO_SMALL,
    ERR_OUT_OF_MEMORY,
    ERR_INTEGER_OVERFLOW,

    ERR_UNSUPPORTED_COMPRESSION,
    ERR_COMPRESSION_FAILED,
    ERR_DECOMPRESSION_FAILED,
    ERR_CHECKSUM_MISMATCH,

    ERR_PERMISSION_DENIED,
    ERR_IO_ERROR,
    ERR_NOT_IMPLEMENTED
};

/*
================
CypherPak Error Helpers
================
*/
constexpr inline const char *CypherPak_ErrorName( const pak_error_t error )
{
    switch ( error ) {
    case pak_error_t::OK:
        return "OK";
    case pak_error_t::ERR_INVALID_ARGUMENT:
        return "ERR_INVALID_ARGUMENT";
    case pak_error_t::ERR_INVALID_PATH:
        return "ERR_INVALID_PATH";
    case pak_error_t::ERR_INVALID_HANDLE:
        return "ERR_INVALID_HANDLE";
    case pak_error_t::ERR_INVALID_STATE:
        return "ERR_INVALID_STATE";
    case pak_error_t::ERR_FILE_OPEN_FAILED:
        return "ERR_FILE_OPEN_FAILED";
    case pak_error_t::ERR_FILE_CLOSE_FAILED:
        return "ERR_FILE_CLOSE_FAILED";
    case pak_error_t::ERR_FILE_READ_FAILED:
        return "ERR_FILE_READ_FAILED";
    case pak_error_t::ERR_FILE_WRITE_FAILED:
        return "ERR_FILE_WRITE_FAILED";
    case pak_error_t::ERR_FILE_SEEK_FAILED:
        return "ERR_FILE_SEEK_FAILED";
    case pak_error_t::ERR_BAD_MAGIC:
        return "ERR_BAD_MAGIC";
    case pak_error_t::ERR_INVALID_HEADER:
        return "ERR_INVALID_HEADER";
    case pak_error_t::ERR_UNSUPPORTED_VERSION:
        return "ERR_UNSUPPORTED_VERSION";
    case pak_error_t::ERR_UNSUPPORTED_FLAGS:
        return "ERR_UNSUPPORTED_FLAGS";
    case pak_error_t::ERR_ENDIAN_MISMATCH:
        return "ERR_ENDIAN_MISMATCH";
    case pak_error_t::ERR_ARCHIVE_CORRUPT:
        return "ERR_ARCHIVE_CORRUPT";
    case pak_error_t::ERR_INVALID_INDEX:
        return "ERR_INVALID_INDEX";
    case pak_error_t::ERR_ENTRY_NOT_FOUND:
        return "ERR_ENTRY_NOT_FOUND";
    case pak_error_t::ERR_DUPLICATE_ENTRY:
        return "ERR_DUPLICATE_ENTRY";
    case pak_error_t::ERR_PATH_NOT_FOUND:
        return "ERR_PATH_NOT_FOUND";
    case pak_error_t::ERR_BUFFER_TOO_SMALL:
        return "ERR_BUFFER_TOO_SMALL";
    case pak_error_t::ERR_OUT_OF_MEMORY:
        return "ERR_OUT_OF_MEMORY";
    case pak_error_t::ERR_INTEGER_OVERFLOW:
        return "ERR_INTEGER_OVERFLOW";
    case pak_error_t::ERR_UNSUPPORTED_COMPRESSION:
        return "ERR_UNSUPPORTED_COMPRESSION";
    case pak_error_t::ERR_COMPRESSION_FAILED:
        return "ERR_COMPRESSION_FAILED";
    case pak_error_t::ERR_DECOMPRESSION_FAILED:
        return "ERR_DECOMPRESSION_FAILED";
    case pak_error_t::ERR_CHECKSUM_MISMATCH:
        return "ERR_CHECKSUM_MISMATCH";
    case pak_error_t::ERR_PERMISSION_DENIED:
        return "ERR_PERMISSION_DENIED";
    case pak_error_t::ERR_IO_ERROR:
        return "ERR_IO_ERROR";
    case pak_error_t::ERR_NOT_IMPLEMENTED:
        return "ERR_NOT_IMPLEMENTED";
    default:
        return "ERR_UNKNOWN";
    }
}

constexpr inline const char *CypherPak_ErrorDesc( const pak_error_t error )
{
    switch ( error ) {
    case pak_error_t::OK:
        return "operation completed successfully";
    case pak_error_t::ERR_INVALID_ARGUMENT:
        return "invalid CypherPak argument";
    case pak_error_t::ERR_INVALID_PATH:
        return "invalid CypherPak path";
    case pak_error_t::ERR_INVALID_HANDLE:
        return "invalid CypherPak handle";
    case pak_error_t::ERR_INVALID_STATE:
        return "invalid CypherPak object state";
    case pak_error_t::ERR_FILE_OPEN_FAILED:
        return "archive file open failed";
    case pak_error_t::ERR_FILE_CLOSE_FAILED:
        return "archive file close failed";
    case pak_error_t::ERR_FILE_READ_FAILED:
        return "archive file read failed";
    case pak_error_t::ERR_FILE_WRITE_FAILED:
        return "archive file write failed";
    case pak_error_t::ERR_FILE_SEEK_FAILED:
        return "archive file seek failed";
    case pak_error_t::ERR_BAD_MAGIC:
        return "archive magic does not match CypherPak";
    case pak_error_t::ERR_INVALID_HEADER:
        return "archive header is invalid";
    case pak_error_t::ERR_UNSUPPORTED_VERSION:
        return "archive version is unsupported";
    case pak_error_t::ERR_UNSUPPORTED_FLAGS:
        return "archive flags are unsupported";
    case pak_error_t::ERR_ENDIAN_MISMATCH:
        return "archive endian tag is invalid";
    case pak_error_t::ERR_ARCHIVE_CORRUPT:
        return "archive data is corrupt";
    case pak_error_t::ERR_INVALID_INDEX:
        return "archive index is invalid";
    case pak_error_t::ERR_ENTRY_NOT_FOUND:
        return "archive entry was not found";
    case pak_error_t::ERR_DUPLICATE_ENTRY:
        return "archive contains duplicate entries";
    case pak_error_t::ERR_PATH_NOT_FOUND:
        return "archive path was not found";
    case pak_error_t::ERR_BUFFER_TOO_SMALL:
        return "provided CypherPak buffer is too small";
    case pak_error_t::ERR_OUT_OF_MEMORY:
        return "CypherPak allocation failed";
    case pak_error_t::ERR_INTEGER_OVERFLOW:
        return "CypherPak integer calculation overflowed";
    case pak_error_t::ERR_UNSUPPORTED_COMPRESSION:
        return "archive compression mode is unsupported";
    case pak_error_t::ERR_COMPRESSION_FAILED:
        return "archive compression failed";
    case pak_error_t::ERR_DECOMPRESSION_FAILED:
        return "archive decompression failed";
    case pak_error_t::ERR_CHECKSUM_MISMATCH:
        return "archive checksum verification failed";
    case pak_error_t::ERR_PERMISSION_DENIED:
        return "CypherPak permission denied";
    case pak_error_t::ERR_IO_ERROR:
        return "CypherPak IO error";
    case pak_error_t::ERR_NOT_IMPLEMENTED:
        return "CypherPak operation is declared but not implemented yet";
    default:
        return "unknown CypherPak error";
    }
}

constexpr inline common::error_t CypherPak_ErrorCode( const pak_error_t error )
{
    return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_PAK, static_cast<common::u16>( error ) );
}

}       // namespace cypher::engine::pak

#endif // CYPHER_ENGINE_PAK_ERROR_H
