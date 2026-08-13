//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherEngine/CypherResource/CypherResource_RenderAssets_Tests.cpp
//  Purpose: Tests VFS-backed cooked render resource loading and ownership.
//  Details: Coverage protects typed registration, zero-copy view lifetime, cache
//           identity, malformed input diagnostics, size bounds, stale handles,
//           and transactional registration rollback.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource_RenderAssets.h"

#include "CypherCommon_MemoryOps.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace cypher::common;
using namespace cypher::engine::resource;

namespace
{

template <usize nLength>
constexpr string_view_t Text( const char ( &text )[nLength] ) noexcept
{
    return { text, nLength - 1u };
}

struct memory_file_t {
    string_view_t path{};
    binary_block_t contents{};
};

struct memory_vfs_t {
    memory_file_t files[8]{};
    usize nFiles{ 0u };
    u32 cReads{ 0u };
};

vfs_status_t MemoryReadAll(
    void *pUserData,
    string_view_t virtualPath,
    usize cbMaximum,
    blob_t *pDest ) noexcept
{
    auto &vfs = *static_cast<memory_vfs_t *>( pUserData );
    for ( usize iFile = 0u; iFile < vfs.nFiles; ++iFile ) {
        if ( !StringView_Equals( vfs.files[iFile].path, virtualPath ) ) {
            continue;
        }
        ++vfs.cReads;
        if ( vfs.files[iFile].contents.cbSize > cbMaximum ) {
            return vfs_status_t::SIZE_LIMIT;
        }
        return Blob_Assign( pDest, vfs.files[iFile].contents )
            ? vfs_status_t::OK
            : vfs_status_t::OUT_OF_MEMORY;
    }
    return vfs_status_t::NOT_FOUND;
}

vfs_t MakeVfs( memory_vfs_t &storage ) noexcept
{
    static const vfs_ops_t ops{ MemoryReadAll, nullptr, nullptr, nullptr };
    return { &ops, &storage, VFS_CAPABILITY_READ_ALL };
}

std::vector<byte> MakeShader()
{
    constexpr char vertex[] =
        "#version 410 core\nvoid main(){gl_Position=vec4(0.0);}\n";
    constexpr char fragment[] =
        "#version 410 core\nout vec4 c;void main(){c=vec4(1.0);}\n";
    cooked_shader_stage_source_t stages[2]{};
    stages[0].stage = render_shader_stage_t::VERTEX;
    stages[0].code = {
        reinterpret_cast<const byte *>( vertex ),
        sizeof( vertex )
    };
    stages[1].stage = render_shader_stage_t::FRAGMENT;
    stages[1].code = {
        reinterpret_cast<const byte *>( fragment ),
        sizeof( fragment )
    };
    const usize cbRequired = CookedShader_RequiredSize( {}, { stages, 2u } );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedShader_Succeeded( CookedShader_Write(
        {},
        { stages, 2u },
        ContentHash_String( Text( "shaders/test.cyshader" ) ),
        { file.data(), file.size() } ) ) );
    return file;
}

std::vector<byte> MakeTexture()
{
    const byte pixels[8]{ 255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u };
    cooked_texture_desc_t texture{};
    texture.nWidth = 2u;
    texture.nHeight = 1u;
    texture.nMipLevels = 1u;
    const cooked_texture_mip_source_t mip{
        2u,
        1u,
        1u,
        8u,
        { pixels, sizeof( pixels ) }
    };
    const usize cbRequired = CookedTexture_RequiredSize(
        texture,
        { &mip, 1u } );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedTexture_Succeeded( CookedTexture_Write(
        texture,
        { &mip, 1u },
        ContentHash_String( Text( "textures/test.cytex" ) ),
        { file.data(), file.size() } ) ) );
    return file;
}

std::vector<byte> MakeMaterial()
{
    const cooked_material_texture_source_t texture{
        Text( "AlbedoMap" ),
        Text( "textures/test.cytex" )
    };
    cooked_material_parameter_source_t roughness{};
    roughness.name = Text( "Roughness" );
    roughness.values[0] = 0.5;
    roughness.nComponents = 1u;
    const cooked_material_source_t material{
        Text( "shaders/test.cyshader" ),
        { &texture, 1u },
        { &roughness, 1u },
        COOKED_MATERIAL_FLAG_NONE
    };
    const usize cbRequired = CookedMaterial_RequiredSize( material );
    std::vector<byte> file( cbRequired );
    REQUIRE( CookedMaterial_Succeeded( CookedMaterial_Write(
        material,
        ContentHash_String( Text( "materials/test.cymat" ) ),
        { file.data(), file.size() } ) ) );
    return file;
}

struct manager_scope_t {
    resource_manager_t manager{};

