//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Validates sampled images and owns their generational GPU handles.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////
#include "CypherRender_Texture.h"
#include "CypherRender_Local.h"
#include "CypherCommon/Tier0/CypherCommon_Platform.h"
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
    common::u32 references{ 0u };
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

render_error_t RegisterCreatedTexture( const render_texture_desc_t &description,
    const backend_texture_t native, const common::u32 mipLevels,
    const bool completeMipChain, render_texture_handle_t *textureOut ) noexcept
{
    texture_record_t record{};
    record.native = native;
    record.info = { description.width, description.height, description.sRGB,
        description.generateMips, description.repeat, mipLevels,
        description.renderTarget, description.format, completeMipChain };
    const auto slot = common::HandleTable_Insert( &textureSystem.records, record );
    if ( !common::Cy_Handle32IsValid( slot ) ) {
        (void)tr.backend.api->DestroyTexture( native, tr.backend.state );
        return common::HandleTable_Count( &textureSystem.records ) >= common::CY_HANDLE_TABLE_MAX_CAPACITY
            ? render_error_t::ERR_LIMIT_EXCEEDED : render_error_t::ERR_OUT_OF_MEMORY;
    }
    *textureOut = common::Cy_Handle64Make( common::Cy_Handle32Index( slot ),
        common::Cy_Handle32Generation( slot ), static_cast<common::u32>( render_object_type_t::TEXTURE ) );
    return render_error_t::OK;
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
            const auto result = tr.backend.api != nullptr && tr.backend.api->DestroyTexture != nullptr
                ? tr.backend.api->DestroyTexture( record.native, tr.backend.state )
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
    if ( !tr.initialized || !textureSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.backend.api->CreateTexture2D == nullptr || tr.backend.api->DestroyTexture == nullptr ) {
        return render_error_t::ERR_UNSUPPORTED;
    }
    if ( description.width == 0u || description.height == 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( description.format >= render_texture_format_t::COUNT ||
         ( ( description.format == render_texture_format_t::D24 ||
             description.format == render_texture_format_t::RGBA16F ) &&
           !description.renderTarget ) ||
         ( description.format == render_texture_format_t::RGBA32F &&
           ( description.renderTarget || description.sRGB ) ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( description.format == render_texture_format_t::RGBA16F &&
         ( tr.info.capabilities & R_CAPABILITY_FLOAT_RENDER_TARGETS ) == 0u ) {
        return render_error_t::ERR_CAPABILITY_MISSING;
    }
    if ( description.renderTarget ) {
        if ( description.sRGB || description.generateMips || description.repeat ||
             data.bytes != nullptr || data.byteSize != 0u ) {
            return render_error_t::ERR_INVALID_ARGUMENT;
        }
    } else if ( data.bytes == nullptr ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( description.width > tr.info.limits.maxTexture2DSize ||
         description.height > tr.info.limits.maxTexture2DSize ||
         description.width > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ||
         description.height > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    const common::u64 pixels = static_cast<common::u64>( description.width ) * description.height;
    const common::u64 bytesPerPixel =
        description.format == render_texture_format_t::RGBA32F ? 16u :
        description.format == render_texture_format_t::RGBA16F ? 8u : 4u;
    if ( pixels > std::numeric_limits<common::u64>::max() / bytesPerPixel ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    const common::u64 required = pixels * bytesPerPixel;
    if ( required > std::numeric_limits<common::usize>::max() ) return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    if ( !description.renderTarget && data.byteSize != required ) return render_error_t::ERR_INVALID_ARGUMENT;
    backend_texture_t native{};
    const auto result = tr.backend.api->CreateTexture2D( description, data, native, tr.backend.state );
    if ( result != render_error_t::OK || native.value == 0u ) {
        if ( native.value != 0u ) {
            (void)tr.backend.api->DestroyTexture( native, tr.backend.state );
        }
        return result != render_error_t::OK ? result : render_error_t::ERR_INTERNAL_ERROR;
    }
    common::u32 mipLevels = 1u;
    if ( description.generateMips ) {
        common::u32 extent = description.width > description.height ? description.width : description.height;
        while ( extent > 1u ) { extent /= 2u; ++mipLevels; }
    }
    return RegisterCreatedTexture( description, native, mipLevels,
        description.generateMips || ( description.width == 1u && description.height == 1u ),
        textureOut );
}
render_error_t R_CreateTexture2DFromCooked(
    const common::cooked_texture_view_t &cooked,
    render_texture_handle_t *textureOut ) noexcept
{
    if ( textureOut == nullptr ) return render_error_t::ERR_INVALID_ARGUMENT;
    *textureOut = R_INVALID_TEXTURE;
    if ( !tr.initialized || !textureSystem.initialized || tr.backend.api == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( cooked.fileBytes.pData == nullptr || cooked.fileBytes.cbSize == 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    // The view borrows its source bytes, so do not trust caller-modified
    // descriptor fields or pixel spans. Reparse the file and use only the
    // canonical view after checking that the supplied view still describes it.
    common::cooked_texture_view_t verified{};
    if ( !common::CookedTexture_Succeeded(
             common::CookedTexture_Read( cooked.fileBytes, &verified ) ) ||
         cooked.nResourceVersion != verified.nResourceVersion ||
         cooked.nMipLevels != verified.nMipLevels ||
         cooked.nSubresources != verified.nSubresources ||
         !common::ContentHash_Equals( cooked.sourceHash, verified.sourceHash ) ||
         cooked.metadataBytes.pData != verified.metadataBytes.pData ||
         cooked.metadataBytes.cbSize != verified.metadataBytes.cbSize ||
         cooked.desc.dimension != verified.desc.dimension ||
         cooked.desc.pixelFormat != verified.desc.pixelFormat ||
         cooked.desc.storageFormat != verified.desc.storageFormat ||
         cooked.desc.usage != verified.desc.usage ||
         cooked.desc.colorSpace != verified.desc.colorSpace ||
         cooked.desc.alphaMode != verified.desc.alphaMode ||
         cooked.desc.alphaCutoff != verified.desc.alphaCutoff ||
         cooked.desc.target != verified.desc.target ||
         cooked.desc.residency != verified.desc.residency ||
         cooked.desc.flags != verified.desc.flags ||
         cooked.desc.nWidth != verified.desc.nWidth ||
         cooked.desc.nHeight != verified.desc.nHeight ||
         cooked.desc.nDepth != verified.desc.nDepth ||
         cooked.desc.nFrames != verified.desc.nFrames ||
         cooked.desc.nLayers != verified.desc.nLayers ||
         cooked.desc.nFaces != verified.desc.nFaces ||
         cooked.desc.nMipLevels != verified.desc.nMipLevels ||
         cooked.desc.nSubresources != verified.desc.nSubresources ||
         cooked.desc.nResidentMipLevels != verified.desc.nResidentMipLevels ||
         cooked.desc.nResidentSubresources != verified.desc.nResidentSubresources ||
         cooked.desc.nStreamingPriority != verified.desc.nStreamingPriority ||
         cooked.desc.iResidentFirstSubresource != verified.desc.iResidentFirstSubresource ||
         cooked.desc.cbResidentData != verified.desc.cbResidentData ||
         cooked.desc.cbData != verified.desc.cbData ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    for ( common::u32 level = 0u; level < verified.nMipLevels; ++level ) {
        const auto &supplied = cooked.mips[level];
        const auto &canonical = verified.mips[level];
        if ( supplied.nLevel != canonical.nLevel ||
             supplied.nWidth != canonical.nWidth ||
             supplied.nHeight != canonical.nHeight ||
             supplied.nDepth != canonical.nDepth ||
             supplied.cbRowPitch != canonical.cbRowPitch ||
             supplied.pixels.pData != canonical.pixels.pData ||
             supplied.pixels.cbSize != canonical.pixels.cbSize ||
             !common::ContentHash_Equals( supplied.contentHash, canonical.contentHash ) ) {
            return render_error_t::ERR_INVALID_ARGUMENT;
        }
    }

    // A tangent-space normal is sampled as linear RGBA8 data. Keep the
    // existing color subset unchanged and reject every other non-color use.
    const bool normalMap = verified.desc.usage ==
        common::render_texture_usage_t::NORMAL;
    if ( verified.desc.dimension != common::render_texture_dimension_t::TEXTURE_2D ||
         ( verified.desc.usage != common::render_texture_usage_t::COLOR &&
           !normalMap ) ||
         ( normalMap &&
           ( verified.desc.colorSpace != common::render_texture_color_space_t::LINEAR ||
             verified.desc.pixelFormat !=
                 common::render_texture_pixel_format_t::RGBA8_UNORM ) ) ||
         verified.desc.residency != common::cooked_texture_residency_t::FULLY_RESIDENT ||
         verified.desc.nDepth != 1u || verified.desc.nFrames != 1u ||
         verified.desc.nLayers != 1u || verified.desc.nFaces != 1u ||
         verified.desc.nMipLevels != verified.nMipLevels ||
         verified.nSubresources != verified.nMipLevels ||
         ( verified.desc.pixelFormat != common::render_texture_pixel_format_t::RGBA8_UNORM &&
           verified.desc.pixelFormat != common::render_texture_pixel_format_t::RGBA8_SRGB &&
           verified.desc.pixelFormat != common::render_texture_pixel_format_t::RGBA32_FLOAT ) ||
         ( verified.desc.pixelFormat == common::render_texture_pixel_format_t::RGBA32_FLOAT &&
           verified.desc.colorSpace != common::render_texture_color_space_t::LINEAR ) ) {
        return render_error_t::ERR_RESOURCE_FORMAT_UNSUPPORTED;
    }
    const common::u64 bytesPerPixel = verified.desc.pixelFormat ==
        common::render_texture_pixel_format_t::RGBA32_FLOAT ? 16u : 4u;
    // CYTX float payloads are little-endian. The GL upload path consumes
    // native float words; do not silently reinterpret them on a big-endian host.
    if ( bytesPerPixel == 16u && !common::Cy_PlatformIsLittleEndian() ) {
        return render_error_t::ERR_RESOURCE_FORMAT_UNSUPPORTED;
    }

    // A one-level texture keeps the original upload route. Multiple levels
    // must reach 1x1 so mip-filtered sampling is complete without synthesis.
    common::u32 expectedLevels = 1u;
    common::u32 extent = verified.desc.nWidth > verified.desc.nHeight
        ? verified.desc.nWidth : verified.desc.nHeight;
    while ( extent > 1u ) { extent /= 2u; ++expectedLevels; }
    if ( verified.nMipLevels > 1u && verified.nMipLevels != expectedLevels ) {
        return render_error_t::ERR_RESOURCE_FORMAT_UNSUPPORTED;
    }

    render_texture_mip_data_t levels[common::CY_COOKED_TEXTURE_MAX_MIP_LEVELS]{};
    common::u32 width = verified.desc.nWidth;
    common::u32 height = verified.desc.nHeight;
    for ( common::u32 level = 0u; level < verified.nMipLevels; ++level ) {
        common::cooked_texture_subresource_view_t supplied{};
        common::cooked_texture_subresource_view_t canonical{};
        if ( !common::CookedTexture_GetSubresource( cooked, level, 0u, 0u, 0u, &supplied ) ||
             !common::CookedTexture_GetSubresource( verified, level, 0u, 0u, 0u, &canonical ) ||
             supplied.nMipLevel != canonical.nMipLevel ||
             supplied.nFrame != canonical.nFrame ||
             supplied.nLayer != canonical.nLayer ||
             supplied.nFace != canonical.nFace ||
             supplied.nWidth != canonical.nWidth ||
             supplied.nHeight != canonical.nHeight ||
             supplied.nDepth != canonical.nDepth ||
             supplied.cbRowPitch != canonical.cbRowPitch ||
             supplied.cbSlicePitch != canonical.cbSlicePitch ||
             supplied.pixels.pData != canonical.pixels.pData ||
             supplied.pixels.cbSize != canonical.pixels.cbSize ||
             !common::ContentHash_Equals( supplied.contentHash, canonical.contentHash ) ) {
            return render_error_t::ERR_INVALID_ARGUMENT;
        }
        const common::u64 rowBytes = static_cast<common::u64>( width ) * bytesPerPixel;
        if ( rowBytes > std::numeric_limits<common::u64>::max() / height ) {
            return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
        }
        const common::u64 imageBytes = rowBytes * height;
        if ( canonical.pixels.pData == nullptr ||
             canonical.nMipLevel != level || canonical.nWidth != width ||
             canonical.nHeight != height || canonical.nDepth != 1u ||
             canonical.cbRowPitch != rowBytes ||
             canonical.cbSlicePitch != imageBytes ||
             canonical.pixels.cbSize != imageBytes ||
             imageBytes > std::numeric_limits<common::usize>::max() ) {
            return render_error_t::ERR_RESOURCE_FORMAT_UNSUPPORTED;
        }
        levels[level] = { width, height,
            { canonical.pixels.pData, canonical.pixels.cbSize } };
        width = width > 1u ? width / 2u : 1u;
        height = height > 1u ? height / 2u : 1u;
    }

    render_texture_desc_t description{};
    description.width = verified.desc.nWidth;
    description.height = verified.desc.nHeight;
    description.sRGB = verified.desc.pixelFormat ==
        common::render_texture_pixel_format_t::RGBA8_SRGB;
    description.format = verified.desc.pixelFormat ==
        common::render_texture_pixel_format_t::RGBA32_FLOAT
            ? render_texture_format_t::RGBA32F : render_texture_format_t::RGBA8;
    description.generateMips = false;
    description.repeat = true;
    if ( verified.nMipLevels == 1u ) {
        return R_CreateTexture2D( description, levels[0].data, textureOut );
    }
    if ( tr.backend.api->CreateTexture2DLevels == nullptr ||
         tr.backend.api->DestroyTexture == nullptr ) {
        return render_error_t::ERR_UNSUPPORTED;
    }
    if ( description.width > tr.info.limits.maxTexture2DSize ||
         description.height > tr.info.limits.maxTexture2DSize ||
         description.width > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ||
         description.height > static_cast<common::u32>( std::numeric_limits<common::i32>::max() ) ) {
        return render_error_t::ERR_RESOURCE_SIZE_UNSUPPORTED;
    }
    backend_texture_t native{};
    const render_error_t result = tr.backend.api->CreateTexture2DLevels(
        description, levels, verified.nMipLevels, native, tr.backend.state );
    if ( result != render_error_t::OK || native.value == 0u ) {
        if ( native.value != 0u ) {
            (void)tr.backend.api->DestroyTexture( native, tr.backend.state );
        }
        return result != render_error_t::OK ? result : render_error_t::ERR_INTERNAL_ERROR;
    }
    return RegisterCreatedTexture( description, native, verified.nMipLevels,
        true, textureOut );
}
render_error_t R_DestroyTexture( render_texture_handle_t texture ) noexcept
{
    if ( !tr.initialized || !textureSystem.initialized || tr.backend.api == nullptr ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t slot{};
    texture_record_t *record = nullptr;
    const auto resolved = ResolveTexture( texture, slot, record );
    if ( resolved != render_error_t::OK ) return resolved;
    if ( record->references != 0u ) return render_error_t::ERR_RESOURCE_BUSY;
    const auto result = tr.backend.api->DestroyTexture( record->native, tr.backend.state );
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

render_error_t R_TextureAcquireReference( render_texture_handle_t texture,
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
    if ( record->references == std::numeric_limits<common::u32>::max() ) {
        return render_error_t::ERR_LIMIT_EXCEEDED;
    }
    ++record->references;
    *nativeOut = record->native;
    *infoOut = record->info;
    return render_error_t::OK;
}

render_error_t R_TextureReleaseReference( render_texture_handle_t texture ) noexcept
{
    if ( !tr.initialized || !textureSystem.initialized ) return render_error_t::ERR_NOT_INITIALIZED;
    common::handle32_t slot{};
    texture_record_t *record = nullptr;
    const auto result = ResolveTexture( texture, slot, record );
    if ( result != render_error_t::OK ) return result;
    if ( record->references == 0u ) return render_error_t::ERR_INTERNAL_ERROR;
    --record->references;
    return render_error_t::OK;
}
} // namespace cypher::engine::render
