//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Adopts validated cooked material dependencies as caller-owned GPU state.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_MaterialBridge.h"
#include "CypherCommon/Tier1/CypherCommon_StringView.h"

#include <cstring>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
constexpr common::string_view_t kMaterialSuffix{ ".cymat_c", 8u };
constexpr common::string_view_t kShaderSuffix{ ".cyshader", 9u };
constexpr common::string_view_t kTextureSuffix{ ".cytex", 6u };
constexpr common::u32 kAcquiredCapacity = 2u + R_MAX_SAMPLED_TEXTURE_BINDINGS;

bool HasSuffix( const common::string_view_t path,
    const common::string_view_t suffix ) noexcept
{
    return common::StringView_IsValid( path ) &&
        path.cchLength >= suffix.cchLength &&
        std::memcmp( path.pData + path.cchLength - suffix.cchLength,
            suffix.pData, suffix.cchLength ) == 0;
}

bool CookedPath( const common::string_view_t authoredPath,
    const common::string_view_t suffix,
    char ( &storage )[resource::CYPHER_RESOURCE_PATH_BUFFER_SIZE],
    common::string_view_t &pathOut ) noexcept
{
    pathOut = {};
    if ( !HasSuffix( authoredPath, suffix ) ||
         authoredPath.cchLength > sizeof( storage ) - 3u ) return false;
    std::memcpy( storage, authoredPath.pData, authoredPath.cchLength );
    storage[authoredPath.cchLength] = '_';
    storage[authoredPath.cchLength + 1u] = 'c';
    storage[authoredPath.cchLength + 2u] = '\0';
    pathOut = { storage, authoredPath.cchLength + 2u };
    return true;
}

bool CopyBindingName( const common::string_view_t name,
    char ( &storage )[common::CY_COOKED_SHADER_MAX_BINDING_NAME_LENGTH + 1u] ) noexcept
{
    if ( !common::StringView_IsValid( name ) || name.cchLength == 0u ||
         name.cchLength >= sizeof( storage ) ) return false;
    std::memcpy( storage, name.pData, name.cchLength );
    storage[name.cchLength] = '\0';
    return true;
}

bool MaterialSubsetSupported( const common::cooked_material_view_t &material ) noexcept
{
    common::content_hash_t emptyVariantHash{};
    if ( !common::CookedMaterial_ComputeVariantHash( {}, &emptyVariantHash ) ) return false;
    if ( material.nResourceVersion != common::CY_COOKED_MATERIAL_RESOURCE_VERSION_V2 ||
         material.domain != common::render_material_domain_t::SURFACE ||
         ( material.alphaMode != common::render_material_alpha_mode_t::OPAQUE &&
           material.alphaMode != common::render_material_alpha_mode_t::MASK &&
           material.alphaMode != common::render_material_alpha_mode_t::BLEND ) ||
         !( material.alphaCutoff >= 0.0F && material.alphaCutoff <= 1.0F ) ||
         ( material.alphaMode != common::render_material_alpha_mode_t::MASK &&
           material.alphaCutoff != 0.5F ) || material.nFeatures != 0u ||
         material.nParameters != 0u ||
         material.nTextures > R_MAX_SAMPLED_TEXTURE_BINDINGS ||
         material.bHasSurface || material.surface.cchLength != 0u ||
         material.constantData.cbSize != 0u ||
         !common::ContentHash_Equals( material.variantHash, emptyVariantHash ) ) return false;
    for ( common::u32 i = 0u; i < material.nTextures; ++i ) {
        const auto &texture = material.textures[i];
        if ( texture.bHasSampler || texture.sampler.cchLength != 0u ||
             texture.nUvSet != 0u || texture.uvScale[0] != 1.0F ||
             texture.uvScale[1] != 1.0F || texture.uvOffset[0] != 0.0F ||
             texture.uvOffset[1] != 0.0F || texture.uvRotation != 0.0F ) return false;
    }
    return true;
}

