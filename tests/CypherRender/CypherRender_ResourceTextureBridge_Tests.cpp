//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests the lifetime boundary between cooked resources and GPU textures.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Local.h"
#include "CypherRender/CypherRender_Texture.h"
#include "CypherResource/CypherResource_RenderAssets.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

namespace common = ::cypher::common;
namespace render = ::cypher::engine::render;
namespace resource = ::cypher::engine::resource;

namespace
{

template <common::usize nLength>
constexpr common::string_view_t Text( const char ( &text )[nLength] ) noexcept
{
    return { text, nLength - 1u };
}

struct memory_vfs_t {
    common::string_view_t path{};
    common::binary_block_t file{};
    common::u32 reads{ 0u };
};

common::vfs_status_t ReadFile(
    void *pUserData,
    common::string_view_t path,
    common::usize maximum,
    common::blob_t *pDest ) noexcept
{
    auto &storage = *static_cast<memory_vfs_t *>( pUserData );
    if ( !common::StringView_Equals( path, storage.path ) ) {
        return common::vfs_status_t::NOT_FOUND;
    }
    ++storage.reads;
    if ( storage.file.cbSize > maximum ) {
        return common::vfs_status_t::SIZE_LIMIT;
    }
    return common::Blob_Assign( pDest, storage.file )
        ? common::vfs_status_t::OK
        : common::vfs_status_t::OUT_OF_MEMORY;
}

struct manager_scope_t {
    resource::resource_manager_t manager{};

    manager_scope_t()
    {
        resource::resource_manager_config_t config = resource::Res_DefaultConfig();
        config.cResourceCapacity = 4u;
        config.cTypeCapacity = 3u;
        REQUIRE( resource::Res_Init( &manager, config ) == resource::resource_error_t::OK );
    }

    ~manager_scope_t()
    {
        if ( resource::Res_IsInitialized( &manager ) ) {
            CHECK( resource::Res_Shutdown( &manager ) == resource::resource_error_t::OK );
        }
    }
};

struct gpu_scope_t {
    render::backend_api_t backend{};
    render::render_texture_desc_t uploadedDescription{};
    common::byte uploadedPixels[8]{};
    common::u32 creates{ 0u };
    common::u32 destroys{ 0u };
    bool failNextCreate{ false };

    gpu_scope_t()
    {
        backend.CreateTexture2D = []( const render::render_texture_desc_t &desc,
            const render::render_texture_data_t &data,
            render::backend_texture_t &out,
            void *state ) noexcept {
            auto &gpu = *static_cast<gpu_scope_t *>( state );
            ++gpu.creates;
            out = {};
            if ( gpu.failNextCreate ) {
                gpu.failNextCreate = false;
                return render::render_error_t::ERR_OUT_OF_MEMORY;
            }
            if ( data.bytes == nullptr || data.byteSize != sizeof( gpu.uploadedPixels ) ) {
                return render::render_error_t::ERR_INVALID_ARGUMENT;
            }
            gpu.uploadedDescription = desc;
            std::memcpy( gpu.uploadedPixels, data.bytes, sizeof( gpu.uploadedPixels ) );
            out.value = 1u;
            return render::render_error_t::OK;
        };
        backend.DestroyTexture = []( render::backend_texture_t texture,
            void *state ) noexcept {
            auto &gpu = *static_cast<gpu_scope_t *>( state );
            if ( texture.value != 1u ) {
                return render::render_error_t::ERR_INVALID_HANDLE;
            }
            ++gpu.destroys;
            return render::render_error_t::OK;
        };
        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.limits.maxTexture2DSize = 4096u;
        REQUIRE( render::R_TextureSystemInit() == render::render_error_t::OK );
    }

    ~gpu_scope_t()
    {
        CHECK( render::R_TextureSystemShutdown() == render::render_error_t::OK );
        render::tr = {};
    }
};

} // namespace

