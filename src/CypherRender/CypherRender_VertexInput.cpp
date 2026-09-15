//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_VertexInput.cpp
//  Purpose: Implements vertex-layout and geometry-stream object ownership.
//  Details: Public buffer handles are validated and retained before native
//           input state is created through the selected renderer backend.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_VertexInput.h"

#include "CypherRender_Local.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"

namespace cypher::engine::render
{

namespace
{

inline constexpr ::cypher::common::usize R_INITIAL_VERTEX_INPUT_CAPACITY = 128u;

struct vertex_input_record_t {
    backend_vertex_input_t native{};
    render_vertex_input_info_t info{};
};

struct vertex_input_system_t {
    ::cypher::common::handle_table_t<vertex_input_record_t> records{};
    bool initialized{ false };
};

vertex_input_system_t vertexInputSystem{};

const render_vertex_binding_desc_t *R_FindLayoutBinding(
    const render_vertex_layout_t &layout,
    const ::cypher::common::u8 binding ) noexcept
{
    for ( ::cypher::common::u32 i = 0u; i < layout.bindingCount; ++i ) {
        if ( layout.bindings[i].binding == binding ) {
            return &layout.bindings[i];
        }
    }
    return nullptr;
}

const render_vertex_buffer_binding_t *R_FindVertexBufferBinding(
    const render_vertex_input_desc_t &description,
    const ::cypher::common::u8 binding ) noexcept
{
    for ( ::cypher::common::u32 i = 0u; i < description.vertexBufferCount; ++i ) {
        if ( description.vertexBuffers[i].binding == binding ) {
            return &description.vertexBuffers[i];
        }
    }
    return nullptr;
}

render_error_t R_ValidateVertexBufferRange(
    const render_vertex_input_desc_t &description,
    const render_vertex_buffer_binding_t &bufferBinding,
    const render_buffer_info_t &bufferInfo ) noexcept
{
    if ( bufferBinding.offset >= bufferInfo.byteSize ) {
        return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
    }

    for ( ::cypher::common::u32 i = 0u;
          i < description.layout.attributeCount;
          ++i ) {
        const render_vertex_attribute_desc_t &attribute =
            description.layout.attributes[i];
        if ( attribute.binding != bufferBinding.binding ) continue;

        render_vertex_format_info_t formatInfo{};
        if ( !R_GetVertexFormatInfo( attribute.format, &formatInfo ) ||
             attribute.offset > ::cypher::common::CY_U64_MAX - bufferBinding.offset ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }

        const ::cypher::common::u64 absoluteOffset =
            bufferBinding.offset + attribute.offset;
        if ( absoluteOffset % formatInfo.alignment != 0u ||
             absoluteOffset > bufferInfo.byteSize ||
             formatInfo.byteSize > bufferInfo.byteSize - absoluteOffset ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
    }
    return render_error_t::OK;
}

render_vertex_input_handle_t R_MakePublicVertexInputHandle(
    const ::cypher::common::handle32_t tableHandle ) noexcept
{
    return ::cypher::common::Cy_Handle64Make(
        ::cypher::common::Cy_Handle32Index( tableHandle ),
        ::cypher::common::Cy_Handle32Generation( tableHandle ),
        static_cast<::cypher::common::u32>( render_object_type_t::VERTEX_INPUT ) );
}

render_error_t R_DecodePublicVertexInputHandle(
    const render_vertex_input_handle_t vertexInput,
    ::cypher::common::handle32_t &tableHandleOut ) noexcept
{
    tableHandleOut = ::cypher::common::CY_HANDLE32_INVALID;
    if ( !::cypher::common::Cy_Handle64IsValid( vertexInput ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    if ( ::cypher::common::Cy_Handle64Type( vertexInput ) !=
         static_cast<::cypher::common::u32>( render_object_type_t::VERTEX_INPUT ) ) {
        return render_error_t::ERR_RESOURCE_TYPE_MISMATCH;
    }

    const ::cypher::common::u32 index =
        ::cypher::common::Cy_Handle64Index( vertexInput );
    const ::cypher::common::u32 generation =
        ::cypher::common::Cy_Handle64Generation( vertexInput );
    if ( index > ::cypher::common::CY_HANDLE32_INDEX_MAX || generation == 0u ||
         !::cypher::common::Cy_Handle32TryMake(
             index,
             generation,
             &tableHandleOut ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    return render_error_t::OK;
}

render_error_t R_ResolveVertexInput(
    const render_vertex_input_handle_t vertexInput,
    ::cypher::common::handle32_t &tableHandleOut,
    vertex_input_record_t *&recordOut ) noexcept
{
    recordOut = nullptr;
    const render_error_t decodeResult = R_DecodePublicVertexInputHandle(
        vertexInput,
        tableHandleOut );
    if ( decodeResult != render_error_t::OK ) return decodeResult;

    recordOut = ::cypher::common::HandleTable_Get(
        &vertexInputSystem.records,
        tableHandleOut );
    return recordOut != nullptr
        ? render_error_t::OK
        : render_error_t::ERR_STALE_HANDLE;
}

render_error_t R_ReleaseVertexInputBuffers(
    const render_vertex_input_info_t &info ) noexcept
{
    render_error_t firstError = render_error_t::OK;
    for ( ::cypher::common::u32 i = 0u; i < info.vertexBufferCount; ++i ) {
        const render_error_t result = R_BufferReleaseReference(
            info.vertexBuffers[i].buffer );
        if ( firstError == render_error_t::OK && result != render_error_t::OK ) {
            firstError = result;
        }
    }

    if ( ::cypher::common::Cy_Handle64IsValid( info.indexBuffer.buffer ) ) {
        const render_error_t result = R_BufferReleaseReference(
            info.indexBuffer.buffer );
        if ( firstError == render_error_t::OK && result != render_error_t::OK ) {
            firstError = result;
        }
    }
    return firstError;
}

} // namespace

render_error_t R_VertexInputSystemInit() noexcept
{
    if ( vertexInputSystem.initialized ) {
        return render_error_t::ERR_ALREADY_INITIALIZED;
    }
    if ( !::cypher::common::HandleTable_Init(
            &vertexInputSystem.records,
            ::cypher::common::Allocator_GetSystem(),
            R_INITIAL_VERTEX_INPUT_CAPACITY ) ) {
        return render_error_t::ERR_OUT_OF_MEMORY;
    }

    vertexInputSystem.initialized = true;
    return render_error_t::OK;
}

render_error_t R_VertexInputSystemShutdown() noexcept
{
    if ( !vertexInputSystem.initialized ) return render_error_t::OK;

    render_error_t firstError = render_error_t::OK;
    if ( tr.backend == nullptr ) {
        firstError = render_error_t::ERR_INTERNAL_ERROR;
    } else {
        (void)::cypher::common::HandleTable_ForEach(
            &vertexInputSystem.records,
            [&]( ::cypher::common::handle32_t,
                 vertex_input_record_t &record ) noexcept {
                const render_error_t destroyResult =
                    tr.backend->DestroyVertexInput(
                        record.native,
                        tr.backend->state );
                if ( firstError == render_error_t::OK &&
                     destroyResult != render_error_t::OK ) {
                    firstError = destroyResult;
                }

                const render_error_t releaseResult =
                    R_ReleaseVertexInputBuffers( record.info );
                if ( firstError == render_error_t::OK &&
                     releaseResult != render_error_t::OK ) {
                    firstError = releaseResult;
                }
                record.native = R_INVALID_BACKEND_VERTEX_INPUT;
                return ::cypher::common::CY_TRUE;
            } );
    }

    ::cypher::common::HandleTable_Shutdown( &vertexInputSystem.records );
    vertexInputSystem.initialized = false;
    return firstError;
}

render_error_t R_ValidateVertexInputDesc(
    const render_vertex_input_desc_t &description ) noexcept
{
    if ( !tr.initialized || !vertexInputSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    const render_error_t layoutResult = R_ValidateVertexLayout(
        description.layout );
    if ( layoutResult != render_error_t::OK ) return layoutResult;
    if ( description.vertexBufferCount != description.layout.bindingCount ||
         description.vertexBufferCount == 0u ||
         description.vertexBufferCount > R_MAX_VERTEX_BINDINGS ) {
        return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
    }

    bool bindingSeen[R_MAX_VERTEX_BINDINGS]{};
    for ( ::cypher::common::u32 i = 0u;
          i < description.vertexBufferCount;
          ++i ) {
        const render_vertex_buffer_binding_t &binding =
            description.vertexBuffers[i];
        if ( binding.binding >= R_MAX_VERTEX_BINDINGS ||
             bindingSeen[binding.binding] ||
             R_FindLayoutBinding( description.layout, binding.binding ) == nullptr ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
        bindingSeen[binding.binding] = true;

        render_buffer_info_t bufferInfo{};
        const render_error_t infoResult = R_GetBufferInfo(
            binding.buffer,
            &bufferInfo );
        if ( infoResult != render_error_t::OK ) return infoResult;
        if ( ( bufferInfo.usage & R_BUFFER_USAGE_VERTEX ) == 0u ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        if ( bufferInfo.mapped ) return render_error_t::ERR_RESOURCE_BUSY;

        const render_error_t rangeResult = R_ValidateVertexBufferRange(
            description,
            binding,
            bufferInfo );
        if ( rangeResult != render_error_t::OK ) return rangeResult;
    }

    for ( ::cypher::common::u32 i = 0u;
          i < description.layout.bindingCount;
          ++i ) {
        if ( R_FindVertexBufferBinding(
                description,
                description.layout.bindings[i].binding ) == nullptr ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
    }

    if ( description.indexBuffer.buffer.value == R_INVALID_BUFFER.value ) {
        return description.indexBuffer.offset == 0u
            ? render_error_t::OK
            : render_error_t::ERR_INDEX_DATA_INVALID;
    }

    const ::cypher::common::u32 indexSize = R_IndexTypeSize(
        description.indexBuffer.type );
    if ( indexSize == 0u ||
         description.indexBuffer.offset % indexSize != 0u ) {
        return render_error_t::ERR_INDEX_DATA_INVALID;
    }

    render_buffer_info_t indexInfo{};
    const render_error_t indexInfoResult = R_GetBufferInfo(
        description.indexBuffer.buffer,
        &indexInfo );
    if ( indexInfoResult != render_error_t::OK ) return indexInfoResult;
    if ( ( indexInfo.usage & R_BUFFER_USAGE_INDEX ) == 0u ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }
    if ( indexInfo.mapped ) return render_error_t::ERR_RESOURCE_BUSY;
    if ( description.indexBuffer.offset > indexInfo.byteSize ||
         indexSize > indexInfo.byteSize - description.indexBuffer.offset ) {
        return render_error_t::ERR_INDEX_DATA_INVALID;
    }
    return render_error_t::OK;
}

render_error_t R_CreateVertexInput(
    const render_vertex_input_desc_t &description,
    render_vertex_input_handle_t *vertexInputOut ) noexcept
{
    if ( vertexInputOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *vertexInputOut = R_INVALID_VERTEX_INPUT;

    const render_error_t validationResult = R_ValidateVertexInputDesc(
        description );
    if ( validationResult != render_error_t::OK ) return validationResult;
    if ( tr.backend == nullptr ) return render_error_t::ERR_NOT_INITIALIZED;

    backend_vertex_input_desc_t backendDescription{};
    backendDescription.layout = description.layout;
    backendDescription.vertexBufferCount = description.vertexBufferCount;
    backendDescription.debugName = description.debugName;

    render_vertex_input_info_t info{};
    info.layout = description.layout;
    info.vertexBufferCount = description.vertexBufferCount;

    ::cypher::common::u32 acquiredVertexBuffers = 0u;
    render_error_t acquireFailure = render_error_t::OK;
    for ( ; acquiredVertexBuffers < description.vertexBufferCount;
          ++acquiredVertexBuffers ) {
        const render_vertex_buffer_binding_t &binding =
            description.vertexBuffers[acquiredVertexBuffers];
        render_buffer_info_t ignoredInfo{};
        const render_error_t acquireResult = R_BufferAcquireReference(
            binding.buffer,
            R_BUFFER_USAGE_VERTEX,
            &backendDescription.vertexBuffers[acquiredVertexBuffers].buffer,
            &ignoredInfo );
        if ( acquireResult != render_error_t::OK ) {
            acquireFailure = acquireResult;
            break;
        }

        backendDescription.vertexBuffers[acquiredVertexBuffers].offset =
            binding.offset;
        backendDescription.vertexBuffers[acquiredVertexBuffers].binding =
            binding.binding;
        info.vertexBuffers[acquiredVertexBuffers] = binding;
    }

    if ( acquiredVertexBuffers != description.vertexBufferCount ) {
        for ( ::cypher::common::u32 i = 0u; i < acquiredVertexBuffers; ++i ) {
            (void)R_BufferReleaseReference( description.vertexBuffers[i].buffer );
        }
        return acquireFailure;
    }

    if ( description.indexBuffer.buffer.value != R_INVALID_BUFFER.value ) {
        render_buffer_info_t ignoredInfo{};
        const render_error_t acquireResult = R_BufferAcquireReference(
            description.indexBuffer.buffer,
            R_BUFFER_USAGE_INDEX,
            &backendDescription.indexBuffer.buffer,
            &ignoredInfo );
        if ( acquireResult != render_error_t::OK ) {
            (void)R_ReleaseVertexInputBuffers( info );
            return acquireResult;
        }
        backendDescription.indexBuffer.offset = description.indexBuffer.offset;
        backendDescription.indexBuffer.type = description.indexBuffer.type;
        info.indexBuffer = description.indexBuffer;
    }

    backend_vertex_input_t nativeVertexInput{};
    const render_error_t createResult = tr.backend->CreateVertexInput(
        backendDescription,
        nativeVertexInput,
        tr.backend->state );
    if ( createResult != render_error_t::OK ||
         !R_IsBackendVertexInputValid( nativeVertexInput ) ) {
        (void)R_ReleaseVertexInputBuffers( info );
        return createResult != render_error_t::OK
            ? createResult
            : render_error_t::ERR_INTERNAL_ERROR;
    }

    vertex_input_record_t record{};
    record.native = nativeVertexInput;
    record.info = info;
    const ::cypher::common::handle32_t tableHandle =
        ::cypher::common::HandleTable_Insert(
            &vertexInputSystem.records,
            record );
    if ( !::cypher::common::Cy_Handle32IsValid( tableHandle ) ) {
        (void)tr.backend->DestroyVertexInput(
            nativeVertexInput,
            tr.backend->state );
        (void)R_ReleaseVertexInputBuffers( info );
        return ::cypher::common::HandleTable_Count( &vertexInputSystem.records ) >=
            ::cypher::common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED
            : render_error_t::ERR_OUT_OF_MEMORY;
    }

    const render_vertex_input_handle_t publicHandle =
        R_MakePublicVertexInputHandle( tableHandle );
    if ( !::cypher::common::Cy_Handle64IsValid( publicHandle ) ) {
        (void)::cypher::common::HandleTable_Remove(
            &vertexInputSystem.records,
            tableHandle );
        (void)tr.backend->DestroyVertexInput(
            nativeVertexInput,
            tr.backend->state );
        (void)R_ReleaseVertexInputBuffers( info );
        return render_error_t::ERR_INTERNAL_ERROR;
    }

    *vertexInputOut = publicHandle;
    return render_error_t::OK;
}

render_error_t R_DestroyVertexInput(
    const render_vertex_input_handle_t vertexInput ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ||
         !vertexInputSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    ::cypher::common::handle32_t tableHandle{};
    vertex_input_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveVertexInput(
        vertexInput,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;

    const render_error_t destroyResult = tr.backend->DestroyVertexInput(
        record->native,
        tr.backend->state );
    if ( destroyResult != render_error_t::OK ) return destroyResult;

    const render_error_t releaseResult = R_ReleaseVertexInputBuffers(
        record->info );
    const bool removed = ::cypher::common::HandleTable_Remove(
        &vertexInputSystem.records,
        tableHandle );
    if ( !removed ) return render_error_t::ERR_INTERNAL_ERROR;
    return releaseResult;
}

bool R_IsVertexInputValid(
    const render_vertex_input_handle_t vertexInput ) noexcept
{
    if ( !tr.initialized || !vertexInputSystem.initialized ) return false;

    ::cypher::common::handle32_t tableHandle{};
    vertex_input_record_t *record = nullptr;
    return R_ResolveVertexInput( vertexInput, tableHandle, record ) ==
        render_error_t::OK;
}

render_error_t R_GetVertexInputInfo(
    const render_vertex_input_handle_t vertexInput,
    render_vertex_input_info_t *infoOut ) noexcept
{
    if ( infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *infoOut = {};
    if ( !tr.initialized || !vertexInputSystem.initialized ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }

    ::cypher::common::handle32_t tableHandle{};
    vertex_input_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolveVertexInput(
        vertexInput,
        tableHandle,
        record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;

    *infoOut = record->info;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