bool TextureSubsetSupported( const common::cooked_texture_view_t &texture ) noexcept
{
    const auto &desc = texture.desc;
    return desc.dimension == common::render_texture_dimension_t::TEXTURE_2D &&
        desc.usage == common::render_texture_usage_t::COLOR &&
        desc.residency == common::cooked_texture_residency_t::FULLY_RESIDENT &&
        ( desc.pixelFormat == common::render_texture_pixel_format_t::RGBA8_UNORM ||
          desc.pixelFormat == common::render_texture_pixel_format_t::RGBA8_SRGB ) &&
        desc.nDepth == 1u && desc.nFrames == 1u && desc.nLayers == 1u &&
        desc.nFaces == 1u && desc.nMipLevels == texture.nMipLevels &&
        texture.nSubresources == texture.nMipLevels;
}

render_material_bridge_result_t ResourceFailure( resource::resource_error_t error ) noexcept
{
    return { render_material_bridge_status_t::RESOURCE_ERROR, error };
}

render_material_bridge_result_t RenderFailure( render_error_t error ) noexcept
{
    return { render_material_bridge_status_t::RENDER_ERROR,
        resource::resource_error_t::OK, error };
}

resource::resource_error_t ReleaseAcquired(
    resource::resource_manager_t *pResources,
    common::resource_handle_t ( &handles )[kAcquiredCapacity] ) noexcept
{
    resource::resource_error_t firstError = resource::resource_error_t::OK;
    for ( common::u32 i = kAcquiredCapacity; i > 0u; --i ) {
        auto &handle = handles[i - 1u];
        if ( !common::ResourceHandle_IsValid( handle ) ) continue;
        const auto error = resource::Res_Release( pResources, handle );
        if ( firstError == resource::resource_error_t::OK &&
             error != resource::resource_error_t::OK ) firstError = error;
        handle = {};
    }
    return firstError;
}
} // namespace

