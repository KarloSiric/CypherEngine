//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherResource/CypherResource_RenderAssets.h
//  Purpose: Declares VFS-backed runtime loaders for cooked render resources.
//  Details: The adapter owns complete cooked files while exposing validated,
//           zero-copy shader, texture, and material views through CypherResource.
//           It contains no graphics-backend objects or source-format compilation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RESOURCE_RENDERASSETS_H
#define CYPHER_ENGINE_RESOURCE_RENDERASSETS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_CookedMaterial.h"
#include "CypherCommon_CookedShader.h"
#include "CypherCommon_CookedTexture.h"
#include "CypherCommon_Vfs.h"
#include "CypherResource.h"

namespace cypher::engine::resource
{

inline constexpr common::usize CYPHER_RESOURCE_MAX_COOKED_SHADER_FILE_SIZE =
    33u * common::CY_MIB;
inline constexpr common::usize CYPHER_RESOURCE_MAX_COOKED_TEXTURE_FILE_SIZE =
    513u * common::CY_MIB;
inline constexpr common::usize CYPHER_RESOURCE_MAX_COOKED_MATERIAL_FILE_SIZE =
    1u * common::CY_MIB;

enum class render_asset_load_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    TYPE_MISMATCH,
    PATH_EXTENSION_MISMATCH,
    PAYLOAD_ALLOCATION_FAILED,
    VFS_READ_FAILED,
    COOKED_FORMAT_INVALID
};

// Captures the last synchronous callback failure. The manager remains the source
// of resource_error_t; this record preserves the lower-level reason for tools,
// diagnostics, and development hosts without changing the generic loader ABI.
struct render_asset_load_diagnostic_t {
    render_asset_load_status_t status{ render_asset_load_status_t::OK };
    common::vfs_status_t vfsStatus{ common::vfs_status_t::OK };
    common::cooked_resource_status_t resourceStatus{
        common::cooked_resource_status_t::OK
    };
    common::cooked_shader_status_t shaderStatus{
        common::cooked_shader_status_t::OK
    };
    common::cooked_texture_status_t textureStatus{
        common::cooked_texture_status_t::OK
    };
    common::cooked_material_status_t materialStatus{
        common::cooked_material_status_t::OK
    };
    common::resource_id_t id{};
    common::resource_type_id_t type{ 0u };
    common::usize iStage{ common::CY_INVALID_SIZE };
    common::usize iMip{ common::CY_INVALID_SIZE };
    common::usize iTexture{ common::CY_INVALID_SIZE };
    common::usize iParameter{ common::CY_INVALID_SIZE };
    common::usize iChunk{ common::CY_INVALID_SIZE };
};

struct render_asset_loader_config_t {
    const common::vfs_t *pVfs{ nullptr };
    const common::allocator_t *pAllocator{ nullptr };
    common::usize cbMaximumShader{
        CYPHER_RESOURCE_MAX_COOKED_SHADER_FILE_SIZE
    };
    common::usize cbMaximumTexture{
        CYPHER_RESOURCE_MAX_COOKED_TEXTURE_FILE_SIZE
    };
    common::usize cbMaximumMaterial{
        CYPHER_RESOURCE_MAX_COOKED_MATERIAL_FILE_SIZE
    };
};

// The context is borrowed by registered callbacks and must outlive the resource
// manager registration. The manager is owner-thread only, so lastDiagnostic is
// intentionally a simple non-atomic record.
struct render_asset_loader_context_t {
    render_asset_loader_config_t config{};
    render_asset_load_diagnostic_t lastDiagnostic{};
};

struct render_asset_type_slots_t {
    common::resource_type_slot_t shader{
        common::CY_RESOURCE_TYPE_SLOT_INVALID
    };
    common::resource_type_slot_t texture{
        common::CY_RESOURCE_TYPE_SLOT_INVALID
    };
    common::resource_type_slot_t material{
        common::CY_RESOURCE_TYPE_SLOT_INVALID
    };
};

CYPHER_NODISCARD render_asset_loader_config_t
CypherResource_DefaultRenderAssetLoaderConfig(
    const common::vfs_t *pVfs ) noexcept;

// Binds a VFS and allocator without acquiring either object. Reinitializing a
// context while its callbacks are registered is invalid.
CYPHER_NODISCARD common::bool_t CypherResource_InitRenderAssetLoader(
    render_asset_loader_context_t *pContext,
    const render_asset_loader_config_t &config ) noexcept;

CYPHER_NODISCARD common::bool_t CypherResource_IsRenderAssetLoaderValid(
    const render_asset_loader_context_t *pContext ) noexcept;

CYPHER_NODISCARD common::resource_type_id_t
CypherResource_ShaderTypeId() noexcept;

CYPHER_NODISCARD common::resource_type_id_t
CypherResource_TextureTypeId() noexcept;

CYPHER_NODISCARD common::resource_type_id_t
CypherResource_MaterialTypeId() noexcept;

CYPHER_NODISCARD resource_loader_t CypherResource_MakeShaderLoader(
    render_asset_loader_context_t *pContext ) noexcept;

CYPHER_NODISCARD resource_loader_t CypherResource_MakeTextureLoader(
    render_asset_loader_context_t *pContext ) noexcept;

CYPHER_NODISCARD resource_loader_t CypherResource_MakeMaterialLoader(
    render_asset_loader_context_t *pContext ) noexcept;

// Registers all three built-in render resource types. If a later registration
// fails, registrations made by this call are removed in reverse order. Runtime
// type slots remain retired, matching the manager's non-reuse rule.
CYPHER_NODISCARD resource_error_t CypherResource_RegisterRenderAssetLoaders(
    resource_manager_t *pManager,
    render_asset_loader_context_t *pContext,
    render_asset_type_slots_t *pSlotsOut = nullptr ) noexcept;

// These typed accessors reject handles belonging to another registered type.
// Returned views and every byte range inside them remain valid only while the
// caller retains at least one reference to the resource handle.
CYPHER_NODISCARD resource_error_t CypherResource_GetCookedShader(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    const common::cooked_shader_view_t **ppShaderOut ) noexcept;

CYPHER_NODISCARD resource_error_t CypherResource_GetCookedTexture(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    const common::cooked_texture_view_t **ppTextureOut ) noexcept;

CYPHER_NODISCARD resource_error_t CypherResource_GetCookedMaterial(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    const common::cooked_material_view_t **ppMaterialOut ) noexcept;

CYPHER_NODISCARD const char *CypherResource_RenderAssetLoadStatusName(
    render_asset_load_status_t status ) noexcept;

} // namespace cypher::engine::resource

#endif // CYPHER_ENGINE_RESOURCE_RENDERASSETS_H
