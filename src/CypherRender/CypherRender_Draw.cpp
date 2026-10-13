//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Validates complete indexed draws before immediate backend submission.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Draw.h"
#include "CypherRender_Local.h"
#include <cstdint>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
bool R_RectFitsExtent( const common::u32 x, const common::u32 y,
    const common::u32 width, const common::u32 height,
    const render_extent_t extent, const common::u32 maximumCount ) noexcept
{
    // Subtract only after checking the origin so an untrusted u32 far edge
    // cannot wrap. OpenGL origins use GLint and sizes use GLsizei.
    return width != 0u && height != 0u && x <= maximumCount && y <= maximumCount &&
        width <= maximumCount && height <= maximumCount &&
        x < extent.width && y < extent.height &&
        width <= extent.width - x && height <= extent.height - y;
}

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
    const render_vertex_input_info_t &input, const common::u32 vertexCount,
    const common::u32 instanceCount ) noexcept
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
        if ( binding == nullptr ||
             ( binding->inputRate == render_vertex_input_rate_t::PER_INSTANCE &&
               binding->instanceDivisor == 0u ) ||
             binding->inputRate >= render_vertex_input_rate_t::COUNT ) {
            return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
        }
        // GL's divisor selects floor(instanceID / divisor). Both counts and
        // the validated stride fit safely in this u64 byte calculation.
        const common::u32 lastElement = binding->inputRate == render_vertex_input_rate_t::PER_VERTEX
            ? vertexCount - 1u
            : ( instanceCount - 1u ) / binding->instanceDivisor;
        const common::u64 lastElementOffset = static_cast<common::u64>( lastElement ) * binding->stride;
        for ( common::u32 j = 0u; j < input.layout.attributeCount; ++j ) {
            const auto &attribute = input.layout.attributes[j];
            if ( attribute.binding != buffer.binding ) continue;
            render_vertex_format_info_t format{};
            if ( !R_GetVertexFormatInfo( attribute.format, &format ) ) {
                return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
            }
            const common::u64 required = lastElementOffset + attribute.offset + format.byteSize;
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
    if ( !tr.initialized || tr.backend.api == nullptr ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !tr.frameActive ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    const auto maximumCount = static_cast<common::u32>( std::numeric_limits<common::i32>::max() );
    if ( description.indexCount == 0u || description.vertexCount == 0u ||
         description.indexCount > maximumCount ||
         description.vertexCount > maximumCount || description.instanceCount == 0u ||
         description.instanceCount > maximumCount ) {
        return render_error_t::ERR_DRAW_DESC_INVALID;
    }
    if ( description.instanceCount > 1u &&
         ( tr.info.capabilities & R_CAPABILITY_INSTANCING ) == 0u ) {
        return render_error_t::ERR_CAPABILITY_MISSING;
    }
    if ( description.scissor.enabled || description.viewport.enabled ) {
        render_extent_t extent = tr.info.drawableExtent;
        if ( tr.activeTarget.value != R_INVALID_TARGET.value ) {
            render_target_info_t target{};
            const render_error_t targetResult = R_GetRenderTargetInfo( tr.activeTarget, &target );
            if ( targetResult != render_error_t::OK ) return targetResult;
            extent = target.extent;
        }
        if ( ( description.scissor.enabled &&
               !R_RectFitsExtent( description.scissor.x, description.scissor.y,
                   description.scissor.width, description.scissor.height,
                   extent, maximumCount ) ) ||
             ( description.viewport.enabled &&
               !R_RectFitsExtent( description.viewport.x, description.viewport.y,
                   description.viewport.width, description.viewport.height,
                   extent, maximumCount ) ) ) {
            return render_error_t::ERR_DRAW_DESC_INVALID;
        }
    }
    backend_draw_indexed_desc_t native{};
    native.scissor = description.scissor;
    native.viewport = description.viewport;
    render_pipeline_info_t pipeline{};
    render_error_t result = R_PipelineResolveForDraw( description.pipeline, &native.pipeline, &pipeline );
    if ( result != render_error_t::OK ) return result;
    const common::u32 primitiveSize = pipeline.topology == render_primitive_topology_t::TRIANGLE_LIST
        ? 3u : pipeline.topology == render_primitive_topology_t::LINE_LIST ? 2u : 0u;
    if ( primitiveSize == 0u || description.indexCount < primitiveSize ||
         description.indexCount % primitiveSize != 0u ) {
        return render_error_t::ERR_DRAW_DESC_INVALID;
    }
    render_vertex_input_info_t input{};
    result = R_VertexInputResolveForDraw( description.vertexInput, &native.vertexInput, &input );
    if ( result != render_error_t::OK ) return result;
    if ( !R_LayoutsMatch( pipeline.vertexLayout, input.layout ) ) return render_error_t::ERR_BINDING_MISMATCH;
    result = R_ValidateDrawVertexRanges( input, description.vertexCount, description.instanceCount );
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
    native.instanceCount = description.instanceCount;

    for ( common::u32 i = 0u; i < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++i ) {
        const auto handle = description.sampledTextures[i];
        const auto sampler = description.samplers[i];
        const bool required = i < pipeline.sampledTextureCount;
        if ( ( handle.value != R_INVALID_TEXTURE.value ) != required ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        if ( !required && sampler.value != R_INVALID_SAMPLER.value ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        if ( !required ) continue;
        if ( handle.value == tr.activeTargetColor.value ||
             handle.value == tr.activeTargetDepth.value ) {
            return render_error_t::ERR_COMMAND_STATE_INVALID;
        }
        render_texture_info_t textureInfo{};
        result = R_TextureResolveForDraw( handle, &native.sampledTextures[i], &textureInfo );
        if ( result != render_error_t::OK ) return result;
        const auto kind = pipeline.sampledTextureKinds[i];
        if ( kind == render_sampled_texture_kind_t::DEPTH_COMPARE_2D &&
             ( textureInfo.format != render_texture_format_t::D24 ||
               sampler.value == R_INVALID_SAMPLER.value ) ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        if ( sampler.value != R_INVALID_SAMPLER.value ) {
            render_sampler_info_t samplerInfo{};
            result = R_SamplerResolveForDraw( sampler, &native.samplers[i], &samplerInfo );
            if ( result != render_error_t::OK ) return result;
            if ( ( kind == render_sampled_texture_kind_t::SCALAR_2D &&
                   samplerInfo.compare != render_sampler_compare_t::NONE ) ||
                 ( kind == render_sampled_texture_kind_t::DEPTH_COMPARE_2D &&
                   samplerInfo.compare == render_sampler_compare_t::NONE ) ) {
                return render_error_t::ERR_BINDING_MISMATCH;
            }
            if ( samplerInfo.mipFilter != render_sampler_mip_filter_t::NONE &&
                 !textureInfo.completeMipChain ) {
                return render_error_t::ERR_BINDING_MISMATCH;
            }
        }
    }

    common::u32 acquiredUniforms = 0u;
    for ( common::u32 i = 0u; i < R_MAX_UNIFORM_BLOCK_BINDINGS; ++i ) {
        const auto handle = description.uniformBuffers[i];
        const auto range = description.uniformRanges[i];
        const bool required = i < pipeline.uniformBlockCount;
        if ( ( handle.value != R_INVALID_BUFFER.value ) != required ) {
            result = render_error_t::ERR_BINDING_MISMATCH;
            break;
        }
        if ( !required ) {
            if ( range.offset != 0u || range.byteSize != 0u ) {
                result = render_error_t::ERR_BINDING_MISMATCH;
                break;
            }
            continue;
        }
        render_buffer_info_t uniformInfo{};
        result = R_BufferAcquireReference(
            handle, R_BUFFER_USAGE_UNIFORM, &native.uniformBuffers[i], &uniformInfo );
        if ( result != render_error_t::OK ) break;
        ++acquiredUniforms;
        if ( uniformInfo.mapped ) {
            result = render_error_t::ERR_RESOURCE_BUSY;
            break;
        }
        if ( range.offset == 0u && range.byteSize == 0u ) {
            if ( uniformInfo.byteSize < pipeline.uniformBlockBytes[i] ) {
                result = render_error_t::ERR_BINDING_MISMATCH;
                break;
            }
            continue;
        }
        const common::u32 alignment = tr.info.limits.uniformBufferOffsetAlignment;
        if ( alignment == 0u ) {
            result = render_error_t::ERR_INTERNAL_ERROR;
            break;
        }
        const auto nativeMax = static_cast<common::u64>(
            std::numeric_limits<std::intptr_t>::max() );
        if ( range.byteSize < pipeline.uniformBlockBytes[i] ||
             range.offset % alignment != 0u ||
             range.offset > nativeMax || range.byteSize > nativeMax ||
             range.byteSize > nativeMax - range.offset ||
             range.offset > uniformInfo.byteSize ||
             range.byteSize > uniformInfo.byteSize - range.offset ) {
            result = render_error_t::ERR_BINDING_MISMATCH;
            break;
        }
        native.uniformRanges[i] = range;
    }
    if ( result != render_error_t::OK ) {
        for ( common::u32 i = 0u; i < acquiredUniforms; ++i ) {
            (void)R_BufferReleaseReference( description.uniformBuffers[i] );
        }
        return result;
    }
    result = tr.backend.api->DrawIndexed( native, tr.backend.state );
    for ( common::u32 i = 0u; i < acquiredUniforms; ++i ) {
        const render_error_t releaseResult = R_BufferReleaseReference( description.uniformBuffers[i] );
        if ( result == render_error_t::OK ) result = releaseResult;
    }
    return result;
}

} // namespace cypher::engine::render