render_error_t R_DestroyCookedMaterialBundle(
    render_material_bundle_t *bundle ) noexcept
{
    if ( bundle == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    render_error_t firstError = render_error_t::OK;
    if ( bundle->pipeline.value != 0u ) {
        const render_error_t error = R_DestroyGraphicsPipeline( bundle->pipeline );
        if ( error != render_error_t::OK ) firstError = error;
        if ( error == render_error_t::OK ) bundle->pipeline = {};
    }
    // The pipeline retains its shader until destruction succeeds.
    if ( bundle->pipeline.value == 0u && bundle->shader.value != 0u ) {
        const render_error_t error = R_DestroyShader( bundle->shader );
        if ( firstError == render_error_t::OK ) firstError = error;
        if ( error == render_error_t::OK ) bundle->shader = {};
    }
    for ( common::u32 i = R_MAX_SAMPLED_TEXTURE_BINDINGS; i > 0u; --i ) {
        auto &texture = bundle->textures[i - 1u];
        if ( texture.value == 0u ) continue;
        const render_error_t error = R_DestroyTexture( texture );
        if ( firstError == render_error_t::OK ) firstError = error;
        if ( error == render_error_t::OK ) texture = {};
    }
    if ( bundle->pipeline.value == 0u && bundle->shader.value == 0u &&
         bundle->textures[0].value == 0u && bundle->textures[1].value == 0u ) {
        *bundle = {};
    }
    return firstError;
}

render_material_bridge_result_t R_CreateCookedMaterialBundle(
    const render_material_bundle_desc_t &description,
    render_material_bundle_t *bundleOut ) noexcept
{
    if ( bundleOut == nullptr ) {
        return { render_material_bridge_status_t::INVALID_ARGUMENT };
    }
    *bundleOut = {};
    if ( description.pResources == nullptr ||
         !HasSuffix( description.cookedMaterialPath, kMaterialSuffix ) ||
         description.uniformBlockCount > R_MAX_UNIFORM_BLOCK_BINDINGS ) {
        return { render_material_bridge_status_t::INVALID_ARGUMENT };
    }

    common::resource_handle_t acquired[kAcquiredCapacity]{};
    render_material_bundle_t pending{};
    render_material_bridge_result_t result{};
    const common::cooked_material_view_t *pMaterial = nullptr;
    const common::cooked_shader_view_t *pShader = nullptr;
    const common::cooked_texture_view_t *textures[R_MAX_SAMPLED_TEXTURE_BINDINGS]{};
    char cookedPath[resource::CYPHER_RESOURCE_PATH_BUFFER_SIZE]{};
    char textureBindingNames[R_MAX_SAMPLED_TEXTURE_BINDINGS]
        [common::CY_COOKED_SHADER_MAX_BINDING_NAME_LENGTH + 1u]{};
    render_pipeline_desc_t pipeline{};

    do {
        auto resourceError = resource::Res_Acquire( description.pResources,
            resource::Res_MaterialTypeId(), description.cookedMaterialPath,
            &acquired[0] );
        if ( resourceError != resource::resource_error_t::OK ) {
            result = ResourceFailure( resourceError );
            break;
        }
        resourceError = resource::Res_GetCookedMaterial( description.pResources,
            acquired[0], &pMaterial );
        if ( resourceError != resource::resource_error_t::OK ) {
            result = ResourceFailure( resourceError );
            break;
        }
        if ( pMaterial == nullptr || !MaterialSubsetSupported( *pMaterial ) ) {
            result.status = render_material_bridge_status_t::MATERIAL_UNSUPPORTED;
            break;
        }
        common::string_view_t path{};
        if ( !CookedPath( pMaterial->shader, kShaderSuffix,
                cookedPath, path ) ) {
            result.status = render_material_bridge_status_t::MATERIAL_UNSUPPORTED;
            break;
        }
        resourceError = resource::Res_Acquire( description.pResources,
            resource::Res_ShaderTypeId(), path, &acquired[1] );
        if ( resourceError != resource::resource_error_t::OK ) {
            result = ResourceFailure( resourceError );
            break;
        }
        resourceError = resource::Res_GetCookedShader( description.pResources,
            acquired[1], &pShader );
        if ( resourceError != resource::resource_error_t::OK ) {
            result = ResourceFailure( resourceError );
            break;
        }
        if ( pShader == nullptr || pShader->nResourceVersion !=
                common::CY_COOKED_SHADER_RESOURCE_VERSION_V3 ) {
            result.status = render_material_bridge_status_t::MATERIAL_UNSUPPORTED;
            break;
        }
        if ( !common::ContentHash_Equals( pMaterial->shaderInterfaceHash,
                pShader->interfaceHash ) ) {
            result.status = render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH;
            break;
        }

        pipeline.vertexLayout = description.vertexLayout;
        pipeline.uniformBlockCount = description.uniformBlockCount;
        pipeline.sampledTextureCount = pMaterial->nTextures;
        pipeline.alphaBlend =
            pMaterial->alphaMode == common::render_material_alpha_mode_t::BLEND;
        pipeline.depthWrite = !pipeline.alphaBlend;
        pipeline.cullBackFaces =
            ( pMaterial->flags & common::COOKED_MATERIAL_FLAG_TWO_SIDED ) == 0u;
        pipeline.debugName = description.debugName;
        for ( common::u32 i = 0u; i < description.uniformBlockCount; ++i ) {
            pipeline.uniformBlocks[i] = description.uniformBlocks[i];
        }

        for ( common::u32 i = 0u; i < pMaterial->nTextures; ++i ) {
            const auto &texture = pMaterial->textures[i];
            const auto *binding = common::CookedShader_FindBindingById(
                *pShader, texture.nLogicalBinding );
            if ( binding == nullptr ||
                 !common::StringView_Equals( binding->name, texture.binding ) ||
                 binding->kind != common::render_shader_binding_kind_t::SAMPLED_TEXTURE ||
                 binding->resourceType != common::render_shader_resource_type_t::TEXTURE_2D ||
                 binding->nArrayElements != 1u ||
                 !CopyBindingName( texture.binding, textureBindingNames[i] ) ) {
                result.status = render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH;
                break;
            }
            pipeline.sampledTextures[i].name = textureBindingNames[i];
        }
        if ( result.status != render_material_bridge_status_t::OK ) break;

        // Reject resources the two-slot draw contract cannot represent. Every
        // declared shader texture and uniform block must have exactly one owner.
        for ( common::u32 i = 0u; i < pShader->nBindings; ++i ) {
            const auto &binding = pShader->bindings[i];
            if ( binding.kind == common::render_shader_binding_kind_t::SAMPLED_TEXTURE ) {
                if ( common::CookedMaterial_FindTextureById(
                         *pMaterial, binding.nLogicalBinding ) == nullptr ) {
                    result.status = render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH;
                    break;
                }
            } else if ( binding.kind == common::render_shader_binding_kind_t::UNIFORM_BUFFER ) {
                bool found = false;
                for ( common::u32 block = 0u; block < description.uniformBlockCount; ++block ) {
                    const auto &declared = description.uniformBlocks[block];
                    if ( declared.name != nullptr &&
                         common::StringView_Equals( binding.name,
                             common::StringView_FromCString( declared.name ) ) &&
                         declared.byteSize == binding.cbByteSize &&
                         binding.resourceType ==
                             common::render_shader_resource_type_t::UNIFORM_BUFFER &&
                         binding.nArrayElements == 1u ) {
                        found = true;
                        break;
                    }
                }
                if ( !found ) {
                    result.status = render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH;
                    break;
                }
            } else if ( binding.kind != common::render_shader_binding_kind_t::VERTEX_INPUT &&
                        binding.kind != common::render_shader_binding_kind_t::FRAGMENT_OUTPUT ) {
                result.status = render_material_bridge_status_t::MATERIAL_UNSUPPORTED;
                break;
            }
        }
        if ( result.status != render_material_bridge_status_t::OK ) break;
        for ( common::u32 block = 0u; block < description.uniformBlockCount; ++block ) {
            const char *name = description.uniformBlocks[block].name;
            const auto *binding = name == nullptr ? nullptr :
                common::CookedShader_FindBinding( *pShader,
                    common::StringView_FromCString( name ) );
            if ( binding == nullptr ||
                 binding->kind != common::render_shader_binding_kind_t::UNIFORM_BUFFER ||
                 binding->resourceType != common::render_shader_resource_type_t::UNIFORM_BUFFER ||
                 binding->nArrayElements != 1u ||
                 binding->cbByteSize != description.uniformBlocks[block].byteSize ) {
                result.status = render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH;
                break;
            }
            for ( common::u32 earlier = 0u; earlier < block; ++earlier ) {
                if ( std::strcmp( description.uniformBlocks[earlier].name, name ) == 0 ) {
                    result.status = render_material_bridge_status_t::SHADER_INTERFACE_MISMATCH;
                    break;
                }
            }
            if ( result.status != render_material_bridge_status_t::OK ) break;
        }
        if ( result.status != render_material_bridge_status_t::OK ) break;

        // Acquire every dependency before the first GPU object is created. A
        // missing texture or mismatched interface cannot leave partial GPU state.
        for ( common::u32 i = 0u; i < pMaterial->nTextures; ++i ) {
            if ( !CookedPath( pMaterial->textures[i].texture, kTextureSuffix,
                    cookedPath, path ) ) {
                result.status = render_material_bridge_status_t::MATERIAL_UNSUPPORTED;
                break;
            }
            resourceError = resource::Res_Acquire( description.pResources,
                resource::Res_TextureTypeId(), path, &acquired[2u + i] );
            if ( resourceError != resource::resource_error_t::OK ) {
                result = ResourceFailure( resourceError );
                break;
            }
            resourceError = resource::Res_GetCookedTexture( description.pResources,
                acquired[2u + i], &textures[i] );
            if ( resourceError != resource::resource_error_t::OK ) {
                result = ResourceFailure( resourceError );
                break;
            }
            if ( textures[i] == nullptr || !TextureSubsetSupported( *textures[i] ) ) {
                result.status = render_material_bridge_status_t::MATERIAL_UNSUPPORTED;
                break;
            }
        }
        if ( result.status != render_material_bridge_status_t::OK ) break;

        render_error_t renderError = R_CreateShader(
            { pShader, description.debugName }, &pending.shader );
        if ( renderError != render_error_t::OK ) {
            result = RenderFailure( renderError );
            break;
        }
        pending.textureCount = pMaterial->nTextures;
        pending.alphaMode = pMaterial->alphaMode;
        pending.alphaCutoff = pMaterial->alphaCutoff;
        pending.materialFlags = pMaterial->flags;
        for ( common::u32 i = 0u; i < pMaterial->nTextures; ++i ) {
            renderError = R_CreateTexture2DFromCooked( *textures[i],
                &pending.textures[i] );
            if ( renderError != render_error_t::OK ) {
                result = RenderFailure( renderError );
                break;
            }
        }
        if ( result.status != render_material_bridge_status_t::OK ) break;

        pipeline.shader = pending.shader;
        renderError = R_CreateGraphicsPipeline( pipeline, &pending.pipeline );
        if ( renderError != render_error_t::OK ) {
            result = RenderFailure( renderError );
            break;
        }
    } while ( false );

    const auto releaseError = ReleaseAcquired( description.pResources, acquired );
    if ( releaseError != resource::resource_error_t::OK &&
         result.status == render_material_bridge_status_t::OK ) {
        result = ResourceFailure( releaseError );
    }
    if ( result.status == render_material_bridge_status_t::OK ) {
        *bundleOut = pending;
        return result;
    }
    const render_error_t cleanupError = R_DestroyCookedMaterialBundle( &pending );
    if ( cleanupError != render_error_t::OK ) {
        result.status = render_material_bridge_status_t::CLEANUP_FAILED;
        result.cleanupRenderError = cleanupError;
        *bundleOut = pending;
    }
    return result;
}

render_error_t R_ApplyCookedMaterialBundleToDraw(
    const render_material_bundle_t &bundle,
    render_draw_indexed_desc_t *draw ) noexcept
{
    if ( draw == nullptr || bundle.textureCount > R_MAX_SAMPLED_TEXTURE_BINDINGS ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !R_IsShaderValid( bundle.shader ) ) return render_error_t::ERR_INVALID_HANDLE;
    render_pipeline_info_t pipelineInfo{};
    const render_error_t infoError = R_GetGraphicsPipelineInfo(
        bundle.pipeline, &pipelineInfo );
    if ( infoError != render_error_t::OK ) return infoError;
    if ( pipelineInfo.shader.value != bundle.shader.value ||
         pipelineInfo.sampledTextureCount != bundle.textureCount ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }
    for ( common::u32 i = 0u; i < bundle.textureCount; ++i ) {
        if ( !R_IsTextureValid( bundle.textures[i] ) ) {
            return render_error_t::ERR_INVALID_HANDLE;
        }
    }
    draw->pipeline = bundle.pipeline;
    for ( common::u32 i = 0u; i < R_MAX_SAMPLED_TEXTURE_BINDINGS; ++i ) {
        draw->sampledTextures[i] = i < bundle.textureCount
            ? bundle.textures[i] : R_INVALID_TEXTURE;
        draw->samplers[i] = R_INVALID_SAMPLER;
    }
    return render_error_t::OK;
}

} // namespace cypher::engine::render
