//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Buffer.cpp
//  Purpose: Implements backend-neutral renderer buffer ownership and dispatch.
//  Details: Public handles resolve through a frontend record table before any
//           operation reaches the selected graphics backend.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Buffer.h"

#include "CypherRender_Local.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"

namespace cypher::engine::render
{

namespace
{

inline constexpr ::cypher::common::usize R_INITIAL_BUFFER_CAPACITY = 256u;

struct buffer_record_t {
    backend_buffer_t native{}; // Opaque object owned by the selected backend.
    render_buffer_info_t info{}; // Frontend policy and current mapping state.
};

struct buffer_system_t {
    ::cypher::common::handle_table_t<buffer_record_t> records{};
    bool initialized{ false };
};

buffer_system_t bufferSystem{};

bool R_IsRangeInside(
    const ::cypher::common::u64 totalSize,
    const render_buffer_range_t &range ) noexcept
{
    return range.byteSize != 0u &&
        range.offset <= totalSize &&
        range.byteSize <= totalSize - range.offset;
}

bool R_IsRangeInside(
    const render_buffer_range_t &outer,
    const render_buffer_range_t &inner ) noexcept
{
    if ( inner.byteSize == 0u || inner.offset < outer.offset ) {
        return false;
    }

    const ::cypher::common::u64 relativeOffset = inner.offset - outer.offset;
    return relativeOffset <= outer.byteSize &&
        inner.byteSize <= outer.byteSize - relativeOffset;
}

render_buffer_handle_t R_MakePublicBufferHandle(
    const ::cypher::common::handle32_t tableHandle ) noexcept
{
    return ::cypher::common::Cy_Handle64Make(
        ::cypher::common::Cy_Handle32Index( tableHandle ),
        ::cypher::common::Cy_Handle32Generation( tableHandle ),
        static_cast<::cypher::common::u32>( render_object_type_t::BUFFER ) );
}

render_error_t R_DecodePublicBufferHandle(
    const render_buffer_handle_t buffer,
    ::cypher::common::handle32_t &tableHandleOut ) noexcept
{
    tableHandleOut = ::cypher::common::CY_HANDLE32_INVALID;
    if ( !::cypher::common::Cy_Handle64IsValid( buffer ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    if ( ::cypher::common::Cy_Handle64Type( buffer ) !=
         static_cast<::cypher::common::u32>( render_object_type_t::BUFFER ) ) {
        return render_error_t::ERR_RESOURCE_TYPE_MISMATCH;
    }

    const ::cypher::common::u32 index =
        ::cypher::common::Cy_Handle64Index( buffer );
    const ::cypher::common::u32 generation =
        ::cypher::common::Cy_Handle64Generation( buffer );
    if ( index > ::cypher::common::CY_HANDLE32_INDEX_MAX || generation == 0u ||
         !::cypher::common::Cy_Handle32TryMake(
             index,
             generation,
             &tableHandleOut ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    return render_error_t::OK;
}

render_error_t R_ResolveBuffer(
    const render_buffer_handle_t buffer,
    ::cypher::common::handle32_t &tableHandleOut,
    buffer_record_t *&recordOut ) noexcept
{
    recordOut = nullptr;
    const render_error_t decodeResult = R_DecodePublicBufferHandle(
        buffer,
        tableHandleOut );
    if ( decodeResult != render_error_t::OK ) {
        return decodeResult;
    }

    recordOut = ::cypher::common::HandleTable_Get(
        &bufferSystem.records,
        tableHandleOut );
    return recordOut != nullptr
        ? render_error_t::OK
        : render_error_t::ERR_STALE_HANDLE;
}

bool R_IsMapAccessValidForMemory(
    const render_buffer_memory_t memory,
    const render_buffer_map_flags_t access ) noexcept
{
    switch ( memory ) {
        case render_buffer_memory_t::UPLOAD:
            return access == R_BUFFER_MAP_WRITE;
        case render_buffer_memory_t::READBACK:
            return access == R_BUFFER_MAP_READ;
        case render_buffer_memory_t::DEVICE_LOCAL:
        case render_buffer_memory_t::COUNT:
        default:
            return false;
    }
}

} // namespace

render_error_t R_BufferSystemInit() noexcept
{
    if ( bufferSystem.initialized ) {
        return render_error_t::ERR_ALREADY_INITIALIZED;
    }

    if ( !::cypher::common::HandleTable_Init(
            &bufferSystem.records,
            ::cypher::common::Allocator_GetSystem(),
            R_INITIAL_BUFFER_CAPACITY ) ) {
        return render_error_t::ERR_OUT_OF_MEMORY;
    }

    bufferSystem.initialized = true;
    return render_error_t::OK;
}

render_error_t R_BufferSystemShutdown() noexcept
{
    if ( !bufferSystem.initialized ) {
        return render_error_t::OK;
    }

    render_error_t firstError = render_error_t::OK;
    if ( tr.backend == nullptr ) {
        firstError = render_error_t::ERR_INTERNAL_ERROR;
    } else {
        (void)::cypher::common::HandleTable_ForEach(
            &bufferSystem.records,
            [&]( ::cypher::common::handle32_t, buffer_record_t &record ) noexcept {
                if ( record.info.mapped ) {
                    const render_error_t unmapResult = tr.backend->UnmapBuffer(
                        record.native,
                        tr.backend->state );
                    if ( firstError == render_error_t::OK &&
                         unmapResult != render_error_t::OK ) {
                        firstError = unmapResult;
                    }
                    record.info.mapped = false;
                    record.info.mapAccess = R_BUFFER_MAP_NONE;
                    record.info.mappedRange = {};
                }

                const render_error_t destroyResult = tr.backend->DestroyBuffer(
                    record.native,
                    tr.backend->state );
                if ( firstError == render_error_t::OK &&
                     destroyResult != render_error_t::OK ) {
                    firstError = destroyResult;
                }
                record.native = R_INVALID_BACKEND_BUFFER;
                return ::cypher::common::CY_TRUE;
            } );
    }

    ::cypher::common::HandleTable_Shutdown( &bufferSystem.records );
    bufferSystem.initialized = false;
    return firstError;
}

render_error_t R_ValidateBufferDesc(
    const render_buffer_desc_t &description,
    const render_buffer_data_t *initialData ) noexcept
{
    if ( description.byteSize == 0u ||
         description.usage == R_BUFFER_USAGE_NONE ||
         ( description.usage & ~R_BUFFER_USAGE_KNOWN_MASK ) != 0u ||
         description.updatePolicy >= render_buffer_update_t::COUNT ||
         description.memory >= render_buffer_memory_t::COUNT ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( initialData == nullptr ) {
        return render_error_t::OK;
    }

    if ( initialData->bytes == nullptr || initialData->byteSize == 0u ||
         initialData->byteSize != description.byteSize ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    return render_error_t::OK;
}

render_error_t R_CreateBuffer(
    const render_buffer_desc_t &description,
    const render_buffer_data_t *initialData,
    render_buffer_handle_t *bufferOut ) noexcept
{
    if ( bufferOut == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    *bufferOut = R_INVALID_BUFFER;
    if ( !tr.initialized || tr.backend == nullptr || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    const render_error_t descriptorResult = R_ValidateBufferDesc(
        description,
        initialData );
    if ( descriptorResult != render_error_t::OK ) {
        return descriptorResult;
    }

    backend_buffer_t nativeBuffer{};
    const render_error_t createResult = tr.backend->CreateBuffer(
        description,
        initialData,
        nativeBuffer,
        tr.backend->state );
    if ( createResult != render_error_t::OK ) {
        return createResult;
    }
    if ( !R_IsBackendBufferValid( nativeBuffer ) ) {
        return render_error_t::ERR_INTERNAL_ERROR;
    }

    buffer_record_t record{};
    record.native = nativeBuffer;
    record.info.byteSize = description.byteSize;
    record.info.usage = description.usage;
    record.info.updatePolicy = description.updatePolicy;
    record.info.memory = description.memory;

    const ::cypher::common::handle32_t tableHandle =
        ::cypher::common::HandleTable_Insert( &bufferSystem.records, record );
    if ( !::cypher::common::Cy_Handle32IsValid( tableHandle ) ) {
        (void)tr.backend->DestroyBuffer( nativeBuffer, tr.backend->state );
        return ::cypher::common::HandleTable_Count( &bufferSystem.records ) >=
            ::cypher::common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED
            : render_error_t::ERR_OUT_OF_MEMORY;
    }

    const render_buffer_handle_t publicHandle =
        R_MakePublicBufferHandle( tableHandle );
    if ( !::cypher::common::Cy_Handle64IsValid( publicHandle ) ) {
        (void)::cypher::common::HandleTable_Remove(
            &bufferSystem.records,
            tableHandle );
        (void)tr.backend->DestroyBuffer( nativeBuffer, tr.backend->state );
        return render_error_t::ERR_INTERNAL_ERROR;
    }

    *bufferOut = publicHandle;
    return render_error_t::OK;
}

render_error_t R_UpdateBuffer(
    const render_buffer_handle_t buffer,
    const ::cypher::common::u64 destinationOffset,
    const render_buffer_data_t &sourceData ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( sourceData.bytes == nullptr || sourceData.byteSize == 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveBuffer(
        buffer,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;
    if ( record->info.mapped ) return render_error_t::ERR_RESOURCE_BUSY;
    if ( record->info.updatePolicy == render_buffer_update_t::IMMUTABLE ||
         record->info.memory == render_buffer_memory_t::READBACK ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( !R_IsRangeInside(
            record->info.byteSize,
            { destinationOffset, sourceData.byteSize } ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    return tr.backend->UpdateBuffer(
        record->native,
        destinationOffset,
        sourceData,
        tr.backend->state );
}

render_error_t R_MapBuffer(
    const render_buffer_handle_t buffer,
    const render_buffer_range_t &range,
    const render_buffer_map_flags_t access,
    render_buffer_mapping_t *mappingOut ) noexcept
{
    if ( mappingOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *mappingOut = {};
    if ( !tr.initialized || tr.backend == nullptr || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( access == R_BUFFER_MAP_NONE ||
         ( access & ~R_BUFFER_MAP_KNOWN_MASK ) != 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveBuffer(
        buffer,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;
    if ( record->info.mapped ) return render_error_t::ERR_RESOURCE_BUSY;
    if ( !R_IsRangeInside( record->info.byteSize, range ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !R_IsMapAccessValidForMemory( record->info.memory, access ) ||
         ( record->info.updatePolicy == render_buffer_update_t::IMMUTABLE &&
           ( access & R_BUFFER_MAP_WRITE ) != 0u ) ) {
        return render_error_t::ERR_INVALID_STATE;
    }

    render_buffer_mapping_t mapping{};
    const render_error_t mapResult = tr.backend->MapBuffer(
        record->native,
        range,
        access,
        mapping,
        tr.backend->state );
    if ( mapResult != render_error_t::OK ) return mapResult;
    if ( mapping.bytes == nullptr || mapping.byteSize != range.byteSize ) {
        (void)tr.backend->UnmapBuffer( record->native, tr.backend->state );
        return render_error_t::ERR_INTERNAL_ERROR;
    }

    record->info.mapped = true;
    record->info.mapAccess = access;
    record->info.mappedRange = range;
    *mappingOut = mapping;
    return render_error_t::OK;
}

render_error_t R_FlushMappedBuffer(
    const render_buffer_handle_t buffer,
    const render_buffer_range_t &range ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveBuffer(
        buffer,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;
    if ( !record->info.mapped ||
         ( record->info.mapAccess & R_BUFFER_MAP_WRITE ) == 0u ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( !R_IsRangeInside( record->info.mappedRange, range ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    return tr.backend->FlushMappedBuffer(
        record->native,
        range,
        tr.backend->state );
}

render_error_t R_InvalidateMappedBuffer(
    const render_buffer_handle_t buffer,
    const render_buffer_range_t &range ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveBuffer(
        buffer,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;
    if ( !record->info.mapped ||
         ( record->info.mapAccess & R_BUFFER_MAP_READ ) == 0u ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( !R_IsRangeInside( record->info.mappedRange, range ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    return tr.backend->InvalidateMappedBuffer(
        record->native,
        range,
        tr.backend->state );
}

render_error_t R_UnmapBuffer( const render_buffer_handle_t buffer ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveBuffer(
        buffer,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;
    if ( !record->info.mapped ) return render_error_t::ERR_INVALID_STATE;

    const render_error_t unmapResult = tr.backend->UnmapBuffer(
        record->native,
        tr.backend->state );
    record->info.mapped = false;
    record->info.mapAccess = R_BUFFER_MAP_NONE;
    record->info.mappedRange = {};
    return unmapResult;
}

render_error_t R_DestroyBuffer( const render_buffer_handle_t buffer ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveBuffer(
        buffer,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;
    if ( record->info.mapped ) return render_error_t::ERR_RESOURCE_BUSY;

    const render_error_t destroyResult = tr.backend->DestroyBuffer(
        record->native,
        tr.backend->state );
    if ( destroyResult != render_error_t::OK ) return destroyResult;

    return ::cypher::common::HandleTable_Remove(
        &bufferSystem.records,
        tableHandle )
        ? render_error_t::OK
        : render_error_t::ERR_INTERNAL_ERROR;
}

bool R_IsBufferValid( const render_buffer_handle_t buffer ) noexcept
{
    if ( !tr.initialized || !bufferSystem.initialized ) return false;

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    return R_ResolveBuffer( buffer, tableHandle, record ) == render_error_t::OK;
}

render_error_t R_GetBufferInfo(
    const render_buffer_handle_t buffer,
    render_buffer_info_t *infoOut ) noexcept
{
    if ( infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *infoOut = {};
    if ( !tr.initialized || !bufferSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    ::cypher::common::handle32_t tableHandle{};
    buffer_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveBuffer(
        buffer,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;

    *infoOut = record->info;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
