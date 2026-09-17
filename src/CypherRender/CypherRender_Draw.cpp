//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Validates complete indexed draws before immediate backend submission.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Draw.h"
#include "CypherRender_Local.h"
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
bool R_LayoutsMatch( const render_vertex_layout_t &a, const render_vertex_layout_t &b ) noexcept
{
    if ( a.bindingCount != b.bindingCount || a.attributeCount != b.attributeCount ) return false;
    for ( common::u32 i = 0u; i < a.bindingCount; ++i ) {
        const auto &left = a.bindings[i];
        bool matched = false;
        for ( common::u32 j = 0u; j < b.bindingCount; ++j ) {
            const auto &right = b.bindings[j];
            if ( left.binding == right.binding && left.stride == right.stride &&
                 left.inputRate == right.inputRate && left.instanceDivisor == right.instanceDivisor ) {
                matched = true;
                break;
            }
        }
        if ( !matched ) return false;
    }
    for ( common::u32 i = 0u; i < a.attributeCount; ++i ) {
        const auto &left = a.attributes[i];
        bool matched = false;
        for ( common::u32 j = 0u; j < b.attributeCount; ++j ) {
            const auto &right = b.attributes[j];
            if ( left.location == right.location && left.binding == right.binding &&
                 left.format == right.format && left.offset == right.offset ) {
                matched = true;
                break;
            }
        }
        if ( !matched ) return false;
    }
    return true;
}

render_error_t R_ValidateDrawVertexRanges(
    const render_vertex_input_info_t &input, const common::u32 vertexCount ) noexcept
{
    for ( common::u32 i = 0u; i < input.vertexBufferCount; ++i ) {
        const auto &buffer = input.vertexBuffers[i];
        render_buffer_info_t info{};
        const render_error_t result = R_GetBufferInfo( buffer.buffer, &info );
        if ( result != render_error_t::OK ) return result;
        if ( info.mapped ) return render_error_t::ERR_RESOURCE_BUSY;
        const render_vertex_binding_desc_t *binding = nullptr;
        for ( common::u32 j = 0u; j < input.layout.bindingCount; ++j ) {
            if ( input.layout.bindings[j].binding == buffer.binding ) binding = &input.layout.bindings[j];
        }
        if ( binding == nullptr || binding->inputRate != render_vertex_input_rate_t::PER_VERTEX ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
        // u32 vertex count times the bounded stride is representable in u64.
        const common::u64 lastVertexOffset = static_cast<common::u64>( vertexCount - 1u ) * binding->stride;
        for ( common::u32 j = 0u; j < input.layout.attributeCount; ++j ) {
            const auto &attribute = input.layout.attributes[j];
            if ( attribute.binding != buffer.binding ) continue;
            render_vertex_format_info_t format{};
            if ( !R_GetVertexFormatInfo( attribute.format, &format ) ) {
                return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
            }
            const common::u64 required = lastVertexOffset + attribute.offset + format.byteSize;
            if ( buffer.offset > info.byteSize || required > info.byteSize - buffer.offset ) {
                return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
            }
        }
    }
    return render_error_t::OK;
}
} // namespace

render_error_t R_DrawIndexed( const render_draw_indexed_desc_t &description ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !tr.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    const auto maximumCount = static_cast<common::u32>( std::numeric_limits<common::i32>::max() );
    if ( description.indexCount == 0u || description.indexCount % 3u != 0u ||
         description.vertexCount == 0u || description.indexCount > maximumCount ||
         description.vertexCount > maximumCount ) {
        return render_error_t::ERR_DRAW_DESC_INVALID;
    }
    backend_draw_indexed_desc_t native{};
    render_pipeline_info_t pipeline{};
    render_error_t result = R_PipelineResolveForDraw( description.pipeline, &native.pipeline, &pipeline );
    if ( result != render_error_t::OK ) return result;
    render_vertex_input_info_t input{};
    result = R_VertexInputResolveForDraw( description.vertexInput, &native.vertexInput, &input );
    if ( result != render_error_t::OK ) return result;
    if ( !R_LayoutsMatch( pipeline.vertexLayout, input.layout ) ) return render_error_t::ERR_BINDING_MISMATCH;
    result = R_ValidateDrawVertexRanges( input, description.vertexCount );
    if ( result != render_error_t::OK ) return result;

    const common::u32 indexSize = R_IndexTypeSize( input.indexBuffer.type );
    if ( input.indexBuffer.buffer.value == R_INVALID_BUFFER.value || indexSize == 0u ) {
        return render_error_t::ERR_INDEX_DATA_INVALID;
    }
    render_buffer_info_t indexInfo{};
    result = R_GetBufferInfo( input.indexBuffer.buffer, &indexInfo );
    if ( result != render_error_t::OK ) return result;
    if ( indexInfo.mapped ) return render_error_t::ERR_RESOURCE_BUSY;
    const common::u64 firstByte = static_cast<common::u64>( description.firstIndex ) * indexSize;
    const common::u64 indexBytes = static_cast<common::u64>( description.indexCount ) * indexSize;
    const common::u64 base = input.indexBuffer.offset;
    if ( base > indexInfo.byteSize || firstByte > indexInfo.byteSize - base ||
         indexBytes > indexInfo.byteSize - base - firstByte ) {
        return render_error_t::ERR_INDEX_DATA_INVALID;
    }
    native.indexType = input.indexBuffer.type;
    native.indexByteOffset = base + firstByte;
    native.indexCount = description.indexCount;
    native.vertexCount = description.vertexCount;

    const bool hasTexture = description.sampledTexture.value != R_INVALID_TEXTURE.value;
    if ( hasTexture != pipeline.sampledTextureRequired ) return render_error_t::ERR_BINDING_MISMATCH;
    if ( hasTexture ) {
        render_texture_info_t textureInfo{};
        result = R_TextureResolveForDraw( description.sampledTexture, &native.sampledTexture, &textureInfo );
        if ( result != render_error_t::OK ) return result;
    }

    const bool hasUniform = description.uniformBuffer.value != R_INVALID_BUFFER.value;
    if ( hasUniform != ( pipeline.uniformBlockBytes != 0u ) ) return render_error_t::ERR_BINDING_MISMATCH;
    if ( hasUniform ) {
        render_buffer_info_t uniformInfo{};
        result = R_BufferAcquireReference(
            description.uniformBuffer, R_BUFFER_USAGE_UNIFORM, &native.uniformBuffer, &uniformInfo );
        if ( result != render_error_t::OK ) return result;
        if ( uniformInfo.mapped || uniformInfo.byteSize < pipeline.uniformBlockBytes ) {
            (void)R_BufferReleaseReference( description.uniformBuffer );
            return uniformInfo.mapped ? render_error_t::ERR_RESOURCE_BUSY : render_error_t::ERR_BINDING_MISMATCH;
        }
    }
    result = tr.backend->DrawIndexed( native, tr.backend->state );
    if ( hasUniform ) {
        const render_error_t releaseResult = R_BufferReleaseReference( description.uniformBuffer );
        if ( result == render_error_t::OK ) result = releaseResult;
    }
    return result;
}

} // namespace cypher::engine::render
