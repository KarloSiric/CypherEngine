//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Error.h
//  Purpose: Declares the CypherMemory Memory Error module.
//  Details: This file participates in the engine allocation layer for arenas, pools,
//           buckets, scratch memory, and diagnostics. Keep ownership and lifetime
//           rules explicit because allocator bugs corrupt everything above them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MEMORY_ERROR_H
#define CYPHER_ENGINE_MEMORY_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "Engine/CypherCommon_Error.h"

namespace cypher::engine::memory
{

enum class mem_error_t : common::u8 {
    OK = 0,

    ERR_INVALID_ARGUMENT,
    ERR_OUT_OF_MEMORY,
    ERR_MEMORY_ALLOCATION,
    ERR_ALREADY_INITIALIZED,
    ERR_NOT_INITIALIZED,
    ERR_INVALID_ALIGNMENT,
    ERR_INVALID_CAPACITY,
    ERR_INVALID_MARKER,
    ERR_INVALID_POINTER,
    ERR_DOUBLE_FREE,
    ERR_INTEGER_OVERFLOW,
    ERR_BUFFER_TOO_SMALL,
    ERR_EXTERNAL_BUFFER_REQUIRED,

    ERR_MEMORY_RESERVE,
    ERR_MEMORY_COMMIT,
    ERR_MEMORY_DECOMMIT,
    ERR_MEMORY_RELEASE
};

constexpr inline const char *CypherMemory_ErrorName( const mem_error_t error )
{
    switch ( error ) {
    case mem_error_t::OK:
        return "OK";
    case mem_error_t::ERR_INVALID_ARGUMENT:
        return "ERR_INVALID_ARGUMENT";
    case mem_error_t::ERR_OUT_OF_MEMORY:
        return "ERR_OUT_OF_MEMORY";
    case mem_error_t::ERR_MEMORY_ALLOCATION:
        return "ERR_MEMORY_ALLOCATION";
    case mem_error_t::ERR_ALREADY_INITIALIZED:
        return "ERR_ALREADY_INITIALIZED";
    case mem_error_t::ERR_NOT_INITIALIZED:
        return "ERR_NOT_INITIALIZED";
    case mem_error_t::ERR_INVALID_ALIGNMENT:
        return "ERR_INVALID_ALIGNMENT";
    case mem_error_t::ERR_INVALID_CAPACITY:
        return "ERR_INVALID_CAPACITY";
    case mem_error_t::ERR_INVALID_MARKER:
        return "ERR_INVALID_MARKER";
    case mem_error_t::ERR_INVALID_POINTER:
        return "ERR_INVALID_POINTER";
    case mem_error_t::ERR_DOUBLE_FREE:
        return "ERR_DOUBLE_FREE";
    case mem_error_t::ERR_INTEGER_OVERFLOW:
        return "ERR_INTEGER_OVERFLOW";
    case mem_error_t::ERR_BUFFER_TOO_SMALL:
        return "ERR_BUFFER_TOO_SMALL";
    case mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED:
        return "ERR_EXTERNAL_BUFFER_REQUIRED";
    case mem_error_t::ERR_MEMORY_RESERVE:
        return "ERR_MEMORY_RESERVE";
    case mem_error_t::ERR_MEMORY_COMMIT:
        return "ERR_MEMORY_COMMIT";
    case mem_error_t::ERR_MEMORY_DECOMMIT:
        return "ERR_MEMORY_DECOMMIT";
    case mem_error_t::ERR_MEMORY_RELEASE:
        return "ERR_MEMORY_RELEASE";
    default:
        return "ERR_UNKNOWN";
    }
}

constexpr inline const char *CypherMemory_ErrorDesc( const mem_error_t error )
{
    switch ( error ) {
    case mem_error_t::OK:
        return "operation completed successfully";
    case mem_error_t::ERR_INVALID_ARGUMENT:
        return "invalid argument passed to memory subsystem";
    case mem_error_t::ERR_OUT_OF_MEMORY:
        return "arena or allocator does not have enough remaining memory";
    case mem_error_t::ERR_MEMORY_ALLOCATION:
        return "memory allocation failure";
    case mem_error_t::ERR_ALREADY_INITIALIZED:
        return "memory object is already initialized";
    case mem_error_t::ERR_NOT_INITIALIZED:
        return "memory object is not initialized";
    case mem_error_t::ERR_INVALID_ALIGNMENT:
        return "requested alignment is invalid";
    case mem_error_t::ERR_INVALID_CAPACITY:
        return "requested allocator capacity is invalid";
    case mem_error_t::ERR_INVALID_MARKER:
        return "arena marker is invalid for this arena state";
    case mem_error_t::ERR_INVALID_POINTER:
        return "pointer does not belong to the allocator";
    case mem_error_t::ERR_DOUBLE_FREE:
        return "pointer has already been freed";
    case mem_error_t::ERR_INTEGER_OVERFLOW:
        return "allocator size calculation overflowed";
    case mem_error_t::ERR_BUFFER_TOO_SMALL:
        return "provided buffer is too small";
    case mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED:
        return "external memory buffer is required";
    case mem_error_t::ERR_MEMORY_RESERVE:
        return "error reserving virtual memory paging";
    case mem_error_t::ERR_MEMORY_COMMIT:
        return "error committing memory";
    case mem_error_t::ERR_MEMORY_DECOMMIT:
        return "error decommitting memory";
    case mem_error_t::ERR_MEMORY_RELEASE:
        return "error releasing committed memory";
    default:
        return "unknown memory subsystem error";
    }
}

constexpr inline common::error_t CypherMemory_ErrorCode( const mem_error_t code )
{
    return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_MEMORY, static_cast<common::u16>( code ) );
}

}       // namespace cypher::engine::memory

#endif // CYPHER_ENGINE_MEMORY_ERROR_H
