//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Resolves one visible World submission into ordered indexed draws.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_WorldDraw.h"

#include "CypherCommon/Formats/CypherCommon_CookedMaterial.h"
#include "CypherRender/CypherRender_Public.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace world = ::cypher::engine::world;
namespace
{

static_assert( sizeof( math::mat4_t ) == 64u );

bool NonnegativeFinite( const math::vec3_t value ) noexcept
{
    return math::Vec3_IsFinite( value ) &&
           value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
}

struct alignas( 16 ) world_model_normal_record_t {
    math::mat4_t model{};
    common::f32 normalColumns[3][4]{};
};
static_assert( sizeof( world_model_normal_record_t ) == 112u );
static_assert( offsetof( world_model_normal_record_t, normalColumns ) == 64u );

struct alignas( 16 ) world_model_cutout_record_t {
    math::mat4_t model{};
    common::f32 cutout[4]{};
};
static_assert( sizeof( world_model_cutout_record_t ) == 80u );
static_assert( offsetof( world_model_cutout_record_t, cutout ) == 64u );

struct alignas( 16 ) world_model_normal_cutout_record_t {
    world_model_normal_record_t lit{};
    common::f32 cutout[4]{};
};
static_assert( sizeof( world_model_normal_cutout_record_t ) == 128u );
static_assert( offsetof( world_model_normal_cutout_record_t, cutout ) == 112u );

bool HasNormalMatrix( const render_world_model_layout_t layout ) noexcept
{
    return layout == render_world_model_layout_t::MODEL_NORMAL ||
           layout == render_world_model_layout_t::MODEL_NORMAL_CUTOUT;
}

bool HasCutout( const render_world_model_layout_t layout ) noexcept
{
    return layout == render_world_model_layout_t::MODEL_CUTOUT ||
           layout == render_world_model_layout_t::MODEL_NORMAL_CUTOUT;
}

common::u64 ModelRecordByteSize(
    const render_world_model_layout_t layout ) noexcept
{
    switch ( layout ) {
    case render_world_model_layout_t::MODEL_ONLY:
        return sizeof( math::mat4_t );
    case render_world_model_layout_t::MODEL_NORMAL:
        return sizeof( world_model_normal_record_t );
    case render_world_model_layout_t::MODEL_CUTOUT:
        return sizeof( world_model_cutout_record_t );
    case render_world_model_layout_t::MODEL_NORMAL_CUTOUT:
        return sizeof( world_model_normal_cutout_record_t );
    }
    return 0u;
}

bool PackModelNormal(
    const math::affine3_t &transform,
    world_model_normal_record_t *output ) noexcept
{
    if ( output == nullptr || !math::Affine3_IsFinite( transform ) ) return false;
    const math::mat3_t linear = math::Affine3_LinearPart( transform );
    const common::f64 a[3][3]{
        { linear.m[0], linear.m[1], linear.m[2] },
        { linear.m[3], linear.m[4], linear.m[5] },
        { linear.m[6], linear.m[7], linear.m[8] }
    }; // Column-major basis vectors promoted before products.
    // The columns of inverse-transpose are the three cyclic cofactors divided
    // by determinant. Binary64 keeps small but representable float transforms
    // from underflowing during determinant/inverse calculation.
    const common::f64 cofactors[3][3]{
        { a[1][1] * a[2][2] - a[1][2] * a[2][1],
          a[1][2] * a[2][0] - a[1][0] * a[2][2],
          a[1][0] * a[2][1] - a[1][1] * a[2][0] },
        { a[2][1] * a[0][2] - a[2][2] * a[0][1],
          a[2][2] * a[0][0] - a[2][0] * a[0][2],
          a[2][0] * a[0][1] - a[2][1] * a[0][0] },
        { a[0][1] * a[1][2] - a[0][2] * a[1][1],
          a[0][2] * a[1][0] - a[0][0] * a[1][2],
          a[0][0] * a[1][1] - a[0][1] * a[1][0] }
    };
    const common::f64 determinant =
        a[0][0] * cofactors[0][0] +
        a[0][1] * cofactors[0][1] +
        a[0][2] * cofactors[0][2];
    if ( !std::isfinite( determinant ) || determinant == 0.0 ) return false;

    world_model_normal_record_t packed{};
    packed.model = math::Affine3_ToMat4( transform );
    const common::f64 maximum = std::numeric_limits<common::f32>::max();
    for ( common::u32 column = 0u; column < 3u; ++column ) {
        for ( common::u32 row = 0u; row < 3u; ++row ) {
            const common::f64 component = cofactors[column][row] / determinant;
            if ( !std::isfinite( component ) || std::fabs( component ) > maximum ) {
                return false;
            }
            packed.normalColumns[column][row] =
                static_cast<common::f32>( component );
        }
    }
    *output = packed;
    return true;
}

int Orientation( const math::mat3_t &linear ) noexcept
{
    // Compute winding in binary64 so valid small float scales do not
    // underflow or hit an arbitrary inverse-matrix tolerance.
    const auto a = static_cast<common::f64>( linear.m[0] );
    const auto b = static_cast<common::f64>( linear.m[1] );
    const auto c = static_cast<common::f64>( linear.m[2] );
    const auto d = static_cast<common::f64>( linear.m[3] );
    const auto e = static_cast<common::f64>( linear.m[4] );
    const auto f = static_cast<common::f64>( linear.m[5] );
    const auto g = static_cast<common::f64>( linear.m[6] );
    const auto h = static_cast<common::f64>( linear.m[7] );
    const auto i = static_cast<common::f64>( linear.m[8] );
    const common::f64 determinant = a * ( e * i - h * f ) -
        d * ( b * i - h * c ) + g * ( b * f - e * c );
    return ( determinant > 0.0 ) - ( determinant < 0.0 );
}

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
    const common::u64 recordBytes,
    common::u64 *offsetOut,
    common::u64 *nextOut ) noexcept
{
    if ( alignment == 0u || recordBytes == 0u || cursor > capacity ) return false;
    const common::u64 remainder = cursor % alignment;
    const common::u64 padding = remainder == 0u ? 0u : alignment - remainder;
    if ( padding > capacity - cursor ) return false;
    const common::u64 offset = cursor + padding;
    if ( recordBytes > capacity - offset ) return false;
    *offsetOut = offset;
    *nextOut = offset + recordBytes;
    return true;
}

