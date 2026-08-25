//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherResource/CypherResource_RenderAssets.cpp
//  Purpose: Implements VFS-backed runtime loaders for cooked render resources.
//  Details: Each payload owns the complete VFS blob because cooked format views
//           borrow their strings and byte ranges directly from serialized storage.
//           Validation finishes before a payload becomes visible to the manager.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource_RenderAssets.h"

#include <new>

namespace cypher::engine::resource
{
namespace
{

inline constexpr common::string_view_t CY_RENDER_SHADER_TYPE_NAME{
    "cypher.shader",
    sizeof( "cypher.shader" ) - 1u
};
inline constexpr common::string_view_t CY_RENDER_TEXTURE_TYPE_NAME{
    "cypher.texture",
    sizeof( "cypher.texture" ) - 1u
};
inline constexpr common::string_view_t CY_RENDER_MATERIAL_TYPE_NAME{
    "cypher.material",
    sizeof( "cypher.material" ) - 1u
};
inline constexpr common::string_view_t CY_RENDER_SHADER_COOKED_EXTENSION{
    ".cyshader_c",
    sizeof( ".cyshader_c" ) - 1u
};
inline constexpr common::string_view_t CY_RENDER_TEXTURE_COOKED_EXTENSION{
    ".cytex_c",
    sizeof( ".cytex_c" ) - 1u
};
inline constexpr common::string_view_t CY_RENDER_MATERIAL_COOKED_EXTENSION{
    ".cymat_c",
    sizeof( ".cymat_c" ) - 1u
};

template <typename view_t>
struct owned_cooked_payload_t {
    common::blob_t storage{};                        // Owns the complete serialized cooked file.
    view_t view{};                                   // Borrows strings and byte ranges from storage.
    const common::allocator_t *pAllocator{ nullptr }; // Releases this wrapper and its blob.
};

using owned_shader_t = owned_cooked_payload_t<common::cooked_shader_view_t>;
using owned_texture_t = owned_cooked_payload_t<common::cooked_texture_view_t>;
using owned_material_t = owned_cooked_payload_t<common::cooked_material_view_t>;

void ResetDiagnostic(
    render_asset_loader_context_t &context,
    common::resource_id_t id,
    common::resource_type_id_t type ) noexcept
{
    context.lastDiagnostic = {};
    context.lastDiagnostic.id = id;
    context.lastDiagnostic.type = type;
}

template <typename payload_t>
payload_t *AllocatePayload(
    render_asset_loader_context_t &context ) noexcept
{
    // Payload and blob deliberately share one allocator contract so partial setup can
    // unwind without mixing ownership domains.
    void *pStorage = common::Allocator_Allocate(
        context.config.pAllocator,
        sizeof( payload_t ),
        alignof( payload_t ) );
    if ( pStorage == nullptr ) {
        context.lastDiagnostic.status =
            render_asset_load_status_t::PAYLOAD_ALLOCATION_FAILED;
        return nullptr;
    }

    auto *pPayload = new ( pStorage ) payload_t{};
    pPayload->pAllocator = context.config.pAllocator;
    if ( !common::Blob_Init(
             &pPayload->storage,
             context.config.pAllocator ) ) {
        pPayload->~payload_t();
        common::Allocator_Free(
            context.config.pAllocator,
            pStorage,
            sizeof( payload_t ),
            alignof( payload_t ) );
        context.lastDiagnostic.status =
            render_asset_load_status_t::PAYLOAD_ALLOCATION_FAILED;
        return nullptr;
    }
    return pPayload;
}

template <typename payload_t>
void DestroyPayload( payload_t *pPayload ) noexcept
{
    if ( pPayload == nullptr ) {
        return;
    }
    // The blob destructor releases the serialized bytes before the wrapper allocation.
    const common::allocator_t *pAllocator = pPayload->pAllocator;
    pPayload->~payload_t();
    common::Allocator_Free(
        pAllocator,
        pPayload,
        sizeof( payload_t ),
        alignof( payload_t ) );
}

template <typename payload_t>
payload_t *ReadPayload(
    render_asset_loader_context_t &context,
    common::string_view_t virtualPath,
    common::usize cbMaximum ) noexcept
{
    payload_t *pPayload = AllocatePayload<payload_t>( context );
    if ( pPayload == nullptr ) {
        return nullptr;
    }

    // Read the complete cooked file once.  Parsed views remain zero-copy and therefore
    // require these bytes to stay owned for the resource's full retained lifetime.
    const common::vfs_status_t vfsStatus = common::Vfs_ReadAll(
        context.config.pVfs,
        virtualPath,
        cbMaximum,
        &pPayload->storage );
    if ( vfsStatus != common::vfs_status_t::OK ) {
        context.lastDiagnostic.status =
            render_asset_load_status_t::VFS_READ_FAILED;
        context.lastDiagnostic.vfsStatus = vfsStatus;
        DestroyPayload( pPayload );
        return nullptr;
    }
    return pPayload;
}

common::bool_t LoadShader(
    void *pUserData,
    common::resource_id_t id,
    common::resource_type_id_t type,
    common::string_view_t virtualPath,
    void **ppResourceOut ) noexcept
{
    if ( ppResourceOut != nullptr ) {
        *ppResourceOut = nullptr;
    }
    auto *pContext = static_cast<render_asset_loader_context_t *>( pUserData );
    if ( ppResourceOut == nullptr ||
         !Res_IsRenderAssetLoaderValid( pContext ) ) {
        return common::CY_FALSE;
    }

    ResetDiagnostic( *pContext, id, type );
    if ( type != Res_ShaderTypeId() ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::TYPE_MISMATCH;
        return common::CY_FALSE;
    }
    if ( !common::StringView_EndsWith(
             virtualPath,
             CY_RENDER_SHADER_COOKED_EXTENSION ) ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::PATH_EXTENSION_MISMATCH;
        return common::CY_FALSE;
    }

    owned_shader_t *pPayload = ReadPayload<owned_shader_t>(
        *pContext,
        virtualPath,
        pContext->config.cbMaximumShader );
    if ( pPayload == nullptr ) {
        return common::CY_FALSE;
    }

    // Validation precedes publication.  No partially decoded view can become visible
    // through the generic resource manager.
    const common::cooked_shader_result_t result = common::CookedShader_Read(
        common::Blob_Block( &pPayload->storage ),
        &pPayload->view );
    if ( !common::CookedShader_Succeeded( result ) ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::COOKED_FORMAT_INVALID;
        pContext->lastDiagnostic.shaderStatus = result.status;
        pContext->lastDiagnostic.resourceStatus = result.resourceStatus;
        pContext->lastDiagnostic.iStage = result.iStage;
        pContext->lastDiagnostic.iChunk = result.iChunk;
        DestroyPayload( pPayload );
        return common::CY_FALSE;
    }

    *ppResourceOut = pPayload;
    return common::CY_TRUE;
}

common::bool_t LoadTexture(
    void *pUserData,
    common::resource_id_t id,
    common::resource_type_id_t type,
    common::string_view_t virtualPath,
    void **ppResourceOut ) noexcept
{
    if ( ppResourceOut != nullptr ) {
        *ppResourceOut = nullptr;
    }
    auto *pContext = static_cast<render_asset_loader_context_t *>( pUserData );
    if ( ppResourceOut == nullptr ||
         !Res_IsRenderAssetLoaderValid( pContext ) ) {
        return common::CY_FALSE;
    }

    ResetDiagnostic( *pContext, id, type );
    if ( type != Res_TextureTypeId() ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::TYPE_MISMATCH;
        return common::CY_FALSE;
    }
    if ( !common::StringView_EndsWith(
             virtualPath,
             CY_RENDER_TEXTURE_COOKED_EXTENSION ) ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::PATH_EXTENSION_MISMATCH;
        return common::CY_FALSE;
    }

    owned_texture_t *pPayload = ReadPayload<owned_texture_t>(
        *pContext,
        virtualPath,
        pContext->config.cbMaximumTexture );
    if ( pPayload == nullptr ) {
        return common::CY_FALSE;
    }

    // The reader validates container layout, texture metadata, and every mip range
    // while retaining zero-copy references into pPayload->storage.
    const common::cooked_texture_result_t result = common::CookedTexture_Read(
        common::Blob_Block( &pPayload->storage ),
        &pPayload->view );
    if ( !common::CookedTexture_Succeeded( result ) ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::COOKED_FORMAT_INVALID;
        pContext->lastDiagnostic.textureStatus = result.status;
        pContext->lastDiagnostic.resourceStatus = result.resourceStatus;
        pContext->lastDiagnostic.iMip = result.iMip;
        pContext->lastDiagnostic.iChunk = result.iChunk;
        DestroyPayload( pPayload );
        return common::CY_FALSE;
    }

    *ppResourceOut = pPayload;
    return common::CY_TRUE;
}

common::bool_t LoadMaterial(
    void *pUserData,
    common::resource_id_t id,
    common::resource_type_id_t type,
    common::string_view_t virtualPath,
    void **ppResourceOut ) noexcept
{
    if ( ppResourceOut != nullptr ) {
        *ppResourceOut = nullptr;
    }
    auto *pContext = static_cast<render_asset_loader_context_t *>( pUserData );
    if ( ppResourceOut == nullptr ||
         !Res_IsRenderAssetLoaderValid( pContext ) ) {
        return common::CY_FALSE;
    }

    ResetDiagnostic( *pContext, id, type );
    if ( type != Res_MaterialTypeId() ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::TYPE_MISMATCH;
        return common::CY_FALSE;
    }
    if ( !common::StringView_EndsWith(
             virtualPath,
             CY_RENDER_MATERIAL_COOKED_EXTENSION ) ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::PATH_EXTENSION_MISMATCH;
        return common::CY_FALSE;
    }

    owned_material_t *pPayload = ReadPayload<owned_material_t>(
        *pContext,
        virtualPath,
        pContext->config.cbMaximumMaterial );
    if ( pPayload == nullptr ) {
        return common::CY_FALSE;
    }

    // Material texture references remain stable IDs/paths here; GPU binding belongs
    // to the renderer and is intentionally outside this resource adapter.
    const common::cooked_material_result_t result = common::CookedMaterial_Read(
        common::Blob_Block( &pPayload->storage ),
        &pPayload->view );
    if ( !common::CookedMaterial_Succeeded( result ) ) {
        pContext->lastDiagnostic.status =
            render_asset_load_status_t::COOKED_FORMAT_INVALID;
        pContext->lastDiagnostic.materialStatus = result.status;
        pContext->lastDiagnostic.resourceStatus = result.resourceStatus;
        pContext->lastDiagnostic.iTexture = result.iTexture;
        pContext->lastDiagnostic.iParameter = result.iParameter;
        pContext->lastDiagnostic.iChunk = result.iChunk;
        DestroyPayload( pPayload );
        return common::CY_FALSE;
    }

    *ppResourceOut = pPayload;
    return common::CY_TRUE;
}

void UnloadShader( void *, void *pResource ) noexcept
{
    DestroyPayload( static_cast<owned_shader_t *>( pResource ) );
}

void UnloadTexture( void *, void *pResource ) noexcept
{
    DestroyPayload( static_cast<owned_texture_t *>( pResource ) );
}

void UnloadMaterial( void *, void *pResource ) noexcept
{
    DestroyPayload( static_cast<owned_material_t *>( pResource ) );
}

template <typename payload_t, typename view_t>
resource_error_t GetCookedView(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    common::resource_type_id_t expectedType,
    const view_t **ppViewOut ) noexcept
{
    if ( ppViewOut != nullptr ) {
        *ppViewOut = nullptr;
    }
    if ( ppViewOut == nullptr ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    // Check the persistent type before casting the opaque payload.  A valid handle of
    // another registered type must never be reinterpreted as this payload template.
    resource_info_t info{};
    const resource_error_t infoResult = Res_GetInfo(
        pManager,
        handle,
        &info );
    if ( infoResult != resource_error_t::OK ) {
        return infoResult;
    }
    if ( info.type != expectedType ) {
        return resource_error_t::INVALID_HANDLE;
    }

    void *pResource = nullptr;
    const resource_error_t getResult = Res_Get(
        pManager,
        handle,
        &pResource );
    if ( getResult != resource_error_t::OK ) {
        return getResult;
    }

    // This is a borrow, not a reference increment.  The caller must already retain
    // handle ownership for as long as it uses the returned view.
    const auto *pPayload = static_cast<const payload_t *>( pResource );
    *ppViewOut = &pPayload->view;
    return resource_error_t::OK;
}

} // namespace

render_asset_loader_config_t Res_DefaultRenderAssetLoaderConfig(
    const common::vfs_t *pVfs ) noexcept
{
    render_asset_loader_config_t config{};
    config.pVfs = pVfs;
    config.pAllocator = common::Allocator_GetSystem();
    return config;
}

common::bool_t Res_InitRenderAssetLoader(
    render_asset_loader_context_t *pContext,
    const render_asset_loader_config_t &config ) noexcept
{
    if ( pContext == nullptr ||
         !common::Vfs_IsValid( config.pVfs ) ||
         ( config.pVfs->capabilities & common::VFS_CAPABILITY_READ_ALL ) == 0u ||
         !common::Allocator_IsValid( config.pAllocator ) ||
         config.cbMaximumShader == 0u ||
         config.cbMaximumTexture == 0u ||
         config.cbMaximumMaterial == 0u ) {
        return common::CY_FALSE;
    }

    // Context borrows both services.  Registration does not extend their lifetimes.
    pContext->config = config;
    pContext->lastDiagnostic = {};
    return common::CY_TRUE;
}

common::bool_t Res_IsRenderAssetLoaderValid(
    const render_asset_loader_context_t *pContext ) noexcept
{
    return pContext != nullptr &&
           common::Vfs_IsValid( pContext->config.pVfs ) &&
           ( pContext->config.pVfs->capabilities &
             common::VFS_CAPABILITY_READ_ALL ) != 0u &&
           common::Allocator_IsValid( pContext->config.pAllocator ) &&
           pContext->config.cbMaximumShader != 0u &&
           pContext->config.cbMaximumTexture != 0u &&
           pContext->config.cbMaximumMaterial != 0u;
}

common::resource_type_id_t Res_ShaderTypeId() noexcept
{
    return common::ResourceTypeId_FromName( CY_RENDER_SHADER_TYPE_NAME );
}

common::resource_type_id_t Res_TextureTypeId() noexcept
{
    return common::ResourceTypeId_FromName( CY_RENDER_TEXTURE_TYPE_NAME );
}

common::resource_type_id_t Res_MaterialTypeId() noexcept
{
    return common::ResourceTypeId_FromName( CY_RENDER_MATERIAL_TYPE_NAME );
}

resource_loader_t Res_MakeShaderLoader(
    render_asset_loader_context_t *pContext ) noexcept
{
    return {
        Res_ShaderTypeId(),
        LoadShader,
        UnloadShader,
        pContext
    };
}

resource_loader_t Res_MakeTextureLoader(
    render_asset_loader_context_t *pContext ) noexcept
{
    return {
        Res_TextureTypeId(),
        LoadTexture,
        UnloadTexture,
        pContext
    };
}

resource_loader_t Res_MakeMaterialLoader(
    render_asset_loader_context_t *pContext ) noexcept
{
    return {
        Res_MaterialTypeId(),
        LoadMaterial,
        UnloadMaterial,
        pContext
    };
}

resource_error_t Res_RegisterRenderAssetLoaders(
    resource_manager_t *pManager,
    render_asset_loader_context_t *pContext,
    render_asset_type_slots_t *pSlotsOut ) noexcept
{
    if ( pSlotsOut != nullptr ) {
        *pSlotsOut = {};
    }
    if ( !Res_IsRenderAssetLoaderValid( pContext ) ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    // Registration is transactional from the caller's perspective.  Roll back earlier
    // registrations in reverse order if a later built-in type cannot be installed.
    render_asset_type_slots_t slots{};
    resource_error_t result = Res_RegisterType(
        pManager,
        Res_MakeShaderLoader( pContext ),
        &slots.shader );
    if ( result != resource_error_t::OK ) {
        return result;
    }

    result = Res_RegisterType(
        pManager,
        Res_MakeTextureLoader( pContext ),
        &slots.texture );
    if ( result != resource_error_t::OK ) {
        // Runtime type slots are monotonic and remain retired after rollback; stale
        // handles can therefore never acquire the identity of a later loader.
        const resource_error_t rollback = Res_UnregisterType(
            pManager,
            Res_ShaderTypeId() );
        return rollback == resource_error_t::OK
            ? result
            : resource_error_t::INTERNAL_ERROR;
    }

    result = Res_RegisterType(
        pManager,
        Res_MakeMaterialLoader( pContext ),
        &slots.material );
    if ( result != resource_error_t::OK ) {
        const resource_error_t textureRollback = Res_UnregisterType(
            pManager,
            Res_TextureTypeId() );
        const resource_error_t shaderRollback = Res_UnregisterType(
            pManager,
            Res_ShaderTypeId() );
        return textureRollback == resource_error_t::OK &&
               shaderRollback == resource_error_t::OK
            ? result
            : resource_error_t::INTERNAL_ERROR;
    }

    if ( pSlotsOut != nullptr ) {
        *pSlotsOut = slots;
    }
    return resource_error_t::OK;
}

resource_error_t Res_GetCookedShader(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    const common::cooked_shader_view_t **ppShaderOut ) noexcept
{
    return GetCookedView<owned_shader_t>(
        pManager,
        handle,
        Res_ShaderTypeId(),
        ppShaderOut );
}

resource_error_t Res_GetCookedTexture(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    const common::cooked_texture_view_t **ppTextureOut ) noexcept
{
    return GetCookedView<owned_texture_t>(
        pManager,
        handle,
        Res_TextureTypeId(),
        ppTextureOut );
}

resource_error_t Res_GetCookedMaterial(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    const common::cooked_material_view_t **ppMaterialOut ) noexcept
{
    return GetCookedView<owned_material_t>(
        pManager,
        handle,
        Res_MaterialTypeId(),
        ppMaterialOut );
}

const char *Res_RenderAssetLoadStatusName(
    render_asset_load_status_t status ) noexcept
{
    switch ( status ) {
        case render_asset_load_status_t::OK: return "OK";
        case render_asset_load_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case render_asset_load_status_t::TYPE_MISMATCH:
            return "TYPE_MISMATCH";
        case render_asset_load_status_t::PATH_EXTENSION_MISMATCH:
            return "PATH_EXTENSION_MISMATCH";
        case render_asset_load_status_t::PAYLOAD_ALLOCATION_FAILED:
            return "PAYLOAD_ALLOCATION_FAILED";
        case render_asset_load_status_t::VFS_READ_FAILED:
            return "VFS_READ_FAILED";
        case render_asset_load_status_t::COOKED_FORMAT_INVALID:
            return "COOKED_FORMAT_INVALID";
    }
    return "UNKNOWN_RENDER_ASSET_LOAD_STATUS";
}

} // namespace cypher::engine::resource