    explicit manager_scope_t( u32 cTypes = 8u )
    {
        resource_manager_config_t config = CypherResource_DefaultConfig();
        config.cResourceCapacity = 8u;
        config.cTypeCapacity = cTypes;
        REQUIRE( CypherResource_Init( &manager, config ) == resource_error_t::OK );
    }

    ~manager_scope_t()
    {
        if ( CypherResource_IsInitialized( &manager ) ) {
            CHECK( CypherResource_Shutdown( &manager ) == resource_error_t::OK );
        }
    }
};

} // namespace

TEST_CASE( "Cooked render resources load through VFS and retain borrowed views",
           "[CypherEngine][Resource][RenderAssets]" )
{
    std::vector<byte> shader = MakeShader();
    std::vector<byte> texture = MakeTexture();
    std::vector<byte> material = MakeMaterial();
    memory_vfs_t storage{};
    storage.files[0] = {
        Text( "shaders/test.cyshader_c" ),
        { shader.data(), shader.size() }
    };
    storage.files[1] = {
        Text( "textures/test.cytex_c" ),
        { texture.data(), texture.size() }
    };
    storage.files[2] = {
        Text( "materials/test.cymat_c" ),
        { material.data(), material.size() }
    };
    storage.nFiles = 3u;
    const vfs_t vfs = MakeVfs( storage );

    render_asset_loader_context_t loaders{};
    REQUIRE( CypherResource_InitRenderAssetLoader(
        &loaders,
        CypherResource_DefaultRenderAssetLoaderConfig( &vfs ) ) );
    manager_scope_t scope{};
    render_asset_type_slots_t slots{};
    REQUIRE( CypherResource_RegisterRenderAssetLoaders(
        &scope.manager,
        &loaders,
        &slots ) == resource_error_t::OK );
    REQUIRE( slots.shader != CY_RESOURCE_TYPE_SLOT_INVALID );
    REQUIRE( slots.texture != CY_RESOURCE_TYPE_SLOT_INVALID );
    REQUIRE( slots.material != CY_RESOURCE_TYPE_SLOT_INVALID );

    resource_handle_t shaderHandle{};
    resource_handle_t textureHandle{};
    resource_handle_t materialHandle{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        CypherResource_ShaderTypeId(),
        storage.files[0].path,
        &shaderHandle ) == resource_error_t::OK );
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        CypherResource_TextureTypeId(),
        storage.files[1].path,
        &textureHandle ) == resource_error_t::OK );
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        CypherResource_MaterialTypeId(),
        storage.files[2].path,
        &materialHandle ) == resource_error_t::OK );
    REQUIRE( storage.cReads == 3u );

    // Corrupting the provider's source storage cannot affect live views: each
    // loader made one owned VFS copy before validating and publishing its payload.
    Cy_MemZero( shader.data(), shader.size() );
    Cy_MemZero( texture.data(), texture.size() );
    Cy_MemZero( material.data(), material.size() );
    const cooked_shader_view_t *pShader = nullptr;
    const cooked_texture_view_t *pTexture = nullptr;
    const cooked_material_view_t *pMaterial = nullptr;
    REQUIRE( CypherResource_GetCookedShader(
        &scope.manager,
        shaderHandle,
        &pShader ) == resource_error_t::OK );
    REQUIRE( CypherResource_GetCookedTexture(
        &scope.manager,
        textureHandle,
        &pTexture ) == resource_error_t::OK );
    REQUIRE( CypherResource_GetCookedMaterial(
        &scope.manager,
        materialHandle,
        &pMaterial ) == resource_error_t::OK );
    REQUIRE( pShader->nStages == 2u );
    REQUIRE( CookedShader_FindStage(
                 *pShader,
                 render_shader_stage_t::VERTEX )->code.pData[0] == '#' );
    REQUIRE( pTexture->nMipLevels == 1u );
    REQUIRE( CookedTexture_FindMip( *pTexture, 0u )->pixels.pData[0] == 255u );
    REQUIRE( StringView_Equals(
        pMaterial->shader,
        Text( "shaders/test.cyshader" ) ) );

    resource_handle_t textureAgain{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        CypherResource_TextureTypeId(),
        storage.files[1].path,
        &textureAgain ) == resource_error_t::OK );
    REQUIRE( ResourceHandle_Equals( textureAgain, textureHandle ) );
    REQUIRE( storage.cReads == 3u );
    REQUIRE( CypherResource_Release( &scope.manager, textureAgain ) ==
             resource_error_t::OK );

    REQUIRE( CypherResource_GetCookedTexture(
        &scope.manager,
        shaderHandle,
        &pTexture ) == resource_error_t::INVALID_HANDLE );
    REQUIRE( pTexture == nullptr );

    REQUIRE( CypherResource_Release( &scope.manager, shaderHandle ) ==
             resource_error_t::OK );
    REQUIRE( CypherResource_Release( &scope.manager, textureHandle ) ==
             resource_error_t::OK );
    REQUIRE( CypherResource_Release( &scope.manager, materialHandle ) ==
             resource_error_t::OK );
    REQUIRE( CypherResource_GetCookedShader(
        &scope.manager,
        shaderHandle,
        &pShader ) == resource_error_t::INVALID_HANDLE );
    REQUIRE( pShader == nullptr );
}