render_error_t ValidateBatch(
    const render_uniform_batch_t &batch,
    const common::u64 recordBytes ) noexcept
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
    if ( recordBytes == 0u || info->limits.maxUniformBlockBytes < recordBytes ) {
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

render_error_t AppendModelRecord(
    const render_world_opaque_prepare_desc_t &description,
    const world::world_object_snapshot_t &snapshot ) noexcept
{
    render_uniform_record_t ignored{};
    const render_material_bundle_t &material =
        description.materials[snapshot.object.materialSlot];
    const common::f32 cutoff = material.alphaMode ==
            common::render_material_alpha_mode_t::MASK
        ? material.alphaCutoff : -1.0F;
    if ( description.modelLayout ==
         render_world_model_layout_t::MODEL_NORMAL_CUTOUT ) {
        world_model_normal_cutout_record_t record{};
        if ( !PackModelNormal( snapshot.object.localToWorld, &record.lit ) ) {
            return render_error_t::ERR_UNSUPPORTED;
        }
        record.cutout[0] = cutoff;
        return R_AppendUniformRecord( description.modelUniforms,
            &record, sizeof( record ), &ignored );
    }
    if ( description.modelLayout == render_world_model_layout_t::MODEL_NORMAL ) {
        world_model_normal_record_t record{};
        // Preflight already packed this same immutable World transform.
        if ( !PackModelNormal( snapshot.object.localToWorld, &record ) ) {
            return render_error_t::ERR_UNSUPPORTED;
        }
        return R_AppendUniformRecord( description.modelUniforms,
            &record, sizeof( record ), &ignored );
    }
    if ( description.modelLayout == render_world_model_layout_t::MODEL_CUTOUT ) {
        world_model_cutout_record_t record{};
        record.model = math::Affine3_ToMat4( snapshot.object.localToWorld );
        record.cutout[0] = cutoff;
        return R_AppendUniformRecord( description.modelUniforms,
            &record, sizeof( record ), &ignored );
    }
    const math::mat4_t model =
        math::Affine3_ToMat4( snapshot.object.localToWorld );
    return R_AppendUniformRecord( description.modelUniforms,
        &model, sizeof( model ), &ignored );
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
    const world::world_object_snapshot_t &snapshot,
    const bool opaqueOnly ) noexcept
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
    const int orientation = Orientation( linear );
    if ( orientation == 0 ) {
        // A zero determinant collapses triangles and has no front face.
        return render_error_t::ERR_UNSUPPORTED;
    }
    if ( HasNormalMatrix( description.modelLayout ) ) {
        world_model_normal_record_t record{};
        if ( !PackModelNormal( object.localToWorld, &record ) ) {
            // Even a nonsingular transform can have a normal inverse outside
            // the shader's finite float range.
            return render_error_t::ERR_UNSUPPORTED;
        }
    }

    const render_material_bundle_t &material =
        description.materials[object.materialSlot];
    render_blend_mode_t expectedBlend = render_blend_mode_t::DISABLED;
    if ( material.alphaMode == common::render_material_alpha_mode_t::BLEND &&
         !opaqueOnly ) {
        expectedBlend = render_blend_mode_t::STRAIGHT_ALPHA;
    } else if ( material.alphaMode == common::render_material_alpha_mode_t::ADDITIVE &&
                !opaqueOnly ) {
        expectedBlend = render_blend_mode_t::ADDITIVE;
    } else if ( material.alphaMode == common::render_material_alpha_mode_t::MASK ) {
        if ( !HasCutout( description.modelLayout ) ||
             material.textureCount == 0u ||
             !( material.alphaCutoff >= 0.0F && material.alphaCutoff <= 1.0F ) ) {
            return render_error_t::ERR_UNSUPPORTED;
        }
    } else if ( material.alphaMode != common::render_material_alpha_mode_t::OPAQUE ) {
        return render_error_t::ERR_UNSUPPORTED;
    }
    render_draw_indexed_desc_t draw = description.drawTemplate;
    render_error_t error = R_ApplyStaticMeshBundleToDraw(
        description.meshes[object.meshSlot], &draw );
    if ( error != render_error_t::OK ) return error;
    error = R_ApplyCookedMaterialBundleToDraw(
        material, orientation < 0, &draw );
    if ( error != render_error_t::OK ) return error;

    render_pipeline_info_t pipeline{};
    error = R_GetGraphicsPipelineInfo( draw.pipeline, &pipeline );
    if ( error != render_error_t::OK ) return error;
    if ( material.alphaMode == common::render_material_alpha_mode_t::MASK ) {
        if ( pipeline.sampledTextureCount == 0u ||
             pipeline.sampledTextureKinds[0] !=
                 render_sampled_texture_kind_t::SCALAR_2D ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
        render_texture_info_t alphaSource{};
        error = R_GetTextureInfo( draw.sampledTextures[0], &alphaSource );
        if ( error != render_error_t::OK ) return error;
        if ( alphaSource.format != render_texture_format_t::RGBA8 &&
             alphaSource.format != render_texture_format_t::RGBA16F &&
             alphaSource.format != render_texture_format_t::RGBA32F ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
    }
    if ( pipeline.uniformBlockCount <= description.modelUniformSlot ||
         pipeline.uniformBlockBytes[description.modelUniformSlot] !=
             ModelRecordByteSize( description.modelLayout ) ||
         pipeline.blendMode != expectedBlend ||
         pipeline.depthWrite != ( expectedBlend == render_blend_mode_t::DISABLED ) ||
         ( !opaqueOnly && !pipeline.depthTest ) ||
         !pipeline.colorWrite ||
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
    const auto linear = math::Affine3_LinearPart( snapshot.object.localToWorld );
    draw.pipeline = Orientation( linear ) < 0
        ? material.mirroredPipeline : material.pipeline;
    for ( common::u32 i = 0u; i < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++i ) {
        if ( i < material.textureCount ) {
            draw.sampledTextures[i] = material.textures[i];
            draw.samplers[i] = R_INVALID_SAMPLER;
        } else if ( i != material.passShadowSlot ) {
            // Material Apply preflight checked the caller-owned shadow pair.
            // Leave that slot from drawTemplate intact only for a bundle that
            // explicitly declares it; ordinary unused slots remain empty.
            draw.sampledTextures[i] = R_INVALID_TEXTURE;
            draw.samplers[i] = R_INVALID_SAMPLER;
        }
    }
    if ( material.materialUniformBuffer.value != R_INVALID_BUFFER.value ) {
        // Material bridge preflight already verified this owned binding.
        draw.uniformBuffers[material.materialUniformSlot] =
            material.materialUniformBuffer;
        draw.uniformRanges[material.materialUniformSlot] = {};
    }
    draw.uniformBuffers[description.modelUniformSlot] =
        description.modelUniforms->buffer;
    draw.uniformRanges[description.modelUniformSlot] = {
        modelOffset, ModelRecordByteSize( description.modelLayout )
    };
    output->objectId = snapshot.id;
    output->draw = draw;
}

render_world_opaque_prepare_desc_t CameraDrawBase(
    const render_world_camera_prepare_desc_t &camera ) noexcept
{
    render_world_opaque_prepare_desc_t base{};
    base.submission = camera.submission;
    base.token = camera.token;
    base.meshes = camera.meshes;
    base.meshCount = camera.meshCount;
    base.materials = camera.materials;
    base.materialCount = camera.materialCount;
    base.modelUniforms = camera.modelUniforms;
    base.modelUniformSlot = camera.modelUniformSlot;
    base.modelLayout = camera.modelLayout;
    base.drawTemplate = camera.drawTemplate;
    return base;
}

bool StorageOverlaps( const void *first, common::u64 firstBytes,
    const void *second, common::u64 secondBytes ) noexcept
{
    if ( firstBytes == 0u || secondBytes == 0u ) return false;
    const auto firstBegin = reinterpret_cast<std::uintptr_t>( first );
    const auto secondBegin = reinterpret_cast<std::uintptr_t>( second );
    constexpr auto maxAddress = std::numeric_limits<std::uintptr_t>::max();
    if ( firstBytes > maxAddress - firstBegin ||
         secondBytes > maxAddress - secondBegin ) return true;
    const auto firstEnd = firstBegin + static_cast<std::uintptr_t>( firstBytes );
    const auto secondEnd = secondBegin + static_cast<std::uintptr_t>( secondBytes );
    return firstBegin < secondEnd && secondBegin < firstEnd;
}

render_error_t CameraScratchIsDisjoint(
    const render_world_camera_prepare_desc_t &description,
    const render_world_camera_draw_t *output,
    const render_world_camera_pass_counts_t *outCounts,
    common::u32 visibleCount ) noexcept
{
    const common::u64 sceneBytes = static_cast<common::u64>( visibleCount ) *
        sizeof( render_scene_item_t );
    const common::u64 indexBytes = static_cast<common::u64>( visibleCount ) *
        sizeof( common::u32 );
    const common::u64 outputBytes = static_cast<common::u64>( visibleCount ) *
        sizeof( render_world_camera_draw_t );
    const common::u64 meshBytes = static_cast<common::u64>( description.meshCount ) *
        sizeof( render_static_mesh_bundle_t );
    const common::u64 materialBytes = static_cast<common::u64>( description.materialCount ) *
        sizeof( render_material_bundle_t );
    const auto *batch = description.modelUniforms;
    const common::u64 stagingBytes = batch->capacityBytes;
    constexpr common::u64 countsBytes = sizeof( render_world_camera_pass_counts_t );
    if ( StorageOverlaps( description.sceneItemsScratch, sceneBytes,
             description.passIndicesScratch, indexBytes ) ||
         StorageOverlaps( description.sceneItemsScratch, sceneBytes,
             output, outputBytes ) ||
         StorageOverlaps( description.passIndicesScratch, indexBytes,
             output, outputBytes ) ||
         StorageOverlaps( description.sceneItemsScratch, sceneBytes,
             outCounts, countsBytes ) ||
         StorageOverlaps( description.passIndicesScratch, indexBytes,
             outCounts, countsBytes ) ||
         StorageOverlaps( output, outputBytes, outCounts, countsBytes ) ||
         StorageOverlaps( output, outputBytes,
             description.meshes, meshBytes ) ||
         StorageOverlaps( output, outputBytes,
             description.materials, materialBytes ) ||
         StorageOverlaps( outCounts, countsBytes,
             description.meshes, meshBytes ) ||
         StorageOverlaps( outCounts, countsBytes,
             description.materials, materialBytes ) ||
         StorageOverlaps( description.sceneItemsScratch, sceneBytes,
             description.meshes, meshBytes ) ||
         StorageOverlaps( description.sceneItemsScratch, sceneBytes,
             description.materials, materialBytes ) ||
         StorageOverlaps( description.passIndicesScratch, indexBytes,
             description.meshes, meshBytes ) ||
         StorageOverlaps( description.passIndicesScratch, indexBytes,
             description.materials, materialBytes ) ||
         StorageOverlaps( description.sceneItemsScratch, sceneBytes,
             batch->staging, stagingBytes ) ||
         StorageOverlaps( description.passIndicesScratch, indexBytes,
             batch->staging, stagingBytes ) ||
         StorageOverlaps( output, outputBytes,
             batch->staging, stagingBytes ) ||
         StorageOverlaps( outCounts, countsBytes,
             batch->staging, stagingBytes ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    return render_error_t::OK;
}

render_error_t SceneAlpha(
    const common::render_material_alpha_mode_t mode,
    render_scene_alpha_t *out ) noexcept
{
    switch ( mode ) {
    case common::render_material_alpha_mode_t::OPAQUE:
    case common::render_material_alpha_mode_t::MASK:
        *out = render_scene_alpha_t::OPAQUE;
        return render_error_t::OK;
    case common::render_material_alpha_mode_t::BLEND:
        *out = render_scene_alpha_t::BLEND;
        return render_error_t::OK;
    case common::render_material_alpha_mode_t::ADDITIVE:
        *out = render_scene_alpha_t::ADDITIVE;
        return render_error_t::OK;
    default:
        return render_error_t::ERR_UNSUPPORTED;
    }
}

render_error_t BlendViewDepth(
    const math::mat4_t &viewFromWorld,
    const math::aabb_t &bounds,
    common::f32 *outDepth ) noexcept
{
    // The World validates finite AABBs. Promote before taking midpoints and
    // applying the view matrix, because finite f32 endpoints can overflow a
    // float addition even though the eventual center/depth is representable.
    const common::f64 cx = ( static_cast<common::f64>( bounds.minimum.x ) +
        static_cast<common::f64>( bounds.maximum.x ) ) * 0.5;
    const common::f64 cy = ( static_cast<common::f64>( bounds.minimum.y ) +
        static_cast<common::f64>( bounds.maximum.y ) ) * 0.5;
    const common::f64 cz = ( static_cast<common::f64>( bounds.minimum.z ) +
        static_cast<common::f64>( bounds.maximum.z ) ) * 0.5;
    const common::f64 ex = ( static_cast<common::f64>( bounds.maximum.x ) -
        static_cast<common::f64>( bounds.minimum.x ) ) * 0.5;
    const common::f64 ey = ( static_cast<common::f64>( bounds.maximum.y ) -
        static_cast<common::f64>( bounds.minimum.y ) ) * 0.5;
    const common::f64 ez = ( static_cast<common::f64>( bounds.maximum.z ) -
        static_cast<common::f64>( bounds.minimum.z ) ) * 0.5;
    const auto *m = viewFromWorld.m;
    const common::f64 centerDepth = -(
        static_cast<common::f64>( m[2] ) * cx +
        static_cast<common::f64>( m[6] ) * cy +
        static_cast<common::f64>( m[10] ) * cz +
        static_cast<common::f64>( m[14] ) );
    const common::f64 frontExtent =
        std::fabs( static_cast<common::f64>( m[2] ) ) * ex +
        std::fabs( static_cast<common::f64>( m[6] ) ) * ey +
        std::fabs( static_cast<common::f64>( m[10] ) ) * ez;
    if ( !std::isfinite( centerDepth ) ||
         !std::isfinite( frontExtent ) ||
         centerDepth + frontExtent <= 0.0 ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    // A crossing-eye box has no positive center depth. Treat it as nearest
    // rather than rejecting visible geometry or inventing a camera distance.
    const common::f64 minimum = std::numeric_limits<common::f32>::min();
    const common::f64 maximum = std::numeric_limits<common::f32>::max();
    *outDepth = static_cast<common::f32>(
        centerDepth < minimum ? minimum :
        centerDepth > maximum ? maximum : centerDepth );
    return render_error_t::OK;
}

bool CastsShadow( const render_material_bundle_t &material ) noexcept
{
    return ( material.materialFlags &
        common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS ) != 0u;
}

common::u64 ShadowRecordBytes(
    const render_material_bundle_t &material ) noexcept
{
    return material.alphaMode == common::render_material_alpha_mode_t::MASK
        ? sizeof( world_model_cutout_record_t ) : sizeof( math::mat4_t );
}

render_pipeline_handle_t ShadowPipeline(
    const render_world_shadow_prepare_desc_t &description,
    const render_material_bundle_t &material,
    const int orientation ) noexcept
{
    if ( material.alphaMode == common::render_material_alpha_mode_t::MASK ) {
        return orientation < 0 ? description.mirroredMaskShadowPipeline
                               : description.maskShadowPipeline;
    }
    return orientation < 0 ? description.mirroredShadowPipeline
                           : description.shadowPipeline;
}

render_error_t PreflightShadowObject(
    const render_world_shadow_prepare_desc_t &description,
    const world::world_object_snapshot_t &snapshot ) noexcept
{
    const world::world_object_t &object = snapshot.object;
    if ( object.meshSlot >= description.meshCount ||
         object.materialSlot >= description.materialCount ||
         !math::Affine3_IsFinite( object.localToWorld ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    const int orientation = Orientation(
        math::Affine3_LinearPart( object.localToWorld ) );
    if ( orientation == 0 ) return render_error_t::ERR_UNSUPPORTED;
    const render_material_bundle_t &material =
        description.materials[object.materialSlot];
    const bool masked =
        material.alphaMode == common::render_material_alpha_mode_t::MASK;
    if ( !CastsShadow( material ) ||
         ( !masked && material.alphaMode !=
             common::render_material_alpha_mode_t::OPAQUE ) ) {
        return render_error_t::ERR_UNSUPPORTED;
    }
    if ( masked && ( material.textureCount == 0u ||
         material.textureCount > R_MAX_SAMPLED_TEXTURE_BINDINGS ||
         !( material.alphaCutoff >= 0.0F &&
            material.alphaCutoff <= 1.0F ) ) ) {
        return render_error_t::ERR_UNSUPPORTED;
    }

    render_draw_indexed_desc_t draw = description.drawTemplate;
    draw.pipeline = ShadowPipeline( description, material, orientation );
    render_error_t error = R_ApplyStaticMeshBundleToDraw(
        description.meshes[object.meshSlot], &draw );
    if ( error != render_error_t::OK ) return error;
    for ( common::u32 slot = 0u;
          slot < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++slot ) {
        if ( draw.sampledTextures[slot].value != R_INVALID_TEXTURE.value ||
             draw.samplers[slot].value != R_INVALID_SAMPLER.value ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
    }
    if ( masked ) draw.sampledTextures[0] = material.textures[0];

    render_pipeline_info_t pipeline{};
    error = R_GetGraphicsPipelineInfo( draw.pipeline, &pipeline );
    if ( error != render_error_t::OK ) return error;
    if ( masked ) {
        render_texture_info_t alphaSource{};
        error = R_GetTextureInfo( draw.sampledTextures[0], &alphaSource );
        if ( error != render_error_t::OK ) return error;
        if ( alphaSource.format != render_texture_format_t::RGBA8 &&
             alphaSource.format != render_texture_format_t::RGBA16F &&
             alphaSource.format != render_texture_format_t::RGBA32F ) {
            return render_error_t::ERR_BINDING_MISMATCH;
        }
    }
    if ( pipeline.uniformBlockCount <= description.modelUniformSlot ||
         pipeline.uniformBlockBytes[description.modelUniformSlot] !=
             ShadowRecordBytes( material ) ||
         pipeline.sampledTextureCount != ( masked ? 1u : 0u ) ||
         ( masked && pipeline.sampledTextureKinds[0] !=
             render_sampled_texture_kind_t::SCALAR_2D ) ||
         pipeline.blendMode != render_blend_mode_t::DISABLED ||
         !pipeline.depthTest || !pipeline.depthWrite ||
         pipeline.colorWrite ||
         pipeline.topology != render_primitive_topology_t::TRIANGLE_LIST ||
         pipeline.polygonMode != render_polygon_mode_t::FILL ||
         pipeline.frontCounterClockwise != ( orientation > 0 ) ||
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

void PublishShadowDraw(
    const render_world_shadow_prepare_desc_t &description,
    const world::world_object_snapshot_t &snapshot,
    const common::u64 modelOffset,
    render_world_shadow_draw_t *output ) noexcept
{
    const render_static_mesh_bundle_t &mesh =
        description.meshes[snapshot.object.meshSlot];
    const render_material_bundle_t &material =
        description.materials[snapshot.object.materialSlot];
    render_draw_indexed_desc_t draw = description.drawTemplate;
    draw.vertexInput = mesh.vertexInput;
    draw.firstIndex = 0u;
    draw.indexCount = mesh.indexCount;
    draw.vertexCount = mesh.vertexCount;
    draw.pipeline = ShadowPipeline( description, material, Orientation(
        math::Affine3_LinearPart( snapshot.object.localToWorld ) ) );
    if ( material.alphaMode == common::render_material_alpha_mode_t::MASK ) {
        draw.sampledTextures[0] = material.textures[0];
    }
    draw.uniformBuffers[description.modelUniformSlot] =
        description.modelUniforms->buffer;
    draw.uniformRanges[description.modelUniformSlot] = {
        modelOffset, ShadowRecordBytes( material )
    };
    output->objectId = snapshot.id;
    output->draw = draw;
}

render_error_t ShadowOutputIsDisjoint(
    const render_world_shadow_prepare_desc_t &description,
    const render_world_shadow_draw_t *output,
    const common::u32 casterCount,
    const common::u32 *outCount ) noexcept
{
    const common::u64 outputBytes =
        static_cast<common::u64>( casterCount ) * sizeof( *output );
    const common::u64 meshBytes =
        static_cast<common::u64>( description.meshCount ) *
        sizeof( render_static_mesh_bundle_t );
    const common::u64 materialBytes =
        static_cast<common::u64>( description.materialCount ) *
        sizeof( render_material_bundle_t );
    const common::u64 stagingBytes = description.modelUniforms->capacityBytes;
    if ( StorageOverlaps( output, outputBytes, outCount, sizeof( *outCount ) ) ||
         StorageOverlaps( output, outputBytes,
             description.meshes, meshBytes ) ||
         StorageOverlaps( output, outputBytes,
             description.materials, materialBytes ) ||
         StorageOverlaps( output, outputBytes,
             description.modelUniforms->staging, stagingBytes ) ||
         StorageOverlaps( output, outputBytes,
             description.modelUniforms,
             sizeof( *description.modelUniforms ) ) ||
         StorageOverlaps( outCount, sizeof( *outCount ),
             description.meshes, meshBytes ) ||
         StorageOverlaps( outCount, sizeof( *outCount ),
             description.materials, materialBytes ) ||
         StorageOverlaps( outCount, sizeof( *outCount ),
             description.modelUniforms->staging, stagingBytes ) ||
         StorageOverlaps( outCount, sizeof( *outCount ),
             description.modelUniforms,
             sizeof( *description.modelUniforms ) ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    return render_error_t::OK;
}

} // namespace

render_error_t R_PackWorldViewLighting(
    const render_world_view_lighting_desc_t &description,
    render_world_view_lighting_record_t *output ) noexcept
{
    if ( output == nullptr || description.pointLightCount > 2u ||
         !math::Mat4_IsAffine( description.viewFromWorld, 0.0f ) ||
         !math::Mat4_IsFinite( description.clipFromView ) ||
         !NonnegativeFinite( description.ambientLight ) ||
         !NonnegativeFinite( description.directionalRadiance ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    math::vec3_t direction{};
    if ( !math::Vec3_TryNormalize(
             description.directionToLight, 0.0f, &direction, nullptr ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    render_world_view_lighting_record_t packed{};
    packed.view = description.viewFromWorld;
    packed.projection = description.clipFromView;
    packed.ambientLight[0] = description.ambientLight.x;
    packed.ambientLight[1] = description.ambientLight.y;
    packed.ambientLight[2] = description.ambientLight.z;
    packed.lightDirection[0] = direction.x;
    packed.lightDirection[1] = direction.y;
    packed.lightDirection[2] = direction.z;
    packed.lightColor[0] = description.directionalRadiance.x;
    packed.lightColor[1] = description.directionalRadiance.y;
    packed.lightColor[2] = description.directionalRadiance.z;
    for ( common::u32 i = 0u; i < description.pointLightCount; ++i ) {
        const render_world_point_light_desc_t &light = description.pointLights[i];
        if ( !math::Vec3_IsFinite( light.position ) ||
             !std::isfinite( light.range ) || light.range <= 0.0f ||
             !NonnegativeFinite( light.color ) ||
             !std::isfinite( light.intensity ) || light.intensity < 0.0f ) {
            return render_error_t::ERR_INVALID_ARGUMENT;
        }
        packed.pointLightPositionRange[i][0] = light.position.x;
        packed.pointLightPositionRange[i][1] = light.position.y;
        packed.pointLightPositionRange[i][2] = light.position.z;
        packed.pointLightPositionRange[i][3] = light.range;
        packed.pointLightColorIntensity[i][0] = light.color.x;
        packed.pointLightColorIntensity[i][1] = light.color.y;
        packed.pointLightColorIntensity[i][2] = light.color.z;
        packed.pointLightColorIntensity[i][3] = light.intensity;
    }
    packed.pointLightControl[0] =
        static_cast<common::i32>( description.pointLightCount );
    *output = packed;
    return render_error_t::OK;
}

render_error_t R_PrepareOpaqueWorldDraws(
    const render_world_opaque_prepare_desc_t &description,
    render_world_opaque_draw_t *output,
    const common::u32 outputCapacity,
    common::u32 *outCount ) noexcept
{
    if ( description.submission == nullptr || outCount == nullptr ||
         description.modelUniformSlot >= R_MAX_UNIFORM_BLOCK_BINDINGS ||
         ModelRecordByteSize( description.modelLayout ) == 0u ||
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
    const common::u64 recordBytes = ModelRecordByteSize( description.modelLayout );
    render_error_t result = ValidateBatch( *description.modelUniforms, recordBytes );
    if ( result != render_error_t::OK ) return result;

    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        if ( world::WorldSubmission_Get(
                 description.submission, description.token, i, &snapshot ) !=
             world::world_submission_error_t::OK ) {
            return render_error_t::ERR_INVALID_STATE;
        }
        result = PreflightObject( description, snapshot, true );
        if ( result != render_error_t::OK ) return result;
    }
    const common::u64 firstCursor = description.modelUniforms->usedBytes;
    common::u64 cursor = firstCursor;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        common::u64 offset = 0u;
        if ( !NextModelOffset( cursor, description.modelUniforms->alignment,
                 description.modelUniforms->capacityBytes, recordBytes,
                 &offset, &cursor ) ) {
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
        result = AppendModelRecord( description, snapshot );
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
            description.modelUniforms->capacityBytes, recordBytes,
            &offset, &cursor );
        PublishDraw( description, snapshot, offset, &output[i] );
    }
    *outCount = visibleCount;
    return render_error_t::OK;
}

render_error_t R_PrepareWorldCameraDraws(
    const render_world_camera_prepare_desc_t &description,
    render_world_camera_draw_t *output,
    const common::u32 outputCapacity,
    render_world_camera_pass_counts_t *outCounts ) noexcept
{
    if ( description.submission == nullptr || outCounts == nullptr ||
         description.modelUniformSlot >= R_MAX_UNIFORM_BLOCK_BINDINGS ||
         ModelRecordByteSize( description.modelLayout ) == 0u ||
         description.drawTemplate.instanceCount != 1u ||
         !math::Mat4_IsAffine( description.viewFromWorld, 0.0f ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    common::u32 visibleCount = 0u;
    if ( world::WorldSubmission_Count(
             description.submission, description.token, &visibleCount ) !=
         world::world_submission_error_t::OK ) {
        return render_error_t::ERR_INVALID_STATE;
    }
    if ( visibleCount > outputCapacity ||
         visibleCount > description.sceneItemCapacity ||
         visibleCount > description.passIndexCapacity ) {
        return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
    }
    if ( visibleCount == 0u ) {
        *outCounts = {};
        return render_error_t::OK;
    }
    if ( output == nullptr || description.meshes == nullptr ||
         description.materials == nullptr ||
         description.modelUniforms == nullptr ||
         description.sceneItemsScratch == nullptr ||
         description.passIndicesScratch == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    const render_world_opaque_prepare_desc_t base = CameraDrawBase( description );
    const common::u64 recordBytes = ModelRecordByteSize( description.modelLayout );
    render_error_t result = ValidateBatch( *description.modelUniforms, recordBytes );
    if ( result != render_error_t::OK ) return result;
    result = CameraScratchIsDisjoint(
        description, output, outCounts, visibleCount );
    if ( result != render_error_t::OK ) return result;

    render_world_camera_pass_counts_t counts{};
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        if ( world::WorldSubmission_Get(
                 description.submission, description.token, i, &snapshot ) !=
             world::world_submission_error_t::OK ) {
            return render_error_t::ERR_INVALID_STATE;
        }
        result = PreflightObject( base, snapshot, false );
        if ( result != render_error_t::OK ) return result;
        const render_material_bundle_t &material =
            description.materials[snapshot.object.materialSlot];
        render_scene_item_t sceneItem{};
        result = SceneAlpha( material.alphaMode, &sceneItem.alpha );
        if ( result != render_error_t::OK ) return result;
        sceneItem.cameraVisible = true;
        if ( sceneItem.alpha == render_scene_alpha_t::OPAQUE ) {
            ++counts.opaqueCount;
        } else if ( sceneItem.alpha == render_scene_alpha_t::BLEND ) {
            result = BlendViewDepth( description.viewFromWorld,
                snapshot.object.worldBounds, &sceneItem.cameraDistance );
            if ( result != render_error_t::OK ) return result;
            ++counts.blendCount;
        } else {
            ++counts.additiveCount;
        }
        description.sceneItemsScratch[i] = sceneItem;
    }

    render_scene_pass_indices_t passes{};
    passes.opaque = description.passIndicesScratch;
    passes.opaqueCapacity = counts.opaqueCount;
    passes.blend = description.passIndicesScratch + counts.opaqueCount;
    passes.blendCapacity = counts.blendCount;
    passes.additive = passes.blend + counts.blendCount;
    passes.additiveCapacity = counts.additiveCount;
    result = R_ClassifySceneItems(
        description.sceneItemsScratch, visibleCount, &passes );
    if ( result != render_error_t::OK ) return result;
    if ( passes.shadowCount != 0u ||
         passes.opaqueCount != counts.opaqueCount ||
         passes.blendCount != counts.blendCount ||
         passes.additiveCount != counts.additiveCount ) {
        return render_error_t::ERR_INTERNAL_ERROR;
    }

    const common::u64 firstCursor = description.modelUniforms->usedBytes;
    common::u64 cursor = firstCursor;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        common::u64 offset = 0u;
        if ( !NextModelOffset( cursor, description.modelUniforms->alignment,
                 description.modelUniforms->capacityBytes, recordBytes,
                 &offset, &cursor ) ) {
            return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
        }
    }

    // Stage in the final draw order. No per-object offset map is needed: the
    // same ordered indices are used again after the one batch upload.
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        if ( world::WorldSubmission_Get( description.submission,
                 description.token, description.passIndicesScratch[i],
                 &snapshot ) != world::world_submission_error_t::OK ) {
            return render_error_t::ERR_INVALID_STATE;
        }
        result = AppendModelRecord( base, snapshot );
        if ( result != render_error_t::OK ) return result;
    }
    result = R_CommitUniformBatch( description.modelUniforms );
    if ( result != render_error_t::OK ) return result;

    cursor = firstCursor;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        // The active submission and resource tables are immutable for this
        // single-threaded call, and every index was obtained by classification.
        (void)world::WorldSubmission_Get( description.submission,
            description.token, description.passIndicesScratch[i], &snapshot );
        common::u64 offset = 0u;
        (void)NextModelOffset( cursor, description.modelUniforms->alignment,
            description.modelUniforms->capacityBytes, recordBytes,
            &offset, &cursor );
        PublishDraw( base, snapshot, offset, &output[i] );
    }
    *outCounts = counts;
    return render_error_t::OK;
}

render_error_t R_PrepareWorldShadowDraws(
    const render_world_shadow_prepare_desc_t &description,
    render_world_shadow_draw_t *output,
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
    if ( visibleCount == 0u ) {
        *outCount = 0u;
        return render_error_t::OK;
    }
    if ( description.materials == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    common::u32 casterCount = 0u;
    bool hasMaskedCaster = false;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        if ( world::WorldSubmission_Get(
                 description.submission, description.token, i, &snapshot ) !=
             world::world_submission_error_t::OK ) {
            return render_error_t::ERR_INVALID_STATE;
        }
        if ( snapshot.object.materialSlot >= description.materialCount ) {
            return render_error_t::ERR_INVALID_ARGUMENT;
        }
        const render_material_bundle_t &material =
            description.materials[snapshot.object.materialSlot];
        if ( !CastsShadow( material ) ) continue;
        if ( material.alphaMode != common::render_material_alpha_mode_t::OPAQUE &&
             material.alphaMode != common::render_material_alpha_mode_t::MASK ) {
            return render_error_t::ERR_UNSUPPORTED;
        }
        hasMaskedCaster |= material.alphaMode ==
            common::render_material_alpha_mode_t::MASK;
        ++casterCount;
    }
    if ( casterCount > outputCapacity ) {
        return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
    }
    if ( casterCount == 0u ) {
        *outCount = 0u;
        return render_error_t::OK;
    }
    if ( output == nullptr || description.meshes == nullptr ||
         description.modelUniforms == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    render_error_t result = ValidateBatch( *description.modelUniforms,
        hasMaskedCaster ? sizeof( world_model_cutout_record_t )
                        : sizeof( math::mat4_t ) );
    if ( result != render_error_t::OK ) return result;
    result = ShadowOutputIsDisjoint(
        description, output, casterCount, outCount );
    if ( result != render_error_t::OK ) return result;

    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        if ( world::WorldSubmission_Get(
                 description.submission, description.token, i, &snapshot ) !=
             world::world_submission_error_t::OK ) {
            return render_error_t::ERR_INVALID_STATE;
        }
        if ( !CastsShadow( description.materials[
                 snapshot.object.materialSlot] ) ) continue;
        result = PreflightShadowObject( description, snapshot );
        if ( result != render_error_t::OK ) return result;
    }
    const common::u64 firstCursor = description.modelUniforms->usedBytes;
    common::u64 cursor = firstCursor;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        // The active submission remains immutable across preflight and staging.
        (void)world::WorldSubmission_Get(
            description.submission, description.token, i, &snapshot );
        const render_material_bundle_t &material =
            description.materials[snapshot.object.materialSlot];
        if ( !CastsShadow( material ) ) continue;
        common::u64 offset = 0u;
        if ( !NextModelOffset( cursor, description.modelUniforms->alignment,
                 description.modelUniforms->capacityBytes,
                 ShadowRecordBytes( material ), &offset, &cursor ) ) {
            return render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED;
        }
    }

    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        // The active submission is immutable during this single-threaded call.
        (void)world::WorldSubmission_Get(
            description.submission, description.token, i, &snapshot );
        if ( !CastsShadow( description.materials[
                 snapshot.object.materialSlot] ) ) continue;
        const render_material_bundle_t &material =
            description.materials[snapshot.object.materialSlot];
        render_uniform_record_t ignored{};
        if ( material.alphaMode == common::render_material_alpha_mode_t::MASK ) {
            world_model_cutout_record_t record{};
            record.model = math::Affine3_ToMat4( snapshot.object.localToWorld );
            record.cutout[0] = material.alphaCutoff;
            result = R_AppendUniformRecord( description.modelUniforms,
                &record, sizeof( record ), &ignored );
        } else {
            const math::mat4_t model =
                math::Affine3_ToMat4( snapshot.object.localToWorld );
            result = R_AppendUniformRecord( description.modelUniforms,
                &model, sizeof( model ), &ignored );
        }
        if ( result != render_error_t::OK ) return result;
    }
    result = R_CommitUniformBatch( description.modelUniforms );
    if ( result != render_error_t::OK ) return result;

    cursor = firstCursor;
    common::u32 published = 0u;
    for ( common::u32 i = 0u; i < visibleCount; ++i ) {
        world::world_object_snapshot_t snapshot{};
        (void)world::WorldSubmission_Get(
            description.submission, description.token, i, &snapshot );
        if ( !CastsShadow( description.materials[
                 snapshot.object.materialSlot] ) ) continue;
        const render_material_bundle_t &material =
            description.materials[snapshot.object.materialSlot];
        common::u64 offset = 0u;
        (void)NextModelOffset( cursor, description.modelUniforms->alignment,
            description.modelUniforms->capacityBytes,
            ShadowRecordBytes( material ), &offset, &cursor );
        PublishShadowDraw(
            description, snapshot, offset, &output[published++] );
    }
    *outCount = casterCount;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
