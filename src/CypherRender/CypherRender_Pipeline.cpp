//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Owns immutable pipeline records and their shader references.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Pipeline.h"
#include "CypherRender_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
struct pipeline_record_t {
    backend_pipeline_t native{};
    render_pipeline_info_t info{};
};
struct pipeline_system_t {
    common::handle_table_t<pipeline_record_t> records{};
    bool initialized{ false };
};
pipeline_system_t pipelineSystem{};

render_error_t R_ResolvePipeline(
    const render_pipeline_handle_t pipeline,
    common::handle32_t &tableHandleOut,
    pipeline_record_t *&recordOut ) noexcept
{
    recordOut = nullptr;
    tableHandleOut = common::CY_HANDLE32_INVALID;
    if ( !common::Cy_Handle64IsValid( pipeline ) ) return render_error_t::ERR_INVALID_HANDLE;
    if ( common::Cy_Handle64Type( pipeline ) !=
         static_cast<common::u32>( render_object_type_t::PIPELINE ) ) {
        return render_error_t::ERR_RESOURCE_TYPE_MISMATCH;
    }
    const common::u32 index = common::Cy_Handle64Index( pipeline );
    const common::u32 generation = common::Cy_Handle64Generation( pipeline );
    if ( index > common::CY_HANDLE32_INDEX_MAX || generation == 0u ||
         !common::Cy_Handle32TryMake( index, generation, &tableHandleOut ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    recordOut = common::HandleTable_Get( &pipelineSystem.records, tableHandleOut );
    return recordOut != nullptr ? render_error_t::OK : render_error_t::ERR_STALE_HANDLE;
}
} // namespace

render_error_t R_PipelineSystemInit() noexcept
{
    if ( pipelineSystem.initialized ) return render_error_t::ERR_ALREADY_INITIALIZED;
    if ( !common::HandleTable_Init(
             &pipelineSystem.records, common::Allocator_GetSystem(), 128u ) ) {
        return render_error_t::ERR_OUT_OF_MEMORY;
    }
    pipelineSystem.initialized = true;
    return render_error_t::OK;
}

render_error_t R_PipelineSystemShutdown() noexcept
{
    if ( !pipelineSystem.initialized ) return render_error_t::OK;
    render_error_t firstError = render_error_t::OK;
    (void)common::HandleTable_ForEach(
        &pipelineSystem.records,
        [&]( common::handle32_t, pipeline_record_t &record ) noexcept {
            const render_error_t destroyResult = tr.backend != nullptr
                ? tr.backend->DestroyGraphicsPipeline( record.native, tr.backend->state )
                : render_error_t::ERR_INTERNAL_ERROR;
            const render_error_t releaseResult = R_ShaderReleaseReference( record.info.shader );
            if ( firstError == render_error_t::OK ) {
                firstError = destroyResult != render_error_t::OK ? destroyResult : releaseResult;
            }
            return common::CY_TRUE;
        } );
    common::HandleTable_Shutdown( &pipelineSystem.records );
    pipelineSystem.initialized = false;
    return firstError;
}

render_error_t R_CreateGraphicsPipeline(
    const render_pipeline_desc_t &description,
    render_pipeline_handle_t *pipelineOut ) noexcept
{
    if ( pipelineOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *pipelineOut = R_INVALID_PIPELINE;
    if ( !tr.initialized || !pipelineSystem.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( R_ValidateVertexLayout( description.vertexLayout ) != render_error_t::OK ) {
        return render_error_t::ERR_VERTEX_LAYOUT_INVALID;
    }
    for ( common::u32 i = 0u; i < description.vertexLayout.bindingCount; ++i ) {
        if ( description.vertexLayout.bindings[i].inputRate !=
             render_vertex_input_rate_t::PER_VERTEX ) {
            return render_error_t::ERR_PIPELINE_DESC_INVALID;
        }
    }
    const bool hasBlock = description.uniformBlockName != nullptr;
    if ( hasBlock != ( description.uniformBlockBytes != 0u ) ||
         ( hasBlock && description.uniformBlockName[0] == '\0' ) ) {
        return render_error_t::ERR_PIPELINE_DESC_INVALID;
    }
    if ( description.uniformBlockBytes > tr.info.limits.maxUniformBlockBytes ||
         ( hasBlock && tr.info.limits.maxUniformBufferBindings == 0u ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }

    if ( description.sampledTextureName != nullptr ) {
        if ( description.sampledTextureName[0] == '\0' ) return render_error_t::ERR_PIPELINE_DESC_INVALID;
        if ( tr.backend->CreateTexture2D == nullptr || tr.backend->DestroyTexture == nullptr ) return render_error_t::ERR_UNSUPPORTED;
        if ( tr.info.limits.maxCombinedTextureUnits == 0u ) return render_error_t::ERR_CAPABILITY_MISSING;
    }

    backend_pipeline_desc_t nativeDescription{};
    render_error_t result = R_ShaderAcquireReference( description.shader, &nativeDescription.shader );
    if ( result != render_error_t::OK ) return result;
    nativeDescription.vertexLayout = description.vertexLayout;
    nativeDescription.depthTest = description.depthTest;
    nativeDescription.depthWrite = description.depthWrite;
    nativeDescription.cullBackFaces = description.cullBackFaces;
    nativeDescription.frontCounterClockwise = description.frontCounterClockwise;
    nativeDescription.alphaBlend = description.alphaBlend;
    nativeDescription.uniformBlockName = description.uniformBlockName;
    nativeDescription.uniformBlockBytes = description.uniformBlockBytes;
    nativeDescription.debugName = description.debugName;
    nativeDescription.sampledTextureName = description.sampledTextureName;

    pipeline_record_t record{};
    result = tr.backend->CreateGraphicsPipeline( nativeDescription, record.native, tr.backend->state );
    if ( result != render_error_t::OK || !R_IsBackendPipelineValid( record.native ) ) {
        (void)R_ShaderReleaseReference( description.shader );
        return result != render_error_t::OK ? result : render_error_t::ERR_INTERNAL_ERROR;
    }
    record.info.shader = description.shader;
    record.info.vertexLayout = description.vertexLayout;
    record.info.depthTest = description.depthTest;
    record.info.depthWrite = description.depthWrite;
    record.info.cullBackFaces = description.cullBackFaces;
    record.info.frontCounterClockwise = description.frontCounterClockwise;
    record.info.alphaBlend = description.alphaBlend;
    record.info.uniformBlockBytes = description.uniformBlockBytes;
    record.info.sampledTextureRequired = description.sampledTextureName != nullptr;
    const common::handle32_t tableHandle = common::HandleTable_Insert( &pipelineSystem.records, record );
    if ( !common::Cy_Handle32IsValid( tableHandle ) ) {
        (void)tr.backend->DestroyGraphicsPipeline( record.native, tr.backend->state );
        (void)R_ShaderReleaseReference( description.shader );
        return common::HandleTable_Count( &pipelineSystem.records ) >= common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED : render_error_t::ERR_OUT_OF_MEMORY;
    }
    *pipelineOut = common::Cy_Handle64Make(
        common::Cy_Handle32Index( tableHandle ), common::Cy_Handle32Generation( tableHandle ),
        static_cast<common::u32>( render_object_type_t::PIPELINE ) );
    if ( !common::Cy_Handle64IsValid( *pipelineOut ) ) {
        (void)common::HandleTable_Remove( &pipelineSystem.records, tableHandle );
        (void)tr.backend->DestroyGraphicsPipeline( record.native, tr.backend->state );
        (void)R_ShaderReleaseReference( description.shader );
        return render_error_t::ERR_INTERNAL_ERROR;
    }
    return render_error_t::OK;
}

render_error_t R_DestroyGraphicsPipeline( const render_pipeline_handle_t pipeline ) noexcept
{
    if ( !tr.initialized || !pipelineSystem.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    common::handle32_t tableHandle{};
    pipeline_record_t *record = nullptr;
    const render_error_t resolveResult = R_ResolvePipeline( pipeline, tableHandle, record );
    if ( resolveResult != render_error_t::OK ) return resolveResult;
    const render_error_t destroyResult = tr.backend->DestroyGraphicsPipeline( record->native, tr.backend->state );
    if ( destroyResult != render_error_t::OK ) return destroyResult;
    const render_error_t releaseResult = R_ShaderReleaseReference( record->info.shader );
    if ( !common::HandleTable_Remove( &pipelineSystem.records, tableHandle ) ) {
        return render_error_t::ERR_INTERNAL_ERROR;
    }
    return releaseResult;
}

bool R_IsGraphicsPipelineValid( const render_pipeline_handle_t pipeline ) noexcept
{
    if ( !tr.initialized || !pipelineSystem.initialized ) return false;
    common::handle32_t tableHandle{};
    pipeline_record_t *record = nullptr;
    return R_ResolvePipeline( pipeline, tableHandle, record ) == render_error_t::OK;
}

render_error_t R_GetGraphicsPipelineInfo(
    const render_pipeline_handle_t pipeline, render_pipeline_info_t *infoOut ) noexcept
{
    if ( infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *infoOut = {};
    backend_pipeline_t ignored{};
    return R_PipelineResolveForDraw( pipeline, &ignored, infoOut );
}

render_error_t R_PipelineResolveForDraw(
    const render_pipeline_handle_t pipeline,
    backend_pipeline_t *nativeOut, render_pipeline_info_t *infoOut ) noexcept
{
    if ( nativeOut == nullptr || infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *nativeOut = R_INVALID_BACKEND_PIPELINE;
    *infoOut = {};
    if ( !tr.initialized || !pipelineSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t tableHandle{};
    pipeline_record_t *record = nullptr;
    const render_error_t result = R_ResolvePipeline( pipeline, tableHandle, record );
    if ( result != render_error_t::OK ) return result;
    *nativeOut = record->native;
    *infoOut = record->info;
    return render_error_t::OK;
}

} // namespace cypher::engine::render
