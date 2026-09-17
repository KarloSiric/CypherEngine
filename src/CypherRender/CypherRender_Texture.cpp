//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Validates sampled images and owns their generational GPU handles.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender_Texture.h"
#include "CypherRender_Local.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_HandleTable.h"
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
struct texture_record_t {
    backend_texture_t native{};
    render_texture_info_t info{};
};
struct texture_system_t {
    common::handle_table_t<texture_record_t> records{};
    bool initialized{ false };
};
texture_system_t textureSystem{};

render_error_t ResolveTexture( render_texture_handle_t texture,
    common::handle32_t &tableHandle, texture_record_t *&record ) noexcept
{
    tableHandle = common::CY_HANDLE32_INVALID;
    record = nullptr;
    if ( !common::Cy_Handle64IsValid( texture ) ) return render_error_t::ERR_INVALID_HANDLE;
    if ( common::Cy_Handle64Type( texture ) != static_cast<common::u32>( render_object_type_t::TEXTURE ) ) {
        return render_error_t::ERR_RESOURCE_TYPE_MISMATCH;
    }
    if ( !common::Cy_Handle32TryMake( common::Cy_Handle64Index( texture ),
             common::Cy_Handle64Generation( texture ), &tableHandle ) ) {
        return render_error_t::ERR_INVALID_HANDLE;
    }
    record = common::HandleTable_Get( &textureSystem.records, tableHandle );
    return record != nullptr ? render_error_t::OK : render_error_t::ERR_STALE_HANDLE;
}
} // namespace

render_error_t R_TextureSystemInit() noexcept
{
    if ( textureSystem.initialized ) return render_error_t::ERR_ALREADY_INITIALIZED;
    if ( !common::HandleTable_Init( &textureSystem.records, common::Allocator_GetSystem(), 128u ) ) {
        return render_error_t::ERR_OUT_OF_MEMORY;
    }
    textureSystem.initialized = true;
    return render_error_t::OK;
}
render_error_t R_TextureSystemShutdown() noexcept
{
    if ( !textureSystem.initialized ) return render_error_t::OK;
    render_error_t firstError = render_error_t::OK;
    (void)common::HandleTable_ForEach( &textureSystem.records,
        [&]( common::handle32_t, texture_record_t &record ) noexcept {
            const auto result = tr.backend != nullptr && tr.backend->DestroyTexture != nullptr
                ? tr.backend->DestroyTexture( record.native, tr.backend->state )
                : render_error_t::ERR_INTERNAL_ERROR;
            if ( firstError == render_error_t::OK ) firstError = result;
            return common::CY_TRUE;
        } );
    common::HandleTable_Shutdown( &textureSystem.records );
    textureSystem.initialized = false;
    return firstError;
}
render_error_t R_CreateTexture2D( const render_texture_desc_t &description,
    const render_texture_data_t &data, render_texture_handle_t *textureOut ) noexcept
{
    if ( textureOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *textureOut = R_INVALID_TEXTURE;
    if ( !tr.initialized || !textureSystem.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.backend->CreateTexture2D == nullptr || tr.backend->DestroyTexture == nullptr ) {
        return render_error_t::ERR_UNSUPPORTED;
    }
    if ( description.width == 0u || description.height == 0u || data.bytes == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( description.width > tr.info.limits.maxTexture2DSize ||
         description.height > tr.info.limits.maxTexture2DSize ||
         description.width > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ||
         description.height > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    const common::u64 required = static_cast<common::u64>( description.width ) * description.height * 4u;
    if ( required > std::numeric_limits<common::usize>::max() ) return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    if ( data.byteSize != required ) return render_error_t::ERR_INVALID_ARGUMENT;
    texture_record_t record{};
    const auto result = tr.backend->CreateTexture2D( description, data, record.native, tr.backend->state );
    if ( result != render_error_t::OK || record.native.value == 0u ) {
        return result != render_error_t::OK ? result : render_error_t::ERR_INTERNAL_ERROR;
    }
    record.info = { description.width, description.height, description.sRGB,
        description.generateMips, description.repeat, 1u };
    if ( description.generateMips ) {
        common::u32 extent = description.width > description.height ? description.width : description.height;
        while ( extent > 1u ) { extent /= 2u; ++record.info.mipLevels; }
    }
    const auto slot = common::HandleTable_Insert( &textureSystem.records, record );
    if ( !common::Cy_Handle32IsValid( slot ) ) {
        (void)tr.backend->DestroyTexture( record.native, tr.backend->state );
        return common::HandleTable_Count( &textureSystem.records ) >= common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED : render_error_t::ERR_OUT_OF_MEMORY;
    }
    *textureOut = common::Cy_Handle64Make( common::Cy_Handle32Index( slot ),
        common::Cy_Handle32Generation( slot ), static_cast<common::u32>( render_object_type_t::TEXTURE ) );
    return render_error_t::OK;
}
render_error_t R_DestroyTexture( render_texture_handle_t texture ) noexcept
{
    if ( !tr.initialized || !textureSystem.initialized || tr.backend == nullptr ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t slot{};
    texture_record_t *record = nullptr;
    const auto resolved = ResolveTexture( texture, slot, record );
    if ( resolved != render_error_t::OK ) return resolved;
    const auto result = tr.backend->DestroyTexture( record->native, tr.backend->state );
    if ( result != render_error_t::OK ) return result;
    return common::HandleTable_Remove( &textureSystem.records, slot )
        ? render_error_t::OK : render_error_t::ERR_INTERNAL_ERROR;
}
bool R_IsTextureValid( render_texture_handle_t texture ) noexcept
{
    if ( !tr.initialized || !textureSystem.initialized ) return false;
    common::handle32_t slot{};
    texture_record_t *record = nullptr;
    return ResolveTexture( texture, slot, record ) == render_error_t::OK;
}
render_error_t R_GetTextureInfo( render_texture_handle_t texture, render_texture_info_t *infoOut ) noexcept
{
    backend_texture_t ignored{};
    return R_TextureResolveForDraw( texture, &ignored, infoOut );
}
render_error_t R_TextureResolveForDraw( render_texture_handle_t texture,
    backend_texture_t *nativeOut, render_texture_info_t *infoOut ) noexcept
{
    if ( nativeOut == nullptr || infoOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *nativeOut = {};
    *infoOut = {};
    if ( !tr.initialized || !textureSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t slot{};
    texture_record_t *record = nullptr;
    const auto result = ResolveTexture( texture, slot, record );
    if ( result != render_error_t::OK ) return result;
    *nativeOut = record->native;
    *infoOut = record->info;
    return render_error_t::OK;
}
} // namespace cypher::engine::render