TEST_CASE( "typed cooked texture upload outlives its resource handle",
    "[CypherRender][Texture][ResourceBridge]" )
{
    constexpr common::byte pixels[8]{ 0x11u, 0x22u, 0x33u, 0xFFu,
                                      0x44u, 0x55u, 0x66u, 0x80u };
    common::cooked_texture_desc_t desc{};
    desc.nWidth = 2u;
    desc.nHeight = 1u;
    desc.nMipLevels = 1u;
    const common::cooked_texture_mip_source_t mip{
        2u, 1u, 1u, 8u, { pixels, sizeof( pixels ) }
    };
    const common::usize required = common::CookedTexture_RequiredSize(
        desc, { &mip, 1u } );
    REQUIRE( required > 0u );
    std::vector<common::byte> file( required );
    REQUIRE( common::CookedTexture_Succeeded( common::CookedTexture_Write(
        desc, { &mip, 1u },
        common::ContentHash_String( Text( "textures/bridge.cytex" ) ),
        { file.data(), file.size() } ) ) );

    memory_vfs_t storage{ Text( "textures/bridge.cytex_c" ),
                          { file.data(), file.size() } };
    static const common::vfs_ops_t ops{ ReadFile, nullptr, nullptr, nullptr };
    const common::vfs_t vfs{ &ops, &storage, common::VFS_CAPABILITY_READ_ALL };
    resource::render_asset_loader_context_t loader{};
    REQUIRE( resource::Res_InitRenderAssetLoader(
        &loader, resource::Res_DefaultRenderAssetLoaderConfig( &vfs ) ) );
    manager_scope_t resources{};
    REQUIRE( resource::Res_RegisterRenderAssetLoaders(
        &resources.manager, &loader ) == resource::resource_error_t::OK );
    gpu_scope_t gpu{};

    common::resource_handle_t resourceHandle{};
    REQUIRE( resource::Res_Acquire( &resources.manager,
        resource::Res_TextureTypeId(), storage.path,
        &resourceHandle ) == resource::resource_error_t::OK );
    REQUIRE( storage.reads == 1u );
    const common::cooked_texture_view_t *pCooked = nullptr;
    REQUIRE( resource::Res_GetCookedTexture( &resources.manager,
        resourceHandle, &pCooked ) == resource::resource_error_t::OK );
    REQUIRE( pCooked != nullptr );

    // The VFS provider can discard its source after acquisition. The resource
    // owns a copy until release; GPU creation consumes that copy synchronously.
    std::memset( file.data(), 0, file.size() );
    gpu.failNextCreate = true;
    render::render_texture_handle_t texture{ 99u };
    REQUIRE( render::R_CreateTexture2DFromCooked( *pCooked, &texture ) ==
             render::render_error_t::ERR_OUT_OF_MEMORY );
    CHECK_FALSE( render::R_IsTextureValid( texture ) );
    CHECK( gpu.destroys == 0u );
    REQUIRE( render::R_CreateTexture2DFromCooked( *pCooked, &texture ) ==
             render::render_error_t::OK );
    REQUIRE( render::R_IsTextureValid( texture ) );
    REQUIRE( gpu.creates == 2u );
    CHECK( gpu.uploadedDescription.width == 2u );
    CHECK( gpu.uploadedDescription.height == 1u );
    CHECK( std::memcmp( gpu.uploadedPixels, pixels, sizeof( pixels ) ) == 0 );

    REQUIRE( resource::Res_Release( &resources.manager, resourceHandle ) ==
             resource::resource_error_t::OK );
    CHECK( resource::Res_GetCookedTexture( &resources.manager,
        resourceHandle, &pCooked ) == resource::resource_error_t::INVALID_HANDLE );
    CHECK( pCooked == nullptr );
    REQUIRE( render::R_IsTextureValid( texture ) );
    render::render_texture_info_t info{};
    REQUIRE( render::R_GetTextureInfo( texture, &info ) == render::render_error_t::OK );
    CHECK( info.width == 2u );
    CHECK( info.height == 1u );
    CHECK( std::memcmp( gpu.uploadedPixels, pixels, sizeof( pixels ) ) == 0 );
    REQUIRE( render::R_DestroyTexture( texture ) == render::render_error_t::OK );
    CHECK( gpu.destroys == 1u );
}
