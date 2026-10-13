//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Validates and uploads one owned static indexed mesh bundle.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_MeshBridge.h"

#include <cstddef>
#include <cstring>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{

render_static_mesh_bridge_result_t InvalidSource(
    const render_error_t error ) noexcept
{
    return { render_static_mesh_bridge_status_t::INVALID_ARGUMENT, error };
}

render_static_mesh_bridge_result_t ValidateSource(
    const render_static_mesh_source_t &source ) noexcept
{
    const auto maximumCount = static_cast<common::u32>(
        std::numeric_limits<common::i32>::max() );
    if ( source.vertexBytes == nullptr || source.indexBytes == nullptr ||
         source.vertexCount == 0u || source.indexCount == 0u ||
         source.vertexCount > maximumCount || source.indexCount > maximumCount ||
         source.vertexLayout.bindingCount != 1u ||
         source.vertexLayout.bindings[0].binding != 0u ||
         source.vertexLayout.bindings[0].inputRate !=
             render_vertex_input_rate_t::PER_VERTEX ||
         source.vertexLayout.bindings[0].instanceDivisor != 0u ) {
        return InvalidSource( render_error_t::ERR_INVALID_ARGUMENT );
    }
    const render_error_t layoutError = R_ValidateVertexLayout(
        source.vertexLayout );
    if ( layoutError != render_error_t::OK ) return InvalidSource( layoutError );
    for ( common::u32 i = 0u; i < source.vertexLayout.attributeCount; ++i ) {
        if ( source.vertexLayout.attributes[i].binding != 0u ) {
            return InvalidSource( render_error_t::ERR_VERTEX_LAYOUT_INVALID );
        }
    }

    const common::u32 indexSize = R_IndexTypeSize( source.indexType );
    if ( indexSize == 0u ) return InvalidSource( render_error_t::ERR_INVALID_ARGUMENT );
    const common::u32 stride = source.vertexLayout.bindings[0].stride;
    const auto maximumBytes = static_cast<common::u64>(
        std::numeric_limits<std::ptrdiff_t>::max() );
    if ( stride == 0u ||
         source.vertexCount > std::numeric_limits<common::u64>::max() / stride ||
         source.indexCount > std::numeric_limits<common::u64>::max() / indexSize ||
         source.vertexByteSize != static_cast<common::u64>( source.vertexCount ) * stride ||
         source.indexByteSize != static_cast<common::u64>( source.indexCount ) * indexSize ||
         source.vertexByteSize > maximumBytes || source.indexByteSize > maximumBytes ) {
        return InvalidSource( render_error_t::ERR_INVALID_ARGUMENT );
    }

    // The borrowed input may be unaligned. Never cast it to a u16/u32 pointer.
    // Exact extent was checked above, before reading the first index.
    const auto *bytes = static_cast<const common::byte *>( source.indexBytes );
    for ( common::u32 i = 0u; i < source.indexCount; ++i ) {
        const auto offset = static_cast<common::usize>( i ) * indexSize;
        common::u32 value = 0u;
        if ( source.indexType == render_index_type_t::UINT16 ) {
            common::u16 narrow = 0u;
            std::memcpy( &narrow, bytes + offset, sizeof( narrow ) );
            value = narrow;
        } else {
            std::memcpy( &value, bytes + offset, sizeof( value ) );
        }
        if ( value >= source.vertexCount ) {
            return { render_static_mesh_bridge_status_t::INDEX_DATA_INVALID,
                render_error_t::ERR_INDEX_DATA_INVALID };
        }
    }
    return {};
}

} // namespace

