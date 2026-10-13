//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Resolves one visible World submission into opaque indexed draws.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_WorldDraw.h"

#include "CypherRender/CypherRender_Public.h"

#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace world = ::cypher::engine::world;
namespace
{

static_assert( sizeof( math::mat4_t ) == 64u );
constexpr common::f32 kMinimumPositiveDeterminant = 1.0e-8f;

bool LayoutsMatch(
    const render_vertex_layout_t &a,
    const render_vertex_layout_t &b ) noexcept
{
    if ( a.bindingCount != b.bindingCount ||
         a.attributeCount != b.attributeCount ) return false;
    for ( common::u32 i = 0u; i < a.bindingCount; ++i ) {
        const auto &left = a.bindings[i];
        bool matched = false;
        for ( common::u32 j = 0u; j < b.bindingCount; ++j ) {
            const auto &right = b.bindings[j];
            if ( left.binding == right.binding && left.stride == right.stride &&
                 left.inputRate == right.inputRate &&
                 left.instanceDivisor == right.instanceDivisor ) {
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

bool NextModelOffset(
    const common::u64 cursor,
    const common::u64 alignment,
    const common::u64 capacity,
    common::u64 *offsetOut,
    common::u64 *nextOut ) noexcept
{
    if ( alignment == 0u || cursor > capacity ) return false;
    const common::u64 remainder = cursor % alignment;
    const common::u64 padding = remainder == 0u ? 0u : alignment - remainder;
    if ( padding > capacity - cursor ) return false;
    const common::u64 offset = cursor + padding;
    if ( sizeof( math::mat4_t ) > capacity - offset ) return false;
    *offsetOut = offset;
    *nextOut = offset + sizeof( math::mat4_t );
    return true;
}

render_error_t ValidateBatch(
    const render_uniform_batch_t &batch ) noexcept
{
    if ( !R_IsInitialized() ) return render_error_t::ERR_NOT_INITIALIZED;
    const render_info_t *info = R_GetInfo();
    if ( info == nullptr ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( batch.buffer.value == R_INVALID_BUFFER.value ||
         batch.staging == nullptr || batch.allocator == nullptr ||
         batch.capacityBytes == 0u || batch.generation == 0u ||
         batch.usedBytes > batch.capacityBytes || batch.sealed || batch.committed ||
         batch.alignment == 0u ||
         batch.alignment != info->limits.uniformBufferOffsetAlignment ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( info->limits.maxUniformBlockBytes < sizeof( math::mat4_t ) ) {
        return render_error_t::ERR_LIMIT_EXCEEDED;
    }
    render_buffer_info_t buffer{};
    const render_error_t error = R_GetBufferInfo( batch.buffer, &buffer );
    if ( error != render_error_t::OK ) return error;
    if ( buffer.mapped || buffer.byteSize < batch.capacityBytes ||
         ( buffer.usage & R_BUFFER_USAGE_UNIFORM ) == 0u ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    return render_error_t::OK;
}

render_error_t ValidateOtherUniforms(
    const render_draw_indexed_desc_t &draw,
    const render_pipeline_info_t &pipeline,
    const common::u32 modelSlot,
    const common::u64 alignment ) noexcept
{
    for ( common::u32 slot = 0u; slot < R_MAX_UNIFORM_BLOCK_BINDINGS; ++slot ) {
        const auto handle = draw.uniformBuffers[slot];
        const auto range = draw.uniformRanges[slot];
        if ( slot == modelSlot ) {
            if ( handle.value != R_INVALID_BUFFER.value ||
                 range.offset != 0u || range.byteSize != 0u ) {
                return render_error_t::ERR_BINDING_MISMATCH;
            }
            continue;
        }
        if ( slot >= pipeline.uniformBlockCount ) {
            if ( handle.value != R_INVALID_BUFFER.value ||
                 range.offset != 0u || range.byteSize != 0u ) {
                return render_error_t::ERR_BINDING_MISMATCH;
            }
            continue;
        }
        render_buffer_info_t buffer{};
        const render_error_t error = R_GetBufferInfo( handle, &buffer );
        if ( error != render_error_t::OK ) return error;
        if ( ( buffer.usage & R_BUFFER_USAGE_UNIFORM ) == 0u || buffer.mapped ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        if ( range.offset == 0u && range.byteSize == 0u ) {
            if ( buffer.byteSize < pipeline.uniformBlockBytes[slot] ) {
                return render_error_t::ERR_BINDING_MISMATCH;
            }
        } else if ( range.offset % alignment != 0u ||
                   range.byteSize < pipeline.uniformBlockBytes[slot] ||
                   range.offset > buffer.byteSize ||
                   range.byteSize > buffer.byteSize - range.offset ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
    }
    return render_error_t::OK;
}

render_error_t PreflightObject(
    const render_world_opaque_prepare_desc_t &description,
    const world::world_object_snapshot_t &snapshot ) noexcept
{
    const world::world_object_t &object = snapshot.object;
    if ( object.meshSlot >= description.meshCount ||
         object.materialSlot >= description.materialCount ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !math::Affine3_IsFinite( object.localToWorld ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    const math::mat3_t linear = math::Affine3_LinearPart( object.localToWorld );
    const common::f32 determinant = math::Mat3_Determinant( linear );
    math::mat3_t inverse{};
    if ( !( determinant > kMinimumPositiveDeterminant ) ||
         !math::Mat3_TryInverse(
             linear, kMinimumPositiveDeterminant, &inverse ) ) {
        // A mirrored object needs a different front-face pipeline policy.
        return render_error_t::ERR_UNSUPPORTED;
    }

    const render_material_bundle_t &material =
        description.materials[object.materialSlot];
    if ( material.alphaMode != common::render_material_alpha_mode_t::OPAQUE ) {
        return render_error_t::ERR_UNSUPPORTED;
    }
    render_draw_indexed_desc_t draw = description.drawTemplate;
    render_error_t error = R_ApplyStaticMeshBundleToDraw(
        description.meshes[object.meshSlot], &draw );
    if ( error != render_error_t::OK ) return error;
    error = R_ApplyCookedMaterialBundleToDraw( material, &draw );
    if ( error != render_error_t::OK ) return error;

    render_pipeline_info_t pipeline{};
    error = R_GetGraphicsPipelineInfo( draw.pipeline, &pipeline );
    if ( error != render_error_t::OK ) return error;
    if ( pipeline.uniformBlockCount <= description.modelUniformSlot ||
         pipeline.uniformBlockBytes[description.modelUniformSlot] !=
             sizeof( math::mat4_t ) ||
         pipeline.blendMode != render_blend_mode_t::DISABLED ||
         !pipeline.depthWrite ||
         pipeline.topology != render_primitive_topology_t::TRIANGLE_LIST ||
         draw.indexCount < 3u || draw.indexCount % 3u != 0u ||
         draw.vertexCount > static_cast<common::u32>(
             std::numeric_limits<common::i32>::max() ) ||
         draw.indexCount > static_cast<common::u32>(
             std::numeric_limits<common::i32>::max() ) ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }
    render_vertex_input_info_t input{};
    error = R_GetVertexInputInfo( draw.vertexInput, &input );
    if ( error != render_error_t::OK ) return error;
    if ( !LayoutsMatch( pipeline.vertexLayout, input.layout ) ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }
    return ValidateOtherUniforms(
        draw, pipeline, description.modelUniformSlot,
        description.modelUniforms->alignment );
}

void PublishDraw(
    const render_world_opaque_prepare_desc_t &description,
    const world::world_object_snapshot_t &snapshot,
    const common::u64 modelOffset,
    render_world_opaque_draw_t *output ) noexcept
{
    const auto &mesh = description.meshes[snapshot.object.meshSlot];
    const auto &material = description.materials[snapshot.object.materialSlot];
    render_draw_indexed_desc_t draw = description.drawTemplate;
    // All handles and bundle invariants were checked before the upload. Copy
    // the same fields as the two Apply* helpers without a fallible late lookup.
    draw.vertexInput = mesh.vertexInput;
    draw.firstIndex = 0u;
    draw.indexCount = mesh.indexCount;
    draw.vertexCount = mesh.vertexCount;
    draw.pipeline = material.pipeline;
    for ( common::u32 i = 0u; i < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++i ) {
        draw.sampledTextures[i] = i < material.textureCount
            ? material.textures[i] : R_INVALID_TEXTURE;
        draw.samplers[i] = R_INVALID_SAMPLER;
    }
    draw.uniformBuffers[description.modelUniformSlot] =
        description.modelUniforms->buffer;
    draw.uniformRanges[description.modelUniformSlot] = {
        modelOffset, sizeof( math::mat4_t )
    };
    output->objectId = snapshot.id;
    output->draw = draw;
}

} // namespace

render_error_t R_PrepareOpaqueWorldDraws(
    const render_world_opaque_prepare_desc_t &description,
    render_world_opaque_draw_t *output,
    const common::u32 outputCapacity,
    common::u32 *outCount ) noexcept
{
    if ( description.submission == nullptr || outCount == nullptr ||
         description.modelUniformSlot >= R_MAX_UNIFORM_BLOCK_BINDINGS ||
         description.drawTemplate.instanceCount != 1u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    common::u32 visibleCount = 0u;
    if ( world::WorldSubmission_Count(
             description.submission, description.token, &visibleCount ) !=
         world::world_submission_error_t::OK ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( visibleCount > outputCapacity ) {
        return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
    }
    if ( visibleCount == 0u ) {
        *outCount = 0u;
        return render_error_t::OK;
    }
    if ( output == nullptr || description.meshes == nullptr ||
         description.materials == nullptr ||
         description.modelUniforms == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    render_error_t result = ValidateBatch( *description.modelUniforms );
    if ( result != render_error_t::OK ) return result;

    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        if ( world::WorldSubmission_Get(
                 description.submission, description.token, i, &snapshot ) !=
             world::world_submission_error_t::OK ) {
            return render_error_t::ERR_INVALID_STATE;
        }
        result = PreflightObject( description, snapshot );
        if ( result != render_error_t::OK ) return result;
    }
    const common::u64 firstCursor = description.modelUniforms->usedBytes;
    common::u64 cursor = firstCursor;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        common::u64 offset = 0u;
        if ( !NextModelOffset( cursor, description.modelUniforms->alignment,
                 description.modelUniforms->capacityBytes, &offset, &cursor ) ) {
            return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
        }
    }

    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        if ( world::WorldSubmission_Get(
                 description.submission, description.token, i, &snapshot ) !=
             world::world_submission_error_t::OK ) {
            return render_error_t::ERR_INVALID_STATE;
        }
        const math::mat4_t model = math::Affine3_ToMat4( snapshot.object.localToWorld );
        render_uniform_record_t ignored{};
        result = R_AppendUniformRecord( description.modelUniforms,
            &model, sizeof( model ), &ignored );
        if ( result != render_error_t::OK ) return result;
    }
    result = R_CommitUniformBatch( description.modelUniforms );
    if ( result != render_error_t::OK ) return result;

    cursor = firstCursor;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        // The submission is immutable and remains active throughout this
        // single-threaded call; the preflight and staging reads succeeded.
        (void)world::WorldSubmission_Get(
            description.submission, description.token, i, &snapshot );
        common::u64 offset = 0u;
        (void)NextModelOffset( cursor, description.modelUniforms->alignment,
            description.modelUniforms->capacityBytes, &offset, &cursor );
        PublishDraw( description, snapshot, offset, &output[i] );
    }
    *outCount = visibleCount;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