TEST_CASE( "Render resource loaders preserve VFS and format diagnostics",
           "[CypherEngine][Resource][RenderAssets][Failure]" )
{
    std::vector<byte> texture = MakeTexture();
    texture.back() ^= 0xFFu;
    memory_vfs_t storage{};
    storage.files[0] = {
        Text( "textures/damaged.cytex_c" ),
        { texture.data(), texture.size() }
    };
    storage.nFiles = 1u;
    const vfs_t vfs = MakeVfs( storage );

    render_asset_loader_context_t loaders{};
    render_asset_loader_config_t config =
        CypherResource_DefaultRenderAssetLoaderConfig( &vfs );
    REQUIRE( CypherResource_InitRenderAssetLoader( &loaders, config ) );
    manager_scope_t scope{};
    REQUIRE( CypherResource_RegisterRenderAssetLoaders(
        &scope.manager,
        &loaders ) == resource_error_t::OK );

    resource_handle_t handle{};
    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        CypherResource_TextureTypeId(),
        storage.files[0].path,
        &handle ) == resource_error_t::LOAD_FAILED );
    REQUIRE_FALSE( ResourceHandle_IsValid( handle ) );
    REQUIRE( loaders.lastDiagnostic.status ==
             render_asset_load_status_t::COOKED_FORMAT_INVALID );
    REQUIRE( loaders.lastDiagnostic.textureStatus ==
             cooked_texture_status_t::RESOURCE_ERROR );
    REQUIRE( loaders.lastDiagnostic.resourceStatus ==
             cooked_resource_status_t::CONTENT_HASH_MISMATCH );

    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        CypherResource_TextureTypeId(),
        Text( "textures/wrong.extension" ),
        &handle ) == resource_error_t::LOAD_FAILED );
    REQUIRE( loaders.lastDiagnostic.status ==
             render_asset_load_status_t::PATH_EXTENSION_MISMATCH );

    REQUIRE( CypherResource_Acquire(
        &scope.manager,
        CypherResource_ShaderTypeId(),
        Text( "shaders/missing.cyshader_c" ),
        &handle ) == resource_error_t::LOAD_FAILED );
    REQUIRE( loaders.lastDiagnostic.status ==
             render_asset_load_status_t::VFS_READ_FAILED );
    REQUIRE( loaders.lastDiagnostic.vfsStatus == vfs_status_t::NOT_FOUND );

    render_asset_loader_context_t boundedLoaders{};
    config.cbMaximumTexture = texture.size() - 1u;
    REQUIRE( CypherResource_InitRenderAssetLoader( &boundedLoaders, config ) );
    manager_scope_t boundedScope{};
    REQUIRE( CypherResource_RegisterRenderAssetLoaders(
        &boundedScope.manager,
        &boundedLoaders ) == resource_error_t::OK );
    REQUIRE( CypherResource_Acquire(
        &boundedScope.manager,
        CypherResource_TextureTypeId(),
        storage.files[0].path,
        &handle ) == resource_error_t::LOAD_FAILED );
    REQUIRE( boundedLoaders.lastDiagnostic.vfsStatus == vfs_status_t::SIZE_LIMIT );
}

TEST_CASE( "Render resource registration rolls back partial type sets",
           "[CypherEngine][Resource][RenderAssets][Registration]" )
{
    memory_vfs_t storage{};
    const vfs_t vfs = MakeVfs( storage );
    render_asset_loader_context_t loaders{};
    REQUIRE( CypherResource_InitRenderAssetLoader(
        &loaders,
        CypherResource_DefaultRenderAssetLoaderConfig( &vfs ) ) );

    manager_scope_t scope{ 2u };
    render_asset_type_slots_t slots{};
    REQUIRE( CypherResource_RegisterRenderAssetLoaders(
        &scope.manager,
        &loaders,
        &slots ) == resource_error_t::TYPE_CAPACITY_EXCEEDED );
    REQUIRE( slots.shader == CY_RESOURCE_TYPE_SLOT_INVALID );
    REQUIRE( CypherResource_GetStats( &scope.manager ).cRegisteredTypes == 0u );

    REQUIRE_FALSE( CypherResource_InitRenderAssetLoader( nullptr, {} ) );
    REQUIRE_FALSE( CypherResource_IsRenderAssetLoaderValid( nullptr ) );
    REQUIRE( StringView_Equals(
        StringView_FromCString( CypherResource_RenderAssetLoadStatusName(
            render_asset_load_status_t::VFS_READ_FAILED ) ),
        Text( "VFS_READ_FAILED" ) ) );
}