render_error_t R_DestroyStaticMeshBundle(
    render_static_mesh_bundle_t *bundle ) noexcept
{
    if ( bundle == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    render_error_t firstError = render_error_t::OK;
    if ( bundle->vertexInput.value != 0u ) {
        const render_error_t error = R_DestroyVertexInput( bundle->vertexInput );
        if ( error == render_error_t::OK ||
             !R_IsVertexInputValid( bundle->vertexInput ) ) {
            bundle->vertexInput = {};
        } else {
            // Its referenced buffers must outlive a still-live input.
            return error;
        }
        firstError = error;
    }

    if ( bundle->indexBuffer.value != 0u ) {
        const render_error_t error = R_DestroyBuffer( bundle->indexBuffer );
        if ( error == render_error_t::OK ||
             !R_IsBufferValid( bundle->indexBuffer ) ) bundle->indexBuffer = {};
        if ( firstError == render_error_t::OK ) firstError = error;
    }
    if ( bundle->vertexBuffer.value != 0u ) {
        const render_error_t error = R_DestroyBuffer( bundle->vertexBuffer );
        if ( error == render_error_t::OK ||
             !R_IsBufferValid( bundle->vertexBuffer ) ) bundle->vertexBuffer = {};
        if ( firstError == render_error_t::OK ) firstError = error;
    }
    if ( bundle->vertexInput.value == 0u && bundle->indexBuffer.value == 0u &&
         bundle->vertexBuffer.value == 0u ) *bundle = {};
    return firstError;
}

render_static_mesh_bridge_result_t R_CreateStaticMeshBundle(
    const render_static_mesh_source_t &source,
    render_static_mesh_bundle_t *bundleOut ) noexcept
{
    if ( bundleOut == nullptr ) return InvalidSource( render_error_t::ERR_INVALID_ARGUMENT );
    if ( bundleOut->vertexBuffer.value != 0u ||
         bundleOut->indexBuffer.value != 0u ||
         bundleOut->vertexInput.value != 0u ) {
        // Never overwrite the caller's live ownership on a repeated create.
        return InvalidSource( render_error_t::ERR_INVALID_ARGUMENT );
    }
    *bundleOut = {};
    const render_static_mesh_bridge_result_t validation = ValidateSource( source );
    if ( validation.status != render_static_mesh_bridge_status_t::OK ) return validation;

    render_static_mesh_bundle_t pending{};
    pending.vertexLayout = source.vertexLayout;
    pending.vertexCount = source.vertexCount;
    pending.indexCount = source.indexCount;
    pending.indexType = source.indexType;

    render_buffer_desc_t buffer{};
    buffer.byteSize = source.vertexByteSize;
    buffer.usage = R_BUFFER_USAGE_VERTEX;
    buffer.updatePolicy = render_buffer_update_t::IMMUTABLE;
    buffer.memory = render_buffer_memory_t::DEVICE_LOCAL;
    buffer.debugName = source.debugName;
    const render_buffer_data_t vertices{ source.vertexBytes, source.vertexByteSize };
    render_error_t createError = R_CreateBuffer(
        buffer, &vertices, &pending.vertexBuffer );
    if ( createError == render_error_t::OK ) {
        buffer.byteSize = source.indexByteSize;
        buffer.usage = R_BUFFER_USAGE_INDEX;
        const render_buffer_data_t indices{ source.indexBytes, source.indexByteSize };
        createError = R_CreateBuffer( buffer, &indices, &pending.indexBuffer );
    }
    if ( createError == render_error_t::OK ) {
        render_vertex_input_desc_t input{};
        input.layout = source.vertexLayout;
        input.vertexBufferCount = 1u;
        input.vertexBuffers[0] = { pending.vertexBuffer, 0u, 0u };
        input.indexBuffer = { pending.indexBuffer, 0u, source.indexType };
        input.debugName = source.debugName;
        createError = R_CreateVertexInput( input, &pending.vertexInput );
    }
    if ( createError == render_error_t::OK ) {
        *bundleOut = pending;
        return {};
    }

    const render_error_t cleanupError = R_DestroyStaticMeshBundle( &pending );
    if ( cleanupError != render_error_t::OK ) {
        *bundleOut = pending;
        return { render_static_mesh_bridge_status_t::CLEANUP_FAILED,
            createError, cleanupError };
    }
    return { render_static_mesh_bridge_status_t::RENDER_ERROR, createError };
}

render_error_t R_ApplyStaticMeshBundleToDraw(
    const render_static_mesh_bundle_t &bundle,
    render_draw_indexed_desc_t *draw ) noexcept
{
    if ( draw == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    if ( bundle.vertexInput.value == 0u || bundle.vertexBuffer.value == 0u ||
         bundle.indexBuffer.value == 0u || bundle.vertexCount == 0u ||
         bundle.indexCount == 0u ) return render_error_t::ERR_INVALID_HANDLE;

    render_vertex_input_info_t input{};
    const render_error_t error = R_GetVertexInputInfo( bundle.vertexInput, &input );
    if ( error != render_error_t::OK ) return error;
    if ( input.vertexBufferCount != 1u ||
         input.vertexBuffers[0].buffer.value != bundle.vertexBuffer.value ||
         input.vertexBuffers[0].offset != 0u ||
         input.vertexBuffers[0].binding != 0u ||
         input.indexBuffer.buffer.value != bundle.indexBuffer.value ||
         input.indexBuffer.offset != 0u ||
         input.indexBuffer.type != bundle.indexType ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }
    draw->vertexInput = bundle.vertexInput;
    draw->firstIndex = 0u;
    draw->indexCount = bundle.indexCount;
    draw->vertexCount = bundle.vertexCount;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
